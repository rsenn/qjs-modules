/**
 * Composable grammar built from Parser classes.
 *
 * Each Parser subclass has a .parse(ctx) that either returns a parse-tree
 * node and advances the token stream, or returns null and rewinds.
 *
 * Combinators are exposed as methods (then, or, optional, many, some, not,
 * as, map) rather than operator overloading, so the same grammar works in
 * plain QuickJS without the operator-overloading extension.
 *
 * The token stream is fed by a Parser (lib/parser.js) instance that in turn
 * wraps a Lexer from the `lexer` C module.
 */

import Parser from '../parser.js';

const isString = v => typeof v == 'string';
const isRegExp = v => v instanceof RegExp;
const isFunction = v => typeof v == 'function';

export class ParseNode {
  constructor(rule, children, tokens) {
    this.rule = rule;
    this.children = children ?? [];
    this.tokens = tokens ?? [];
  }

  get type() {
    return this.rule?.name ?? this.rule?.constructor?.name;
  }

  get text() {
    return this.tokens.map(t => t.lexeme).join('');
  }

  get loc() {
    return this.tokens[0]?.loc;
  }

  toJSON() {
    const { type, text, children } = this;
    return { type, text, children: children.map(c => (c instanceof ParseNode ? c.toJSON() : c)) };
  }
}

/** Abstract base class. All grammar building blocks derive from this. */
export class GrammarRule {
  constructor(name) {
    this.name = name;
  }

  /** Attempt to match at the current position. Returns ParseNode or null.
   *  Subclasses must implement _parse(ctx). This wrapper handles snapshot/restore. */
  parse(ctx) {
    const snap = ctx.mark();
    const result = this._parse(ctx);
    if(result == null) ctx.restore(snap);
    return result;
  }

  _parse(_ctx) {
    throw new Error(`${this.constructor.name}._parse not implemented`);
  }

  /* -------- composition methods -------- */

  then(...rest) {
    return new Sequence([this, ...rest.map(coerce)]);
  }

  or(...rest) {
    return new Alternatives([this, ...rest.map(coerce)]);
  }

  optional() {
    return new Optional(this);
  }

  many() {
    return new ZeroOrMore(this);
  }

  some() {
    return new OneOrMore(this);
  }

  not() {
    return new Not(this);
  }

  as(name) {
    const copy = Object.create(Object.getPrototypeOf(this));
    Object.assign(copy, this);
    copy.name = name;
    return copy;
  }

  map(fn) {
    return new Mapped(this, fn);
  }

  toString() {
    return this.name ?? this.constructor.name;
  }
}

/** Matches a terminal token by type, lexeme, regexp, or predicate. */
export class Terminal extends GrammarRule {
  constructor(matcher, name) {
    super(name ?? (isString(matcher) ? matcher : (matcher?.name ?? 'Terminal')));
    this.matcher = matcher;
    if(isString(matcher)) this.test = tok => tok.type == matcher || tok.lexeme == matcher;
    else if(isRegExp(matcher)) this.test = tok => matcher.test(tok.lexeme);
    else if(isFunction(matcher)) this.test = matcher;
    else throw new TypeError(`Terminal matcher must be string, RegExp, or function`);
  }

  _parse(ctx) {
    const tok = ctx.peek();
    if(tok && this.test(tok)) {
      ctx.consume();
      return new ParseNode(this, [], [tok]);
    }
    return null;
  }

  toString() {
    if(isString(this.matcher)) return JSON.stringify(this.matcher);
    return this.name;
  }
}

/** References a rule by name in the grammar. Resolution is deferred to parse-time. */
export class NonTerminal extends GrammarRule {
  constructor(name) {
    super(name);
  }

  _parse(ctx) {
    const rule = ctx.grammar.get(this.name);
    if(!rule) throw new Error(`Undefined rule '${this.name}' at ${ctx.peek()?.loc}`);
    const child = rule.parse(ctx);
    if(child == null) return null;
    return new ParseNode(this, [child], child.tokens);
  }

  toString() {
    return this.name;
  }
}

/** Matches child rules in order. Fails and rewinds if any child fails. */
export class Sequence extends GrammarRule {
  constructor(children, name) {
    super(name ?? 'Sequence');
    this.children = children;
  }

