import { Lexer, Token } from 'lexer';
import { define } from 'util';

export const CTokens = {
  preprocessor: /#[^\n\\]*(:?\\\n[^\n\\]*|\\.[^\n\\]*)*/,
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
  semi: /;/,
  lbrace: /{|<%/,
  rbrace: /}|%>/,
  comma: /,/,
  colon: /:/,
  equal: /=/,
  lparen: /\(/,
  rparen: /\)/,
  lbrack: /\[|<:/,
  rbrack: /\]|:>/,
  dot: /\./,
  amp: /&/,
  bang: /!/,
  tilde: /~/,
  minus: /\-/,
  plus: /\+/,
  star: /\*/,
  slash: /\//,
  percent: /%/,
  lt: /</,
  gt: />/,
  caret: /\^/,
  pipe: /\|/,
  question: /\?/
  // whitespace: /[ \t\v\r\n\f]/
};

export class CLexer extends Lexer {
  constructor(input, filename, mask) {
    super(input, Lexer.LONGEST, filename, mask);

    this.addRules();
  }

  addRules() {
    for(let name in CTokens) this.addRule(name, CTokens[name]);

    this.addRule('whitespace', /[ \t\v\r\n\f]/, 0x8000);
  }
}

globalThis.CLexer = CLexer;

define(CLexer.prototype, { [Symbol.toStringTag]: 'CLexer' });

export default CLexer;
