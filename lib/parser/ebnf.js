/* parser/ebnf.js: parseEBNF() and EBNFParser, BNF plus groups, options and repeats.
 * depends on: parser/bnf.js.
 * rule: `( )` groups, `[ ]` options and `{ }` repeats (ISO) or `* + ?` after an
 * item (W3C, ANTLR); ISO is chosen when rules use `=`, never `::=`. */
import { BNFParser, node } from './bnf.js';

export class EBNFParser extends BNFParser {
  static tokens = {
    ...BNFParser.tokens,
    hex: String.raw`#x[0-9A-Fa-f]+`,
    class: String.raw`\[\^?(?:[^\]\\\s,|'"]|\\.)+\]`,
    number: String.raw`\d+`,
  };
  static skip = String.raw`(?:\s+|/\*[\s\S]*?\*/|//[^\n]*|\(\*[\s\S]*?\*\))+`;
  static iso; /* true or false forces a dialect, undefined looks at the text */

  constructor(text, fileName) {
    super(text, fileName);

    /* ISO: `a b = c, d;` names hold spaces, `,` joins, `[ ]` and `{ }` nest */
    this.iso = this.constructor.iso ?? (!/::=|:=/.test(text) && /^[ \t]*[A-Za-z][\w ]*=/m.test(text));
    if(this.iso) this.kinds = this.kinds.filter(([kind]) => kind != 'class');
  }

  isRuleStart() {
    if(!this.iso) return super.isRuleStart();

    let n = 0;
    while(this.at('name', n)) n++;

    return n > 0 && this.at('assign', n);
  }

  symbol(lhs) {
    if(!this.iso) return super.symbol(lhs);

    const words = [this.expect('name').text];
    while(this.at('name') && (lhs || !this.isRuleStart())) words.push(this.next().text);

    return words.join(' ');
  }

  /* ISO `a, b`: the comma between items is only a separator */
  sequence() {
    const items = [];

    while(!this.endOfSequence()) {
      items.push(this.element());
      if(this.iso) this.eat(',');
    }

    return node('Sequence', { items });
  }

  endOfSequence() {
    return super.endOfSequence() || (this.iso && this.at('assign'));
  }

  element() {
    let item = this.primary();

    if(!this.iso) while(/^[*+?]$/.test(this.peek().type)) item = node('Repeat', { op: this.next().text, item });
    if(this.eat('-')) item = node('Except', { item, except: this.primary() });

    return item;
  }

  primary() {
    const t = this.peek();

    switch (t.type) {
      case '(':
        return node('Group', { body: this.group('(', ')') });
      case '[':
        return node('Optional', { body: this.group('[', ']') });
      case '{':
        return node('Repeat', { op: '*', item: node('Group', { body: this.group('{', '}') }) });
      case 'class':
        return node('CharClass', { text: this.next().text });
      case 'hex':
        return node('Char', { code: parseInt(this.next().text.slice(2), 16) });
      case 'number':
        if(this.at('*', 1)) {
          const count = +this.next().text;

          this.next();
          return node('Repeat', { op: '*', count, item: this.primary() });
        }
        break;
      case '?': {
        this.next();

        const end = this.text.indexOf('?', t.end);
        this.buf.length = 0;
        this.pos = end + 1;

        return node('Special', { text: this.text.slice(t.end, end).trim() });
      }
      case 'name':
        if(this.iso) return node('Nonterminal', { name: this.symbol() });
    }

    return super.primary();
  }

  group(open, close) {
    this.expect(open);

    const body = this.choice();

    this.expect(close);
    return body;
  }
}

/* .ebnf text -> Grammar; Choice items may also be Group, Optional, Repeat {op, item, count?},
 * Except {item, except}, CharClass, Char {code} and Special {text}.
 *
 *   returns Grammar
 *   throws  SyntaxError  with `file:line:column`
 */
export const parseEBNF = (text, fileName) => new EBNFParser(text, fileName).parse();

export default parseEBNF;
