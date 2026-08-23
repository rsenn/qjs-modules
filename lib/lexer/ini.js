import { define } from 'util';
import { Lexer } from 'lexer';
import { Token } from 'lexer';

export const IniTokens = {
  comment: /[;#][^\n]*/,
  section: /\[[^\]\n]*\]/,
  string: /"(\\.|[^\\"\n])*"/,
  stringSingle: /'(\\.|[^\\'\n])*'/,
  newline: /\r?\n/,
  equals: /[=:]/,
  /* Generic key/value word - a parser distinguishes key from value by
   * position (before/after {equals}), not the lexer. */
  text: /[^\s\[\]=:;#\r\n]+/,
};

export class IniLexer extends Lexer {
  constructor(input, mode = Lexer.LONGEST, filename, mask) {
    super(input, mode, filename, mask);

    this.addRules();
  }

  addRules() {
    for(const name in IniTokens) this.addRule(name, IniTokens[name]);

    this.addRule('whitespace', /[ \t]+/);
  }
}

globalThis.IniLexer = IniLexer;

define(IniLexer.prototype, { [Symbol.toStringTag]: 'IniLexer' });

export default IniLexer;
