#!/usr/bin/env qjsm
import * as fs from 'fs';
import * as path from 'path';
import * as std from 'std';
import { isatty, readdir, SIGINT, signal, setTimeout } from 'os';
import { getOpt, isMainModule } from 'util';
import CLexer from 'lexer/c.js';
import process from 'process';

const COMMENT = new Set(['singleLineComment', 'multiLineComment']);
const AGG = new Set(['struct', 'union', 'class']);
const QUAL = new Set(['const', 'volatile', 'restrict', 'static', 'extern', 'register', 'inline', 'auto', 'typedef', 'friend', 'constexpr', 'mutable']);
const ACCESS = new Set(['public', 'private', 'protected']);
const OPENERS = new Set(['(', '[', '{']);
const CLOSERS = new Set([')', ']', '}']);

// LP64 (x86_64 Linux) sizes; each entry is [size, alignment]
const PRIM = {
  char: [1, 1],
  short: [2, 2],
  'short int': [2, 2],
  int: [4, 4],
  long: [8, 8],
  'long int': [8, 8],
  'long long': [8, 8],
  'long long int': [8, 8],
  float: [4, 4],
  double: [8, 8],
  'long double': [16, 16],
  _Bool: [1, 1],
  bool: [1, 1],
  wchar_t: [4, 4],
  int8_t: [1, 1],
  uint8_t: [1, 1],
  int16_t: [2, 2],
  uint16_t: [2, 2],
  int32_t: [4, 4],
  uint32_t: [4, 4],
  int64_t: [8, 8],
  uint64_t: [8, 8],
  size_t: [8, 8],
  ssize_t: [8, 8],
  ptrdiff_t: [8, 8],
  intptr_t: [8, 8],
  uintptr_t: [8, 8],
  off_t: [8, 8],
  time_t: [8, 8],
};

function lex(source, filename) {
  const lexer = new CLexer(source, undefined, filename);
  const toks = [];
  let tok;
  while((tok = lexer.nextToken())) if(tok.type != 'whitespace') toks.push(tok);
  return toks;
}

/**
 * Finds function definitions by heuristic: a ')' followed by '{' starts a body at
 * brace depth 0, and the next '}' in column 1 ends it. Each result carries `start`/`end`
 * offsets into the source covering the declaration start (including leading comments)
 * through the closing brace.
 */
export function findFunctions(source, filename) {
  const toks = lex(source, filename);
  const end = t => t.charPos + t.charLength;
  const funcs = [];
  let boundary = 0; // index of first token after the previous top-level declaration

  for(let i = 0; i < toks.length; i++) {
    const t = toks[i];

    if(t.type == 'preprocessor' || t.type == 'semi') {
      boundary = i + 1;
      continue;
    }

    if(t.type != 'rparen') continue;

    let j = i + 1;
    while(j < toks.length && COMMENT.has(toks[j].type)) j++;
    if(toks[j]?.type != 'lbrace') continue;

    let k = j + 1;
    while(k < toks.length && !(toks[k].type == 'rbrace' && toks[k].loc.column == 1)) k++;
    if(k >= toks.length) break;

    let depth = 0,
      p = i;
    for(; p >= 0; p--) {
      if(toks[p].type == 'rparen') depth++;
      else if(toks[p].type == 'lparen' && --depth == 0) break;
    }
    let n = p - 1;
    while(n >= 0 && COMMENT.has(toks[n].type)) n--;
    if(n < boundary || toks[n]?.type != 'identifier') continue;

    funcs.push({
      name: toks[n].lexeme,
      line: toks[n].loc.line,
      column: toks[n].loc.column,
      start: toks[boundary].charPos,
      end: end(toks[k]),
    });

    i = k;
    boundary = k + 1;
  }

  return funcs;
}

function matching(ts, i) {
  let d = 0;
  for(let j = i; j < ts.length; j++) {
    if(OPENERS.has(ts[j].lexeme)) d++;
    else if(CLOSERS.has(ts[j].lexeme) && --d == 0) return j;
  }
  return ts.length - 1;
}

function splitTop(ts, sep) {
  const parts = [];
  let cur = [],
    d = 0;
  for(const t of ts) {
    if(OPENERS.has(t.lexeme)) d++;
    else if(CLOSERS.has(t.lexeme)) d--;
    if(d == 0 && t.lexeme == sep) {
      parts.push(cur);
      cur = [];
      continue;
    }
    cur.push(t);
  }
  if(cur.length) parts.push(cur);
  return parts;
}

function isFuncBrace(ts, j) {
  let p = j - 1;
  while(p >= 0 && ['const', 'noexcept', 'override', 'final'].includes(ts[p].lexeme)) p--;
  return ts[p]?.lexeme == ')';
}

/** Yields the `;`-terminated statements at depth 0; function definitions yield their signature with `.fn = true` and the tokens between the braces as `.body`. */
function* statements(ts) {
  let i = 0;

  while(i < ts.length) {
    const l = ts[i].lexeme;

    if(l == '}' || l == ';') {
      i++;
      continue;
    }

    if(l == 'namespace' || (l == 'extern' && ts[i + 1]?.type == 'string_literal' && ts[i + 2]?.lexeme == '{')) {
      while(i < ts.length && ts[i].lexeme != '{') i++;
      i++;
      continue;
    }

    let j = i,
      done = false;
    for(; j < ts.length; j++) {
      const t = ts[j].lexeme;
      if(t == '(' || t == '[') j = matching(ts, j);
      else if(t == '{') {
        const fn = isFuncBrace(ts, j);
        const sig = ts.slice(i, j);
        const open = j;
        j = matching(ts, j);
        if(fn) {
          sig.fn = true;
          sig.body = ts.slice(open + 1, j);
          yield sig;
          i = j + 1;
          done = true;
          break;
        }
      } else if(t == ';') {
        yield ts.slice(i, j);
        i = j + 1;
        done = true;
        break;
      }
    }

    if(!done) {
      yield ts.slice(i);
      break;
    }
  }
}

const num = ts => {
  if(!ts.length) return 0;
  if(ts.length > 1) return null;
  const v = Number(ts[0].lexeme.replace(/[uUlL]+$/, ''));
  return isNaN(v) ? null : v;
};

const alignUp = (n, a) => Math.ceil(n / a) * a;

/** `{ endLine, endColumn }`: the position just past the last token of `toks`. */
const endOf = toks => {
  const last = toks.reduce((a, t) => (t.charPos > a.charPos ? t : a));
  return { endLine: last.loc.line, endColumn: last.loc.column + last.charLength };
};

const mk = (name, type, size, align, line, extra) => {
  const desc = { name, type, size, align, line, methods: [], getters: [], setters: [], fields: [], prototypeChain: [], ...extra };
  for(const key of ['methods', 'getters', 'setters', 'fields', 'prototypeChain']) if(!desc[key]?.length) delete desc[key];
  return desc;
};

const leading = ts => {
  const set = new Set();
  let i = 0;
  while(i < ts.length && QUAL.has(ts[i].lexeme)) set.add(ts[i++].lexeme);
  return { i, set };
};

function baseOf(spec, reg, text) {
  const words = spec.map(t => t.lexeme).filter(w => !QUAL.has(w) && w != 'signed' && w != 'unsigned');
  const key = words.join(' ') || 'int';
  const hit = PRIM[key] ? { size: PRIM[key][0], align: PRIM[key][1] } : reg.get(key);
  return { size: hit?.size ?? null, align: hit?.align ?? null, text: text(spec) };
}

