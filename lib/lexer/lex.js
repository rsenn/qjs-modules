import { define } from 'util';
import { Lexer } from 'lexer';
import { Token } from 'lexer';

/* Nested `{ ... }` of C code: strings, char literals and comments may hold
 * unbalanced braces, so they are matched as units; depth is bounded. */
const CString = String.raw`"(?:\\[\s\S]|[^"\\])*"`;
const CChar = String.raw`'(?:\\[\s\S]|[^'\\\n])*'`;
const CComment = String.raw`/\*[\s\S]*?\*/|//[^\n]*`;
const CAtom = String.raw`[^{}"'/]|/(?![*/])|${CString}|${CChar}|${CComment}|'`;

const braces = depth => (depth ? String.raw`\{(?:${CAtom}|${braces(depth - 1)})*\}` : String.raw`\{(?:${CAtom})*\}`);

/* one item of a flex pattern; whitespace ends the pattern unless it is
 * quoted, in a [class] or escaped. jison also quotes with '...'. */
const patternItem = jison => [
  String.raw`"(?:\\.|[^"\\\n])*"`,
  ...(jison ? [String.raw`'(?:\\.|[^'\\\n])*'`] : []),
  String.raw`\[\^?\]?(?:\[:\^?\w+:\]|\\.|[^\]\\\n])*\]`,
  String.raw`\{[^{}\s]*\}`,
  String.raw`\\(?:x[0-9A-Fa-f]{1,2}|[0-7]{1,3}|.)`,
  String.raw`[^\s"\[\\{]`,
].join('|');

/* flex source -> tokens: the three sections split by %% (INITIAL, RULES, USER) */
export const LexTokens = {
  /* [name, pattern] in the order they are tried (Lexer.FIRST) */
  codeblock: String.raw`(?<![^\n])%\{[\s\S]*?(?<![^\n])%\}`,
  top: String.raw`(?<![^\n])%top\s*${braces(6)}`,
  section: String.raw`(?<![^\n])%%`,
  comment: String.raw`/\*[\s\S]*?\*/`,
  directive: String.raw`(?<![^\n])%[A-Za-z]+`,
  eof: String.raw`(?<![^\n])(?:<[\w,*]+>)?<<EOF>>`,
  scope: String.raw`(?<![^\n])<[\w,*]+>\{(?=[ \t]*(?:\n|$))`,
  condition: String.raw`(?<![^\n])<[\w,*]+>`,
  indented: String.raw`(?<![^\n])[ \t]+[^\s][^\n]*`,
  name: String.raw`(?<![^\n])[A-Za-z_][\w-]*(?=[ \t])`,
  regex: jison => String.raw`(?:(?<![^\n])|(?<=(?<![^\n])<[\w,*]+>))(?!<[\w,*]*>)(?:${patternItem(jison)})+`,
};

export class LexLexer extends Lexer {
  /* jison: single-quoted strings in patterns too (`'"'[^"]+'"'`) */
  constructor(input, mode = Lexer.FIRST, filename, mask, jison = false) {
    super(input, mode, filename, mask);

    this.jison = jison;
    this.addRules();
  }

  addRules() {
    const rx = src => new RegExp(src);

    /* section 1: definitions */
    this.addRule('codeblock', rx('<INITIAL,RULES,SCOPE>' + LexTokens.codeblock));
    this.addRule('top', rx('<INITIAL>' + LexTokens.top));
    this.addRule('section', rx('<INITIAL>' + LexTokens.section), lexer => lexer.pushState('RULES'));
    this.addRule('user_section', rx('<RULES,SCOPE>' + LexTokens.section), lexer => {
      lexer.popState();
      lexer.pushState('USER');
    });
    this.addRule('comment', rx('<INITIAL>' + LexTokens.comment));
    this.addRule('rule_comment', rx('<RULES,SCOPE>(?<![^\\n])' + LexTokens.comment));
    this.addRule('directive', rx('<INITIAL>' + LexTokens.directive), lexer => lexer.pushState('DIRECTIVE'));
    this.addRule('d_string', rx('<DIRECTIVE>"(?:\\\\.|[^"\\n])*"'));
    this.addRule('d_equals', rx('<DIRECTIVE>='));
    this.addRule('d_number', rx('<DIRECTIVE>[0-9]+(?![\\w-])'));
    this.addRule('d_word', rx('<DIRECTIVE>[^\\s="]+'));
    this.addRule('d_ws', rx('<DIRECTIVE>[ \\t]+'), (lexer, skip) => skip());
    this.addRule('d_newline', rx('<DIRECTIVE>\\r?\\n'), (lexer, skip) => {
      lexer.popState();
      skip();
    });
    this.addRule('indented', rx('<INITIAL,RULES,SCOPE>' + LexTokens.indented));
    this.addRule('def_name', rx('<INITIAL>' + LexTokens.name));
    this.addRule('def_regex', rx('<INITIAL>(?<=[ \\t])[^\\s][^\\n]*?(?=[ \\t]*\\r?(?:\\n|$))'));

    /* section 2: rules */
    this.addRule('eof', rx('<RULES,SCOPE>' + LexTokens.eof));
    this.addRule('scope', rx('<RULES>' + LexTokens.scope), lexer => lexer.pushState('SCOPE'));
    this.addRule('scope_end', rx('<SCOPE>(?<![^\\n])\\}(?=[ \\t]*(?:\\n|$))'), lexer => lexer.popState());
    this.addRule('condition', rx('<RULES,SCOPE>' + LexTokens.condition));
    this.addRule('action_fallthrough', rx('<RULES,SCOPE>(?<=[ \\t])\\|(?=[ \\t]*(?:\\n|$))'));
    this.addRule('action', rx('<RULES,SCOPE>(?<=[ \\t])' + braces(6)));
    this.addRule('action_percent', rx('<RULES,SCOPE>(?<=[ \\t])%\\{[\\s\\S]*?%\\}'));
    this.addRule('action_line', rx('<RULES,SCOPE>(?<=[ \\t])[^\\s{][^\\n]*?(?=[ \\t]*\\r?(?:\\n|$))'));
    this.addRule('regex', rx('<RULES,SCOPE>' + LexTokens.regex(this.jison)));

    /* section 3: user code, to the end of the input */
    this.addRule('user', rx('<USER>[\\s\\S]+'));

    this.addRule('ws', rx('<INITIAL,RULES,SCOPE>[ \\t]+'), (lexer, skip) => skip());
    this.addRule('newline', rx('<INITIAL,RULES,SCOPE>\\r?\\n'), (lexer, skip) => skip());
  }
}

globalThis.LexLexer = LexLexer;

define(LexLexer.prototype, { [Symbol.toStringTag]: 'LexLexer' });

export default LexLexer;
