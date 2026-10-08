/* parser/g4.js: parseG4() and G4Parser, ANTLR 4 grammar files.
 * depends on: parser/ebnf.js (groups, `* + ?`, char classes), parser/bnf.js (scanCode).
 * rule: code blocks `{ }` are opaque text; `options`, `tokens`, `channels`
 * bodies are split into names and values, nothing else is interpreted. */
import { EBNFParser } from './ebnf.js';
import { node, scanCode } from './bnf.js';

const code = (text, pos) => (text[pos] == '{' ? scanCode(text, pos) : -1);

export class G4Parser extends EBNFParser {
  static iso = false;
  static tokens = {
    string: String.raw`'(?:\\.|[^'\\\n])*'`,
    code,
    class: String.raw`\[(?:\\.|[^\]\\])*\]`,
    angle: String.raw`<[\w.]+=[\w.]+>`,
    name: String.raw`[A-Za-z_]\w*`,
    colons: '::',
    assign: String.raw`:`,
    arrow: '->',
    plusassign: String.raw`\+=`,
    dotdot: String.raw`\.\.`,
    number: String.raw`\d+`,
  };

  parse() {
    const grammar = node('Grammar', { kind: null, name: null, decls: [], rules: [] });
    const word = (w, n = 0) => this.at('name', n) && this.peek(n).text == w;

    if(word('lexer') || word('parser')) grammar.kind = this.next().text;
    if(!word('grammar')) this.error("expected 'grammar'");
    this.next();
    grammar.name = this.expect('name').text;
    this.expect(';');

    while(!this.at('eof')) {
      if((word('options') || word('tokens') || word('channels')) && this.at('code', 1)) grammar.decls.push(this.block());
      else if(word('import') && this.at('name', 1)) grammar.decls.push(this.importDecl());
      else if(this.at('@')) grammar.decls.push(this.actionDecl());
      else if(word('mode') && this.at('name', 1) && this.at(';', 2)) {
        this.next();
        grammar.rules.push(node('Mode', { name: this.next().text }));
        this.next();
      } else grammar.rules.push(this.rule());
    }

    return grammar;
  }

  /* `options { a = b; }`, `tokens { A, B }`, `channels { C }` */
  block() {
    const kind = this.next().text;
    const body = this.next().text.slice(1, -1);

    if(kind == 'options') return node('Options', { items: [...body.matchAll(/(\w+)\s*=\s*([^;]+?)\s*;/g)].map(m => ({ name: m[1], value: m[2] })) });

    return node(kind == 'tokens' ? 'Tokens' : 'Channels', { names: body.split(/[\s,]+/).filter(Boolean) });
  }

  importDecl() {
    this.next();

    const names = [];
    do names.push(this.expect('name').text);
    while(this.eat(',') || (this.at('=') && this.next() && this.eat('name')));
    this.expect(';');

    return node('Import', { names });
  }

  /* `@header { ... }`, `@lexer::members { ... }` */
  actionDecl() {
    this.next();

    let name = this.expect('name').text,
      scope = null;

    if(this.eat('colons')) [scope, name] = [name, this.expect('name').text];

    return node('ActionDecl', { scope, name, code: this.expect('code').text });
  }

  rule() {
    const word = w => this.at('name') && this.peek().text == w;
    const fragment = word('fragment') && this.at('name', 1) ? !!this.next() : false;
    const rule = node('Rule', { name: this.expect('name').text, fragment });

    if(this.adjacent('class')) rule.args = this.next().text;

    for(;;) {
      if(word('returns') || word('locals')) {
        const key = this.next().text;
        rule[key] = this.expect('class').text;
      } else if(word('throws')) {
        this.next();
        rule.throws = [];
        do rule.throws.push(this.expect('name').text);
        while(this.eat(','));
      } else if(word('options') && this.at('code', 1)) rule.options = this.block();
      else if(this.at('@')) (rule.actions ??= []).push(this.actionDecl());
      else break;
    }

    this.expect('assign');
    rule.body = this.choice();
    this.eat(';') || this.at('eof') || this.ruleAhead() || this.expect(';');

    while(word('catch') || word('finally')) {
      const kind = this.next().text;
      const args = kind == 'catch' ? this.expect('class').text : null;

      (rule.handlers ??= []).push(node('Handler', { kind, args, code: this.expect('code').text }));
    }

    return rule;
  }

  /* a token that starts right where the previous one ended: `expr[0]` */
  adjacent(type) {
    return this.at(type) && this.peek().pos == this.last?.end;
  }

  /* a rule start after the body: the `;` may be missing in sloppy files */
  ruleAhead() {
    return this.isRuleStart() || (this.at('name') && this.peek().text == 'fragment' && this.at('name', 1) && this.at('assign', 2));
  }

  endOfSequence() {
    return this.at('eof') || /^([|;)]|arrow|#)$/.test(this.peek().type) || this.ruleAhead();
  }

  /* an alternative: items, then `-> command, ...` and `# Label` */
  sequence() {
    const seq = super.sequence();

    if(this.eat('arrow')) {
      seq.commands = [];

      do {
        const cmd = node('Command', { name: this.expect('name').text });

        if(this.eat('(')) {
          cmd.arg = this.next().text;
          this.expect(')');
        }

        seq.commands.push(cmd);
      } while(this.eat(','));
    }

    if(this.eat('#')) seq.label = this.expect('name').text;

    return seq;
  }

  element() {
    if(this.at('name') && (this.at('=', 1) || this.at('plusassign', 1))) {
      const name = this.next().text;
      const op = this.next().text;

      return node('Label', { name, op, item: this.element() });
    }

    let item = this.primary();

    while(/^[*+?]$/.test(this.peek().type)) {
      const op = this.next().text;

      item = node('Repeat', { op, item, greedy: !this.eat('?') });
    }

    return item;
  }

  primary() {
    switch (this.peek().type) {
      case 'code': {
        const text = this.next().text;

        return this.adjacent('?') && this.next() ? node('Predicate', { code: text }) : node('Action', { code: text });
      }
      case '~':
        this.next();
        return node('Not', { item: this.primary() });
      case '.':
        this.next();
        return node('Wildcard');
      case 'angle':
        return node('Option', { text: this.next().text });
      case 'string': {
        const from = this.terminal(this.next());

        return this.eat('dotdot') ? node('Range', { from, to: this.terminal(this.expect('string')) }) : from;
      }
      case 'name': {
        const ref = node('Nonterminal', { name: this.next().text });

        if(this.adjacent('class')) ref.args = this.next().text;
        return ref;
      }
    }

    return super.primary();
  }
}

/* .g4 text -> Grammar.
 *
 *   returns Grammar  {kind: 'lexer'|'parser'|null, name, decls, rules}
 *            decls: Options {items}, Tokens/Channels {names}, Import {names}, ActionDecl
 *            rules: Rule {name, fragment, args?, returns?, body: Choice, handlers?} and Mode {name};
 *            a Sequence may carry `commands` (`-> skip`) and `label` (`# Name`);
 *            items: Terminal, Range, Nonterminal {args?}, CharClass, Wildcard, Not,
 *            Group, Repeat {op, greedy}, Label, Action, Predicate, Option
 *   throws  SyntaxError  with `file:line:column`
 */
export const parseG4 = (text, fileName) => new G4Parser(text, fileName).parse();

export default parseG4;