function parseDeclarator(part) {
  let t = part,
    bits;
  const dims = [];

  const c = t.findIndex(x => x.lexeme == ':');
  if(c >= 0) {
    bits = num(t.slice(c + 1));
    t = t.slice(0, c);
  }

  while(t.at(-1)?.lexeme == ']') {
    const o = t.findLastIndex(x => x.lexeme == '[');
    dims.unshift(num(t.slice(o + 1, -1)));
    t = t.slice(0, o);
  }

  const p = t.findIndex(x => x.lexeme == '(');
  if(p >= 0 && t[p + 1]?.lexeme == '*') return { name: t.slice(p + 2).find(x => x.type == 'identifier')?.lexeme ?? null, stars: 1, dims, bits, fnptr: true };

  const last = t.at(-1);
  return { name: last?.type == 'identifier' ? last.lexeme : null, stars: t.filter(x => x.lexeme == '*').length, dims, bits, fnptr: false };
}

function sized(base, d) {
  const ptr = d.fnptr || d.stars > 0;
  let size = ptr ? 8 : base.size;
  for(const n of d.dims) size = size == null || n == null ? null : size * n;

  const type = base.text + (d.fnptr ? ' (*)()' : d.stars ? ' ' + '*'.repeat(d.stars) : '') + d.dims.map(n => `[${n || ''}]`).join('');
  return { name: d.name, type, size, align: ptr ? 8 : base.align, bits: d.bits };
}

// SysV x86_64 (GCC) packing: a bitfield may not straddle a boundary of its declared type's size
function layout(kind, members) {
  const fields = [];
  let cursor = 0, // bit position
    maxSize = 0,
    maxAlign = 1,
    known = true;

  for(const m of members) {
    const isBits = m.bits !== undefined;
    if(m.align && !(isBits && m.name == null)) maxAlign = Math.max(maxAlign, m.align);

    const field = { name: m.name, type: m.type, offset: null, size: m.size };

    if(isBits) field.bits = m.bits;
    if(m.fields) field.fields = m.fields;

    if(m.size == null || m.align == null || (isBits && m.bits == null)) known = false;

    if(known) {
      if(isBits) {
        const unit = m.size * 8;
        if(m.bits == 0) cursor = alignUp(cursor, unit);
        else {
          if(kind == 'union') cursor = 0;
          else if(Math.floor(cursor / unit) != Math.floor((cursor + m.bits - 1) / unit)) cursor = alignUp(cursor, unit);
          field.offset = Math.floor(cursor / 8);
          field.bitOffset = cursor % 8;
          cursor += m.bits;
          if(kind == 'union') {
            maxSize = Math.max(maxSize, Math.ceil(m.bits / 8));
            cursor = 0;
          }
        }
        if(m.bits == 0) delete field.bits;
      } else if(kind == 'union') {
        field.offset = 0;
        maxSize = Math.max(maxSize, m.size);
      } else {
        field.offset = alignUp(Math.ceil(cursor / 8), m.align);
        cursor = (field.offset + m.size) * 8;
      }
    }
    fields.push(field);
  }

  return { fields, align: maxAlign, size: known ? alignUp(kind == 'union' ? maxSize : Math.ceil(cursor / 8), maxAlign) : null };
}

/**
 * Parses `struct|union|class|enum [Tag] [: bases] { body }` starting at ts[k]. `desc` is null when
 * there's no body (forward declaration, or `struct X var`), in which case the caller falls back.
 */
function parseAggregate(ts, k, reg, text) {
  const kind = ts[k].lexeme;
  let i = k + 1;

  if(kind == 'enum' && (ts[i]?.lexeme == 'class' || ts[i]?.lexeme == 'struct')) i++;
  while(ts[i]?.lexeme == '__attribute__') i = matching(ts, i + 1) + 1;

  let tag = null;
  if(ts[i]?.type == 'identifier') tag = ts[i++].lexeme;

  if(ts[i]?.lexeme == ':') while(i < ts.length && ts[i].lexeme != '{') i++;
  if(ts[i]?.lexeme != '{') return { tag, desc: null, next: i };

  const close = matching(ts, i);
  const body = ts.slice(i + 1, close);
  const { line, column } = ts[k].loc;
  const offset = ts[k].charPos,
    end = ts[close].charPos + ts[close].charLength,
    endLine = ts[close].loc.line,
    endColumn = ts[close].loc.column + ts[close].charLength;
  let desc;

  if(kind == 'enum') {
    desc = mk(tag, kind, 4, 4, line, { column, offset, end, endLine, endColumn, fields: splitTop(body, ',').map(p => ({ name: p[0].lexeme })) });
  } else {
    const members = [],
      methods = [];

    for(let s of statements(body)) {
      while(ACCESS.has(s[0]?.lexeme) && s[1]?.lexeme == ':') s = s.slice(2);
      if(!s.length || s[0].lexeme == 'using' || leading(s).set.has('static') || leading(s).set.has('typedef') || leading(s).set.has('friend')) continue;

      const d = parseDeclaration(s, reg, text);

      if(d.method) {
        methods.push({ name: d.method });
        continue;
      }

      if(!d.decls.length && d.base.desc && !d.base.desc.name) d.decls.push({ name: null, stars: 0, dims: [], bits: undefined, fnptr: false });

      for(const dd of d.decls) {
        const m = sized(d.base, dd);
        if(d.base.desc && !dd.stars) m.fields = d.base.desc.fields;
        members.push(m);
      }
    }

    const l = layout(kind, members);
    desc = mk(tag, kind, l.size, l.align, line, { column, offset, end, endLine, endColumn, fields: l.fields, methods });
  }

  if(tag) {
    reg.set(`${kind} ${tag}`, desc);
    if(kind != 'enum') reg.set(tag, desc);
  }

  return { tag, desc, next: close + 1 };
}

/**
 * Splits a declaration into its base type and declarators. Returns `{ method }` for C++ member
 * functions; otherwise `{ base, decls }`. Aggregates defined inline are pushed to `out` when given.
 */
function parseDeclaration(s, reg, text, out) {
  const i = leading(s).i;
  let base = null,
    rest;

  if(AGG.has(s[i]?.lexeme) || s[i]?.lexeme == 'enum') {
    const r = parseAggregate(s, i, reg, text);
    if(r.desc) {
      base = { size: r.desc.size, align: r.desc.align, text: `${r.desc.type} ${r.desc.name ?? '<anonymous>'}`, desc: r.desc };
      rest = s.slice(r.next);
      out?.push(r.desc);
    }
  }

  if(!base) {
    const p0 = splitTop(s.slice(i), ',')[0] ?? [];
    const paren = p0.findIndex(t => t.lexeme == '(');

    if(paren >= 0 && p0[paren + 1]?.lexeme != '*') return { method: p0[paren - 1]?.lexeme ?? '' };

    let e = p0.length;
    if(paren >= 0) e = paren;
    else {
      const c = p0.findIndex(t => t.lexeme == ':');
      if(c >= 0) e = c;
      while(p0[e - 1]?.lexeme == ']') e = p0.slice(0, e).findLastIndex(t => t.lexeme == '[');
      if(p0[e - 1]?.type == 'identifier') e--;
      while(e > 0 && (p0[e - 1].lexeme == '*' || p0[e - 1].lexeme == '&' || QUAL.has(p0[e - 1].lexeme))) e--;
    }

    base = baseOf(s.slice(i, i + e), reg, text);
    rest = s.slice(i + e);
  }

  return { base, decls: splitTop(rest, ',').map(parseDeclarator) };
}

