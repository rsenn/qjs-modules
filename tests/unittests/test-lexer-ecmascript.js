/* lib/lexer/ecmascript.js: shebang line and regex literals in templates.
 * Each scan builds one Lexer and reads it to the end before the next. */
import ECMAScriptLexer from '../../lib/lexer/ecmascript.js';
import { toArrayBuffer } from 'misc';
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

  'a shebang in an ArrayBuffer or typed array'() {
    const src = '#!/usr/bin/env qjsm\nlet x;';

    eq('shebang:#!/usr/bin/env qjsm', scan(toArrayBuffer(src))[0]);
    eq('shebang:#!/usr/bin/env qjsm', scan(new Uint8Array(toArrayBuffer(src)))[0]);
    eq(scan(src).join(), scan(toArrayBuffer(src)).join());
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

  'a slash after an operand is division'() {
    const lexemes = src => scan(src).filter(t => !t.startsWith('whitespace:'));

    eq('identifier:b,punctuator:/,identifier:c,punctuator:/,identifier:d', lexemes('b / c / d').join());
    eq('punctuator:),punctuator:/,numericLiteral:2', lexemes('(1) / 2').slice(-3).join());
    eq('punctuator:],punctuator:/', lexemes('a[0] / 2').slice(-3, -1).join());
    eq('keyword:this,punctuator:/', lexemes('this / 2').slice(0, 2).join());
    eq('punctuator:++,punctuator:/', lexemes('i++ / 2').slice(1, 3).join());
  },

  'a slash where an operand starts is a regex'() {
    for(const src of ['x = /re/g', 'return /re/', 'f(/re/)', '{} /re/', 'if(x) /re/', 'while(f(a)) /re/'])
      assert(scan(src).some(t => t.startsWith('regexpLiteral:/re/')), src);
  },

  'a brace inside a template substitution does not end it'() {
    eq('templateLiteralHead:`a${,punctuator:{,identifier:b,punctuator::,numericLiteral:1,punctuator:},punctuator:.,identifier:b,nestedEnd:},templateLiteralTail:c`', scan('`a${{b:1}.b}c`').join());
  },

  'division inside a template substitution and after a template'() {
    const tokens = scan('`a${x / 2}b` / 3');

    assert(!tokens.some(t => t.startsWith('regexpLiteral')), tokens.join('|'));
  },

  'a regex literal inside a template substitution'() {
    const tokens = scan('`${a.replace(/\\/+$/, "")}`');

    assert(tokens.some(t => t.startsWith('regexpLiteral:/\\/+$/')), tokens.join('|'));
  },
});
