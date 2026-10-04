/* lib/lexer/{ini,make,cmake}.js: where a comment starts and where it does not.
 * Each scan builds one Lexer and reads it to the end before the next. */
import CMakeLexer from '../../lib/lexer/cmake.js';
import IniLexer from '../../lib/lexer/ini.js';
import { GNUMakeLexer } from '../../lib/lexer/make.js';
import { eq, tests } from '../../lib/tinytest.js';

const SKIP = new Set(['whitespace', 'newline']);

const scan = lexer => [...lexer].filter(t => !SKIP.has(t.type)).map(t => `${t.type}:${t.lexeme}`);
const ini = src => scan(new IniLexer(src, IniLexer.LONGEST, 'test.ini'));
const make = src => scan(new GNUMakeLexer(src, GNUMakeLexer.LONGEST, 'Makefile'));
const cmake = src => scan(new CMakeLexer(src, CMakeLexer.LONGEST, 'CMakeLists.txt'));

tests({
  'ini: a marker at the start of a line is a comment'() {
    eq('comment:; semi,comment:# hash', ini('; semi\n# hash\n').join());
  },

  'ini: a marker on the first line of the input is a comment'() {
    eq('comment:#first', ini('#first').join());
  },

  'ini: a marker after whitespace is a comment'() {
    eq('text:k,equals:=,text:v,comment:; tail', ini('k=v ; tail').join());
  },

  'ini: a marker inside a value stays in the value'() {
    eq('text:url,equals:=,text:http,equals::,text://a#b', ini('url=http://a#b').join());
    eq('text:x;y,equals:=,text:1', ini('x;y=1').join());
  },

  'ini: a value that starts with # right after = is not a comment'() {
    eq('text:color,equals:=,text:#fff', ini('color=#fff').join());
  },

  'make: \\# is a literal hash, not a comment'() {
    eq('name:B,assign:=,escapedHash:\\#,name:esc', make('B=\\#esc').join());
  },

  'make: an odd run of backslashes escapes the hash, an even one does not'() {
    eq('name:a,backslashPair:\\\\,comment:#c', make('a\\\\#c').join());
    eq('escapedHash:\\\\\\#', make('\\\\\\#').join());
    eq('backslashPair:\\\\,backslashPair:\\\\,comment:#c', make('\\\\\\\\#c').join());
  },

  'make: ordinary comments and line continuations are unchanged'() {
    eq('comment:# top,name:A,assign:=,name:1,comment:# tail', make('# top\nA=1 # tail').join());
    eq('name:G,assign:=,name:a,lineContinuation:\\\n,name:b,comment:# c', make('G=a \\\n b # c').join());
  },

  'make: a # in a recipe line is still a comment'() {
    eq('name:echo,name:hi,comment:# recipe', make('all:\n\techo hi # recipe').slice(2).join());
  },

  'cmake: a # ends an unquoted argument and starts a comment'() {
    eq('identifier:message,lparen:(,identifier:a,lineComment:#b)', cmake('message(a#b)').join());
  },

  'cmake: a # inside a quoted argument or after \\ is not a comment'() {
    eq('identifier:set,lparen:(,identifier:A,quotedArgument:"# no",rparen:),lineComment:# tail', cmake('set(A "# no") # tail').join());
    eq('identifier:message,lparen:(,unquotedArgument:a\\#b,rparen:)', cmake('message(a\\#b)').join());
  },
});
