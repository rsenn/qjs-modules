import { define } from 'util';
import { Lexer } from 'lexer';
import { Token } from 'lexer';

/* Token rules ported from tests/Shell-Grammar.l (POSIX shell grammar).
 * Order matters for same-length ties in Lexer.LONGEST mode: multi-char
 * operators before their single-char prefixes, keywords before {NAME}. */
export const ShellTokens = {
  comment: /#[^\n]*/,

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

  /* Literal placeholder tokens, as given verbatim in the grammar. */
  word: /WORD/,
  assign: /ASSIGN/,
  redir: /REDIR/,

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

  whitespace: /[ \t\v\f]+/,
};

export class ShellLexer extends Lexer {
  constructor(input, mode = Lexer.LONGEST, filename, mask) {
    super(input, mode, filename, mask);

    this.addRules();
  }

  addRules() {
    for(const name in ShellTokens) this.addRule(name, ShellTokens[name]);

    /* "." { /* discard bad characters *\/ } */
    this.addRule('badChar', /[\s\S]/, (lexer, skip) => skip());
  }
}

globalThis.ShellLexer = ShellLexer;

define(ShellLexer.prototype, { [Symbol.toStringTag]: 'ShellLexer' });

export default ShellLexer;
