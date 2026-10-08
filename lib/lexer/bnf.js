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

  /* `{ ... }` is embedded code in these files, and repetition in plain EBNF */
  static actionFiles = /\.(y|yy|l|ll|g4)$/i;

  constructor(input, filename) {
    //console.log('BNFLexer.constructor', { input, filename });
    super(input, Lexer.FIRST, filename);

    this.mask = -1;
    this.skip = 0;
    this.section = /\.y$/.test(filename ?? '') ? BNFLexer.section.PROLOGUE : BNFLexer.section.DECLARATIONS;
    this.actions = BNFLexer.actionFiles.test(filename ?? '');
    this.yacc = /\.yy?$/i.test(filename ?? '');
    this.antlr = /\.g4$/i.test(filename ?? '');
    this.sections = 0;

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

    /* ANTLR's reserved words only count in .g4 files */
    this.addRule('keyword', this.antlr ? /(grammar|lexer|parser|fragment|import|mode|options|tokens|channels|returns|locals|throws|catch|finally)(?![-\w])/ : /grammar(?![-\w])/);
    this.addRule('lbrace', /{/, (tok, lexeme, state) => {
      if(!this.actions) return;
      this.braces = 0;
      if(this.beginCode) return this.beginCode(tok, lexeme, state);
      this.pushState(globalThis.code ?? 'C');
    });
    this.addRule('rbrace', /}/);

    this.addRule('section2', /%%/, () => {
      /* what follows the second %% of a yacc file is C code */
      if(++this.sections == 2 && this.yacc) this.pushState('C');
    });
    //this.addRule('section2', /%%/, lexer => lexer.pushState('LEXRULES'));

    this.addRule('action', /{/, lexer => lexer.pushState(globalThis.code ?? 'C'));
    this.addRule('lexstart', /%lex/, lexer => lexer.pushState('LEXDEFINE'));
    //this.addRule('comment', /\/\/[^\n]*/);
    this.addRule('x_ws', /[ \t]+/, (lexer, skip) => skip());

    this.addRule('directive', /%[A-Za-z_][A-Za-z0-9_]*\b/, lexer => {
      lexer.mode = Lexer.FIRST;
      lexer.pushState('DIRECTIVE');
    });
    this.addRule('d_string', /<DIRECTIVE>"(\\.|[^"\n])*"/);
    this.addRule('d_newline', /<DIRECTIVE>\r?\n/, (lexer, skip) => {
      lexer.popState();
      lexer.mode = Lexer.FIRST;
    });

    this.addRule('d_ws', /<DIRECTIVE>[ \t]+/, (lexer, skip) => skip());
    this.addRule('d_identifier', /<DIRECTIVE>[.A-Za-z_][-\w]*/);
    this.addRule('d_number', /<DIRECTIVE>[0-9]+/);
    this.addRule('d_name', /<DIRECTIVE><([-\w]*)>/);
    this.addRule('d_any', /<DIRECTIVE><>/);
    this.addRule('l_ws', /<LEXDEFINE>[ \t]+/, (lexer, skip) => skip());
    this.addRule('l_section', /<LEXDEFINE>%%/, lexer => lexer.pushState('LEXRULES'));
    this.addRule('l_identifier', /<LEXDEFINE>[A-Za-z_][-\w]*/);
    this.addRule('l_newline', /<LEXDEFINE>\r?\n/);

    this.addRule('r_identifier', /<LEXRULES>[.A-Za-z_][-\w]*/);
    this.addRule('r_colon', /<LEXRULES>:/);
    this.addRule('r_pipe', /<LEXRULES>\|/);
    this.addRule('r_semi', /<LEXRULES>;/);
    this.addRule('r_ws', /<LEXRULES>[ \t\r\n]+/, (lexer, skip) => skip());

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

    /* comments are tokens; `#` is one only in the grammar itself, so that
     * `#include` in embedded C is left to the C rules */
    this.addRule('multiline_comment', /\/\*([^\*]|[\r\n]|(\*+([^\/\*]|[\n\r])))*\*+\//);
    this.addRule('ebnf_comment', /<INITIAL>\(\*([^*]|\*+[^)*])*\*+\)/);
    this.addRule('singleline_comment', /\/\/.*/);
    if(this.actions) this.addRule('hash_comment', /<INITIAL>#.*/);
    else {
      this.addRule('hex', /<INITIAL>#x[0-9A-Fa-f]+/);
      this.addRule('hash_comment', /<INITIAL>#(?!x[0-9A-Fa-f]).*/);
    }
    this.addRule('identifier', /[@A-Za-z_][-\w]*/);
    this.addRule('assoc', /[<]assoc=[a-z]*[>]/);
    this.addRule('nonterminal', /<INITIAL><[A-Za-z_][^<>\n]*>/);
    this.addRule('range', "'.'\\.\\.'.'");
    this.addRule('dotdot', /\.\./);

    /* `[ \t]` is a character class in yacc/lex/ANTLR; in EBNF `[ a , b ]` is
     * an option, so a class there has no spaces, commas, bars or quotes */
    if(this.actions) this.addRule('char_class', /\[([^\]\\]|\\.)+\]/);
    else {
      this.addRule('char_class', /\[\^?([^\]\\\s,|'"]|\\.)+\]/);
      this.addRule('lbracket', /\[/);
      this.addRule('rbracket', /\]/);
    }

    this.addRule('literal', /'(\\.|[^'\n])*'/);
    this.addRule('string', /<INITIAL>"(\\.|[^"\n])*"/);

    /* `? any text ?`; a `?` right after a name or `)` is the optional mark */
    if(!this.actions) this.addRule('special', /<INITIAL>(?<![\w)\]}'"*+?])\?[^?\n]+\?/);

    this.addRule('number', /<INITIAL>[0-9]+/);
    this.addRule('bar', /\|/);
    this.addRule('comma', /,/);
    this.addRule('semi', /;/);
    this.addRule('assign', /::=/);
    this.addRule('dcolon', /::/);
    this.addRule('colon', /:/);
    this.addRule('asterisk', /\*/);
    this.addRule('dot', /\./);
    this.addRule('plus', /\+/);
    this.addRule('tilde', /\~/);
    this.addRule('arrow', /->/);
    this.addRule('minus', /<INITIAL>-/);
    this.addRule('equals', /=/);
    // this.addRule('action', /{lbrace}[^${rbrace}]*{rbrace}/);

    this.addRule('question', /\?/);
    this.addRule('lparen', /\(/);
    this.addRule('rparen', /\)/);

    //this.addRule('bracegroup', /{[^}]*}/);

    //this.addRule('newline', /\r?\n/, (lexer, skip) => skip());
    this.addRule('ws', /[ \t\r\n]/ /*, (lexer, skip) => skip()*/);

    this.addRule('cstart', /%{/, lexer => lexer.pushState(globalThis.code ?? 'C'));
    this.addRule('cend', /<C,JS>%}/, lexer => lexer.popState());

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

    /* plain EBNF is free-form: any other character is a token, not an error */
    if(!this.actions) this.addRule('other', /<INITIAL>[\s\S]/);
  }

  /* prettier-ignore */ get [Symbol.toStringTag]() {
    return "BNFLexer";
  }
}

globalThis.BNFLexer = BNFLexer;

export default BNFLexer;
