/* parser/bnf.js: parseBNF() and BNFParser, the base of the grammar-file parsers.
 * depends on: nothing (one sticky regexp per token kind, no lexer module).
 * rule: the AST is Grammar > Rule > Choice > Sequence > item; dialects only
 * add item kinds (ebnf.js, g4.js, yacc.js extend BNFParser). */

export const node = (type, props) => ({ type, ...props });

/* end of the `{ ... }` code block that starts at text[pos]; braces inside
 * strings, char literals and comments do not count. */
export function scanCode(text, pos) {
  for(let depth = 0, i = pos; i < text.length; i++) {
    const c = text[i];

    if(c == '{') depth++;
    else if(c == '}' && --depth == 0) return i + 1;
    else if(c == '"' || c == "'") {
      for(i++; i < text.length && text[i] != c && text[i] != '\n'; i++) if(text[i] == '\\') i++;
    } else if(c == '/' && text[i + 1] == '/') i = text.indexOf('\n', i) < 0 ? text.length : text.indexOf('\n', i);
    else if(c == '/' && text[i + 1] == '*') i = (text.indexOf('*/', i + 2) + 1 || text.length) ;
  }

  return -1;
}

export class BNFParser {
  /* [kind, regexp source | (text, pos) => end or -1], tried in order; a
   * subclass spreads these and overrides a kind (null drops it) */
  static tokens = {
    string: String.raw`'[^'\n]*'|"[^"\n]*"`,
    angle: String.raw`<[^<>\n]+>`,
    name: String.raw`[A-Za-z_][\w-]*`,
    assign: String.raw`::=|:=|:|=`,
  };
  static punct = String.raw`[\s\S]`; /* anything else is a token of its own kind */
  static skip = String.raw`(?:\s+|/\*[\s\S]*?\*/|//[^\n]*)+`;

  constructor(text, fileName) {
    const { tokens, skip } = this.constructor;

    this.text = text;
    this.fileName = fileName;
    this.pos = 0;
    this.buf = [];
    this.skipRe = new RegExp(skip, 'y');
    this.kinds = Object.entries(tokens)
      .filter(([, m]) => m)
      .map(([kind, m]) => [kind, typeof m == 'function' ? m : new RegExp(m, 'y')]);
  }

  /* the n-th token ahead, without consuming it */
  peek(n = 0) {
    while(this.buf.length <= n) this.buf.push(this.lex());

    return this.buf[n];
  }

  next() {
    return (this.last = this.buf.length ? this.buf.shift() : this.lex());
  }

  at(type, n = 0) {
    return this.peek(n).type == type;
  }

  eat(type) {
    return this.at(type) ? this.next() : null;
  }

  expect(type) {
    return this.eat(type) ?? this.error(`expected ${type}, found '${this.peek().text}'`);
  }

  error(message, pos = this.peek().pos) {
    const before = this.text.slice(0, pos).split('\n');

    throw new SyntaxError(`${this.fileName ?? ''}:${before.length}:${before.at(-1).length + 1}: ${message}`);
  }

  lex() {
    const { text } = this;

    this.skipRe.lastIndex = this.pos;
    if(this.skipRe.exec(text)) this.pos = this.skipRe.lastIndex;

    const pos = this.pos;
    if(pos >= text.length) return { type: 'eof', text: '', pos, end: pos };

    for(const [type, m] of this.kinds) {
      let end = -1;

      if(typeof m == 'function') end = m(text, pos);
      else {
        m.lastIndex = pos;
        if(m.exec(text)) end = m.lastIndex;
      }

      if(end > pos) return this.token(type, pos, end);
    }

    const t = this.token(text[pos], pos, pos + 1);
    return t;
  }

  token(type, pos, end) {
    this.pos = end;

    return { type, text: this.text.slice(pos, end), pos, end };
  }

  /* the grammar: a Rule per `name ::= alternatives`. */
  parse() {
    const rules = [];

    while(!this.at('eof')) rules.push(this.rule());

    return node('Grammar', { rules });
  }

  isRuleStart() {
    return (this.at('name') || this.at('angle')) && this.at('assign', 1);
  }

  rule() {
    const rule = node('Rule', { name: this.symbol(true) });

    this.head(rule);
    this.expect('assign');
    rule.body = this.choice();

    if(!this.eat(';')) this.eat('.');

    return rule;
  }

  /* what a dialect allows between the name and the assign (yacc: `[alias]`) */
  head(rule) {}

  /* name of a rule or symbol: `expr` or `<expr>`; `lhs` is true before the assign */
  symbol(lhs) {
    const t = this.next();

    if(t.type == 'angle') return t.text.slice(1, -1);
    if(t.type != 'name') this.error(`expected a name, found '${t.text}'`, t.pos);

    return t.text;
  }

  choice() {
    const alternatives = [this.sequence()];

    while(this.eat('|')) alternatives.push(this.sequence());

    return node('Choice', { alternatives });
  }

  sequence() {
    const items = [];

    while(!this.endOfSequence()) items.push(this.element());

    return node('Sequence', { items });
  }

  endOfSequence() {
    return this.at('eof') || /^[|;.)\]}]$/.test(this.peek().type) || this.isRuleStart();
  }

  element() {
    return this.primary();
  }

  primary() {
    const t = this.peek();

    if(t.type == 'string') return this.terminal(this.next());
    if(t.type == 'name' || t.type == 'angle') return node('Nonterminal', { name: this.symbol() });

    return this.error(`unexpected '${t.text}'`);
  }

  terminal(t) {
    return node('Terminal', { value: t.text.slice(1, -1), quote: t.text[0] });
  }
}

/* .bnf text -> Grammar.
 *
 *   string  text      `<expr> ::= <term> | "+" <expr>` and the like
 *   string  fileName  for error messages
 *
 *   returns Grammar  {type, rules: [Rule {name, body: Choice}]}
 *   throws  SyntaxError  with `file:line:column`
 */
export const parseBNF = (text, fileName) => new BNFParser(text, fileName).parse();

export default parseBNF;
