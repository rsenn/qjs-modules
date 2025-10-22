import { CTokens } from './c.js';
import { ECMAScriptDefines } from './ecmascript.js';
import { ECMAScriptRules } from './ecmascript.js';
import { Lexer } from 'lexer';
import { Token } from 'lexer';

export class BNFLexer extends Lexer {
  static section = {
    PROLOGUE: 0,
    DECLARATIONS: 1,
    SCANNERCODE: 2,
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
    SKIP: 1 << 15,
  };

  constructor(input, filename) {
    //console.log('BNFLexer.constructor', { input, filename });
    super(input, Lexer.FIRST, filename);

    this.mask = -1; //BNFLexer.states.BNF;
    this.skip = 0; //BNFLexer.states.SKIP;
    this.section = 1 ///\.y$/.test(filename)
      ? BNFLexer.section.PROLOGUE
      : BNFLexer.section.DECLARATIONS;

    this.handler = (arg, tok) => {
      const { id, type, lexeme } = this.token;

      console.log(`Unmatched token '${type}' (${id}) at ` + arg.loc + ' state=' + arg.states[arg.state] + '\n' + arg.currentLine() + '\n' + ' '.repeat(arg.loc.column - 1) + '^');
    };

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

    this.addRule('keyword', /(grammar)/);
    this.addRule('lbrace', /{/, (tok, lexeme, state) => {
      this.braces = 0;
      if(this.beginCode) return this.beginCode(tok, lexeme, state);
      this.pushState(globalThis.code ?? 'C');
    });
    this.addRule('rbrace', /}/);

    this.addRule('section2', /%%/);
    this.addRule(
      'section',
      /%%/,
      () => this.pushState('LEXRULES') /*, (tok, lexeme, state) =>
      ++this.section == BNFLexer.section.SCANNERCODE
        ? this.beginCode
          ? this.beginCode(tok, lexeme, state)
          : this.pushState(globalThis.code ?? 'C')
        : undefined*/,
    );

    this.addRule('cstart', /%{/, () => this.pushState(globalThis.code ?? 'C'));
    this.addRule('cend', /<C,JS>%}/, () => this.popState());
    this.addRule('action', /{/, () => this.pushState(globalThis.code ?? 'C'));
    this.addRule('lexstart', /%lex/, () => this.pushState('LEXDEFINE'));
    //this.addRule('comment', /\/\/[^\n]*/);
    this.addRule('newline', /\r?\n/, (lexer, skip) => skip());
    this.addRule('x_ws', /[ \t]+/, (lexer, skip) => skip());

    this.addRule('directive', /%[A-Za-z_][A-Za-z0-9_]*\b/, () => {
      this.mode = Lexer.FIRST;
      this.pushState('DIRECTIVE');
    });
    this.addRule('d_string', /<DIRECTIVE>"(\\.|[^"\n])*"/);
    this.addRule('d_newline', /<DIRECTIVE>\r?\n/, (lexer, skip) => {
      this.popState();
      this.mode = Lexer.FIRST;
      //skip();f
    });

    this.addRule('d_ws', /<DIRECTIVE>[ \t]+/, (lexer, skip) => skip());
    this.addRule('d_identifier', /<DIRECTIVE>[.A-Za-z_][-\w]*/);
    this.addRule('d_number', /<DIRECTIVE>[0-9]+/);
    this.addRule('d_name', /<DIRECTIVE><([-\w]*)>/);
    this.addRule('d_any', /<DIRECTIVE><>/);
    this.addRule('l_ws', /<LEXDEFINE>[ \t]+/, (lexer, skip) => skip());
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
    this.addRule('p_ws', /<LEXPATTERN>[ \t]+/, (lexer, skip) => skip());
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

    this.addRule('ws', /[ \t\r\n]/ /*, (lexer, skip) => skip()*/);

    for(const name in CTokens) {
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
      this.addRule('c_' + name, '<C>' + Lexer.toString(CTokens[name]), fn);
    }
    this.addRule('c_newline', '<C>\\r?\n', (lexer, skip) => {
      /*skip();*/
    });
    this.addRule('c_ws', '<C>[ \\t]+', (lexer, skip) => skip());

    for(const name in ECMAScriptDefines) this.define(name, ECMAScriptDefines[name]);
    for(const [name, expr, mask = JS] of ECMAScriptRules) {
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
    this.addRule('symbol', /<C>\$([0-9]+|{Identifier})/);
  }

  /* prettier-ignore */ get [Symbol.toStringTag]() {
    return "BNFLexer";
  }
}

globalThis.BNFLexer = BNFLexer;

export default BNFLexer;
