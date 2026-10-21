/* lexer/cpp.js: CPPDefines, CPPRules, CPPLexer, foldDirectives.
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
  /* a backslash before the newline joins the lines, skipped, never a token */
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

/* one directive, the pp_* tokens from pp_hash on, as one 'preprocessor'
 * token from '#' to the line end, continuations included. pp_hash can
 * carry the newline before the '#' ("\n#"). */
function directive(source, run, filename) {
  const first = run[0],
    last = run.findLast(t => t.type != 'pp_newline');
  const skip = first.lexeme.indexOf('#');
  const charPos = first.charPos + skip;
  const lines = first.lexeme.slice(0, skip).split('\n').length - 1;
  const lexeme = source.slice(charPos, last.charPos + last.charLength);
  const loc = { line: first.loc.line + lines, column: lines ? 1 : first.loc.column + skip, filename };

  return { type: 'preprocessor', lexeme, charPos, charLength: lexeme.length, loc };
}

/**
 * Folds the pp_* tokens of a CLexer into one 'preprocessor' token per
 * directive, for scanners that only care where a directive is and what
 * its text says.
 *
 *   foldDirectives([int, a, semi, pp_hash, pp_directive, ...], src)
 *   [int, a, semi, { type: 'preprocessor', lexeme: '#define A 1', ... }]
 *
 *   Token[]  tokens    in source order, any non-pp_ tokens may be dropped
 *   string   source    the text they were lexed from
 *   string   filename  for the loc of the new tokens
 *
 *   returns a new array, other tokens unchanged
 */
export function foldDirectives(tokens, source, filename) {
  const out = [];
  let run = null;

  const flush = () => {
    if(run) out.push(directive(source, run, filename));
    run = null;
  };

  for(const tok of tokens) {
    if(tok.type == 'pp_hash') {
      flush();
      run = [tok];
    } else if(run && tok.type.startsWith('pp_')) {
      run.push(tok);
    } else {
      flush();
      out.push(tok);
    }
  }

  flush();
  return out;
}

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
