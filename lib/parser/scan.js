/* parser/scan.js: the items a grammar parses: characters, lex-file tokens, g4 lexer tokens.
 * depends on: parser/cfg.js (decode); a LexFile (parser/lex.js) for lexItems().
 * rule: an item is {type, text, pos, end} with offsets into the source. */
import { decode } from './cfg.js';

/* one item per character; whitespace is dropped when `skipWs` */
export function charItems(text, skipWs = false) {
  const items = [];

  for(let pos = 0; pos < text.length; ) {
    const c = String.fromCodePoint(text.codePointAt(pos)),
      end = pos + c.length;

    if(!(skipWs && /\s/.test(c))) items.push({ type: 'char', text: c, pos, end });
    pos = end;
  }

  return items;
}

/* the tokens of a lex/flex/jison-lex file.
 *
 *   LexFile  file   parseLex() result; its rules do the matching
 *   string   text   source to scan
 *   object   opts   {fileName, identToken, mode: Lexer.LONGEST (flex) or Lexer.FIRST (jison)}
 *
 *   returns Item[]  type is what the rule returns (`return NAME;`, `return '+';`); a rule
 *                   that returns `yytext[0]` gives the text, any other call gives `identToken`
 *   throws  SyntaxError  where no rule matches
 */
export function lexItems(file, text, { fileName, identToken = 'IDENTIFIER', mode } = {}) {
  const info = new Map(file.compile().rules.map(r => [r.name, r]));
  const items = [];

  try {
    for(const tok of file.scan(text, fileName, mode)) {
      const r = info.get(tok.type);

      items.push({
        type: r?.token ?? (/yytext\[0\]|\breturn\s+yytext\b/.test(r?.action ?? '') ? tok.lexeme : identToken),
        text: tok.lexeme,
        pos: tok.loc.charOffset,
        end: tok.loc.charOffset + tok.charLength,
      });
    }
  } catch(e) {
    throw new SyntaxError(e.message);
  }

  return items;
}

const escRe = s => s.replace(/[\\^$.*+?()[\]{}|\/]/g, '\\$&');
const escSet = s => s.replace(/[\\\]\[^-]/g, '\\$&');

/* an ANTLR character set `[a-z\-\]]` as the inside of a JS `[...]` */
function classBody(text) {
  return text.slice(1, -1).replace(/\\(u\{[0-9A-Fa-f]+\}|u[0-9A-Fa-f]{4}|[pP]\{[^}]*\}|[\s\S])/g, (m, c) => {
    if(c[0] == 'u' && c.length > 1) return m;
    if(/^[pP]\{/.test(c)) return m;
    if(/[nrtbf\\\-\]\[^]/.test(c)) return '\\' + c;

    return escSet(c);
  });
}

/* the lexer rules of one or more g4 grammars, as a tokenizer.
 *
 *   Grammar[]  grammars  parseG4() results; their uppercase rules are the tokens
 *   Set        literals  quoted terminals of the parser rules, each its own token unless a rule is exactly it
 *
 *   returns function  text => Item[]; a rule's commands skip, channel(), type(), pushMode(), popMode(),
 *                     mode() apply, `more` and predicates do not
 *   throws  SyntaxError  on a character no rule matches, or a rule that cannot be a regexp
 */
