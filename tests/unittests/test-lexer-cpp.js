/* lib/lexer/cpp.js and the CPP mode of lib/lexer/c.js: pp_* tokens inside a
 * directive, C tokens outside, back to C at the first unescaped newline. */
import CLexer from '../../lib/lexer/c.js';
import CPPLexer from '../../lib/lexer/cpp.js';
import { eq, tests } from '../../lib/tinytest.js';

const SKIP = new Set(['whitespace', 'pp_whitespace']);

const scan = (Lexer, src) => [...new Lexer(src, Lexer.LONGEST, 'test.c')].filter(t => !SKIP.has(t.type)).map(t => `${t.type}:${t.lexeme.trim()}`);
const c = src => scan(CLexer, src);

tests({
  'a directive at the start of the input is lexed in CPP mode'() {
    eq('pp_hash:#,pp_directive:define,pp_identifier:X,pp_number:1,pp_newline:', c('#define X 1\n').join());
  },

  'the newline ends the directive and C resumes'() {
    eq('pp_hash:#,pp_directive:undef,pp_identifier:X,pp_newline:,int:int,identifier:x,semi:;', c('#undef X\nint x;').join());
  },

  'an escaped newline continues the directive'() {
    const t = c('#define A \\\n  1\nint');

    eq('pp_hash:#,pp_directive:define,pp_identifier:A,pp_continuation:\\,pp_number:1,pp_newline:,int:int', t.join());
  },

  'a # after leading blanks starts a directive, one inside C does not need a newline first'() {
    eq('lbrace:{,pp_hash:#,pp_directive:if,pp_number:0,pp_newline:,rbrace:}', c('{\n  # if 0\n}').join());
  },

  'consecutive directives each enter CPP mode'() {
    eq('pp_hash:#,pp_directive:if,pp_identifier:A,pp_newline:,pp_hash:#,pp_directive:endif,pp_newline:', c('#if A\n#endif\n').join());
  },

  'the header name of #include is one token'() {
    eq('pp_hash:#,pp_directive:include,pp_header_name:<sys/types.h>,pp_newline:', c('#include <sys/types.h>\n').join());
    eq('pp_hash:#,pp_directive:include,pp_string:"a.h",pp_newline:', c('#include "a.h"\n').join());
  },

  'outside #include, < and > are punctuators'() {
    eq('pp_hash:#,pp_directive:if,pp_identifier:A,pp_punctuator:<,pp_identifier:B,pp_punctuator:>,pp_identifier:C,pp_newline:', c('#if A<B>C\n').join());
  },

  'a comment inside a directive is a pp_comment; a block comment may span lines'() {
    eq('pp_hash:#,pp_directive:define,pp_identifier:A,pp_comment:/* x\n y */,pp_number:1,pp_newline:', c('#define A /* x\n y */ 1\n').join());
    eq('pp_hash:#,pp_directive:define,pp_identifier:A,pp_comment:// t,pp_newline:', c('#define A // t\n').join());
  },

  'an apostrophe in #error is pp_other, not an error'() {
    eq('pp_hash:#,pp_directive:error,pp_identifier:don,pp_other:\',pp_identifier:t,pp_newline:', c("#error don't\n").join());
  },

  'a null directive is just the hash'() {
    eq('pp_hash:#,pp_newline:,int:int', c('#\nint').join());
  },

  'stringify and paste operators are punctuators'() {
    eq('pp_hash:#,pp_directive:define,pp_identifier:S,pp_punctuator:(,pp_identifier:a,pp_punctuator:),pp_punctuator:#,pp_identifier:a,pp_punctuator:##,pp_identifier:b,pp_newline:', c('#define S(a) #a ## b\n').join());
  },

  'a # inside a comment or string in C does not start a directive'() {
    eq('multiLineComment:/* # */,string_literal:"#",identifier:x', c('/* # */ "#" x').join());
  },

  'CPPLexer on its own lexes directives with the same rules'() {
    eq('pp_hash:#,pp_directive:define,pp_identifier:X,pp_number:2,pp_newline:', scan(CPPLexer, '#define X 2\n').join());
  },
});
