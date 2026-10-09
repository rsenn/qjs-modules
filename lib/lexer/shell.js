import { define } from 'util';
import { Lexer } from 'lexer';
import { Token } from 'lexer';

/* WORD/ASSIGN/REDIR were ported from tests/Shell-Grammar.l as literal
 * placeholder rules (matching the text "WORD" etc. verbatim) - the .l
 * file leaves their real lexing to the grammar's semantic actions,
 * which this lexer has none of. These defines fill that gap with
 * actual POSIX shell lexical rules:
 *  - WORD: a run of single/double-quoted spans, `backquoted` command
 *    substitution, $(...)/$((...)) and ${...} expansions (one level of
 *    nested parens/braces, so arithmetic's double parens and a nested
 *    ${var:+...${other:-x}...} both work), $NAME/$1/$@ etc. parameter
 *    refs, \-escapes, and plain unquoted chars.
 *  - ASSIGN: NAME=WORD (POSIX's ASSIGNMENT_WORD).
 *  - REDIR: the fd-duplication/close target after <& or >& (a digit
 *    run, or "-" to close the descriptor) - not a real .y token, but
 *    the closest POSIX concept to what "REDIR" names. */
export const ShellDefines = {
  SingleQuoted: /'[^']*'/,
  DoubleQuoted: /"(\\.|[^\\"])*"/,
  Backquoted: /`(\\.|[^`\\])*`/,
  DollarParen: /\$\((?:[^()]|\([^()]*\))*\)/,
  DollarBrace: /\$\{(?:[^{}]|\{[^{}]*\})*\}/,
  DollarSimple: /\$([A-Za-z_][A-Za-z0-9_]*|[0-9]|[@*#?$!_-])/,
  /* [\s\S] (not '.') so a \<newline> line continuation is consumed as
   * part of the word instead of splitting it and losing the backslash
   * to badChar. */
  Escaped: /\\[\s\S]/,
  WordChar: /[^\s|&;()<>\\$'"`]+/,
  WORD: /({SingleQuoted}|{DoubleQuoted}|{Backquoted}|{DollarParen}|{DollarBrace}|{DollarSimple}|{Escaped}|{WordChar})+/,
  ASSIGN: /[A-Za-z_][A-Za-z0-9_]*=({WORD})?/,
  REDIR: /[0-9]+|-/,
};

/* Token rules ported from tests/Shell-Grammar.l (POSIX shell grammar).
 * Order matters for same-length ties in Lexer.LONGEST mode: multi-char
 * operators before their single-char prefixes, keywords before {NAME},
 * and {NAME}/keywords before {WORD} (WORD's plain-text alternative
 * matches the same span as a bare NAME or keyword; see the comment
 * below). */
export const ShellTokens = {
  /* INITIAL only: in the SHEBANG state a CRLF-terminated `#!` line would be one byte
   * longer as a comment (`#[^\n]*` takes the \r) and win in LONGEST mode */
  comment: /<INITIAL>#[^\n]*/,

  newline: /\n/,

  dsemi: /;;/,
  semi: /;/,
  and_if: /&&/,
  backgnd: /&/,
  or_if: /\|\|/,
  pipe: /\|/,
  lparen: /\(/,
  rparen: /\)/,
  bq: /`/,

  dlessdash: /<<-/,
  dless: /<</,
  lessand: /<&/,
  lessgreat: /<>/,
  less: /</,
  dgreat: />>/,
  greatand: />&/,
  clobber: />\|/,
  great: />/,

  io_number: /[0-9]+(?=[<>])/,

  /* Keywords are listed before {NAME} in Lexer.LONGEST mode so that an
   * equal-length match (e.g. "if") resolves to the keyword, not NAME -
   * the grammar relies on flex's same-length "first rule wins" rule,
   * but lists NAME (line 33) ahead of the keywords (lines 39-54); ported
   * here in the order that actually makes keyword recognition work. */
  bang: /!/,
  case: /case/,
  do: /do/,
  done: /done/,
  elif: /elif/,
  else: /else/,
  esac: /esac/,
  fi: /fi/,
  for: /for/,
  if: /if/,
  in: /in/,
  then: /then/,
  until: /until/,
  while: /while/,
  lbrace: /{/,
  rbrace: /}/,

  name: /[a-zA-Z_][a-zA-Z_0-9]+/,

  /* WORD/ASSIGN/REDIR go last: WORD's plain-text alternative overlaps
   * both {NAME} and the keywords above on a bare word (e.g. "if"), so
   * they're registered after those to lose that tie - WORD only wins
   * where it actually matches more (quotes, expansions, escapes). */
  assign: /{ASSIGN}/,
  word: /{WORD}/,
  redir: /{REDIR}/,

  whitespace: /[ \t\v\f]+/,
};

/* input is a string, an ArrayBuffer or a view on one (typed array, Buffer);
 * same check as lib/lexer/ecmascript.js */
function startsWithShebang(input) {
  if(typeof input == 'string') return input.startsWith('#!');

  if(input instanceof ArrayBuffer || ArrayBuffer.isView(input)) {
    const u8 = ArrayBuffer.isView(input) ? new Uint8Array(input.buffer, input.byteOffset, input.byteLength) : new Uint8Array(input);

    return u8.length >= 2 && u8[0] == 0x23 && u8[1] == 0x21;
  }

  return false;
}

export class ShellLexer extends Lexer {
  constructor(input, mode = Lexer.LONGEST, filename, mask) {
    super(input, mode, filename, mask);

    this.addDefines();
    this.addRules();

    /* a `#!` line is a token of its own only at the very start of the input;
       anywhere else it is an ordinary comment */
    if(startsWithShebang(input)) this.pushState('SHEBANG');
  }

  addDefines() {
    for(const name in ShellDefines) this.define(name, ShellDefines[name]);
  }

  addRules() {
    this.addRule('shebang', '<SHEBANG>#![^\\r\\n]*', lex => lex.popState());

    for(const name in ShellTokens) this.addRule(name, ShellTokens[name]);

    /* "." { /* discard bad characters *\/ } */
    this.addRule('badChar', /[\s\S]/, (lexer, skip) => skip());
  }
}

globalThis.ShellLexer = ShellLexer;

define(ShellLexer.prototype, { [Symbol.toStringTag]: 'ShellLexer' });

export default ShellLexer;
