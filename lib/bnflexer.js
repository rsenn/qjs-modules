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
    //console.log('BNFLexer.constructor', { input, filename });
    super(input, Lexer.FIRST, filename);

    this.mask = -1; //BNFLexer.states.BNF;
    this.skip = 0; //BNFLexer.states.SKIP;
    this.section = 1 ///\.y$/.test(filename)
      ? BNFLexer.section.PROLOGUE
      : BNFLexer.section.DECLARATIONS;

    this.handler = (arg, tok) =>
      console.log(
        'Unmatched token at ' +
          arg.loc +
          ' state=' +
          arg.states[arg.state] +
          '\n' +
          arg.currentLine() +
          '\n' +
          ' '.repeat(arg.loc.column - 1) +
          '^'
      );

    this.addRules();
  }

  /*  get state() {
    return Object.entries(BNFLexer.states).find(([name,value]) => this.mask & value)[0];
  }*/

  addRules() {
    const { BNF, C, JS, DIRECTIVE, LEXDEFINE, LEXPATTERN, LEXRULES, LEXACTION, SKIP } = BNFLexer.states;

    this.define('char', /'(\\.|[^'\n])'/);
    this.define('chars', /'([^'\n])*'/);
    this.define('minus', /\-/);
    this.define('word', /[A-Za-z_][-\w]*/);
    this.addRule('lbrace', /{/, (tok, lexeme, state) => {
      this.braces = 0;
      if(this.beginCode) return this.beginCode(tok, lexeme, state);
      this.pushState(globalThis.code ?? 'C');
    });
    this.addRule('rbrace', /}/);
    this.addRule('section', /%%/, (tok, lexeme, state) =>
      ++this.section == BNFLexer.section.SCANNERCODE
        ? this.beginCode
          ? this.beginCode(tok, lexeme, state)
          : this.pushState(globalThis.code ?? 'C')
        : undefined
    );
    this.addRule('cstart', /%{/, () => this.pushState(globalThis.code ?? 'C'));
    this.addRule('cend', /<C>%}/, () => this.popState());
    this.addRule('cend', /<JS>%}/, () => this.popState());
    this.addRule('action', /{/, () => this.pushState(globalThis.code ?? 'C'));
    this.addRule('lexstart', /%lex/, () => this.pushState('LEXDEFINE'));

    this.addRule('directive', /%{word}\b/, () => {
      this.mode = Lexer.FIRST;
      this.pushState('DIRECTIVE');
      //      return DIRECTIVE;
    });
    this.addRule('string', /<DIRECTIVE>"(\\.|[^"\n])*"/);
    this.addRule('newline', /<DIRECTIVE>\r?\n/, () => {
      this.popState();
      this.mode = Lexer.FIRST;
    });

    this.addRule('ws', /<DIRECTIVE>[ \t]+/, (lexer, skip) => skip());
    this.addRule('identifier', /<DIRECTIVE>[.A-Za-z_][-\w]*/);
    this.addRule('number', /<DIRECTIVE>[0-9]+/);
    this.addRule('name', /<DIRECTIVE><([-\w]*)>/);
    this.addRule('any', /<DIRECTIVE><>/);
    this.addRule('l_ws', /<LEXDEFINE>[ \t]+/ /*, (lexer, skip) => skip()*/);
    this.addRule('l_section', /<LEXDEFINE>%%/, () => this.pushState('LEXRULES'));
    this.addRule('l_identifier', /<LEXDEFINE>[A-Za-z_][-\w]*/);
    this.addRule('l_newline', /<LEXDEFINE>\r?\n/);

    this.addRule('p_section', /<LEXPATTERN>%%\n/);
    this.addRule('p_literal', /<LEXPATTERN>\"(\\.|[^\\\"\n])*\"|\'(\\.|[^\\\'\n])*\'/);
    this.addRule('p_state', /<LEXPATTERN><[A-Za-z0-9_,*]+>/);
    this.addRule('p_subst', /<LEXPATTERN>{[A-Za-z0-9_]+}/);
    this.addRule('p_lp', /<LEXPATTERN>\(/);
    this.addRule('p_rp', /<LEXPATTERN>\)/);
    this.addRule('p_bar', /<LEXPATTERN>\|/);
    this.addRule('p_dot', /<LEXPATTERN>\./);
    this.addRule('p_escape', /<LEXPATTERN>\\./);
    this.addRule('p_postfix', /<LEXPATTERN>[?*+]/);
    this.addRule('p_cstart', /<LEXPATTERN>[ \t]+{/);
    this.addRule('p_class', /<LEXPATTERN>\[([^\]\\]|\\.)+\]/);
    this.addRule('p_ws', /<LEXPATTERN>[ \t]+/);
    this.addRule('p_newline', /<LEXPATTERN>\r?\n/);
    this.addRule('p_char', /<LEXPATTERN>./);

    this.addRule('multiline_comment', /\/\*([^\*]|[\r\n]|(\*+([^\/\*]|[\n\r])))*\*+\//, (lexer, skip) => skip());
    this.addRule('singleline_comment', /(\/\/.*|#.*)/, (lexer, skip) => skip());
    this.addRule('identifier', /[@A-Za-z_][-\w]*/);
    this.addRule('assoc', /[<]assoc=[a-z]*[>]/);
    this.addRule('range', "'.'\\.\\.'.'");
    this.addRule('dotdot', /\.\./);
    this.addRule('char_class', /\[([^\]\\]|\\.)+\]/);
    this.addRule('literal', /'(\\.|[^'\n])*'/);
    this.addRule('bar', /\|/);
    this.addRule('comma', /,/);
    this.addRule('semi', /;/);
    this.addRule('dcolon', /::/);
    this.addRule('colon', /:/);
    this.addRule('asterisk', /\*/);
    this.addRule('dot', /\./);
    this.addRule('plus', /\+/);
    this.addRule('tilde', /\~/);
    this.addRule('arrow', /->/);
    this.addRule('equals', /=/);
    // this.addRule('action', /{lbrace}[^${rbrace}]*{rbrace}/);

    this.addRule('question', /\?/);
    this.addRule('lparen', /\(/);
    this.addRule('rparen', /\)/);

    //this.addRule('bracegroup', /{[^}]*}/);

    this.addRule('ws', /[ \t\r\n]+/, (lexer, skip) => skip());

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
      this.addRule(name, '<C>' + Lexer.toString(CTokens[name]), fn);
    }
    this.addRule('newline', '<C>\r?\n', (lexer, skip) => {
      /*skip();*/
    });
    this.addRule('ws', '<C>[ \t]+', (lexer, skip) => skip());

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

      this.addRule(name, '<JS>' + Lexer.toString(expr), mask, fn);
    }

    this.addRule('target', /\$\$/, JS | C);
    this.addRule('symbol', /<C>\$([0-9]+|{Identifier})/);
  }

  /* prettier-ignore */ get [Symbol.toStringTag]() {
    return "BNFLexer";
  }
}

globalThis.BNFLexer = BNFLexer;

export default BNFLexer;
