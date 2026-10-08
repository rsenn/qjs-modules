/* parser/yacc.js: parseYacc() and YaccParser, yacc/bison/jison grammar files.
 * depends on: parser/ebnf.js (groups and `* + ?`, which jison accepts), parser/bnf.js.
 * rule: prologue, `%lex` blocks, actions and the epilogue stay opaque text;
 * declarations are `%name` plus their raw argument tokens. */
import { EBNFParser } from './ebnf.js';
import { node, scanCode } from './bnf.js';

const code = (text, pos) => (text[pos] == '{' ? scanCode(text, pos) : -1);

export class YaccParser extends EBNFParser {
  static iso = false;
  static tokens = {
    cblock: String.raw`%\{[\s\S]*?%\}`,
    lexblock: String.raw`%lex[\s\S]*?(?<![^\n])/lex`,
    section: '%%',
    directive: String.raw`%[A-Za-z_][\w-]*`,
    string: String.raw`'(?:\\.|[^'\\\n])*'|"(?:\\.|[^"\\\n])*"`,
    code,
    angle: String.raw`<[^<>\n]*>`,
    alias: String.raw`\[[^\]\n]*\]`,
    name: String.raw`[A-Za-z_][\w.-]*`,
    assign: ':',
    number: String.raw`\d+`,
  };

  parse() {
    const grammar = node('Grammar', { decls: [], rules: [], epilogue: null });
    const ends = /^(section|directive|cblock|lexblock|eof)$/;

    while(!this.at('section') && !this.at('eof')) {
      const t = this.next();

      if(t.type == 'cblock') grammar.decls.push(node('Prologue', { code: t.text.slice(2, -2) }));
      else if(t.type == 'lexblock') grammar.decls.push(node('Lex', { text: t.text.slice(4, -4) }));
      else if(t.type == 'directive') {
        const args = [];

        while(!ends.test(this.peek().type)) args.push(this.next());
        grammar.decls.push(node('Directive', { name: t.text, args: args.map(a => ({ type: a.type, text: a.text })) }));
      } else this.error(`unexpected '${t.text}' in the declarations`, t.pos);
    }

    if(this.eat('section')) while(!this.at('section') && !this.at('eof')) grammar.rules.push(this.rule());

    if(this.at('section')) {
      grammar.epilogue = this.text.slice(this.next().end);
      this.buf.length = 0;
      this.pos = this.text.length;
    }

    return grammar;
  }

  endOfSequence() {
    return super.endOfSequence() || this.at('section');
  }

  element() {
    const t = this.peek();

    if(t.type == 'code') return node('Action', { code: this.next().text });
    if(t.type == 'angle') return node('Tag', { name: this.next().text.slice(1, -1) });

    if(t.type == 'directive') {
      this.next();

      /* %prec NAME, %dprec N, %merge <T> take one argument; %empty none */
      return node('Directive', { name: t.text, arg: t.text == '%empty' ? null : this.next().text });
    }

    return super.element();
  }

  head(rule) {
    if(this.at('alias')) rule.alias = this.next().text.slice(1, -1);
  }

  /* `expr[left]` names a symbol; a `[note]` apart from one is an Annotation */
  primary() {
    if(this.at('alias')) return node('Annotation', { text: this.next().text.slice(1, -1) });

    const item = super.primary();

    if(this.at('alias') && this.peek().pos == this.last.end) item.alias = this.next().text.slice(1, -1);

    return item;
  }
}

/* yacc/bison/jison text -> Grammar.
 *
 *   returns Grammar  {decls, rules: [Rule {name, body: Choice}], epilogue}
 *            decls: Prologue {code}, Lex {text} (jison `%lex ... /lex`, feed it to parseLex),
 *                   Directive {name, args: [{type, text}]}
 *            items: Terminal, Nonterminal {alias?}, Action {code}, Tag, Directive {name, arg}, Annotation {text},
 *                   Group, Optional, Repeat (jison's EBNF)
 *   throws  SyntaxError  with `file:line:column`
 */
export const parseYacc = (text, fileName) => new YaccParser(text, fileName).parse();

export default parseYacc;
