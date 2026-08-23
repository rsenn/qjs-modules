import { define } from 'util';
import { Lexer } from 'lexer';
import { Token } from 'lexer';

export const CMakeTokens = {
  bracketComment: /#\[(=*)\[[\s\S]*?\]\1\]/,
  lineComment: /#[^\n]*/,
  bracketArgument: /\[(=*)\[[\s\S]*?\]\1\]/,
  quotedArgument: /"(\\.|[^\\"])*"/,
  variableRef: /\$(ENV|CACHE)?\{[^{}]*\}/,
  identifier: /[A-Za-z_][A-Za-z0-9_]*/,
  lparen: /\(/,
  rparen: /\)/,
  newline: /\r?\n/,
  dollar: /\$/,
  /* Excludes '$' so a run of unquoted text stops at a variable reference
   * instead of swallowing it - {variableRef} would otherwise tie with (or
   * lose to, once followed by more text) the longest-match here. */
  unquotedArgument: /(\\.|[^ \t\r\n()#"\\$])+/,
};

export class CMakeLexer extends Lexer {
  constructor(input, mode = Lexer.LONGEST, filename, mask) {
    super(input, mode, filename, mask);

    this.addRules();
  }

  addRules() {
    for(const name in CMakeTokens) this.addRule(name, CMakeTokens[name]);

    this.addRule('whitespace', /[ \t]+/, 0x8000);
  }
}

globalThis.CMakeLexer = CMakeLexer;

define(CMakeLexer.prototype, { [Symbol.toStringTag]: 'CMakeLexer' });

export default CMakeLexer;
