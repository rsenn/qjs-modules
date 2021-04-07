import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';
import * as xml from 'xml.so';
import { Predicate } from 'predicate.so';
import { Lexer, Token, SyntaxError } from 'lexer.so';
import Console from './console.js';

('use strict');
('use math');

globalThis.inspect = inspect;

function WriteFile(file, tok) {
  let f = std.open(file, 'w+');
  f.puts(tok);
  console.log('Wrote "' + file + '": ' + tok.length + ' bytes');
}

function DumpLexer(lex) {
  const { size, pos, start, line, column, lineStart, lineEnd, columnIndex } = lex;

  return `Lexer ${inspect({
    start,
    pos,
    size /*, line, column, lineStart, lineEnd, columnIndex*/
  })}`;
}
function DumpToken(tok) {
  const { length, offset, chars, loc } = tok;

  return `Token ${inspect({ chars, offset, length, loc }, { depth: Infinity })}`;
}

const tokenRules = {
  singleLineComment: /\/\/.*/,
  multiLineComment: /\/\*([^\*]|[\r\n]|(\*+([^\/\*]|[\n\r])))*\*+\//,
  while: /while/,
  volatile: /volatile/,
  void: /void/,
  unsigned: /unsigned/,
  union: /union/,
  typedef: /typedef/,
  switch: /switch/,
  struct: /struct/,
  static: /static/,
  sizeof: /sizeof/,
  signed: /signed/,
  short: /short/,
  return: /return/,
  register: /register/,
  long: /long/,
  int: /int/,
  if: /if/,
  goto: /goto/,
  for: /for/,
  float: /float/,
  extern: /extern/,
  enum: /enum/,
  else: /else/,
  double: /double/,
  do: /do/,
  default: /default/,
  continue: /continue/,
  const: /const/,
  char: /char/,
  case: /case/,
  break: /break/,
  auto: /auto/,
  bool: /_Bool/,
  complex: /_Complex/,
  imaginary: /_Imaginary/,
  inline: /inline/,
  restrict: /restrict/,
  identifier: /[A-Za-z_]\w*/,
  hexadecimal: /0[xX][a-fA-F0-9]+((u|U)|((u|U)?(l|L|ll|LL))|((l|L|ll|LL)(u|U)))?/,
  octal: /0[0-7]+((u|U)|((u|U)?(l|L|ll|LL))|((l|L|ll|LL)(u|U)))?/,
  decimal: /[0-9]+((u|U)|((u|U)?(l|L|ll|LL))|((l|L|ll|LL)(u|U)))?/,
  char_literal: /[a-zA-Z_]?\'(\\.|[^\\'\n])+\'/,
  floatWithoutPoint: /[0-9]+([Ee][+-]?[0-9]+)(f|F|l|L)?/,
  floatWithNothingBeforePoint: /[0-9]*\.[0-9]+([Ee][+-]?[0-9]+)?(f|F|l|L)?/,
  floatWithNothingAfterPoint: /[0-9]+\.[0-9]*([Ee][+-]?[0-9]+)?(f|F|l|L)?/,
  string_literal: /[a-zA-Z_]?\"(\\.|[^\\"\n])*\"/,
  ellipsis: /\.\.\./,
  right_assign: />>=/,
  left_assign: /<<=/,
  add_assign: /\+=/,
  sub_assign: /\-=/,
  mul_assign: /\*=/,
  div_assign: /\/=/,
  mod_assign: /%=/,
  and_assign: /&=/,
  xor_assign: /\^=/,
  or_assign: /\|=/,
  right_op: />>/,
  left_op: /<</,
  inc_op: /\+\+/,
  dec_op: /\-\-/,
  ptr_op: /\->/,
  and_op: /&&/,
  or_op: /\|\|/,
  le_op: /<=/,
  ge_op: />=/,
  eq_op: /==/,
  ne_op: /!=/,
  ';': /;/,
  '{': /{|<%/,
  '}': /}|%>/,
  ',': /,/,
  ':': /:/,
  '=': /=/,
  '(': /\(/,
  ')': /\)/,
  '[': /\[|<:/,
  ']': /\]|:>/,
  '.': /\./,
  '&': /&/,
  '!': /!/,
  '~': /~/,
  '-': /\-/,
  '+': /\+/,
  '*': /\*/,
  '/': /\//,
  '%': /%/,
  '<': /</,
  '>': />/,
  '^': /\^/,
  '|': /\|/,
  '?': /\?/,
  whitespace: /[ \t\v\r\n\f]/,
  unmatched: /./
};
function main(...args) {
  console = new Console({
    colors: true,
    depth: 8,
    maxArrayLength: 100,
    maxStringLength: 100,
    compact: false
  });
  let file = args[0] ?? scriptArgs[0];
  let str = std.loadFile(file, 'utf-8');
  let len = str.length;
  console.log('len', len);

  let lexer = new Lexer(str, file, Lexer.LONGEST);

  console.log('lexer:', lexer);

  lexer.addRule('IDENTIFIER', '[A-Za-z_]\\w*');
  lexer.addRule('ASSIGN_OP', '>>=|<<=|\\+=|\\-=|\\*=|/=|%=|&=|\\^=|\\|=');
  lexer.addRule('ARITH_OP', '\\+\\+|--|\\+|-|/|\\*|%');
  lexer.addRule('LOGIC_OP', '==|!=|>|<|>=|<=|&&|\\|\\||!|<=>');
  lexer.addRule('BIT_OP', '&|\\||\\^|<<|>>|~');
  lexer.addRule( "WHITESPACE", "[ \\t\\v\\r\\n\\f]+");

  try {
    let tok;

    for(let tok of lexer) {
      console.log('tok:', tok);
      //console.log((tok.loc + '').padEnd(16), tok.type.padEnd(20), tok.toString());
    }
  } catch(err) {
    console.log('err:', err.message);
    /*console.log(lexer.currentLine());
    console.log('^'.padStart(lexer.loc.column));*/
      }
  return;

  std.gc();
}

main(...scriptArgs.slice(1));