/** Collects struct/union/class/enum definitions, typedefs and `using` aliases at file scope, with member offsets and sizes. */
export function findTypes(source, filename) {
  const ts = lex(source, filename).filter(t => !COMMENT.has(t.type) && t.type != 'preprocessor');
  const text = toks => (toks.length ? source.slice(toks[0].charPos, toks.at(-1).charPos + toks.at(-1).charLength).replace(/\s+/g, ' ') : '');
  const reg = new Map(),
    out = [];

  for(let s of statements(ts)) {
    if(s.fn || !s.length) continue;

    const isUsing = s[0].lexeme == 'using' && s[1]?.lexeme != 'namespace' && s[2]?.lexeme == '=';
    if(isUsing) s = [...s.slice(3), s[1]];

    const q = leading(s);
    const isTypedef = isUsing || q.set.has('typedef');

    if(!isTypedef && !AGG.has(s[q.i]?.lexeme) && s[q.i]?.lexeme != 'enum') continue;

    const d = parseDeclaration(s, reg, text, out);
    if(d.method || !isTypedef) continue;

    const named = d.base.desc && !d.base.desc.name ? d.decls.find(x => x.name && !x.stars && !x.fnptr) : null;
    if(named) {
      d.base.desc.name = named.name;
      d.base.text = `${d.base.desc.type} ${named.name}`;
      reg.set(named.name, d.base.desc);
    }

    for(const dd of d.decls) {
      if(!dd.name || dd === named) continue;
      const m = sized(d.base, dd);
      reg.set(dd.name, m);
      out.push(mk(dd.name, 'typedef', m.size, m.align, s[0].loc.line, { column: s[0].loc.column, offset: Math.min(...s.map(t => t.charPos)), end: Math.max(...s.map(t => t.charPos + t.charLength)), ...endOf(s), target: m.type }));
    }
  }

  return out;
}

const TYPE_START = new Set(['void', 'char', 'short', 'int', 'long', 'float', 'double', 'signed', 'unsigned', '_Bool', 'struct', 'union', 'enum', ...QUAL]);
const TAG_KINDS = new Set(['struct', 'union', 'class', 'enum']);
const WEAK_KINDS = new Set([...TAG_KINDS, 'field']); // may be overridden by a later real declaration of the same name
const ATTRIBUTE = /^__(attribute|declspec|asm)/;

const isIdent = t => t?.type == 'identifier';

/** `ident (ident | '*' | qualifier)+ ident` followed by something a declarator can continue with: the C "a * b;" ambiguity resolves to a declaration. `(` only counts at file scope (inside a body `a * b(c)` is an expression). */
function isDeclShape(ts, top) {
  // leading run of type-ish tokens: identifiers (typedef names, macros), type keywords, `*`
  let i = 0;
  while(ts[i] && (isIdent(ts[i]) || ts[i].lexeme == '*' || TYPE_START.has(ts[i].lexeme))) i++;

  // `Type (*fn)(...)`: a function pointer declarator after a type
  if(i >= 1 && ts[i]?.lexeme == '(' && ts[i + 1]?.lexeme == '*' && (isIdent(ts[i - 1]) || ts[i - 1].lexeme == '*')) return true;

  return i >= 2 && isIdent(ts[i - 1]) && (ts[i] == undefined || ['=', ',', ';', '[', ':', ...(top ? ['('] : [])].includes(ts[i].lexeme));
}

/** Splits a parameter list's inner tokens into the parameter name tokens and the remaining (type) tokens. */
function parseParams(inner) {
  const names = [],
    types = [];

  for(const p of splitTop(inner, ',')) {
    // function pointer parameter: `int (*fn)(int)` is named by the identifier inside `(*...)`
    const fp = p.findIndex((t, i) => t.lexeme == '(' && p[i + 1]?.lexeme == '*');
    if(fp >= 0) {
      const close = matching(p, fp);
      const nameTok = p.slice(fp + 2, close).find(isIdent);
      // the pointed-to function's own parameter names are not references either
      const inner = p[close + 1]?.lexeme == '(' ? parseParams(p.slice(close + 2, matching(p, close + 1))) : { names: [] };
      for(const t of p) (t === nameTok || inner.names.includes(t) ? names : types).push(t);
      continue;
    }

    let last = p.length - 1;
    while(p[last]?.lexeme == ']') last = p.slice(0, last).findLastIndex(t => t.lexeme == '[') - 1;

    const k = p.findLastIndex((t, i) => i <= last && isIdent(t));
    // the last identifier names the parameter unless it is the whole type (`Foo`, `struct Foo`)
    const named = k > 0 && k == last && !AGG.has(p[k - 1].lexeme) && p[k - 1].lexeme != 'enum';

    p.forEach((t, i) => {
      if(i == k && named) names.push(t);
      else types.push(t);
    });
  }

  return { names, types };
}

/** Finds the `(...)` groups at depth 0 of `ts` as [openIndex, closeIndex] pairs. */
function topGroups(ts) {
  const groups = [];
  for(let i = 0; i < ts.length; i++) {
    if(ts[i].lexeme == '(') {
      const e = matching(ts, i);
        groups.push([i, e]);
      i = e;
    } else if(ts[i].lexeme == '[' || ts[i].lexeme == '{') i = matching(ts, i);
  }
  return groups;
}

/** The lexer doesn't know every literal form (`0b11u`, `9ll`): an identifier glued to a number token is its tail. */
const isNumberSuffix = (prev, t) => /^(decimal|hexadecimal|octal|binary|float)/.test(prev?.type) && prev.charPos + prev.charLength == t.charPos;

/** Identifier tokens in `ts` that are references: not member names after `.`/`->`, not a leading label. */
function refTokens(ts) {
  const out = [];

  for(let i = 0; i < ts.length; i++) {
    const t = ts[i];

    // `__attribute__((...))`, `__declspec(...)`: compiler annotations, not references
    if(isIdent(t) && ATTRIBUTE.test(t.lexeme)) {
      if(ts[i + 1]?.lexeme == '(') i = matching(ts, i + 1);
      continue;
    }

    if(isIdent(t) && !isNumberSuffix(ts[i - 1], t) && ts[i - 1]?.lexeme != '.' && ts[i - 1]?.lexeme != '->' && !(i == 0 && ts[1]?.lexeme == ':')) out.push(t);
  }

  return out;
}

/**
 * Analyses one declaration-shaped statement (no trailing `;`). Returns null if `s`
 * doesn't look like a declaration; otherwise `{ declared, refs, params }`: `declared`
 * is `[{ tok, kind }]` (kind: function-ish 'prototype', 'data', 'typedef', 'enumerator'),
 * `refs` every other identifier token used (types, initializers, array sizes,
 * enumerator values, struct member types), `params` the parameter name tokens of a
 * function declarator. Never throws on malformed input: unrecognised shapes simply
 * contribute their identifiers to `refs`.
 */
