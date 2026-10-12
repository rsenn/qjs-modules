#!/usr/bin/env qjsm
import { getOptHelp } from 'util';
import * as std from 'std';
import * as os from 'os';
import { JsonParser, JsonWriter } from 'json';
import { toString, toArrayBuffer } from 'util';
import { ArrayBufferSink } from 'arraybuffer_sink';

const { NEED_DATA, NONE, OBJECT, OBJECT_END, ARRAY, ARRAY_END, KEY, STRING, TRUE, FALSE, NULL, NUMBER, COMMENT } = JsonParser;

/* Wraps a file (or stdin, fd 0) as a JsonParser reader method: JsonParser calls
 * read(buf, len) to pull raw bytes on demand, straight off the fd via os.read()
 * (a raw read(2), unlike std's FILE* which is buffered) — so the input is never
 * read into one big string up front the way readAsString()/readFileSync() would,
 * and nothing is held back in a stdio buffer either. */
function fileReader(file) {
  const fd = file === '-' ? 0 : os.open(file, os.O_RDONLY);

  if(fd < 0) throw new Error(`cannot open '${file}'`);

  return {
    read(buf, len) {
      const n = os.read(fd, buf, 0, len);
      if(n < 0) throw new Error(`read '${file}' failed`);
      return n;
    },
    close() {
      if(file !== '-') os.close(fd);
    },
  };
}

/* Replaces `file` with `text` atomically: written and flushed to a sibling temp file
 * (same directory, so rename(2) stays on one filesystem), which keeps the original's
 * permission bits, then renamed over it. On any failure the temp file is removed and
 * the original is untouched. */
function writeAtomic(file, text) {
  const [st, err] = os.stat(file);

  if(err) throw new Error(`cannot stat '${file}'`);

  const tmp = `${file}.jsonpp-${Date.now()}.tmp`;
  const fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, st.mode & 0o7777);

  if(fd < 0) throw new Error(`cannot create '${tmp}'`);

  const f = std.fdopen(fd, 'w');

  try {
    if(typeof text == 'string') f.puts(text);
    else f.write(text, 0, text.byteLength);

    f.flush();

    if(f.error()) throw new Error(`write '${tmp}' failed`);
  } catch(error) {
    f.close();
    os.remove(tmp);
    throw error;
  }

  if(f.close() !== 0 || os.rename(tmp, file) !== 0) {
    os.remove(tmp);
    throw new Error(`cannot replace '${file}'`);
  }
}

/* Streams the token sequence produced by JsonParser straight to output text —
 * it never builds the parsed value in memory (unlike JSON.parse + JSON.stringify),
 * so output for the front of a document starts before the rest has been tokenized,
 * and numbers are copied verbatim instead of being rounded through a JS number. */
function prettyPrint(reader, filename, opts, write) {
  const pending = [];
  let comma = false,
    ws = '';

  const parser = new JsonParser(reader, filename);
  const writer = new JsonWriter((buf, len) => {
    let r = 0;
    const str = toString(buf, 0, len);

    if(comma) {
      if(!/^\s/.test(str)) {
        for(let comment of pending) {
          const b = toArrayBuffer(comment.replaceAll('\n', ws) + ws);
          const tmp = write(b, b.byteLength);
          if(tmp > 0) r += tmp;
        }

        pending.splice(0, pending.length);
        comma = false;
      } else {
        ws += str;
      }
    } else if(str == ',') {
      comma = true;
      ws = '';
    }

    const tmp = write(buf, len);
    if(tmp > 0) r += tmp;

    return r;
  }, opts);

  parser.comments = true;

  /* Comments arrive between tokens but the comma before the next element is only written
   * once that element is seen, so they wait here and are written right before it (after
   * the comma) or before the closing bracket, never between a value and its comma. */

  for(;;) {
    const tok = parser.parse();

    const { token } = parser;

    switch (tok) {
      case NEED_DATA:
      case NONE:
        if(writer.depth) throw new Error('unexpected end of input');
        return;

      case COMMENT:
        pending.push(token);
        break;

      case OBJECT:
        writer.objectStart();
        break;

      case OBJECT_END:
        writer.objectEnd();
        break;

      case ARRAY:
        writer.arrayStart();
        break;

      case ARRAY_END:
        writer.arrayEnd();
        break;

      case KEY:
        writer.key(token);
        break;

      case STRING:
        writer.value(token);
        break;

      case NUMBER: {
        let num = parseFloat(token);

        if(num + '' != token) num = BigInt(token);

        writer.value(num);
        break;
      }

      case TRUE:
      case FALSE:
      case NULL:
        writer.value(tok === TRUE ? true : tok === FALSE ? false : null);
        break;
    }
  }
}

function main(...args) {
  let inPlace = false,
    opts = { indent: 2 };

  const params = getOptHelp(
    {
      indent: ['NUM', num => (opts.indent = +num), 'i'],
      compact: [false, () => ((opts.minify = true), (opts.indent = 0)), 'c'],
      output: ['FILE', null, 'o'],
      write: [false, () => (inPlace = true), 'w'],
      'bare-keys': [false, () => (opts.bareKeys = true), 'b'],
      'single-quotes': [false, () => (opts.singleQuotes = true), 's'],
      'hex-numbers': [false, () => (opts.hexNumbers = true), 'x'],
      minify: [false, () => (opts.minify = true), 'm'],
      '@': '*files',
    },
    args,
    {
      indent: `spaces per nesting level (default: 2)`,
      compact: `dense single-line output (same as --indent=0)`,
      output: `write to FILE instead of stdout`,
      write: `rewrite each input file in place, atomically via temp file +\nrename (DESTRUCTIVE; a file that fails to parse is left alone)`,
      'bare-keys': `don't quote keys unless needed (JSON5)`,
      'single-quotes': `use single-quotes instead of double (JSON5)`,
      'hex-numbers': `use hexadecimal number literals (JSON5)`,
      minify: `output compacted (JSON5)`,
    },
  );

  const files = params['@'].length ? params['@'] : ['-'];
  const out = params.output ? std.open(params.output, 'w+') : std.out;
  const put = s => out.puts(s);

  let failed = false;

  if(inPlace && (params.output || files.includes('-'))) {
    std.err.puts('--write needs file arguments and cannot be combined with --output\n');
    std.exit(2);
  }

  for(const file of files) {
    try {
      const reader = fileReader(file);

      try {
        if(inPlace) {
          /* buffered: the file is only opened for writing once the whole input parsed */
          const sink = new ArrayBufferSink();

          prettyPrint(reader, file, opts, (buf, len) => sink.write(buf, 0, len));
          sink.write('\n');

          writeAtomic(file, sink.flush());
        } else {
          const write = (buf, len) => std.out.write(buf, 0, len ?? buf.byteLength);

          prettyPrint(reader, file === '-' ? '<stdin>' : file, opts, write);

          std.out.puts('\n');
          std.out.flush();
        }
      } finally {
        reader.close();
      }
    } catch(error) {
      std.err.puts(`${file}: ${error.message}\n`);
      failed = true;
    }
  }

  out.flush();

  if(failed) std.exit(1);
}

main(...scriptArgs.slice(1));
