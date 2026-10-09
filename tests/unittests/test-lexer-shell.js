/* lib/lexer/shell.js: a shebang line at the start of the input.
 * Each scan builds one Lexer and reads it to the end before the next. */
import ShellLexer from '../../lib/lexer/shell.js';
import { toArrayBuffer } from 'misc';
import { eq, tests } from '../../lib/tinytest.js';

const scan = src => [...new ShellLexer(src, undefined, 'test.sh')].filter(t => t.type != 'whitespace').map(t => `${t.type}:${t.lexeme}`);

tests({
  'a shebang line at the start is one token'() {
    eq('shebang:#!/bin/sh', scan('#!/bin/sh\necho hi\n')[0]);
  },

  'the code after a shebang lexes normally'() {
    eq('shebang:#!/bin/sh,newline:\n,name:echo,name:hi,newline:\n', scan('#!/bin/sh\necho hi\n').join());
  },

  'a shebang without a trailing newline'() {
    eq('shebang:#!/bin/sh', scan('#!/bin/sh').join());
  },

  'a CRLF line ending stays out of the shebang token'() {
    eq('shebang:#!/bin/sh', scan('#!/bin/sh\r\nls\r\n')[0]);
  },

  'ArrayBuffer and typed array input'() {
    const src = '#!/usr/bin/env sh\nls\n';

    eq('shebang:#!/usr/bin/env sh', scan(toArrayBuffer(src))[0]);
    eq('shebang:#!/usr/bin/env sh', scan(new Uint8Array(toArrayBuffer(src)))[0]);
  },

  'a #! anywhere but the start is an ordinary comment'() {
    eq('newline:\n,comment:#!/bin/sh,newline:\n,name:ls,newline:\n', scan('\n#!/bin/sh\nls\n').join());
    eq('shebang:#!/bin/sh,newline:\n,comment:#!again,newline:\n', scan('#!/bin/sh\n#!again\n').join());
  },

  'input without a shebang is unchanged'() {
    eq('name:echo,name:hi,newline:\n', scan('echo hi\n').join());
    eq('comment:# note,newline:\n,name:ls,newline:\n', scan('# note\nls\n').join());
  },
});