function parseDecl(s, top) {
  const q = leading(s);
  if(!s.length || !(q.i > 0 || TYPE_START.has(s[0].lexeme) || isDeclShape(s, top))) return null;

  const declared = [],
    refs = [],
    params = [],
    members = []; // declarations found in an aggregate body, named once the aggregate's own name is known
  let rest = s.slice(q.i),
    tag = null;

  if(AGG.has(rest[0]?.lexeme) || rest[0]?.lexeme == 'enum') {
    const isEnum = rest[0].lexeme == 'enum';
    let k = 1;
    tag = isIdent(rest[k]) ? rest[k++] : null;

    // `struct S { ... }` defines the tag, a lone `struct S;` forward-declares it, anything else uses it
    if(tag) {
      if(rest[k]?.lexeme == '{') declared.push({ tok: tag, kind: rest[0].lexeme });
      else if(k == rest.length) declared.push({ tok: tag, kind: 'prototype' });
      else refs.push(tag);
    }

    if(rest[k]?.lexeme == '{') {
      const e = matching(rest, k);
      const inner = rest.slice(k + 1, e);

      for(const item of splitTop(inner, isEnum ? ',' : ';')) {
        if(isEnum) {
          if(isIdent(item[0])) declared.push({ tok: item[0], kind: 'enumerator' });
          refs.push(...refTokens(item.slice(1)));
        } else {
          const f = parseDecl(item);
          if(f) {
            refs.push(...f.refs);
            members.push(...f.declared);
          } else refs.push(...refTokens(item));
        }
      }

      rest = rest.slice(e + 1);
    } else rest = rest.slice(k);
  }

  const isTypedef = q.set.has('typedef');

  for(const part of splitTop(rest, ',')) {
    const eq = part.findIndex(t => t.lexeme == '=');
    const decl = eq >= 0 ? part.slice(0, eq) : part;
    if(eq >= 0) refs.push(...refTokens(part.slice(eq + 1)));

    // a bitfield's width is not a declarator
    const colon = decl.findIndex(t => t.lexeme == ':');
    const dd = colon >= 0 ? decl.slice(0, colon) : decl;
    if(colon >= 0) refs.push(...refTokens(decl.slice(colon + 1)));

    const groups = topGroups(dd);
    const fnptr = groups.find(([o]) => dd[o + 1]?.lexeme == '*' && !isIdent(dd[o - 1]));
    // the function's own `name(...)` group, not an annotation macro before (`FORMAT_STRING(2, 3) name(...)`)
    // or after (`name(...) FORMAT_STRING(3, 4)`) it: prefer a non-ALL-CAPS name, then the last group
    const candidates = groups.filter(([o]) => isIdent(dd[o - 1]) && !ATTRIBUTE.test(dd[o - 1].lexeme));
    const fnGroup = candidates.findLast(([o]) => !/^[A-Z_0-9]+$/.test(dd[o - 1].lexeme)) ?? candidates.at(-1);

    let name = null,
      kind = isTypedef ? 'typedef' : 'data',
      paramGroup = null;

    if(fnptr) {
      name = dd.slice(fnptr[0] + 1, fnptr[1]).find(isIdent) ?? null;
      paramGroup = groups[groups.indexOf(fnptr) + 1];
    } else if(fnGroup) {
      name = dd[fnGroup[0] - 1];
      if(!isTypedef) kind = 'prototype';
      paramGroup = fnGroup;
    } else {
      // last identifier outside any bracket/paren: `int *p`, `char buf[16]`, `Foo bar`
      for(let i = 0; i < dd.length; i++) {
        if(dd[i].lexeme == '[' || dd[i].lexeme == '(') i = matching(dd, i);
        else if(isIdent(dd[i])) name = dd[i];
      }
    }

    let params2 = null;
    if(paramGroup) {
      params2 = parseParams(dd.slice(paramGroup[0] + 1, paramGroup[1]));
      if(kind == 'prototype' || fnptr) params.push(...params2.names);
    }

    // `extern int x;` declares without defining, like a prototype
    if(kind == 'data' && q.set.has('extern') && eq < 0) kind = 'prototype';

    if(name) declared.push({ tok: name, kind });

    for(const t of refTokens(dd)) {
      if(t === name) continue;
      if(params2 && params2.names.includes(t)) continue;
      refs.push(t);
    }
  }

  // fields are named `<parent>.<field>`: the tag, else the typedef/variable the aggregate is declared as
  if(members.length) {
    const parent = tag?.lexeme ?? declared.find(x => !TAG_KINDS.has(x.kind) && x.kind != 'enumerator')?.tok.lexeme ?? null;

    for(const x of members) {
      if(x.kind == 'enumerator' || TAG_KINDS.has(x.kind)) declared.push(x);
      else if(x.kind == 'field') declared.push(x.orphan && parent ? { ...x, name: `${parent}.${x.name}`, orphan: false } : x);
      else declared.push({ tok: x.tok, kind: 'field', name: parent ? `${parent}.${x.tok.lexeme}` : x.tok.lexeme, orphan: !parent });
    }
  }

  return { declared, refs, params };
}

/**
 * Classifies one statement (C source text, or a token array without the trailing `;`
 * / a function signature carrying `.fn`): 'function' (has a body), 'prototype', 'typedef', 'data',
 * 'type' (a bare struct/union/enum definition with no declarator) or 'other' (not a
 * declaration - an expression/control statement).
 */
export function classifyStatement(s) {
  if(typeof s == 'string') s = statements(lex(s, '<statement>').filter(t => !COMMENT.has(t.type) && t.type != 'preprocessor')).next().value ?? [];

  if(s.fn) return 'function';

  let d;
  try {
    d = parseDecl(s, true);
  } catch(e) {
    return 'other';
  }

  if(!d) return 'other';

  const first = d.declared.find(x => x.kind != 'enumerator' && x.kind != 'field');
  return !first || TAG_KINDS.has(first.kind) ? 'type' : first.kind;
}

const PP_IDENTS = /\/\*[\s\S]*?\*\/|\/\/[^\n]*|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'|\b\d[\w.]*|([A-Za-z_]\w*)/g;
const PP_EXPR = new Set(['if', 'elif', 'ifdef', 'ifndef', 'elifdef', 'elifndef', 'undef']);

/** Position of character `off` of a multi-line token (line continuations included). */
function posAt(tok, off) {
  const before = tok.lexeme.slice(0, off);
  const nl = before.lastIndexOf('\n');
  return nl < 0 ? { line: tok.loc.line, column: tok.loc.column + off } : { line: tok.loc.line + before.split('\n').length - 1, column: off - nl };
}

/** A pseudo-token for text found `off` characters into the preprocessor token `tok`. */
function ppToken(tok, off, lexeme, extra) {
  return { lexeme, loc: posAt(tok, off), charPos: tok.charPos + off, charLength: lexeme.length, ...extra };
}

/** Identifier-like words in `text` (outside comments, strings and numbers) as `{ lexeme, loc, off }` pseudo-tokens positioned relative to `tok`. */
function* ppWords(tok, text, base) {
  for(const m of text.matchAll(PP_IDENTS)) if(m[1]) yield ppToken(tok, base + m.index, m[1], { off: base + m.index });
}

/**
 * Parses one preprocessor directive token: `#define NAME[(params)] body` yields the
 * macro name (`define`), its parameter names (`params`) and body tokens (`body`);
 * conditionals/`#undef` yield their identifiers as `refs`; anything else (`#include`,
 * `#pragma`, ...) yields nothing.
 */
