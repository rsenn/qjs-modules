/* parser/earley.js: Earley, a parser for any context-free grammar.
 * depends on: nothing.
 * rule: the input is an array of items (tokens or characters); terminals are
 * predicates on one item; the tree picks one derivation of an ambiguity. */

/* a grammar for Earley:
 *
 *   {start: 'expr', rules: {expr: [{rhs: ['expr', plus, 'term'], prec: 1, assoc: 'left', label: 'Add'}]}}
 *
 * An rhs symbol is a string (a nonterminal) or `{match: item => bool, name}`
 * (a terminal). A rule name starting with `$` is a helper: its node is not in
 * the tree, its children go to the parent. `rec: true` marks `$r -> $r x`.
 */
export class Earley {
  constructor({ start, rules }) {
    this.start = start;
    this.prods = [];
    this.byLhs = new Map();

    let base = 0;

    for(const lhs in rules) {
      const list = [];

      for(const alt of rules[lhs]) {
        const rhs = alt.rhs.map(s => (typeof s == 'string' ? { nt: s } : s));
        const prod = { id: this.prods.length, lhs, rhs, base, prec: alt.prec, assoc: alt.assoc, label: alt.label, rec: rhs[0]?.nt == lhs && lhs[0] == '$' };

        base += rhs.length + 1;
        this.prods.push(prod);
        list.push(prod);
      }

      this.byLhs.set(lhs, list);
    }

    for(const p of this.prods) for(const s of p.rhs) if(s.nt && !this.byLhs.has(s.nt)) throw new Error(`undefined rule '${s.nt}' used by '${p.lhs}'`);

    /* nullable rules, each with the production that makes it empty */
    this.nullable = new Map();

    for(let changed = true; changed; ) {
      changed = false;

      for(const p of this.prods)
        if(!this.nullable.has(p.lhs) && p.rhs.every(s => s.nt && this.nullable.has(s.nt))) {
          this.nullable.set(p.lhs, p);
          changed = true;
        }
    }
  }

  /* the tree of `items` for the start rule.
   *
   *   Item[]  items  anything; terminals call `match(item)` on them
   *
   *   returns Node  {rule, label, start, end, children: [Node | {token, index}]}
   *                 start/end are item indexes, end exclusive
   *   throws  SyntaxError  {index, expected: [terminal names]} at the furthest item reached
   */
  parse(items, start = this.start) {
    if(!this.byLhs.has(start)) throw new Error(`no rule '${start}'`);

    const n = items.length;
    const chart = Array.from({ length: n + 1 }, () => ({ list: [], map: new Map(), wait: new Map() }));
    const add = (k, prod, dot, origin, pred, child) => {
      const set = chart[k];
      const key = (prod.base + dot) * (n + 1) + origin;
      let it = set.map.get(key);

      if(!it) {
        it = { prod, dot, origin, end: k, derivs: [] };
        set.map.set(key, it);
        set.list.push(it);

        const next = prod.rhs[dot]?.nt;
        if(next) (set.wait.get(next) ?? set.wait.set(next, []).get(next)).push(it);
      }

      if(pred) it.derivs.push({ pred, child });
    };

    for(const p of this.byLhs.get(start)) add(0, p, 0, 0);

    let far = 0;

    for(let k = 0; k <= n; k++) {
      const set = chart[k];

      if(!set.list.length) break;
      far = k;

      for(let i = 0; i < set.list.length; i++) {
        const it = set.list[i],
          sym = it.prod.rhs[it.dot];

        if(!sym) {
          for(const w of chart[it.origin].wait.get(it.prod.lhs) ?? []) add(k, w.prod, w.dot + 1, w.origin, w, it);
        } else if(sym.nt) {
          for(const p of this.byLhs.get(sym.nt)) add(k, p, 0, k);
          if(this.nullable.has(sym.nt)) add(k, it.prod, it.dot + 1, it.origin, it, { empty: sym.nt });
        } else if(k < n && sym.match(items[k])) add(k + 1, it.prod, it.dot + 1, it.origin, it, { token: items[k], index: k });
      }
    }

    const ends = chart[n].list.filter(it => !it.prod.rhs[it.dot] && it.prod.lhs == start && it.origin == 0);
    const done = ends.length ? ends.reduce((a, b) => this.better({ child: a }, { child: b }, {}).child) : null;

    if(!done) {
      const expected = [...new Set(chart[far].list.map(it => it.prod.rhs[it.dot]).filter(s => s && !s.nt).map(s => s.name))];
      const error = new SyntaxError(`unexpected ${far < n ? 'item ' + far : 'end of input'}, expected ${expected.slice(0, 8).join(' ')}`);

      throw Object.assign(error, { index: far, expected });
    }

    this.items = items;
    return this.node(done);
  }

  /* which of two derivations of one item to keep: a precedence level
   * (lower is nearer the root), then the alternative order; for two splits
   * of one production, %left prefers the shorter last child, else longer. */
  better(a, b, prod) {
    const ca = a.child,
      cb = b.child;

    if(!ca.prod || !cb.prod) return a;

    if(ca.origin != cb.origin) {
      const lastShort = prod.assoc == 'left';

      return (ca.origin > cb.origin) == lastShort ? a : b;
    }

    const pa = ca.prod,
      pb = cb.prod;

    if(pa.prec != null && pb.prec != null && pa.prec != pb.prec) return pa.prec < pb.prec ? a : b;

    return pa.id <= pb.id ? a : b;
  }

  best(it) {
    return it.derivs.reduce((a, b) => this.better(a, b, it.prod));
  }

  /* the children of a completed item, in order: items, tokens or empties */
  kids(it) {
    const out = [];

    while(it.dot > 0) {
      const d = this.best(it);

      out.push(d.child);
      it = d.pred;
    }

    return out.reverse();
  }

  node(it) {
    return { rule: it.prod.lhs, label: it.prod.label, start: it.origin, end: it.end, children: this.flat(this.kids(it), it.prod) };
  }

  /* kid refs -> nodes; a helper rule is replaced by its own children */
  flat(kids, prod) {
    const out = [];

    for(const k of kids) {
      if(k.token) out.push(k);
      else if(k.empty) {
        if(k.empty[0] != '$') out.push({ rule: k.empty, start: 0, end: 0, empty: true, children: this.empty(k.empty) });
      } else if(k.prod.lhs[0] != '$') out.push(this.node(k));
      else out.push(...this.expand(k));
    }

    return out;
  }

  /* a helper item as its children; `$r -> $r x` runs are walked, not recursed */
  expand(it) {
    const parts = [];

    for(;;) {
      const kids = this.kids(it);

      if(it.prod.rec && kids[0]?.prod?.lhs == it.prod.lhs) {
        parts.push(kids.slice(1));
        it = kids[0];
      } else {
        parts.push(kids);
        break;
      }
    }

    return parts.reverse().flatMap(p => this.flat(p));
  }

  /* the children of the empty derivation of a nullable rule */
  empty(name) {
    return this.flat(this.nullable.get(name).rhs.map(s => ({ empty: s.nt })));
  }
}

export default Earley;