export function g4Lexer(grammars, literals = new Set()) {
  const defs = new Map(),
    rules = [];
  let mode = 'DEFAULT_MODE';

  for(const g of grammars)
    for(const r of g.rules) {
      if(r.type == 'Mode') mode = r.name;
      else if(/^[A-Z]/.test(r.name)) defs.set(r.name, { rule: r, mode });
    }

  const memo = new Map();

  const setBody = n => {
    if(n.type == 'Terminal') return escSet(decode(n.value));
    if(n.type == 'Range') return `${escSet(decode(n.from.value))}-${escSet(decode(n.to.value))}`;
    if(n.type == 'CharClass') return classBody(n.text);
    if(n.type == 'Group') return n.body.alternatives.map(a => a.items.map(setBody).join('')).join('');

    throw new SyntaxError('cannot negate ' + n.type);
  };

  const item = n => {
    switch (n.type) {
      case 'Terminal':
        return escRe(decode(n.value));
      case 'Range':
        return `[${setBody(n)}]`;
      case 'CharClass':
        return `[${classBody(n.text)}]`;
      case 'Wildcard':
        return '[\\s\\S]';
      case 'Not':
        return `[^${setBody(n.item)}]`;
      case 'Nonterminal': {
        if(n.name == 'EOF') return '$';
        if(!defs.has(n.name)) throw new SyntaxError(`undefined lexer rule ${n.name}`);
        if(memo.get(n.name) === null) throw new SyntaxError(`lexer rule ${n.name} is recursive`);

        if(!memo.has(n.name)) {
          memo.set(n.name, null);
          memo.set(n.name, choice(defs.get(n.name).rule.body));
        }

        return `(?:${memo.get(n.name)})`;
      }
      case 'Group':
        return `(?:${choice(n.body)})`;
      case 'Repeat':
        return `(?:${item(n.item)})${n.op}${n.greedy === false ? '?' : ''}`;
      case 'Label':
        return item(n.item);
      default:
        return '';
    }
  };

  const choice = body => body.alternatives.map(seq).join('|');
  const seq = s => s.items.map(item).join('');

  /* backtracking matcher for recursive rules: a generator of end offsets, in priority order.
   * `leaf` nodes are single regexps; the rest recurse over the g4 tree. */
  const leaf = new Map();
  const leafRe = n => (leaf.has(n) ? leaf : leaf.set(n, new RegExp(item(n), 'uy'))).get(n);

  function* mItem(n, text, pos) {
    switch (n.type) {
      case 'Nonterminal':
        if(n.name != 'EOF') {
          if(!defs.has(n.name)) throw new SyntaxError(`undefined lexer rule ${n.name}`);
          return yield* mChoice(defs.get(n.name).rule.body, text, pos);
        }
      case 'Terminal':
      case 'Range':
      case 'CharClass':
      case 'Wildcard':
      case 'Not': {
        const re = leafRe(n);
        re.lastIndex = pos;
        const x = re.exec(text);
        if(x) yield pos + x[0].length;
        return;
      }
      case 'Group':
        return yield* mChoice(n.body, text, pos);
      case 'Label':
        return yield* mItem(n.item, text, pos);
      case 'Repeat': {
        const min = n.op == '+' ? 1 : 0,
          max = n.op == '?' ? 1 : Infinity,
          greedy = n.greedy !== false;
        const go = function* (at, count) {
          if(count >= min && !greedy) yield at;
          if(count < max) for(const e of mItem(n.item, text, at)) if(e > at || count < min) yield* go(e, count + 1);
          if(count >= min && greedy) yield at;
        };
        return yield* go(pos, 0);
      }
    }
  }

  function* mSeq(items, i, text, pos) {
    if(i == items.length) return yield pos;
    for(const e of mItem(items[i], text, pos)) yield* mSeq(items, i + 1, text, e);
  }

  function* mChoice(body, text, pos) {
    for(const a of body.alternatives) yield* mSeq(a.items, 0, text, pos);
  }

  /* RegExp stand-in: sticky exec at lastIndex, first complete path wins */
  const matcher = alt => ({
    lastIndex: 0,
    exec(text) {
      for(const e of mSeq(alt.items, 0, text, this.lastIndex)) return [text.slice(this.lastIndex, e)];
      return null;
    },
  });

  /* one matcher per alternative, so each has its own commands */
  for(const [name, { rule, mode }] of defs)
    if(!rule.fragment)
      for(const alt of rule.body.alternatives) {
        let re;

        try {
          re = new RegExp(seq(alt), 'uy');
        } catch(e) {
          if(!/is recursive/.test(e.message)) throw e;
          re = matcher(alt);
        }

        rules.push({ name, re, mode, commands: alt.commands ?? [], literal: alt.items.length == 1 && alt.items[0].type == 'Terminal' ? decode(alt.items[0].value) : null });
      }

  const defined = new Set(rules.map(r => r.literal));
  const implicit = [...literals].filter(l => l && !defined.has(l)).map((l, i) => ({ name: l, re: new RegExp(escRe(l), 'uy'), mode: null, commands: [] }));
  const byMode = m => [...implicit, ...rules.filter(r => r.mode == m)];

  return text => {
    const items = [],
      stack = [];
    let m = 'DEFAULT_MODE',
      pos = 0;

    while(pos < text.length) {
      let best = null,
        len = 0;

      for(const r of byMode(m)) {
        r.re.lastIndex = pos;

        const x = r.re.exec(text);

        if(x && x[0].length > len) [best, len] = [r, x[0].length];
      }

      if(!best) {
        const before = text.slice(0, pos).split('\n');

        throw new SyntaxError(`${before.length}:${before.at(-1).length + 1}: token recognition error at '${text[pos]}'`);
      }

      let type = best.name,
        hidden = false;

      for(const c of best.commands)
        if(c.name == 'skip' || (c.name == 'channel' && c.arg != 'DEFAULT_TOKEN_CHANNEL')) hidden = true;
        else if(c.name == 'type') type = c.arg;
        else if(c.name == 'pushMode') (stack.push(m), (m = c.arg));
        else if(c.name == 'popMode') m = stack.pop() ?? m;
        else if(c.name == 'mode') m = c.arg;

      if(!hidden) items.push({ type, text: text.slice(pos, pos + len), pos, end: pos + len });
      pos += len;
    }

    return items;
  };
}
