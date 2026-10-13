/* lexer/cpp.js: CPPDefines, CPPRules, CPPLexer.
 * rules for the C preprocessor language; lexer/c.js merges them into CLexer.
 * rule: every rule name starts with pp_; states are INITIAL (the C
 * language), CPP (inside a directive), CPP_DIR (just after the `#`) and
 * CPP_INC (after #include, where <name.h> is one token). */
import { define, declare, enumerable } from '../util.js';
import { Lexer } from 'lexer';

export const CPPDefines = enumerable({
  PPSpace: /[ \t\v\f\r]/,
  PPNewline: /\r?\n/,
  PPIdentifier: /[A-Za-z_]\w*/,
  PPNumber: /\.?[0-9]([eEpP][+-]|[\w.])*/,
  PPString: /(u8|[LuU])?"(\\.|[^\\"\n])*"/,
  PPChar: /[LuU]?'(\\.|[^\\'\n])+'/,
  PPBlockComment: /\/\*([^*]|\*+[^*\/])*\*+\//,
  PPLineComment: /\/\/[^\n]*/,
  PPHeaderName: /<[^>\n]+>/,
  PPPunctuator: /%:%:|\.\.\.|<<=|>>=|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||\*=|\/=|%=|\+=|-=|&=|\^=|\|=|##|<:|:>|<%|%>|%:|[-+*\/%&|^~!<>=?:;,.()\[\]{}#]/,
});

/* a whole directive line as one token, for lexers without a CPP mode */
export const CPPLine = /#[^\n\\]*(:?\\\n[^\n\\]*|\\.[^\n\\]*)*/;

/* leaves the directive: back to INITIAL whatever depth it is at */
const endDirective = lexer => {
  while(lexer.topState() != 'INITIAL') lexer.popState();
};

/* #include and friends: the next <...> is a header name */
const INCLUDES = new Set(['include', 'include_next', 'import']);

export const CPPRules = [
  /* `#` at the start of a line (or of the input, or right after a
   * directive ended): `\n  #` is one token, longer than whitespace */
  ['pp_hash', /<INITIAL>(\r?\n[ \t]*)?#/, lexer => lexer.pushState('CPP_DIR') && undefined],
  ['pp_comment', /<CPP,CPP_DIR,CPP_INC>({PPBlockComment}|{PPLineComment})/],
  ['pp_continuation', /<CPP,CPP_DIR,CPP_INC>\\\r?\n/, (lexer, skip) => skip()],
  [
    'pp_whitespace',
    /<CPP,CPP_DIR,CPP_INC>{PPSpace}+/, //, (lexer, skip) => (skip(),0)
  ],
  ['pp_newline', /<CPP,CPP_DIR,CPP_INC>{PPNewline}/, endDirective],
  [
    'pp_directive',
    /<CPP_DIR>{PPIdentifier}/,
    (lexer, ...args) => {
      lexer.popState();
      lexer.pushState(INCLUDES.has(lexer.lexeme) ? 'CPP_INC' : 'CPP');
    },
  ],
  ['pp_header_name', /<CPP_INC>{PPHeaderName}/, lexer => lexer.popState() && lexer.pushState('CPP')],
  ['pp_identifier', /<CPP,CPP_INC>{PPIdentifier}/],
  ['pp_number', /<CPP,CPP_INC>{PPNumber}/],
  ['pp_string', /<CPP,CPP_INC>{PPString}/],
  ['pp_char', /<CPP,CPP_INC>{PPChar}/],
  ['pp_punctuator', /<CPP,CPP_DIR,CPP_INC>{PPPunctuator}/],
  /* anything else on the line, e.g. the `'` in `#error don't` */
  ['pp_other', /<CPP,CPP_DIR,CPP_INC>[^ \t\v\f\r\n]/],
];

export class CPPLexer extends Lexer {
  constructor(input, mode = Lexer.LONGEST, filename, mask) {
    super(input, mode, filename, mask);

    this.addDefines();
    this.addRules();
  }

  addDefines() {
    for(const name of Object.getOwnPropertyNames(CPPDefines)) this.define(name, CPPDefines[name]);
  }

  addRules() {
    for(const [name, ...rule] of CPPRules) this.addRule(name, ...rule);

    /* between directives: blanks and newlines that are not followed by `#` */
    this.addRule('whitespace', /<INITIAL>[ \t\v\r\n\f]/);
  }
}

declare(CPPLexer.prototype, { [Symbol.toStringTag]: 'CPPLexer' });

export default CPPLexer;
