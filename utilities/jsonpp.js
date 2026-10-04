#!/usr/bin/env qjsm
import { getOpt } from 'util';
import * as std from 'std';
import * as os from 'os';
import { JsonParser } from 'json';

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
    f.puts(text);
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

function usage(exitCode) {
  std.puts(
    `Usage: ${scriptArgs[0]} [OPTIONS] <files...>\n\n` +
      `Streaming JSON pretty-printer (JsonParser-based; no files means stdin).\n\n` +
      `  -i, --indent <N>   spaces per nesting level (default: 2)\n` +
      `  -c, --compact      dense single-line output (same as --indent=0)\n` +
      `  -o, --output FILE  write to FILE instead of stdout\n` +
      `  -w, --write        rewrite each input file in place, atomically via temp file +\n` +
      `                     rename (DESTRUCTIVE; a file that fails to parse is left alone)\n` +
      `  -h, --help         show this help\n`,
  );
  std.exit(exitCode);
}

/* Streams the token sequence produced by JsonParser straight to output text —
 * it never builds the parsed value in memory (unlike JSON.parse + JSON.stringify),
 * so output for the front of a document starts before the rest has been tokenized,
 * and numbers are copied verbatim instead of being rounded through a JS number. */
function prettyPrint(reader, filename, indent, put) {
  const parser = new JsonParser(reader, filename);
  const stack = [];
  let afterKey = false;

  parser.comments = true;

  /* Comments arrive between tokens but the comma before the next element is only written
   * once that element is seen, so they wait here and are written right before it (after
   * the comma) or before the closing bracket, never between a value and its comma. */
  const pending = [];

  function flushComments(depth) {
    for(const c of pending) {
      put(c);
      put(c.startsWith('//') ? '\n' + ' '.repeat(indent * depth) : ' ');
    }

    pending.length = 0;
  }

  function pad(depth) {
    if(indent) put('\n' + ' '.repeat(indent * depth));
  }

  function beforeValue() {
    if(afterKey) {
      afterKey = false;
      flushComments(stack.length);
      return;
    }

    const frame = stack[stack.length - 1];

    if(frame) {
      if(frame.count++ > 0) put(',');
      pad(stack.length);
    }

    flushComments(stack.length);
  }

  for(;;) {
    const tok = parser.parse();

    switch (tok) {
      case NEED_DATA:
      case NONE:
        if(stack.length || afterKey) throw new Error('unexpected end of input');

        for(const c of pending) put('\n' + c);

        return;

      case COMMENT:
        pending.push(parser.token);
        break;

      case OBJECT:
      case ARRAY: {
        beforeValue();
        put(tok === OBJECT ? '{' : '[');
        stack.push({ count: 0 });
        break;
      }

      case OBJECT_END:
      case ARRAY_END: {
        const frame = stack.pop();

        const had = pending.length > 0;

        for(const c of pending) {
          pad(stack.length + 1);
          put(c);
        }

        pending.length = 0;
        if(frame.count > 0 || had) pad(stack.length);
        put(tok === OBJECT_END ? '}' : ']');
        break;
      }

      case KEY: {
        const frame = stack[stack.length - 1];
        if(frame.count++ > 0) put(',');
        pad(stack.length);
        flushComments(stack.length);
        put(JSON.stringify(parser.token) + ':' + (indent ? ' ' : ''));
        afterKey = true;
        break;
      }

      case STRING: {
        beforeValue();
        put(JSON.stringify(parser.token));
        break;
      }

      case NUMBER: {
        beforeValue();
        put(parser.token);
        break;
      }

      case TRUE:
      case FALSE:
      case NULL: {
        beforeValue();
        put(tok === TRUE ? 'true' : tok === FALSE ? 'false' : 'null');
        break;
      }
    }
  }
}

function main(...args) {
  let indent = 2;
  let inPlace = false;

  const params = getOpt(
    {
      help: [false, () => usage(0), 'h'],
      indent: [true, v => (indent = +v), 'i'],
      compact: [false, () => (indent = 0), 'c'],
      output: [true, null, 'o'],
      write: [false, () => (inPlace = true), 'w'],
      '@': 'files',
    },
    args,
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
          const chunks = [];

          prettyPrint(reader, file, indent, s => chunks.push(s));
          chunks.push('\n');

          writeAtomic(file, chunks.join(''));
        } else {
          prettyPrint(reader, file === '-' ? '<stdin>' : file, indent, put);
          put('\n');
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
