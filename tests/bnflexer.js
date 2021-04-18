import { Lexer, Token, SyntaxError } from 'lexer.so';
import { CTokens } from './clexer.js';

export class BNFLexer extends Lexer {
  constructor(input, filename) {
    super(input, filename, Lexer.FIRST);

    this.addRules();
  }

  addRules() {
    this.define('CHAR', /'(\\.|[^'\n])'/);
    this.define('CHARS', /'([^'\n])*'/);
    this.define('MINUS', /\-/);
    this.addRule('LBRACE', /{/);
    this.addRule('RBRACE', /}/);

    this.addRule('MULTILINE_COMMENT', /\/\*([^\*]|[\r\n]|(\*+([^\/\*]|[\n\r])))*\*+\//);
    this.addRule('SINGLELINE_COMMENT', /\/\/.*/);
    this.addRule('IDENTIFIER', /[@A-Za-z_]\w*/);
    this.addRule('CHAR_CLASS', /\[[^\]]+\]/);
    this.addRule('RANGE', "'.'\\.\\.'.'");
    this.addRule('LITERAL', /'(\\.|[^'\n])*\'/);
    this.addRule('BAR', /\|/);
    this.addRule('SEMI', /;/);
    this.addRule('DCOLON', /::/);
    this.addRule('COLON', /:/);
    this.addRule('ASTERISK', /\*/);
    this.addRule('DOT', /\./);
    this.addRule('PLUS', /\+/);
    this.addRule('TILDE', /\~/);
    this.addRule('ARROW', /->/);
    this.addRule('EQUALS', /=/);
    // this.addRule('ACTION', /{LBRACE}[^${RBRACE}]*{RBRACE}/);

    this.addRule('QUESTION', /\?/);
    this.addRule('LP', /\(/);
    this.addRule('RP', /\)/);
    this.addRule('WS', /[ \t\r\n]+/);
  }
}

export default BNFLexer;