function parseDirective(tok) {
  const d = /^#\s*(\w+)/.exec(tok.lexeme);
  if(!d) return null;

  if(d[1] == 'define') {
    const m = /^(?:[ \t]|\\\n)*([A-Za-z_]\w*)(\([^)]*\))?/.exec(tok.lexeme.slice(d[0].length));
    if(!m) return null;

    const nameOff = d[0].length + m[0].indexOf(m[1]);
    const bodyOff = d[0].length + m[0].length;
    const params = m[2] ? [...ppWords(tok, m[2], d[0].length + m[0].indexOf(m[2]))] : [];
    if(m[2]?.includes('...')) params.push({ lexeme: '__VA_ARGS__' });

    // blank out line continuations (keeping offsets) and lex the body like ordinary code
    const body = tok.lexeme.slice(bodyOff).replace(/\\\r?\n/g, m => ' '.repeat(m.length));
    const toks = lex(body, tok.loc.filename).filter(t => !COMMENT.has(t.type));

    return { define: ppToken(tok, nameOff, m[1]), fnlike: !!m[2], params, body: toks.map(t => ppToken(tok, bodyOff + t.charPos, t.lexeme, { type: t.type, charLength: t.charLength })), refs: [] };
  }

  if(PP_EXPR.has(d[1])) return { define: null, params: [], refs: [...ppWords(tok, tok.lexeme.slice(d[0].length), d[0].length)].filter(t => t.lexeme != 'defined' && !t.lexeme.startsWith('__has_')) };

  return null;
}

/**
 * Finds `#define` directives: kind 'define' for `#define CONST 213`, 'macro' for the
 * function-like `#define MAX(a, b) ...` (`params` lists the parameter names). `value` is the
 * replacement text on one line; `offset`/`end` cover the whole directive.
 */
export function findDefines(source, filename) {
  const out = [];

  for(const tok of lex(source, filename)) {
    if(tok.type != 'preprocessor') continue;

    const pp = parseDirective(tok);
    if(!pp?.define) continue;

    const { line, column } = tok.loc;
    const value = pp.body.length ? source.slice(pp.body[0].charPos, pp.body.at(-1).charPos + pp.body.at(-1).charLength).replace(/\\\r?\n/g, ' ').replace(/\s+/g, ' ') : '';
    const { line: endLine, column: endColumn } = posAt(tok, tok.lexeme.length);

    out.push({
      name: pp.define.lexeme,
      kind: pp.fnlike ? 'macro' : 'define',
      line,
      column,
      offset: tok.charPos,
      end: tok.charPos + tok.charLength,
      endLine,
      endColumn,
      ...(pp.fnlike ? { params: pp.params.map(t => (t.lexeme == '__VA_ARGS__' ? '...' : t.lexeme)) } : {}),
      value,
    });
  }

  return out;
}

/**
 * Tracks every identifier in a C source: where it's declared (`declaration`: first
 * `{ kind, file, line, column, static?, in? }`, kind one of function/data/typedef/enumerator/macro/label/field/struct/union/enum; `in` = the function a label belongs to),
 * where it has a `prototype` (declaration without a body, in headers
 * or forward declarations), and all `references` (`{ file, line, column, in? }`, `in` = the
 * enclosing function; identifiers in function bodies - statements and expressions -
 * and in variable initializers). Locals/parameters shadow same-named globals inside
 * their function and aren't entered into the table. Matching is by bare name, so
 * same-named statics in different files share one record. Pass the returned map back
 * in as `ids` to accumulate across files. Tolerant by design: unbalanced brackets,
 * macros and unknown constructs degrade to "all identifiers are references" instead
 * of throwing. `pre` is the preprocessed text of `source`: `#define`s and conditionals are
 * still read from `source`, everything else from `pre`.
 *
 * @returns {Map<string, {name: string, declaration: object|null, prototype: object[], references: object[]}>}
 */
export function findIdentifiers(source, filename, ids = new Map(), pre) {
  const all = lex(source, filename).filter(t => !COMMENT.has(t.type));
  const ts = (pre === undefined ? all : lex(pre, filename).filter(t => !COMMENT.has(t.type))).filter(t => t.type != 'preprocessor');
  const entry = name => ids.get(name) ?? ids.set(name, { name, declaration: null, prototype: [], references: [] }).get(name);
  const pos = t => ({ file: filename, line: t.loc.line, column: t.loc.column, offset: t.charPos, end: t.charPos + t.charLength, endLine: t.loc.line, endColumn: t.loc.column + t.charLength });

  const ref = (t, fn, locals) => {
    if(locals?.has(t.lexeme)) return;
    entry(t.lexeme).references.push(fn ? { ...pos(t), in: fn } : pos(t));
  };

  const declare = (d, isStatic, isFn) => {
    const e = entry(d.name ?? d.tok.lexeme);
    if(d.kind == 'prototype' && !isFn) e.prototype.push(pos(d.tok));
    else if(!e.declaration || (WEAK_KINDS.has(e.declaration.kind) && !WEAK_KINDS.has(d.kind))) e.declaration = { kind: isFn ? 'function' : d.kind, ...pos(d.tok), ...(isStatic ? { static: true } : {}), ...(d.in ? { in: d.in } : {}) };
  };

  const scanBody = (body, fn, locals) => {
    const stmt = st => {
      for(;;) {
        const l = st[0]?.lexeme;
        if(l == 'else' || l == 'do') st = st.slice(1);
        else if(l == 'default') st = st.slice(2);
        else if(isIdent(st[0]) && st[1]?.lexeme == ':') {
          declare({ tok: st[0], kind: 'label', in: fn }, false, false);
          st = st.slice(2);
        }
        else if(l == 'case') {
          const c = st.findIndex(t => t.lexeme == ':');
          const e = c < 0 ? st.length : c;
          for(const t of refTokens(st.slice(1, e))) ref(t, fn, locals);
          st = st.slice(e + 1);
        } else break;
      }

      if(!st.length) return;

      if(['if', 'while', 'for', 'switch'].includes(st[0].lexeme) && st[1]?.lexeme == '(') {
        const e = matching(st, 1);
        const parts = st[0].lexeme == 'for' ? splitTop(st.slice(2, e), ';') : [st.slice(2, e)];
        for(const p of parts) stmt(p);
        stmt(st.slice(e + 1));
        return;
      }

      const d = parseDecl(st);

      if(d) {
        // a declarator is in scope for its own initializer and for later declarators
        for(const x of [...d.declared.filter(x => x.kind != 'field').map(x => x.tok), ...d.params]) locals.add(x.lexeme);
        for(const t of d.refs) ref(t, fn, locals);
      } else for(const t of refTokens(st)) ref(t, fn, locals);
    };

    let cur = [],
      depth = 0;
    const flush = () => {
      if(cur.length) {
        try {
          stmt(cur);
        } catch(e) {
          for(const t of refTokens(cur)) ref(t, fn, locals);
        }
      }
      cur = [];
    };

    for(let n = 0; n < body.length; n++) {
      const t = body[n];

      // `= { ... }` is an initializer, not a block: keep it in the declaration
      if(t.lexeme == '{' && cur.at(-1)?.lexeme == '=') {
        const e = matching(body, n);
        cur.push(...body.slice(n, e + 1));
        n = e;
        continue;
      }

      if(t.lexeme == '(' || t.lexeme == '[') depth++;
      else if(t.lexeme == ')' || t.lexeme == ']') depth--;

      if(depth <= 0 && (t.lexeme == ';' || t.lexeme == '{' || t.lexeme == '}')) flush();
      else cur.push(t);
    }

    flush();
  };

  for(const tok of all) {
    if(tok.type != 'preprocessor') continue;

    const pp = parseDirective(tok);
    if(!pp) continue;

    const name = pp.define?.lexeme;
    const params = new Set(pp.params.map(t => t.lexeme));

    if(pp.define) declare({ tok: pp.define, kind: 'macro' }, false, false);
    for(const t of pp.refs) ref(t, name, params);
    if(pp.body) scanBody(pp.body, name, params);
  }

  for(let s of statements(ts)) {
    if(!s.length) continue;

    try {
      const isStatic = s.some(t => t.lexeme == 'static');
      let d = parseDecl(s, true);

      // stray tokens (e.g. the tail of a comment the lexer ended early) in front of a declaration
      if(!d) {
        let k = 0;
        while(s[k] && !isIdent(s[k]) && !TYPE_START.has(s[k].lexeme)) k++;
        if(k && k < s.length) {
          const rest = Object.assign(s.slice(k), { fn: s.fn, body: s.body });
          d = parseDecl(rest, true);
          if(d) s = rest;
        }
      }

      // `MACRO(args)` lines carry no `;`, so they run into the declaration after them: peel them off
      if(!d) {
        let k = 0;
        while(isIdent(s[k]) && s[k + 1]?.lexeme == '(' && matching(s, k + 1) < s.length - 1) {
          const e = matching(s, k + 1);
          for(const t of refTokens(s.slice(k, e + 1))) ref(t);
          k = e + 1;
        }

        if(k) {
          const rest = Object.assign(s.slice(k), { fn: s.fn, body: s.body });
          d = parseDecl(rest, true);
          if(d) s = rest;
        }
      }

      if(!d) {
        for(const t of refTokens(s)) ref(t);
        continue;
      }

      const fnName = s.fn ? d.declared.find(x => x.kind == 'prototype')?.tok.lexeme : undefined;

      for(const x of d.declared) declare(x, isStatic, s.fn && x.tok.lexeme == fnName);
      for(const t of d.refs) ref(t, fnName);

      if(s.fn) scanBody(s.body, fnName, new Set(d.params.map(t => t.lexeme)));
    } catch(e) {
      for(const t of refTokens(s)) ref(t);
      if(s.body) for(const t of refTokens(s.body)) ref(t);
    }
  }

  return ids;
}

