import { Lexer, Token, SyntaxError } from 'lexer.so';
import { CTokens } from './clexer.js';

export class BNFLexer extends Lexer {
  constructor(input, filename) {
    super(input, filename, Lexer.FIRST);

    this.addRules();

    this.mask = 0b01;
    this.skip = 0x80;
  }

  addRules() {
    this.define('char', /'(\\.|[^'\n])'/);
    this.define('chars', /'([^'\n])*'/);
    this.define('minus', /\-/);
    this.addRule('lbrace', /{/, 0b01);
    this.addRule('rbrace', /}/, 0b01);

    this.addRule('multiline_comment', /\/\*([^\*]|[\r\n]|(\*+([^\/\*]|[\n\r])))*\*+\//, 0b01);
    this.addRule('singleline_comment', /\/\/.*/, 0b01);
    this.addRule('identifier', /[@A-Za-z_]\w*/, 0b01);
    this.addRule('char_class', /\[[^\]]+\]/, 0b01);
    this.addRule('range', "'.'\\.\\.'.'", 0b01);
    this.addRule('literal', /'(\\.|[^'\n])*\'/, 0b01);
    this.addRule('bar', /\|/, 0b01);
    this.addRule('semi', /;/, 0b01);
    this.addRule('dcolon', /::/, 0b01);
    this.addRule('colon', /:/, 0b01);
    this.addRule('asterisk', /\*/, 0b01);
    this.addRule('dot', /\./, 0b01);
    this.addRule('plus', /\+/, 0b01);
    this.addRule('tilde', /\~/, 0b01);
    this.addRule('arrow', /->/, 0b01);
    this.addRule('equals', /=/, 0b01);
    // this.addRule('action', /{lbrace}[^${rbrace}]*{rbrace}/, 0b01);

    this.addRule('question', /\?/, 0b01);
    this.addRule('lparen', /\(/, 0b01);
    this.addRule('rparen', /\)/, 0b01);
    this.addRule('ws', /[ \t\r\n]+/, 0x80);

    for(let name in CTokens) this.addRule(name, CTokens[name], 0b10);
  }
}

export default BNFLexer;