  _parse(ctx) {
    const results = [];
    const tokens = [];
    for(const child of this.children) {
      const r = child.parse(ctx);
      if(r == null) return null;
      results.push(r);
      tokens.push(...r.tokens);
    }
    return new ParseNode(this, results, tokens);
  }

  then(...rest) {
    /* flatten only when the current Sequence has no user-assigned name -
     * a named Sequence is meant to be treated as a single group */
    if(this.name === 'Sequence') return new Sequence([...this.children, ...rest.map(coerce)]);
    return new Sequence([this, ...rest.map(coerce)]);
  }

  toString() {
    return this.children
      .map(c => {
        const s = c.toString();
        return c instanceof Alternatives ? `(${s})` : s;
      })
      .join(' ');
  }
}

/** Tries each alternative in order. Returns first successful match. */
export class Alternatives extends GrammarRule {
  constructor(children, name) {
    super(name ?? 'Alternatives');
    this.children = children;
  }

  _parse(ctx) {
    for(const child of this.children) {
      const r = child.parse(ctx);
      if(r != null) return new ParseNode(this, [r], r.tokens);
    }
    return null;
  }

  or(...rest) {
    if(this.name === 'Alternatives') return new Alternatives([...this.children, ...rest.map(coerce)]);
    return new Alternatives([this, ...rest.map(coerce)]);
  }

  toString() {
    return this.children.map(c => c.toString()).join(' | ');
  }
}

export class Optional extends GrammarRule {
  constructor(child, name) {
    super(name ?? `Optional`);
    this.child = child;
  }

  _parse(ctx) {
    const r = this.child.parse(ctx);
    if(r != null) return new ParseNode(this, [r], r.tokens);
    return new ParseNode(this, [], []);
  }

  toString() {
    const s = this.child.toString();
    return this.child instanceof Sequence || this.child instanceof Alternatives ? `(${s})?` : `${s}?`;
  }
}

export class ZeroOrMore extends GrammarRule {
  constructor(child, name) {
    super(name ?? 'ZeroOrMore');
    this.child = child;
  }

  _parse(ctx) {
    const results = [];
    const tokens = [];
    for(;;) {
      const r = this.child.parse(ctx);
      if(r == null) break;
      results.push(r);
      tokens.push(...r.tokens);
      if(r.tokens.length == 0) break; /* avoid infinite loop on empty match */
    }
    return new ParseNode(this, results, tokens);
  }

  toString() {
    const s = this.child.toString();
    return this.child instanceof Sequence || this.child instanceof Alternatives ? `(${s})*` : `${s}*`;
  }
}

export class OneOrMore extends GrammarRule {
  constructor(child, name) {
    super(name ?? 'OneOrMore');
    this.child = child;
  }

  _parse(ctx) {
    const results = [];
    const tokens = [];
    const first = this.child.parse(ctx);
    if(first == null) return null;
    results.push(first);
    tokens.push(...first.tokens);
    for(;;) {
      const r = this.child.parse(ctx);
      if(r == null) break;
      results.push(r);
      tokens.push(...r.tokens);
      if(r.tokens.length == 0) break;
    }
    return new ParseNode(this, results, tokens);
  }

  toString() {
    const s = this.child.toString();
    return this.child instanceof Sequence || this.child instanceof Alternatives ? `(${s})+` : `${s}+`;
  }
}

/** Negative lookahead — succeeds when child does NOT match. Consumes nothing. */
export class Not extends GrammarRule {
  constructor(child, name) {
    super(name ?? 'Not');
    this.child = child;
  }

  _parse(ctx) {
    const snap = ctx.mark();
    const r = this.child.parse(ctx);
    ctx.restore(snap);
    return r == null ? new ParseNode(this, [], []) : null;
  }

  toString() {
    return `~${this.child}`;
  }
}

/** Positive lookahead — succeeds when child matches but consumes nothing. */
export class Peek extends GrammarRule {
  constructor(child, name) {
    super(name ?? 'Peek');
    this.child = child;
  }

  _parse(ctx) {
    const snap = ctx.mark();
    const r = this.child.parse(ctx);
    ctx.restore(snap);
    return r != null ? new ParseNode(this, [], []) : null;
  }

