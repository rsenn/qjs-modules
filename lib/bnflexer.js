import { Lexer, Token, SyntaxError } from 'lexer';
import { CTokens } from './clexer.js';
import { JSDefines, JSRules } from './jslexer.js';

export class BNFLexer extends Lexer {
  static section = {
    PROLOGUE: 0,
    DECLARATIONS: 1,
    SCANNERCODE: 2
  };

  static states = {
    BNF: 1,
    C: 1 << 2,
    JS: 1 << 3,
    DIRECTIVE: 1 << 4,
    LEXDEFINE: 1 << 5,
    LEXPATTERN: 1 << 6,
    LEXRULES: 1 << 7,
    LEXACTION: 1 << 8,
    SKIP: 1 << 15
  };

  constructor(input, filename) {
    super(input, Lexer.FIRST, filename);

    this.mask = BNFLexer.states.BNF;
    this.skip = BNFLexer.states.SKIP;
    this.section = 1 ///\.y$/.test(filename)
      ? BNFLexer.section.PROLOGUE
      : BNFLexer.section.DECLARATIONS;

    this.addRules();
  }

  get state() {
    return Object.entries(BNFLexer.states).find(([name,value]) => this.mask & value)[0];
  }

  addRules() {
    const {
      BNF,
      C,
      JS,
      DIRECTIVE,
      LEXDEFINE,
      LEXPATTERN,
      LEXRULES,
      LEXACTION,
      SKIP
    } = BNFLexer.states;

    this.define('char', /'(\\.|[^'\n])'/);
    this.define('chars', /'([^'\n])*'/);
    this.define('minus', /\-/);
    this.define('word', /[A-Za-z_][-\w]*/);
    this.addRule('lbrace', /{/, DIRECTIVE | BNF, (tok, lexeme, state) => {
      this.braces = 0;
      if(this.beginCode) return this.beginCode(tok, lexeme, state);
      return JS | C;
    });
    this.addRule('rbrace', /}/, BNF);
    this.addRule('section', /%%/, BNF, (tok, lexeme, state) =>
      ++this.section == BNFLexer.section.SCANNERCODE
        ? this.beginCode
          ? this.beginCode(tok, lexeme, state)
          : C | JS
        : BNF
    );
    this.addRule('cstart', /%{/, BNF, () => C | JS);
    this.addRule('lexstart', /%lex/, BNF, () => LEXDEFINE);

    this.addRule('directive', /%{word}\b/, DIRECTIVE | BNF, () => {
      this.mode = Lexer.FIRST;
      return DIRECTIVE;
    });
    this.addRule('string', /<DIRECTIVE>"(\\.|[^"\n])*"/, DIRECTIVE);
    this.addRule('newline', /<DIRECTIVE>\r?\n/, DIRECTIVE, () => BNF);
    this.addRule('identifier', /<DIRECTIVE>[.A-Za-z_][-\w]*/, DIRECTIVE);
    this.addRule('number', /<DIRECTIVE>[0-9]+/, DIRECTIVE);
    this.addRule('name', /<DIRECTIVE><([-\w]*)>/, DIRECTIVE);
    this.addRule('any', /<DIRECTIVE><>/, DIRECTIVE | BNF);

    this.addRule('l_identifier', /[A-Za-z_][-\w]*/, LEXDEFINE, () => this.mask << 1);
    this.addRule('l_section', /%%/, LEXDEFINE, () => LEXRULES);
    this.addRule('l_pattern', /{RegularExpressionBody}/, LEXPATTERN);
    this.addRule('l_newline', /\r?\n/, LEXPATTERN, () => this.mask >> 1);

    this.addRule('l_pattern2', /[^\s]+/, LEXRULES, () => LEXACTION | C); //LEXRULES | (this.beginCode ? this.beginCode(tok, lexeme, state) : C | JS));
    this.addRule('l_newline2', /\r?\n/, LEXACTION | SKIP, () => LEXRULES); //LEXRULES | (this.beginCode ? this.beginCode(tok, lexeme, state) : C | JS));
    this.addRule('l_end', /\/lex/, LEXRULES, () => BNF);

    this.addRule('multiline_comment', /\/\*([^\*]|[\r\n]|(\*+([^\/\*]|[\n\r])))*\*+\//, SKIP | BNF);
    this.addRule('singleline_comment', /(\/\/.*|#.*)/, SKIP | BNF);
    this.addRule('identifier', /[@A-Za-z_][-\w]*/, BNF);
    this.addRule('range', "'.'\\.\\.'.'", BNF);
    this.addRule('dotdot', /\.\./, BNF);
    this.addRule('char_class', /\[([^\]\\]|\\.)+\]/, BNF);
    this.addRule('literal', /'(\\.|[^'\n])*'/, DIRECTIVE | BNF);
    this.addRule('bar', /\|/, BNF);
    this.addRule('comma', /,/, BNF);
    this.addRule('semi', /;/, DIRECTIVE | BNF);
    this.addRule('dcolon', /::/, BNF);
    this.addRule('colon', /:/, BNF);
    this.addRule('asterisk', /\*/, BNF);
    this.addRule('dot', /\./, BNF);
    this.addRule('plus', /\+/, BNF);
    this.addRule('tilde', /\~/, BNF);
    this.addRule('arrow', /->/, BNF);
    this.addRule('equals', /=/, BNF);
    // this.addRule('action', /{lbrace}[^${rbrace}]*{rbrace}/, BNF);

    this.addRule('question', /\?/, BNF);
    this.addRule('lparen', /\(/, BNF);
    this.addRule('rparen', /\)/, BNF);

    //this.addRule('bracegroup', /{[^}]*}/, BNF);

    this.addRule('ws', /[ \t\r\n]+/, SKIP | LEXDEFINE | LEXRULES | BNF);

    for(let name in CTokens) {
      let fn;
      if(name.endsWith('brace')) {
        fn =
          name[0] == 'l'
            ? () => {
                //console.log(`braces = ${this.braces}`);
                this.braces = (this.braces | 0) + 1;
              }
            : () => {
                //console.log(`braces = ${this.braces}`);
                this.braces = (this.braces | 0) - 1;
                if(this.braces < 0) return BNF;
              };
      }

      //console.log('c_' + name, '<C>' + Lexer.toString(CTokens[name]), C);
      this.addRule('c_' + name, '<C>' + Lexer.toString(CTokens[name]), C, fn);
    }

    for(let name in JSDefines) this.define(name, JSDefines[name]);
    for(let [name, expr, mask = JS] of JSRules) {
      let fn;
      if(name.startsWith('punctuator'))
        fn = (tok, lexeme, state) => {
          //console.log(`braces = ${this.braces}`);
          if(lexeme == '{') {
            this.braces = (this.braces | 0) + 1;
          } else if(lexeme == '}') {
            this.braces = (this.braces | 0) - 1;
            if(this.braces < 0) return BNF;
          }
        };

      this.addRule('js_' + name, '<JS>' + Lexer.toString(expr), mask, fn);
    }

    this.addRule('target', /\$\$/, JS | C);
    this.addRule('symbol', /\$([0-9]+|{Identifier})/, JS | C);
    this.addRule('cend', /%}/, JS | C, () => BNF);
  }

  get [Symbol.toStringTag]() {
    return "BNFLexer";
  }
}

export default BNFLexer;