function* walkFiles(dir) {
  const [entries, err] = readdir(dir);
  if(err || !entries) return;

  for(const entry of entries.filter(e => e != '.' && e != '..').sort()) {
    const p = `${dir.replace(/\/+$/, '')}/${entry}`;
    const [sub, subErr] = readdir(p);

    if(!subErr && sub) yield* walkFiles(p);
    else if(/\.[ch]$/i.test(entry)) yield p;
  }
}

/** Replaces each directory in `paths` with the *.c/*.h files below it (recursively, sorted). */
function expandPaths(paths) {
  return paths.flatMap(p => {
    const [entries, err] = readdir(p);
    return p != '-' && !err && entries ? [...walkFiles(p)] : [p];
  });
}

const KINDS = ['function', 'define', 'macro', 'typedef', 'struct', 'union', 'class', 'enum'];
const LOC_MODES = ['line', 'offset', 'loc', 'range', 'file', 'start', 'end'];

/**
 * Rewrites the position properties of `rec` (`line`, `column`, `offset`, `end`, as the
 * finders record them) into the requested shapes: `line` -> .line + .column, `offset` ->
 * .offset, `loc` -> .loc { line, column, file }, `range` -> .range { start, end, file },
 * `file` -> .file, `start` -> .start "<file>:<line>:<column>", `end` -> .end
 * "<file>:<line>:<column>" (just past the last character).
 */
function placed(rec, file, modes) {
  const { line, column, offset, end, endLine, endColumn, file: own, ...rest } = rec;
  const f = own ?? file;
  const out = { ...rest };

  if(modes.includes('line')) Object.assign(out, { line, column });
  if(modes.includes('offset')) out.offset = offset;
  if(modes.includes('loc')) out.loc = { line, column, file: f };
  if(modes.includes('range')) out.range = { start: offset, end, file: f };
  if(modes.includes('file')) out.file = f;
  if(modes.includes('start')) out.start = `${f}:${line}:${column}`;
  if(modes.includes('end')) out.end = `${f}:${endLine}:${endColumn}`;

  return out;
}

function placedIdentifier(e, modes, count) {
  return {
    ...e,
    ...(count ? { references: e.references.length } : {}),
    declaration: e.declaration && placed(e.declaration, undefined, modes),
    prototype: e.prototype.map(p => placed(p, undefined, modes)),
    ...(count ? {} : { references: e.references.map(r => placed(r, undefined, modes)) }),
  };
}

// declaration kinds `-i` leaves out: not what a symbol listing is after
const HIDDEN_KINDS = new Set(['macro', 'enumerator', 'label']);

/** `{ endLine, endColumn }` of character offset `off` in `source`. */
function endPos(source, off) {
  const before = source.slice(0, off),
    nl = before.lastIndexOf('\n');
  return { endLine: before.split('\n').length, endColumn: off - nl };
}

const ENTRY = '<entry>',
  FILE_SCOPE = '<file-scope>';

/**
 * Names declared in `ids` that no walk from the entry points reaches. Edges run from the
 * enclosing function/macro of each reference to the referenced name; references outside
 * any function (initializers, array sizes, prototypes) count as roots, so they keep their
 * targets alive. Matching is by bare name.
 */
function unvisited(ids) {
  const edges = new Map(),
    seen = new Set([ENTRY, FILE_SCOPE]),
    todo = [ENTRY, FILE_SCOPE];

  for(const e of ids.values())
    for(const r of e.references) {
      const from = r.in ?? FILE_SCOPE;
      if(!edges.has(from)) edges.set(from, new Set());
      edges.get(from).add(e.name);
    }

  while(todo.length)
    for(const to of edges.get(todo.pop()) ?? [])
      if(!seen.has(to)) {
        seen.add(to);
        todo.push(to);
      }

  return [...ids.values()].filter(e => e.declaration && !seen.has(e.name));
}

/** Predicate for a reference-count expression: `1`, `<=2`, `>1`, `!=0` or the inclusive range `1,4`; null if malformed. */
function countTest(expr) {
  let m;
  if((m = /^\s*(\d+)\s*,\s*(\d+)\s*$/.exec(expr))) return n => n >= +m[1] && n <= +m[2];
  if(!(m = /^\s*(<=|>=|==|!=|<|>|=)?\s*(\d+)\s*$/.exec(expr))) return null;
  const v = +m[2];
  return { '<=': n => n <= v, '>=': n => n >= v, '<': n => n < v, '>': n => n > v, '!=': n => n != v }[m[1]] ?? (n => n == v);
}

/**
 * Runs `prog` (e.g. `cpp`, `gcc -E`) on `file` and returns the text of `file` alone: lines of
 * included headers are dropped, the rest stay on the line they came from (via the `# N "file"`
 * markers), so positions match the original file. Macros are expanded, `#define`s are gone.
 * Returns null with a message on stderr if the preprocessor fails.
 */
