#!/usr/bin/env qjsm
import { readFileSync } from 'fs';
import { getOpt } from 'util';
import * as std from 'std';
import { JsonParser } from 'json';

function usage(exitCode) {
  std.puts(
    `Usage: ${scriptArgs[0]} [OPTIONS] <files...>\n\n` +
      `Streaming JSON pretty-printer (JsonParser-based; no files means stdin).\n\n` +
      `  -i, --indent <N>   spaces per nesting level (default: 2)\n` +
      `  -c, --compact      dense single-line output (same as --indent=0)\n` +
      `  -o, --output FILE  write to FILE instead of stdout\n` +
      `  -h, --help         show this help\n`,
  );
  std.exit(exitCode);
}

/* Streams the token sequence produced by JsonParser straight to output text —
 * it never builds the parsed value in memory (unlike JSON.parse + JSON.stringify),
 * so output for the front of a document starts before the rest has been tokenized,
 * and numbers are copied verbatim instead of being rounded through a JS number. */
function prettyPrint(input, filename, indent, put) {
  const parser = new JsonParser(input, filename);
  const stack = [];
  let afterKey = false;

  function pad(depth) {
    if(indent) put('\n' + ' '.repeat(indent * depth));
  }

  function beforeValue() {
    if(afterKey) {
      afterKey = false;
      return;
    }

    const frame = stack[stack.length - 1];

    if(frame) {
      if(frame.count++ > 0) put(',');
      pad(stack.length);
    }
  }

  for(;;) {
    const tok = parser.parse();

    switch (tok) {
      case 'NEED_DATA':
      case 'NONE':
        return;

      case 'OBJECT':
      case 'ARRAY': {
        beforeValue();
        put(tok === 'OBJECT' ? '{' : '[');
        stack.push({ count: 0 });
        break;
      }

      case 'OBJECT_END':
      case 'ARRAY_END': {
        const frame = stack.pop();
        if(frame.count > 0) pad(stack.length);
        put(tok === 'OBJECT_END' ? '}' : ']');
        break;
      }

      case 'KEY': {
        const frame = stack[stack.length - 1];
        if(frame.count++ > 0) put(',');
        pad(stack.length);
        put(JSON.stringify(parser.token) + ':' + (indent ? ' ' : ''));
        afterKey = true;
        break;
      }

      case 'STRING': {
        beforeValue();
        put(JSON.stringify(parser.token));
        break;
      }

      case 'NUMBER': {
        beforeValue();
        put(parser.token);
        break;
      }

      case 'TRUE':
      case 'FALSE':
      case 'NULL': {
        beforeValue();
        put(tok.toLowerCase());
        break;
      }
    }
  }
}

function main(...args) {
  let indent = 2;

  const params = getOpt(
    {
      help: [false, () => usage(0), 'h'],
      indent: [true, v => (indent = +v), 'i'],
      compact: [false, () => (indent = 0), 'c'],
      output: [true, null, 'o'],
      '@': 'files',
    },
    args,
  );

  const files = params['@'].length ? params['@'] : ['-'];
  const out = params.output ? std.open(params.output, 'w+') : std.out;
  const put = s => out.puts(s);

  let failed = false;

  for(const file of files) {
    const input = file === '-' ? std.in.readAsString() : readFileSync(file, { encoding: 'utf-8' });

    try {
      prettyPrint(input, file === '-' ? '<stdin>' : file, indent, put);
      put('\n');
    } catch(error) {
      std.err.puts(`${file}: ${error.message}\n`);
      failed = true;
    }
  }

  out.flush();

  if(failed) std.exit(1);
}

main(...scriptArgs.slice(1));
