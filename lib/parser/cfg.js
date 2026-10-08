/* parser/cfg.js: toCFG(), a grammar AST (bnf, ebnf, g4, yacc) as a grammar for Earley.
 * depends on: nothing (the AST comes from parser/{bnf,ebnf,g4,yacc}.js).
 * rule: EBNF is desugared into `$`-named helper rules; a terminal is a
 * predicate on one token (or one character when `scannerless`). */

const Escapes = { n: '\n', r: '\r', t: '\t', b: '\b', f: '\f', v: '\v', 0: '\0' };

/* a quoted literal's text with its C/ANTLR escapes decoded: `\\n` -> newline */
export function decode(s) {
  return s.replace(/\\(?:u\{([0-9A-Fa-f]+)\}|u([0-9A-Fa-f]{4})|x([0-9A-Fa-f]{2})|([\s\S]))/g, (m, u1, u2, x, c) => (u1 || u2 || x ? String.fromCodePoint(parseInt(u1 || u2 || x, 16)) : (Escapes[c] ?? c)));
}

const any = { match: () => true, name: '.' };
const never = name => ({ match: () => false, name });
const ofType = name => ({ match: t => t.type === name, name, tok: name });

/* a W3C/ANTLR character class `[a-z#x41]` as a predicate on a one-char item */
function classMatcher(text) {
  try {
    const re = new RegExp(text.replace(/#x([0-9A-Fa-f]+)/g, '\\u{$1}'), 'u');

    return { match: t => t.text.length > 0 && re.test(t.text), name: text };
  } catch(e) {
    return never(text);
  }
}

/* grammar AST -> {start, rules, literals, usesEOF, warnings}.
 *
 *   Grammar  grammar  result of parseBNF/parseEBNF/parseG4/parseYacc
 *   string   kind     'bnf' | 'ebnf' | 'g4' | 'yacc'
 *   object   options  {scannerless, start}
 *
 *   returns object  `rules` and `start` go to `new Earley(...)`; `literals` is the set of
 *                   quoted terminals (a g4 lexer needs them); `warnings` lists what was ignored
 */
export function toCFG(grammar, kind, { scannerless = false, start } = {}) {
  const rules = {},
    literals = new Set(),
    warnings = [],
    levels = new Map();
  let n = 0,
    usesEOF = false;

  const warn = msg => warnings.includes(msg) || warnings.push(msg);
  const helper = () => '$' + ++n;
  const text = s => (kind == 'g4' || kind == 'yacc' ? decode(s) : s);

  /* the symbols of a Terminal: one predicate per token, or per character */
  const term = value => {
    const v = text(value);

    literals.add(v);

    return scannerless ? [...v].map(c => ({ match: t => t.text === c, name: c, tok: c })) : [{ match: t => t.text === v || t.type === v, name: `'${v}'`, tok: v }];
  };

  const matchers = node => {
    if(node.type == 'Terminal') return term(node.value);
    if(node.type == 'Nonterminal') return [ofType(node.name)];
    if(node.type == 'Group') return node.body.alternatives.flatMap(a => a.items.flatMap(matchers));

    warn('~ of ' + node.type + ' ignored');
    return [];
  };

  /* helper rule for a Choice; returns its name */
  const choice = (body, name = helper()) => {
    (rules[name] ??= []);
    for(const alt of body.alternatives) rules[name].push({ rhs: alt.items.flatMap(item), label: alt.label, precName: precOf(alt.items) });

    return name;
  };

  const precOf = items => items.find(i => i.type == 'Directive' && i.name == '%prec')?.arg;

  function item(node) {
    switch (node.type) {
      case 'Terminal':
        return term(node.value);
      case 'Nonterminal':
        if(kind == 'g4' && node.name == 'EOF') return [];
        if(node.name == 'EOF') usesEOF = true;
        return [node.name];
      case 'Group':
        return [choice(node.body)];
      case 'Optional': {
        const h = choice(node.body);

        rules[h].push({ rhs: [] });
        return [h];
      }
      case 'Repeat': {
        const inner = item(node.item),
          h = helper();

        if(node.count != null) return Array.from({ length: node.count }, () => inner).flat();

        rules[h] = node.op == '*' ? [{ rhs: [] }, { rhs: [h, ...inner] }] : node.op == '+' ? [{ rhs: inner }, { rhs: [h, ...inner] }] : [{ rhs: [] }, { rhs: inner }];
        return [h];
      }
      case 'Except': {
        const a = item(node.item),
          b = item(node.except);

        if(a.length == 1 && b.length == 1 && a[0].match && b[0].match) return [{ match: t => a[0].match(t) && !b[0].match(t), name: a[0].name + ' - ' + b[0].name }];

        warn('exception ignored: a - b');
        return a;
      }
      case 'CharClass':
        return scannerless ? [classMatcher(node.text)] : [never(node.text)];
      case 'Char':
        return [{ match: t => t.text === String.fromCodePoint(node.code), name: '#x' + node.code.toString(16) }];
      case 'Special':
        warn('special sequence ignored: ? ' + node.text + ' ?');
        return [never('? ' + node.text + ' ?')];
      case 'Wildcard':
        return [any];
      case 'Not': {
        const ms = matchers(node.item);

        return [{ match: t => !ms.some(m => m.match(t)), name: '~' }];
      }
      case 'Label':
        return item(node.item);
      default:
        /* Action, Predicate, Option, Tag, Annotation, Directive */
        return [];
    }
  }

  if(kind == 'yacc') {
    let level = 0;

    for(const d of grammar.decls)
      if(d.type == 'Directive' && /^%(left|right|nonassoc|precedence)$/.test(d.name)) {
        level++;
        for(const a of d.args) if(a.type == 'name' || a.type == 'string') levels.set(a.type == 'string' ? decode(a.text.slice(1, -1)) : a.text, { level, assoc: d.name.slice(1) });
      }
  }

  let first = null;

  for(const rule of grammar.rules) {
    if(rule.type != 'Rule' || (kind == 'g4' && (rule.fragment || /^[A-Z]/.test(rule.name)))) continue;

    first ??= rule.name;
    choice(rule.body, rule.name);
  }

  /* strings that name no rule are token types; a precedence is the last token's, or %prec's */
  for(const name in rules)
    for(const p of rules[name]) {
      const key = p.precName ?? [...p.rhs].reverse().map(s => (typeof s == 'string' ? s : s.tok)).find(k => levels.has(k));
      const lv = levels.get(key);

      if(lv) Object.assign(p, { prec: lv.level, assoc: lv.assoc });

      p.rhs = p.rhs.map(s => {
        if(typeof s != 'string' || rules[s]) return s;
        if(scannerless) warn('undefined rule ' + s);

        return scannerless ? never(s) : ofType(s);
      });
    }

  const declared = kind == 'yacc' ? grammar.decls.find(d => d.type == 'Directive' && d.name == '%start')?.args[0]?.text : null;

  return { start: start ?? declared ?? first, rules, literals, usesEOF, warnings };
}

export default toCFG;