function preprocess(prog, file, includes) {
  const quote = v => `'${v.replace(/'/g, `'\\''`)}'`;
  const f = std.popen(`${prog} ${includes.map(d => '-I' + quote(d)).join(' ')} ${quote(file)}`, 'r');
  const text = f.readAsString();
  if(f.close() != 0) {
    std.err.puts(`extract-c.js: ${prog} failed on ${file}\n`);
    return null;
  }

  const out = [];
  let main = null,
    mine = false,
    no = 1;

  for(const l of text.split('\n')) {
    const m = /^# (\d+) "((?:[^"\\]|\\.)*)"/.exec(l);

    if(m) {
      no = +m[1];
      if(main === null && !m[2].startsWith('<')) main = m[2];
      mine = m[2] === main;
      continue;
    }

    if(mine) out[no - 1] = l;
    no++;
  }

  return Array.from(out, l => l ?? '').join('\n');
}

/** Example `jq` queries for the JSON just written to `file`; `mode` is 'identifiers', 'types' or 'records' (-F alone). */
function jqHints(file, mode, count) {
  const q = {
    identifiers: [
      [count ? `.[] | select(.references == 0) | .name` : `.[] | select(.references | length == 0) | .name`, 'unused identifiers'],
      [`.[] | select(.declaration.kind == "function") | "\\(.name) \\(.declaration.line)"`, 'functions with line'],
      [`map(.name) | sort | .[]`, 'all names'],
    ],
    types: [
      [`.[] | select(.type == "struct") | "\\(.name) \\(.size)"`, 'struct names and sizes'],
      [`.[] | select(.type == "typedef") | "\\(.name) = \\(.target)"`, 'typedef targets'],
      [`map(.name) | sort | .[]`, 'all names'],
    ],
    records: [
      [`group_by(.kind) | map({ (.[0].kind): length }) | add`, 'count per kind'],
      [`.[] | select(.kind == "define") | "\\(.name) = \\(.value)"`, 'defines with value'],
      [`map(.name) | sort | .[]`, 'all names'],
    ],
  }[mode];

  return `wrote ${file}; try:\n` + q.map(([expr, what]) => `  jq -r '${expr}' ${file}  # ${what}`).join('\n');
}

/**
 * Opens a REPL on `vars`, set as properties of globalThis, and resolves when it is left
 * (Ctrl-D, `\\q`); the caller reads the properties back, so reassigning them there changes
 * what is written.
 */
async function openRepl(vars) {
  const { REPL } = await import('repl');

  Object.assign(globalThis, vars);
  std.err.puts(`extract-c.js: REPL, globalThis has ${Object.keys(vars).join(', ')}; Ctrl-D writes the output\n`);

  const repl = (globalThis.repl = new REPL('extract-c ', false));

  repl.exit = function() {
    for(const handler of this.cleanupHandlers) handler.call(this);
    this.running = false;
  };
  repl.loadSaveOptions();
  repl.historyLoad();

  await repl.run();
  signal(SIGINT, null);
}

