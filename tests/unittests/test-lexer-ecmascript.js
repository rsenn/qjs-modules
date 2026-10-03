/* lib/lexer/ecmascript.js: shebang line and regex literals in templates.
 * Each scan builds one Lexer and reads it to the end before the next. */
import ECMAScriptLexer from '../../lib/lexer/ecmascript.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

const scan = src => [...new ECMAScriptLexer(src, 'test.js')].map(t => `${t.type}:${t.lexeme}`);

function lexError(src) {
  try {
    scan(src);
  } catch(e) {
    return e.message;
  }
}

tests({
  'a shebang line at the start is one token'() {
    eq('shebang:#!/usr/bin/env qjsm', scan('#!/usr/bin/env qjsm\nlet x;')[0]);
  },

  'the code after a shebang lexes normally'() {
    eq('shebang:#!/bin/sh,whitespace:\n,keyword:let,whitespace: ,identifier:x,punctuator:;', scan('#!/bin/sh\nlet x;').join());
  },

  'a shebang without a trailing newline'() {
    eq('shebang:#!/bin/x', scan('#!/bin/x').join());
  },

  'a CRLF line ending stays out of the shebang token'() {
    eq('shebang:#!/bin/x', scan('#!/bin/x\r\n')[0]);
  },

  'a shebang is only valid at the very start'() {
    assert(/No matching token/.test(lexError('a\n#!x\n')), 'mid-file');
    assert(/No matching token/.test(lexError('#!/bin/sh\n#!again\n')), 'second line');
  },

  'source without a shebang is unchanged'() {
    eq('keyword:let,whitespace: ,identifier:y,whitespace: ,punctuator:=,whitespace: ,numericLiteral:2,punctuator:;', scan('let y = 2;').join());
  },

  'a regex literal inside a template substitution'() {
    const tokens = scan('`${a.replace(/\\/+$/, "")}`');

    assert(tokens.some(t => t.startsWith('regexpLiteral:/\\/+$/')), tokens.join('|'));
  },
});