  toString() {
    return `&${this.child}`;
  }
}

/** Wraps a rule so its parse result is transformed by a user function. */
export class Mapped extends GrammarRule {
  constructor(child, fn, name) {
    super(name ?? child.name);
    this.child = child;
    this.fn = fn;
  }

  _parse(ctx) {
    const r = this.child.parse(ctx);
    if(r == null) return null;
    const mapped = this.fn(r, ctx);
    if(mapped instanceof ParseNode) return mapped;
    const node = new ParseNode(this, [r], r.tokens);
    node.value = mapped;
    return node;
  }

  toString() {
    return this.name ?? this.child.toString();
  }
}

/** Matches nothing, consumes nothing, always succeeds. */
export class Empty extends GrammarRule {
  constructor() {
    super('Empty');
  }
  _parse() {
    return new ParseNode(this, [], []);
  }
}

/** ParseContext feeds a Parser (lib/parser.js) into the grammar rules and
 *  supports mark/restore for backtracking. */
export class ParseContext {
  constructor(parser, grammar) {
    this.parser = parser;
    this.grammar = grammar;
    this.buffer = [];
    this.pos = 0;
  }

  _fill(n) {
    while(this.buffer.length < this.pos + n + 1) {
      const tok = this.parser.consume();
      if(!tok) return false;
      this.buffer.push(tok);
    }
    return true;
  }

  peek(offset = 0) {
    this._fill(offset);
    return this.buffer[this.pos + offset];
  }

  consume() {
    const tok = this.peek(0);
    if(tok) this.pos++;
    return tok;
  }

  mark() {
    return this.pos;
  }

  restore(pos) {
    this.pos = pos;
  }

  get eof() {
    return this.peek() == null;
  }
}

/** A grammar is a named collection of rules with a designated start rule. */
export class Grammar {
  constructor(start = null) {
    this.rules = new Map();
    this.start = start;
  }

  define(name, rule) {
    if(!(rule instanceof GrammarRule)) rule = coerce(rule);
    rule = rule.as(name);
    this.rules.set(name, rule);
    if(this.start == null) this.start = name;
    return rule;
  }

  get(name) {
    return this.rules.get(name);
  }

  ref(name) {
    return new NonTerminal(name);
  }

  terminal(matcher, name) {
    return new Terminal(matcher, name);
  }

  seq(...items) {
    return new Sequence(items.map(coerce));
  }

  alt(...items) {
    return new Alternatives(items.map(coerce));
  }

  /** Parse an input using this grammar's start rule against a Parser instance. */
  parse(parser) {
    if(this.start == null) throw new Error('Grammar has no start rule');
    const startRule = this.rules.get(this.start);
    if(!startRule) throw new Error(`Start rule '${this.start}' not defined`);
    const ctx = new ParseContext(parser, this);
    const result = startRule.parse(ctx);
    if(result == null) {
      const tok = ctx.peek();
      throw new Error(`Parse failed at ${tok?.loc ?? '<eof>'} on token ${tok?.type ?? '<eof>'} '${tok?.lexeme ?? ''}'`);
    }
    return result;
  }

  toString() {
    return [...this.rules.values()].map(r => `${r.name} = ${r};`).join('\n');
  }
}

/** Coerce a plain value into a GrammarRule if possible. */
export function coerce(v) {
  if(v instanceof GrammarRule) return v;
  if(isString(v) || isRegExp(v) || isFunction(v)) return new Terminal(v);
  throw new TypeError(`Cannot coerce ${typeof v} into a GrammarRule`);
}

/** Convenience factories for building grammars fluently. */
export const t = (m, name) => new Terminal(m, name);
export const ref = name => new NonTerminal(name);
export const seq = (...items) => new Sequence(items.map(coerce));
export const alt = (...items) => new Alternatives(items.map(coerce));
export const opt = item => new Optional(coerce(item));
export const many = item => new ZeroOrMore(coerce(item));
export const some = item => new OneOrMore(coerce(item));
export const not = item => new Not(coerce(item));
export const peek = item => new Peek(coerce(item));
export const empty = () => new Empty();

export default Grammar;