async function main(...args) {
  let pattern, list, types, identifiers, fields, splitDir, output, kinds, count, countExpr, deps, entries, prog, wantRepl;
  const includes = [];
  const defaultPrep = () => (std.popen('command -v cpp >/dev/null 2>&1', 'r').close() == 0 ? 'cpp' : 'gcc -E');
  let locModes = ['line'];

  const params = getOpt(
    {
      pattern: [true, v => (pattern = new RegExp(v)), 'p'],
      list: [false, () => (list = true), 'l'],
      types: [false, () => (types = true), 't'],
      identifiers: [false, () => (identifiers = true), 'i'],
      fields: [false, () => (fields = true), 'f'],
      count: [false, () => (count = true), 'c'],
      'count-filter': [true, v => (countExpr = v), 'C'],
      deps: [false, () => (deps = identifiers = true), 'd'],
      main: [true, v => (entries = v.split(',')), 'm'],
      preprocess: [false, () => (prog = defaultPrep()), 'E'],
      include: [true, v => includes.push(v), 'I'],
      preprocessor: [true, v => (prog = v), 'e'],
      repl: [false, () => (wantRepl = true), 'r'],
      filter: [true, v => (kinds = v.split(',')), 'F'],
      loc: [true, v => (locModes = v.split(',')), 'L'],
      split: [true, v => (splitDir = v), 's'],
      output: [true, v => (output = v), 'o'],
      help: [false, null, 'h'],
      '@': 'files',
    },
    args,
  );
  const files = expandPaths(params['@']);

  /* standard input: the file `-`, or no file at all when it is not a terminal */
  if(!files.length && !params.help && !isatty(0)) files.push('-');

  if(locModes.some(m => !LOC_MODES.includes(m))) {
    console.log(`extract-c.js: --loc: expected ${LOC_MODES.join(', ')}`);
    return 1;
  }

  const countOk = countExpr === undefined ? null : countTest(countExpr);
  if(countExpr !== undefined && !countOk) {
    console.log(`extract-c.js: --count-filter: expected N, <N, <=N, >N, >=N, !=N or MIN,MAX`);
    return 1;
  }

  if(kinds?.some(k => !KINDS.includes(k))) {
    console.log(`extract-c.js: --filter: expected ${KINDS.join(', ')}`);
    return 1;
  }

  if(params.help || !files.length) {
    console.log(`Usage: extract-c.js [-p REGEXP] [-l] [-t] [-i] [-f] [-c] [-C EXPR] [-d] [-m NAME[,NAME]] [-E] [-e PROG] [-I DIR] [-r] [-F KIND[,KIND]] [-L MODE] [-s DIR] [-o FILE] FILE|DIR...

  -p, --pattern REGEXP  only functions/types whose name matches (default: all)
  -l, --list            print "file:line:column: name" instead of the source / IR
  -t, --types           emit struct/union/class/enum/typedef/using as JSON IR
                        (describeObject()-shaped; fields carry byte offset and
                        size, assuming LP64) instead of functions
  -i, --identifiers     emit every identifier's declaration, prototypes and
                        references (across all FILEs) as JSON, leaving out names
                        without a declaration, macros, enumerators and labels;
                        with -l one
                        "file:line:column: kind name<TAB>prototypes<TAB>references"
                        line per name
  -f, --fields          with -i, also list struct/union fields (<parent>.<field>)
  -c, --count           with -i, "references" is the number of references, not
                        the list: jq '.[] | select(.references == 0)' dumps
                        the unused identifiers
  -C, --count-filter EXPR
                        with -i, only identifiers whose reference count matches:
                        N, <N, <=N, >N, >=N, !=N, or an inclusive range MIN,MAX
  -d, --deps            with -i, build the dependency graph and print the
                        identifiers no walk from the entry points reaches to
                        stdout (the -i output then only goes to -o FILE)
  -m, --main NAME[,NAME]
                        entry points for -d (default: main); each gains one
                        reference, so main is counted as used
  -E, --preprocess      run the sources through cpp (else gcc -E) before looking at
                        functions, types and identifiers; #defines are always read
                        from the unpreprocessed text. Included headers' content is
                        dropped, lines stay put; offsets/ranges and function text
                        refer to the expanded source
  -e, --preprocessor PROG
                        like -E with this command, e.g. -e 'gcc -E -Iinclude'
                        (the file name is appended)
  -I, --include DIR     add DIR to the preprocessor's include path (repeatable);
                        only used with -E / -e
  -r, --repl            open a REPL before the output is written; globalThis gets
                        files, ids (Map of identifiers), records (the JSON IR),
                        chunks (the text output), dead (with -d) and the find*
                        functions. Reassigning them changes the output. Ctrl-D
                        writes it. The same REPL opens when Ctrl-C is pressed
                        once while the files are being read (twice aborts)
  -F, --filter KIND[,KIND]
                        only these kinds: function, define (#define CONST 1),
                        macro (#define F(x) ...), typedef, struct, union, class,
                        enum. Alone it emits all matching records as JSON IR
                        (functions with their source as .text); with -t / -i it
                        narrows that output
  -L, --loc MODE[,MODE]  how -t/-i JSON reports positions (default: line):
                        line   .line and .column
                        offset .offset (character offset)
                        loc    .loc   { line, column, file }
                        range  .range { start, end, file } (character offsets)
                        file   .file (the file name)
                        start  .start "<file>:<line>:<column>"
                        end    .end   "<file>:<line>:<column>" (just past the last character)
  -s, --split DIR       write each function to DIR/<name>.c, prefixed with the
                        text preceding the file's first function (#includes etc.)
  -o, --output FILE     write to FILE instead of stdout

A DIR argument stands for every *.c/*.h file below it, searched recursively.`);
    return params.help ? 0 : 1;
  }

  if(splitDir) fs.mkdirSync(splitDir, { recursive: true });

  let chunks = [],
    irs = [],
    ids = new Map();

  const progress = isatty(2);
  const redraw = text => {
    std.err.puts(text);
    std.err.flush();
  };

  if(wantRepl && !isatty(0)) {
    std.err.puts('extract-c.js: --repl needs a terminal on standard input\n');
    return 1;
  }

  /* Ctrl-C once asks for the REPL, twice aborts; handled between files */
  if(progress) {
    signal(SIGINT, () => {
      if(wantRepl) {
        redraw('\r\x1b[K');
        std.exit(130);
      }
      wantRepl = true;
      redraw('\r\x1b[Kextract-c.js: REPL after the last file (Ctrl-C again aborts)\n');
    });
  }

  for(const [n, arg] of files.entries()) {
    if(progress) {
      redraw(`\r${n + 1}/${files.length}`);
      await new Promise(resolve => setTimeout(resolve, 0));
    }
    const file = arg == '-' ? '<stdin>' : arg;
    const source = arg == '-' ? std.in.readAsString() : fs.readFileSync(arg, 'utf8');
    let code = source;

    if(prog) {
      if(arg == '-') {
        std.err.puts('extract-c.js: cannot preprocess standard input\n');
        return 1;
      }
      if((code = preprocess(prog, arg, includes)) === null) return 1;
    }

    if(identifiers) {
      findIdentifiers(source, file, ids, prog ? code : undefined);
      continue;
    }

    if(kinds && !types) {
      const recs = [];
      if(kinds.includes('function')) for(const f of findFunctions(code, file)) recs.push({ name: f.name, kind: 'function', line: f.line, column: f.column, offset: f.start, end: f.end, ...endPos(code, f.end), text: code.slice(f.start, f.end) });
      for(const d of findDefines(source, file)) if(kinds.includes(d.kind)) recs.push(d);
      for(const { type, ...d } of findTypes(code, file)) if(kinds.includes(type)) recs.push({ kind: type, ...d });

      for(const d of recs.filter(d => !pattern || pattern.test(d.name ?? '')).sort((a, b) => a.line - b.line || a.column - b.column)) {
        if(list) chunks.push(`${file}:${d.line}:${d.column}: ${d.kind} ${d.name}\n`);
        else irs.push(placed(d, file, locModes));
      }
      continue;
    }

    if(types) {
      for(const d of findTypes(code, file).filter(d => (!pattern || pattern.test(d.name ?? '')) && (!kinds || kinds.includes(d.type)))) {
        if(list) chunks.push(`${file}:${d.line}:${d.column}: ${d.type} ${d.name}\n`);
        else irs.push(placed(d, file, locModes));
      }
      continue;
    }

    const all = findFunctions(code, file);
    const funcs = pattern ? all.filter(f => pattern.test(f.name)) : all;
    const preamble = all.length ? code.slice(0, all[0].start).trimEnd() : '';

    for(const f of funcs) {
      const text = code.slice(f.start, f.end);

      if(splitDir) {
        const dest = path.join(splitDir, f.name + '.c');
        fs.writeFileSync(dest, (preamble ? preamble + '\n\n' : '') + text + '\n');
        console.log(`${file}:${f.line}:${f.column}: ${f.name} -> ${dest}`);
      } else if(list) chunks.push(`${file}:${f.line}:${f.column}: ${f.name}\n`);
      else chunks.push(text + '\n\n');
    }
  }

  if(progress) {
    signal(SIGINT, null);
    if(files.length) redraw('\r\x1b[K');
  }

  const shown = e => {
    if(pattern && !pattern.test(e.name)) return false;
    if(!e.declaration) return false;
    if(kinds ? !kinds.includes(e.declaration.kind) : HIDDEN_KINDS.has(e.declaration.kind)) return false;
    return fields || kinds || e.declaration.kind != 'field';
  };
  const line = e => `${e.declaration.file}:${e.declaration.line}:${e.declaration.column}: ${e.declaration.kind} ${e.name}\t${e.prototype.length}\t${e.references.length}\n`;
  let dead = null;

  if(deps) {
    const noRef = { file: ENTRY, line: 0, column: 0, offset: 0, end: 0, endLine: 0, endColumn: 0, in: ENTRY };

    for(const name of entries ?? ['main']) {
      const e = ids.get(name);
      if(!e?.declaration) {
        std.err.puts(`extract-c.js: --main: no declaration of '${name}'\n`);
        return 1;
      }
      e.references.push({ ...noRef });
    }

    dead = unvisited(ids).filter(shown);
  }

  if(identifiers && (!deps || output)) {
    for(const e of ids.values()) {
      if(!shown(e) || (countOk && !countOk(e.references.length))) continue;
      if(list) chunks.push(line(e));
      else irs.push(placedIdentifier(e, locModes, count));
    }
  }

  if(wantRepl) {
    await openRepl({ files, ids, records: irs, chunks, dead, findFunctions, findTypes, findDefines, findIdentifiers });
    ({ records: irs, chunks, dead } = globalThis);
  }

  if(irs.length) chunks.push(JSON.stringify(irs, null, 2));

  if(chunks.length) {
    const out = chunks.join('');
    if(output) {
      fs.writeFileSync(output, out);
      if(irs.length) std.err.puts(jqHints(output, identifiers ? 'identifiers' : kinds && !types ? 'records' : 'types', count) + '\n');
    }
    else console.log(out.trimEnd());
  }

  if(dead) console.log(list ? dead.map(line).join('').trimEnd() : JSON.stringify(dead.map(e => placedIdentifier(e, locModes, count)), null, 2));

  return 0;
}

if(isMainModule(import.meta.url))
  main(...scriptArgs.slice(1)).then(
    rc => process.exit(rc),
    e => {
      std.err.puts(`${e?.stack ?? e}\n`);
      process.exit(1);
    },
  );
