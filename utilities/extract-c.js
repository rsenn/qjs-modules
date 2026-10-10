#!/usr/bin/env qjsm
import * as fs from 'fs';
import * as path from 'path';
import { Pointer } from 'pointer';
import * as std from 'std';
import * as os from 'os';
import { isatty, read, readdir, SIGINT, signal, setTimeout, ttyGetWinSize } from 'os';
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
/* hidden full position of a placed record: { file, line, column, offset, end, endLine, endColumn } */
const SRC = Symbol('src');
const SHOWN = Object.freeze({ [Symbol.toStringTag]: 'Shown' });

/* xterm-256color palette */
const COLOR = {
  gutter: 240,
  rule: 238,
  path: 110,
  pos: 180,
  dim: 245,
  warn: 203,
  label: { declaration: 39, prototype: 178, reference: 108 },
  kind: { function: 39, data: 114, struct: 214, union: 214, class: 214, enum: 178, typedef: 141, macro: 203, define: 215, field: 150, enumerator: 150, label: 245 },
  code: { keyword: 141, string: 150, number: 215, comment: 244, directive: 109, ident: 252 },
};
const MARK = '\x1b[1;38;5;231;48;5;25m';
const NUMBER = /^(decimal|hexadecimal|octal|binary|float)/;

const pos = p => p?.[SRC] ?? p;

/** True if `format()` knows how to lay out `v`: a record from this tool, or an array of them. */
export function showable(v) {
  if(Array.isArray(v)) return v.length > 0 && v.every(showable);
  if(!v || typeof v != 'object' || typeof v.name != 'string') return false;
  return !!(v.declaration && v.prototype) || !!(v.kind || v.type) && (v.line !== undefined || !!v[SRC]);
}

const sourceCache = new Map();
const readLines = file => {
  if(!sourceCache.has(file)) {
    let lines = null;
    try {
      lines = fs.readFileSync(file, 'utf8').split('\n');
    } catch(e) {}
    sourceCache.set(file, lines);
  }
  return sourceCache.get(file);
};

/** Colors the C in `text`; `marks` are [line, column, style = 'mark'] (1-based, within `text`) of tokens to emphasise. Returns one string per line. */
function highlightC(text, marks, paint, width) {
  const out = [''],
    vis = [0];
  const put = (s, code, mark) => {
    for(const [i, seg] of s.split('\n').entries()) {
      if(i) {
        out.push('');
        vis.push(0);
      }
      const n = out.length - 1;
      const room = Math.max(0, width - vis[n]);
      const part = seg.replace(/\t/g, '  ');
      const cut = part.length > room ? part.slice(0, Math.max(0, room - 1)) + '…' : part;
      vis[n] += cut.length;
      out[n] += paint(mark ? (mark[2] ?? 'mark') : code, cut);
    }
  };

  try {
    const lexer = new CLexer(text, undefined, '<show>');
    let t;
    while((t = lexer.nextToken())) {
      let code = COLOR.code.ident;
      if(t.type == 'whitespace') code = null;
      else if(COMMENT.has(t.type)) code = COLOR.code.comment;
      else if(t.type == 'preprocessor') code = COLOR.code.directive;
      else if(t.type == 'string_literal' || t.type == 'char_literal') code = COLOR.code.string;
      else if(NUMBER.test(t.type)) code = COLOR.code.number;
      else if(t.type == t.lexeme && /^[a-z_]\w*$/.test(t.lexeme)) code = COLOR.code.keyword;
      else if(t.type != 'identifier') code = COLOR.dim;
      put(t.lexeme, code, marks.find(([l, c]) => l == t.loc.line && c == t.loc.column));
    }
  } catch(e) {
    return text.split('\n').map(l => l.slice(0, width));
  }
  return out;
}

/*
 * A screen row of show() output is a String object that also says what a click on it means:
 *
 *   atoms  pointer atoms of the record part the row shows, relative to the record
 *   src    { file, line, column } the row points at
 *   cells  [{ x0, x1, atoms }] clickable parts with their own pointer (1-based screen x)
 *   code   { file, line, text, x0, from, lead } for rows of source text
 *   item   index of the record in the shown array
 *   root   code of the object `atoms` is relative to, when it is not the record itself
 */
const tag = (s, meta) => Object.assign(new String(s), meta);
const visibleLength = s => String(s).replace(/\x1b\[[0-9;]*m/g, '').length;

/** A row from parts `[text, color, atoms?, raw?]`; a part with `atoms` becomes a clickable cell. `raw` replaces `text` in the output (pre-painted text of the same width). */
function parts(o, list, meta = {}) {
  const cells = [];
  let x = meta.x ?? 1,
    s = '';

  for(const [text, color, atoms, raw] of list) {
    s += raw ?? (color == null ? text : o.paint(color, text));
    if(atoms) cells.push({ x0: x, x1: x + text.length - 1, atoms });
    x += text.length;
  }

  return tag(s, { ...meta, cells });
}

/** The source column (1-based) shown at 0-based display offset `d` of `text` read from index `from`; tabs show as 2 columns, `lead` columns are the '…' prefix of a windowed line. */
export function srcColAt(text, from, lead, d) {
  let shown = lead,
    i = from;

  for(; i < text.length; i++) {
    const w = text[i] == '\t' ? 2 : 1;
    if(d < shown + w) return i + 1;
    shown += w;
  }

  return text.length + 1;
}

/** The source lines of `p` as gutter-numbered rows, with the token at p.line:p.column emphasised. */
function snippet(p, o, mark = true, atoms = []) {
  const lines = o.read(p.file);
  const gutter = (n, bar = '│') => `  ${String(n).padStart(o.gw)} ${o.paint(COLOR.gutter, bar)} `;
  if(!lines) return [tag(`  ${' '.repeat(o.gw)} ${o.paint(COLOR.gutter, '│')} ${o.paint(COLOR.warn, `(cannot read ${p.file})`)}`, { atoms })];

  const first = p.line,
    last = Math.min(Math.max(p.endLine ?? p.line, p.line), lines.length);
  const shown = Math.min(last, first + o.lines - 1);
  let text = lines.slice(first - 1, shown).join('\n'),
    col = p.column,
    lead = '',
    from = 0;

  /* a long single line is windowed around the mark */
  if(first == shown && text.length > o.width) {
    from = Math.max(0, col - 1 - 40);
    lead = from ? '…' : '';
    text = lead + text.slice(from, from + o.width);
    col = col - from + lead.length;
  }

  const rows = highlightC(text, mark && p.column ? [[1, col]] : [], o.paint, o.width).map((row, i) => {
    const n = first + i;
    return tag(gutter(n) + row, { atoms, src: { file: p.file, line: n, column: p.column }, code: { file: p.file, line: n, text: lines[n - 1], x0: o.gw + 6, from, lead: lead.length } });
  });
  if(shown < last) rows.push(tag(`  ${' '.repeat(o.gw)} ${o.paint(COLOR.gutter, '┊')} ${o.paint(COLOR.dim, `… ${last - shown} more line${last - shown > 1 ? 's' : ''}`)}`, { atoms }));
  return rows;
}

const where = (p, o) => o.paint(COLOR.path, p.file ?? '?') + o.paint(COLOR.pos, `:${p.line}:${p.column}`);

/**
 * One annotated source location: `label  file:line:col  note`, then its source. `atoms` is where
 * the location lives in the record; `obj` is the object holding file/line/column (only keys it has
 * become clickable).
 */
function location(label, p, o, { note = '', mark = true, atoms = [], obj = p, root } = {}) {
  const key = k => (obj && Object.hasOwn(obj, k) ? [...atoms, k] : undefined);
  const head = parts(
    o,
    [
      [label.padEnd(13), COLOR.label[label] ?? COLOR.dim, atoms],
      [p.file ?? '?', COLOR.path, key('file')],
      [`:${p.line}`, COLOR.pos, key('line')],
      [`:${p.column}`, COLOR.pos, key('column')],
      ...(note ? [['  ' + note, COLOR.dim]] : []),
    ],
    { atoms, src: { file: p.file, line: p.line, column: p.column } },
  );
  return [head, ...snippet(p, o, mark, atoms)].map(r => (root ? Object.assign(r, { root }) : r));
}

const rule = o => o.paint(COLOR.rule, '─'.repeat(Math.min(o.width, 72)));

function formatIdentifier(r, o) {
  const d = pos(r.declaration);
  const raw = o.ids?.get(r.name);
  const fromRaw = !Array.isArray(r.references) && raw;
  const refs = (Array.isArray(r.references) ? r.references : raw?.references) ?? [];
  const nrefs = Array.isArray(r.references) ? r.references.length : typeof r.references == 'number' ? r.references : refs.length;
  const protos = (r.prototype?.length ? r.prototype : raw?.prototype) ?? [];
  const meta = [r.declaration.static ? 'static' : null, `${protos.length} prototype${protos.length == 1 ? '' : 's'}`, nrefs ? `${nrefs} reference${nrefs == 1 ? '' : 's'}` : null].filter(Boolean).join(' · ');
  const root = fromRaw ? `ids.get(${JSON.stringify(r.name)})` : undefined;

  o.gw = Math.max(String(d.line).length, ...protos.map(p => String(pos(p).line).length), ...refs.slice(0, o.refs).map(p => String(pos(p).line).length));

  const out = [
    parts(
      o,
      [
        [`■ ${r.declaration.kind}`, COLOR.kind[r.declaration.kind] ?? COLOR.dim, ['declaration', 'kind']],
        [' '],
        [r.name, 255, ['name']],
        ['  '],
        [meta, COLOR.dim, ['references']],
        ...(nrefs ? [] : [['  '], ['✗ unreferenced', COLOR.warn]]),
      ],
      { atoms: [], src: { file: d.file, line: d.line, column: d.column } },
    ),
  ];
  out.push(...location('declaration', d, o, { note: d.in ? `in ${d.in}` : '', atoms: ['declaration'], obj: r.declaration }));
  for(const [i, p] of protos.entries()) out.push(...location('prototype', pos(p), o, { atoms: ['prototype', i], obj: p, root: fromRaw ? root : undefined }));
  for(const [i, p] of refs.slice(0, o.refs).entries()) out.push(...location('reference', pos(p), o, { note: pos(p).in ? `in ${pos(p).in}` : '', atoms: ['references', i], obj: p, root: fromRaw ? root : undefined }));
  if(refs.length > o.refs) out.push(o.paint(COLOR.dim, `… ${refs.length - o.refs} more references`));
  else if(nrefs > refs.length && !refs.length) out.push(o.paint(COLOR.dim, `(${nrefs} references; use ids.get('${r.name}') for their locations)`));
  return out;
}

function formatType(r, o) {
  const p = pos(r);
  const kind = r.kind ?? r.type;
  const meta = [r.size != null ? `size ${r.size}` : null, r.align != null ? `align ${r.align}` : null, r.target ? `= ${r.target}` : null].filter(Boolean).join(' · ');
  o.gw = String(p.endLine ?? p.line).length;

  const out = [parts(o, [[`■ ${kind}`, COLOR.kind[kind] ?? COLOR.dim, [r.kind ? 'kind' : 'type']], [' '], [r.name ?? '<anonymous>', 255, ['name']], ['  '], [meta, COLOR.dim, ['size']]], { atoms: [], src: { file: p.file, line: p.line, column: p.column } })];
  if(p.file) out.push(...location('declaration', p, o, { mark: false, obj: r }));

  const fields = r.fields ?? [];
  if(fields.length) {
    const named = fields.map((f, i) => [f, i]).filter(([f]) => f.name != null || f.type);
    if(kind == 'enum') {
      const items = [['  ']];
      for(const [f, i] of named) items.push([f.name, COLOR.kind.enumerator, ['fields', i, 'name']], [', ', COLOR.dim]);
      out.push(parts(o, items.slice(0, -1), { atoms: ['fields'] }));
    } else {
      const w = Math.max(...named.map(([f]) => (f.type ?? '').length));
      out.push(o.paint(COLOR.dim, `  offset  size  ${'type'.padEnd(w)}  name`));
      for(const [f, i] of named) {
        const off = f.offset == null ? '?' : '+' + f.offset + (f.bitOffset ? '.' + f.bitOffset : '');
        out.push(
          parts(
            o,
            [
              ['  '],
              [off.padStart(6), COLOR.pos, ['fields', i, 'offset']],
              ['  '],
              [String(f.size ?? '?').padStart(4), COLOR.pos, ['fields', i, 'size']],
              ['  '],
              [(f.type ?? '').padEnd(w), COLOR.code.keyword, ['fields', i, 'type']],
              ['  '],
              [f.name ?? '', COLOR.code.ident, ['fields', i, 'name']],
              ...(f.bits !== undefined ? [[` : ${f.bits}`, COLOR.dim, ['fields', i, 'bits']]] : []),
            ],
            { atoms: ['fields', i], src: { file: p.file, line: p.line, column: p.column } },
          ),
        );
      }
    }
  }
  return out;
}

function formatDefine(r, o) {
  const p = pos(r);
  o.gw = String(p.endLine ?? p.line).length;
  const sig = r.kind == 'macro' ? `(${r.params.join(', ')})` : '';
  const out = [
    parts(
      o,
      [
        [`■ ${r.kind}`, COLOR.kind[r.kind] ?? COLOR.dim, ['kind']],
        [' '],
        [r.name + sig, 255, ['name']],
        ['  '],
        ['= ', COLOR.dim],
        [r.value, null, ['value'], highlightC(r.value, [], o.paint, o.width).join(' ')],
      ],
      { atoms: [], src: { file: p.file, line: p.line, column: p.column } },
    ),
  ];
  if(p.file) out.push(...location('declaration', p, o, { mark: false, obj: r }));
  return out;
}

function formatFunction(r, o) {
  const p = pos(r);
  o.gw = String(p.endLine ?? p.line).length;
  const out = [parts(o, [['■ function', COLOR.kind.function, ['kind']], [' '], [r.name, 255, ['name']], ['  '], [`${(p.endLine ?? p.line) - p.line + 1} lines`, COLOR.dim]], { atoms: [], src: { file: p.file, line: p.line, column: p.column } })];
  if(p.file) out.push(...location('declaration', p, o, { obj: r }));
  return out;
}

/** The active show() filter: `file`/`name` are RegExp or null, `kind` is a Set or null; set by the REPL's `\filter`. */
const filter = { file: null, kind: null, name: null };

const filterActive = () => !!(filter.file || filter.kind || filter.name);

/** True if record `r` passes every set part of the filter (file: its declaration's file, kind: its kind, name: its name). */
function passes(r) {
  const p = pos(r.declaration ?? r);
  return (!filter.file || filter.file.test(p?.file ?? '')) && (!filter.kind || filter.kind.has(r.declaration?.kind ?? r.kind ?? r.type)) && (!filter.name || filter.name.test(r.name ?? ''));
}

const filterText = () => [filter.file && `file=${filter.file.source}`, filter.kind && `kind=${[...filter.kind].join(',')}`, filter.name && `name=${filter.name.source}`].filter(Boolean).join(' ');

/**
 * formatRows: lays out extract-c records as colored (xterm-256color), annotated text, and keeps
 * what a click on each row means. Source text is looked up by each record's file and line/column
 * range.
 *
 * ```js
 * const { text, rows } = formatRows(ids.get('main'), { color: false });
 * ```
 *
 *   object|array  value         identifier / type / function / define / macro record(s)
 *   boolean       opts.color    default: stdout is a terminal
 *   number        opts.width    columns, default: terminal width
 *   number        opts.lines    source lines per location, default 12
 *   number        opts.refs     references per identifier, default 20
 *   number        opts.items    array items, default 20
 *   function      opts.read     file => lines[] | null, default: reads the file
 *
 *   returns  { text, rows, cols }: text without trailing newline, one tagged row per line
 *   throws   TypeError if `value` is not showable
 *
 * records not passing `show.filter` are left out (see the REPL's `\filter`).
 */
export function formatRows(value, opts = {}) {
  if(!showable(value)) throw new TypeError('format(): not an extract-c record');

  const color = opts.color ?? isatty(1);
  let cols = 100;
  try {
    cols = ttyGetWinSize?.(1)?.[0] || cols;
  } catch(e) {}
  cols = opts.width ?? cols;

  const o = {
    paint: color ? (code, s) => (code == null ? s : code == 'mark' ? `${MARK}${s}\x1b[0m` : `\x1b[38;5;${code}m${s}\x1b[0m`) : (code, s) => s,
    width: Math.max(40, cols - 8),
    lines: opts.lines ?? 12,
    refs: opts.refs ?? 20,
    read: opts.read ?? readLines,
    ids: show.ids,
    gw: 1,
  };
  const all = Array.isArray(value) ? value : [value];
  const entries = all.map((r, i) => [r, i]).filter(([r]) => !filterActive() || passes(r));
  const rows = [];

  if(filterActive()) rows.push(o.paint(COLOR.dim, `filter ${filterText()}  (${entries.length} of ${all.length})`));
  if(!entries.length) return { text: String(rows[0]), rows, cols };

  const shownItems = entries.slice(0, opts.items ?? 20);
  for(const [n, [r, idx]] of shownItems.entries()) {
    if(n) rows.push(rule(o));
    const part = r.declaration && r.prototype ? formatIdentifier(r, o) : r.kind == 'define' || r.kind == 'macro' ? formatDefine(r, o) : r.kind == 'function' ? formatFunction(r, o) : formatType(r, o);
    for (const row of part) rows.push(Object.assign(typeof row == 'string' ? tag(row, {}) : row, { item: idx }));
  }
  if(entries.length > shownItems.length) rows.push(o.paint(COLOR.dim, `… ${entries.length - shownItems.length} more`));

  return { text: rows.join('\n'), rows, cols };
}

/** format: `formatRows(value, opts).text`. */
export function format(value, opts) {
  return formatRows(value, opts).text;
}

/**
 * The part of a show() row under terminal column `x`: `{ atoms, item, root, src }` where `atoms`
 * are pointer atoms relative to the record, `src` is `{ file, line, column }` (for source text the
 * column of the character clicked), or null for a row that points nowhere.
 */
export function locate(row, x) {
  if(!row || (!row.atoms && !row.cells?.length && !row.code)) return null;

  const cell = row.cells?.find(c => x >= c.x0 && x <= c.x1);
  let src = row.src && { ...row.src };
  if(row.code) {
    const c = row.code;
    src = { file: c.file, line: c.line, column: x < c.x0 ? 1 : srcColAt(c.text, c.from, c.lead, x - c.x0) };
  }

  return { atoms: cell?.atoms ?? row.atoms ?? [], item: row.item, root: row.root, src };
}

/**
 * The show() row under screen cell (x, y), when the output ends just above screen row `promptRow`
 * on a terminal `cols` wide (long rows wrap onto several screen rows). Returns `{ row, x }` with
 * `x` unwrapped, or null.
 */
export function rowAt(rows, cols, promptRow, x, y) {
  const heights = rows.map(r => Math.max(1, Math.ceil(visibleLength(r) / cols)));
  let top = promptRow - heights.reduce((a, b) => a + b, 0);

  for(const [i, h] of heights.entries()) {
    if(y >= top && y < top + h) return { row: rows[i], x: x + (y - top) * cols };
    top += h;
  }

  return null;
}

/** A JSON pointer string in `.prop[0].value` form for `atoms`; '' for the root. */
export function pointerString(atoms) {
  const s = String(Pointer.of(...atoms));
  return !s || s[0] == '[' ? s : '.' + s;
}

/**
 * JS code that reaches the clicked part from globalThis: `globalThis.records[2].references[3]`.
 * `roots` is { records, dead, ids }; a value found in none of them is reached as `globalThis.shown`.
 */
export function codeFor(shown, hit, roots) {
  let base;
  const named = (list, name, i) => (Array.isArray(list) && list[i] === shown ? name : null);

  if(hit.root) base = hit.root;
  else if(Array.isArray(shown)) base = (roots.records === shown ? 'records' : roots.dead === shown ? 'dead' : 'shown') + `[${hit.item}]`;
  else {
    const i = roots.records?.indexOf(shown) ?? -1,
      j = roots.dead?.indexOf(shown) ?? -1;
    base = i >= 0 ? `records[${i}]` : j >= 0 ? `dead[${j}]` : roots.ids?.get(shown.name) === shown ? `ids.get(${JSON.stringify(shown.name)})` : 'shown';
  }

  return `globalThis.${base}${pointerString(hit.atoms)}`;
}

/**
 * show: prints `format(value, opts)` to stdout; returns a marker the REPL does not echo. `show.ids`
 * (set by `--repl`) resolves reference lists of `-c` records; `show.last` keeps the rows of the
 * latest output for clicks.
 */
export function show(value, opts) {
  const { text, rows, cols } = formatRows(value, opts);
  std.out.puts(text + '\n');
  show.last = { value, rows, cols };
  return SHOWN;
}
show.ids = null;
show.last = null;
show.filter = filter;

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

  Object.defineProperty(out, SRC, { value: { file: f, line, column, offset, end, endLine, endColumn } });
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
 * The REPL's `\filter` directive: `\filter file REGEX kind K[,K] name REGEX`, any subset;
 * `\filter clear [file|kind|name...]` unsets; no argument shows the filter. Returns the message to print.
 */
function setFilter(args) {
  const keys = ['file', 'kind', 'name'];

  try {
    if(args[0] == 'clear') for(const k of args.length > 1 ? args.slice(1) : keys) filter[k] = null;
    else {
      if(args.length % 2 || args.some((a, i) => !(i % 2) && !keys.includes(a))) return `usage: \\filter [file REGEX] [kind KIND[,KIND]] [name REGEX] | clear [file|kind|name]...\n`;
      for(let i = 0; i < args.length; i += 2) filter[args[i]] = args[i] == 'kind' ? new Set(args[i + 1].split(',')) : new RegExp(args[i + 1]);
    }
  } catch(e) {
    return `${e.message}\n`;
  }

  return (filterActive() ? `show() filter: ${filterText()}` : 'show() filter: none') + '\n';
}

/* ---- call graph: callees/callers/reachable/callTree and the browse() TUI ---- */

const callIndexCache = new WeakMap();

/** Map caller -> [{ callee, file, line, column }]: every reference made from inside a function, from `ids`. */
function callIndex(ids) {
  if(!callIndexCache.has(ids)) {
    const index = new Map();
    for(const e of ids.values())
      for(const r of e.references) {
        if(!r.in) continue;
        if(!index.has(r.in)) index.set(r.in, []);
        index.get(r.in).push({ callee: e.name, file: r.file, line: r.line, column: r.column });
      }
    callIndexCache.set(ids, index);
  }
  return callIndexCache.get(ids);
}

const isDefined = (ids, name) => ids.get(name)?.declaration?.kind == 'function';

/** Functions defined in `ids` that `name` refers to, as call sites `[{ callee, file, line, column }]`. */
export function callees(ids, name) {
  return (callIndex(ids).get(name) ?? []).filter(s => isDefined(ids, s.callee));
}

/** Functions whose body refers to `name`, as `[{ caller, file, line, column }]`. */
export function callers(ids, name) {
  return (ids.get(name)?.references ?? []).filter(r => r.in).map(r => ({ caller: r.in, file: r.file, line: r.line, column: r.column }));
}

/** Names of the functions reachable from `name` through calls, `name` itself included; `up` follows callers instead. */
export function reachable(ids, name, up = false) {
  const seen = new Set([name]),
    todo = [name];

  while(todo.length)
    for(const n of (up ? callers(ids, todo.pop()).map(s => s.caller) : callees(ids, todo.pop()).map(s => s.callee)))
      if(!seen.has(n)) {
        seen.add(n);
        todo.push(n);
      }

  return seen;
}

/**
 * callTree: the call graph below `name` as indented text; `↻` marks a function already on the path.
 *
 * ```js
 * callTree(ids, 'main', { depth: 2 })
 * // main
 * // ├─ parse
 * // │  └─ lex
 * // └─ run
 * ```
 *
 *   Map     ids          the identifier table
 *   string  name         root function
 *   number  opts.depth   levels to descend, default 3
 *   boolean opts.up      walk callers instead of callees
 *   boolean opts.color   default: stdout is a terminal
 */
export function callTree(ids, name, opts = {}) {
  const depth = opts.depth ?? 3;
  const paint = (opts.color ?? isatty(1)) ? (code, t) => `\x1b[38;5;${code}m${t}\x1b[0m` : (code, t) => t;
  const out = [paint(COLOR.kind.function, name)];

  const walk = (n, prefix, path) => {
    const next = [...new Set((opts.up ? callers(ids, n).map(s => s.caller) : callees(ids, n).map(s => s.callee)))];
    for(const [i, c] of next.entries()) {
      const last = i == next.length - 1;
      const cycle = path.includes(c);
      out.push(prefix + paint(COLOR.gutter, last ? '└─ ' : '├─ ') + paint(isDefined(ids, c) ? COLOR.kind.function : COLOR.dim, c) + (cycle ? paint(COLOR.warn, ' ↻') : ''));
      if(!cycle && path.length < depth) walk(c, prefix + paint(COLOR.gutter, last ? '   ' : '│  '), [...path, c]);
    }
  };

  walk(name, '', [name]);
  return out.join('\n');
}

const fileInfoCache = new Map();

/** `{ lines, funcs, lineOf(offset) }` of a source file, or null if unreadable. */
function fileInfo(read, file) {
  const key = file;
  if(!fileInfoCache.has(key)) {
    const lines = read(file);
    let info = null;
    if(lines) {
      const text = lines.join('\n'),
        starts = [];
      let at = 0;
      for(const l of lines) {
        starts.push(at);
        at += l.length + 1;
      }
      info = {
        lines,
        funcs: findFunctions(text, file),
        lineOf: off => Math.max(1, starts.findLastIndex(s => s <= off) + 1),
      };
    }
    fileInfoCache.set(key, info);
  }
  return fileInfoCache.get(key);
}

/** The browser's view of one function: its source lines and the call sites inside them that can be descended into. */
function openFrame(b, name) {
  const d = b.ids.get(name)?.declaration;
  const info = d?.kind == 'function' && d.file ? fileInfo(b.read, d.file) : null;
  if(!info) return null;

  const f = info.funcs.find(f => f.name == name && f.line == d.line) ?? info.funcs.find(f => f.name == name);
  const from = f ? info.lineOf(f.start) : d.line;
  const to = f ? info.lineOf(f.end - 1) : Math.min(info.lines.length, d.line + 30);
  const lines = info.lines.slice(from - 1, to);

  const sites = callees(b.ids, name)
    .filter(s => s.file == d.file && s.line >= from && s.line <= to && /^\s*\(/.test(lines[s.line - from].slice(s.column - 1 + s.callee.length)))
    .sort((a, b) => a.line - b.line || a.column - b.column);

  return { name, file: d.file, from, to, lines, sites, top: 0, sel: sites.length ? 0 : -1 };
}

/**
 * A call-graph browser state on `ids`, starting in `name`; null if `name` has no body in the scanned
 * files. Drive it with `browserEvent()`, draw it with `renderBrowser()`.
 */
export function makeBrowser(ids, name, read = readLines) {
  const b = { ids, read, stack: [], status: '', layout: null, done: false };
  const frame = openFrame(b, name);
  if(!frame) return null;
  b.stack.push(frame);
  return b;
}

const SITE_STYLE = { call: '\x1b[4;1;38;5;81m', sel: '\x1b[1;38;5;16;48;5;220m' };
const expandedCol = (line, col) => col + (line.slice(0, col - 1).match(/\t/g)?.length ?? 0);

/**
 * Lays the browser out on a `cols` x `rows` screen: returns `{ lines, crumbs, sites, bodyH }` where
 * `lines` are the screen rows (with escapes), `crumbs` the clickable breadcrumb ranges
 * `[{ x0, x1, level }]` and `sites` the clickable call sites `[{ y, x0, x1, index }]` (1-based screen
 * coordinates). Draws with color unless `color` is false.
 */
export function renderBrowser(b, cols, rows, color = true) {
  const fr = b.stack.at(-1);
  const paint = color
    ? (code, t) => (code == null ? t : SITE_STYLE[code] ? `${SITE_STYLE[code]}${t}\x1b[0m` : code == 'mark' ? `${MARK}${t}\x1b[0m` : `\x1b[38;5;${code}m${t}\x1b[0m`)
    : (code, t) => t;
  const bodyH = Math.max(1, rows - 4);
  const gw = String(fr.to).length;
  const out = [];

  /* breadcrumb */
  const crumbs = [];
  let x = 1,
    crumb = '';
  for(const [level, f] of b.stack.entries()) {
    if(level) {
      crumb += paint(COLOR.gutter, ' › ');
      x += 3;
    }
    crumbs.push({ x0: x, x1: x + f.name.length - 1, level });
    crumb += paint(level == b.stack.length - 1 ? 255 : COLOR.kind.function, f.name);
    x += f.name.length;
  }
  out.push(crumb);

  out.push(`${paint(COLOR.kind.function, '■ function')} ${paint(255, fr.name)}  ${where({ file: fr.file, line: fr.from, column: 1 }, { paint })}${paint(COLOR.dim, `–${fr.to}  ${fr.sites.length} call${fr.sites.length == 1 ? '' : 's'}`)}`);

  /* body */
  const win = fr.lines.slice(fr.top, fr.top + bodyH);
  const marks = [];
  const sites = [];
  for(const [i, s] of fr.sites.entries()) {
    const li = s.line - fr.from - fr.top;
    if(li < 0 || li >= win.length) continue;
    marks.push([li + 1, s.column, i == fr.sel ? 'sel' : 'call']);
    const x0 = gw + 3 + expandedCol(win[li], s.column);
    sites.push({ y: 3 + li, x0, x1: x0 + s.callee.length - 1, index: i });
  }
  const rendered = highlightC(win.join('\n'), marks, paint, Math.max(10, cols - gw - 4));
  for(let i = 0; i < bodyH; i++) out.push(i < win.length ? `${paint(COLOR.dim, String(fr.from + fr.top + i).padStart(gw))} ${paint(COLOR.gutter, '│')} ${rendered[i] ?? ''}` : paint(COLOR.gutter, '~'));

  /* callers, status */
  const names = [...new Set(callers(b.ids, fr.name).map(s => s.caller))];
  out.push(paint(COLOR.dim, `called by: ${names.length ? names.slice(0, 6).join(', ') + (names.length > 6 ? ` (+${names.length - 6})` : '') : '-'}`));
  out.push(b.status ? paint(COLOR.warn, b.status) : paint(COLOR.dim, 'click/Enter descend · Tab/←→ next call · right-click/Backspace/Esc back · wheel/↑↓ scroll · q quit'));

  return { lines: out, crumbs, sites, bodyH };
}

const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

/** Scrolls frame `fr` so that its selected call site is on screen. */
function reveal(fr, bodyH) {
  const s = fr.sites[fr.sel];
  if(!s) return;
  const li = s.line - fr.from;
  if(li < fr.top || li >= fr.top + bodyH) fr.top = clamp(li - (bodyH >> 1), 0, Math.max(0, fr.lines.length - bodyH));
}

/** Descends into the function of call site `index` of the top frame; sets `b.status` if it cannot. */
function descend(b, index) {
  const fr = b.stack.at(-1);
  const site = fr.sites[index];
  if(!site) return;
  fr.sel = index;
  const next = openFrame(b, site.callee);
  if(next) {
    b.stack.push(next);
    b.status = '';
  } else b.status = `${site.callee}: no body in the scanned files`;
}

/**
 * Applies one input event (from `parseInput()`) to browser `b`, laid out as by `renderBrowser()` (`lay`).
 * Sets `b.done` when the user quits. Mouse: left click on a call site descends, on a breadcrumb jumps
 * back to that level, right click goes back, the wheel scrolls.
 */
export function browserEvent(b, ev, lay) {
  const fr = b.stack.at(-1);
  const maxTop = Math.max(0, fr.lines.length - lay.bodyH);
  const back = () => (b.stack.length > 1 ? b.stack.pop() : (b.done = true));
  b.status = '';

  if(ev.type == 'mouse') {
    if(!ev.press) return;
    if(ev.button == 'wheeldown') fr.top = clamp(fr.top + 3, 0, maxTop);
    else if(ev.button == 'wheelup') fr.top = clamp(fr.top - 3, 0, maxTop);
    else if(ev.button == 'right') back();
    else if(ev.button == 'left') {
      const c = ev.y == 1 && lay.crumbs.find(c => ev.x >= c.x0 && ev.x <= c.x1);
      const s = lay.sites.find(s => s.y == ev.y && ev.x >= s.x0 && ev.x <= s.x1);
      if(c) b.stack.length = c.level + 1;
      else if(s) descend(b, s.index);
    }
    return;
  }

  switch (ev.key) {
    case 'q':
    case 'ctrl-c':
    case 'ctrl-d':
      b.done = true;
      break;
    case 'esc':
    case 'backspace':
    case 'h':
    case 'u':
      back();
      break;
    case 'enter':
    case ' ':
    case 'l':
      descend(b, fr.sel);
      break;
    case 'tab':
    case 'right':
    case 'n':
    case 'left':
    case 'shifttab':
    case 'p':
      if(fr.sites.length) {
        const dir = ['left', 'shifttab', 'p'].includes(ev.key) ? -1 : 1;
        fr.sel = (fr.sel + dir + fr.sites.length) % fr.sites.length;
        reveal(fr, lay.bodyH);
      }
      break;
    case 'down':
      fr.top = clamp(fr.top + 1, 0, maxTop);
      break;
    case 'up':
      fr.top = clamp(fr.top - 1, 0, maxTop);
      break;
    case 'pgdn':
      fr.top = clamp(fr.top + lay.bodyH - 1, 0, maxTop);
      break;
    case 'pgup':
      fr.top = clamp(fr.top - lay.bodyH + 1, 0, maxTop);
      break;
    case 'home':
      fr.top = 0;
      break;
    case 'end':
      fr.top = maxTop;
      break;
  }
}

const KEYS = { '\x1b[A': 'up', '\x1b[B': 'down', '\x1b[C': 'right', '\x1b[D': 'left', '\x1bOA': 'up', '\x1bOB': 'down', '\x1bOC': 'right', '\x1bOD': 'left', '\x1b[5~': 'pgup', '\x1b[6~': 'pgdn', '\x1b[H': 'home', '\x1b[F': 'end', '\x1b[1~': 'home', '\x1b[4~': 'end', '\x1b[Z': 'shifttab', '\x1b[3~': 'delete' };
const BUTTONS = { 0: 'left', 1: 'middle', 2: 'right', 64: 'wheelup', 65: 'wheeldown' };

/**
 * Splits terminal input into events: keys `{ type: 'key', key }` (up, down, left, right, pgup, pgdn,
 * home, end, tab, shifttab, enter, backspace, esc, ctrl-c, ctrl-d, or the character) and SGR mouse
 * reports `{ type: 'mouse', button, x, y, press }`.
 *
 * ```js
 * parseInput('\x1b[<0;12;5M');  // [{ type: 'mouse', button: 'left', x: 12, y: 5, press: true }]
 * ```
 */
export function parseInput(s) {
  const events = [];

  for(let i = 0; i < s.length; ) {
    const mouse = /^\x1b\[<(\d+);(\d+);(\d+)([Mm])/.exec(s.slice(i));
    if(mouse) {
      events.push({ type: 'mouse', button: BUTTONS[+mouse[1] & ~28] ?? 'other', x: +mouse[2], y: +mouse[3], press: mouse[4] == 'M' });
      i += mouse[0].length;
      continue;
    }

    const seq = /^\x1b(?:\[[0-9;]*[~A-Za-z]|O[A-D])/.exec(s.slice(i));
    if(seq) {
      events.push({ type: 'key', key: KEYS[seq[0]] ?? 'other' });
      i += seq[0].length;
      continue;
    }

    const c = s[i++];
    events.push({ type: 'key', key: c == '\x1b' ? 'esc' : c == '\r' || c == '\n' ? 'enter' : c == '\x7f' || c == '\b' ? 'backspace' : c == '\t' ? 'tab' : c == '\x03' ? 'ctrl-c' : c == '\x04' ? 'ctrl-d' : c });
  }

  return events;
}

/**
 * browse: full-screen call-graph browser (alternate screen, mouse); blocks until quit.
 *
 * ```js
 * browse(ids, 'main');   // click a highlighted call to descend into it
 * ```
 *
 *   Map     ids   the identifier table
 *   string  name  function to start in
 *
 *   returns  true if the browser ran, false if `name` has no body in the scanned files
 */
export function browse(ids, name) {
  const b = makeBrowser(ids, name);
  if(!b) return false;

  const buf = new Uint8Array(256);
  const out = s => {
    std.out.puts(s);
    std.out.flush();
  };

  /* alternate screen, hidden cursor, SGR mouse reports */
  out('\x1b[?1049h\x1b[?25l\x1b[?1000h\x1b[?1006h');
  try {
    while(!b.done) {
      let size = null;
      try {
        size = ttyGetWinSize(1);
      } catch(e) {}
      const [cols, rows] = size ?? [80, 24];
      const lay = renderBrowser(b, cols, rows);
      b.layout = lay;
      out('\x1b[H' + lay.lines.map(l => l + '\x1b[K').join('\r\n') + '\x1b[J');

      const n = read(0, buf.buffer, 0, buf.length);
      if(n <= 0) break;
      for(const ev of parseInput(String.fromCharCode(...buf.slice(0, n)))) browserEvent(b, ev, lay);
    }
  } finally {
    out('\x1b[?1006l\x1b[?1000l\x1b[?25h\x1b[?1049l');
  }

  return true;
}

/* ---- clicks on show() output in the REPL ---- */

/**
 * Splits mouse reports and cursor-position replies out of the byte stream the REPL reads.
 * Returns a function taking one byte and returning the bytes to pass on (none while a sequence
 * may still be completing); `onMouse({ button, x, y, press })` and `onCursor(row, col)` fire for
 * `ESC [ < b ; x ; y M|m` and `ESC [ row ; col R`.
 */
export function makeInputFilter(onMouse, onCursor) {
  let held = '';

  return byte => {
    const c = String.fromCharCode(byte);
    if(!held && c != '\x1b') return [byte];

    held += c;

    let m;
    if((m = /^\x1b\[<(\d+);(\d+);(\d+)([Mm])$/.exec(held))) {
      held = '';
      onMouse({ button: BUTTONS[+m[1] & ~28] ?? 'other', x: +m[2], y: +m[3], press: m[4] == 'M' });
      return [];
    }
    if((m = /^\x1b\[(\d+);(\d+)R$/.exec(held))) {
      held = '';
      onCursor(+m[1], +m[2]);
      return [];
    }
    if(held == '\x1b' || /^\x1b\[<?[\d;]*$/.test(held)) return [];

    const bytes = [...held].map(ch => ch.charCodeAt(0));
    held = '';
    return bytes;
  };
}

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

/** Base64 of the UTF-8 bytes of `s`. */
export function base64(s) {
  const bytes = [...unescape(encodeURIComponent(s))].map(c => c.charCodeAt(0));
  let out = '';
  for(let i = 0; i < bytes.length; i += 3) {
    const n = (bytes[i] << 16) | ((bytes[i + 1] ?? 0) << 8) | (bytes[i + 2] ?? 0);
    out += B64[(n >> 18) & 63] + B64[(n >> 12) & 63] + (i + 1 < bytes.length ? B64[(n >> 6) & 63] : '=') + (i + 2 < bytes.length ? B64[n & 63] : '=');
  }
  return out;
}

const findExe = name => (process.env.PATH ?? '').split(':').some(d => d && fs.existsSync(`${d}/${name}`));
const absolute = file => (file.startsWith('/') ? file : path.resolve(file));

/** Writes `text` to the stdin of shell command `cmd`; returns true if it exited with 0. */
function pipeTo(cmd, text) {
  const f = std.popen(cmd, 'w');
  if(!f) return false;
  f.puts(text);
  return f.close() == 0;
}

/**
 * Copies `text` to the clipboard with shell command `cmd` (an `xclip` command is also run for the
 * `clipboard` selection); falls back to the OSC 52 escape. Returns how it was copied.
 */
function copyText(cmd, text) {
  if(cmd && pipeTo(cmd, text)) {
    if(/xclip/.test(cmd) && !/-selection|-sel\b/.test(cmd)) pipeTo(`${cmd} -selection clipboard`, text);
    return cmd.replace(/^\w+=\S+\s+/, '').split(/\s+/)[0];
  }

  std.out.puts(`\x1b]52;c;${base64(text)}\x07`);
  std.out.flush();
  return 'OSC 52';
}

/** The default editor command template: sublime if it is on PATH (`subl file:line:col` jumps to the location). */
const defaultEditor = () => process.env.EXTRACT_C_EDITOR ?? (findExe('subl') ? 'subl {loc}' : findExe('sublime_text') ? 'sublime_text {loc}' : null);

/** Starts the editor template `tpl` ({loc} {file} {line} {column}) on `src` without waiting; returns an error text or null. */
function openInEditor(tpl, src) {
  if(!tpl) return 'no editor found; set one with \\editor CMD {loc}';

  const file = absolute(src.file);
  const args = tpl.split(/\s+/).filter(Boolean).map(a => a.replace(/\{loc\}/g, `${file}:${src.line}:${src.column}`).replace(/\{file\}/g, file).replace(/\{line\}/g, src.line).replace(/\{column\}/g, src.column));

  try {
    return os.exec(args, { block: false, usePath: true }) > 0 ? null : `${args[0]}: failed to start`;
  } catch(e) {
    return `${args[0]}: ${e.message}`;
  }
}

/**
 * Mouse support for the REPL on the output of show(): a click works out what is under the cursor
 * (pointer, source location), shows it in a status row at the bottom of the screen, inserts the
 * code reaching it into the prompt and copies the location to the clipboard; a right click opens
 * it in the editor.
 *
 * Returns `{ on, off, suspend, resume, state }`; `roots()` gives { records, dead, ids }.
 */
function installMouse(repl, roots) {
  const state = { on: false, pending: null, timer: null, inserted: '', editor: defaultEditor(), clip: process.env.EXTRACT_C_CLIP ?? 'DISPLAY=:0 xclip -in' };
  const out = s => {
    std.out.puts(s);
    std.out.flush();
  };
  const size = () => {
    try {
      return ttyGetWinSize(1);
    } catch(e) {
      return null;
    }
  };

  const status = text => {
    const sz = size();
    if(!sz || !state.on) return;
    const line = text.slice(0, sz[0] - 1).padEnd(sz[0] - 1);
    out(`\x1b7\x1b[${sz[1]};1H\x1b[0;48;5;236;38;5;252m${line}\x1b[0m\x1b8`);
  };

  /* the bottom row is kept out of the scroll region, for the status bar */
  const reserve = scroll => {
    const sz = size();
    if(sz) out(`${scroll ? '\n\x1b[A' : ''}\x1b7\x1b[1;${sz[1] - 1}r\x1b8`);
  };
  const release = () => {
    const sz = size();
    if(sz) out(`\x1b[r\x1b7\x1b[${sz[1]};1H\x1b[2K\x1b8`);
  };

  const finish = (ev, cursorRow) => {
    const shown = show.last;
    const sz = size();
    const cols = sz?.[0] ?? shown.cols;
    if(cols != shown.cols) return status('terminal was resized: run show() again');

    const promptLen = visibleLength(repl.prompt ?? '');
    const promptRow = cursorRow - Math.floor((promptLen + repl.cursorPos) / cols);
    const hit = rowAt(shown.rows, cols, promptRow, ev.x, ev.y);
    const loc = hit && locate(hit.row, hit.x);
    if(!loc) return status('nothing to point at here');

    globalThis.shown = shown.value;
    const code = codeFor(shown.value, loc, roots());
    const ptr = pointerString(loc.atoms) || '(record)';
    const where = loc.src?.file ? `${loc.src.file}:${loc.src.line}:${loc.src.column}` : null;
    let note = '';

    if(ev.button == 'right') {
      note = where ? openInEditor(state.editor, loc.src) ?? `opened in ${state.editor.split(/\s+/)[0]}` : 'no source location';
    } else {
      if(where) note = `copied via ${copyText(state.clip, `${absolute(loc.src.file)}:${loc.src.line}:${loc.src.column}`)}`;

      /* a second click replaces the expression the first one inserted */
      if(state.inserted && repl.cmd.endsWith(state.inserted) && repl.cursorPos == repl.cmd.length) {
        repl.cmd = repl.cmd.slice(0, repl.cmd.length - state.inserted.length);
        repl.cursorPos = repl.cmd.length;
      }
      repl.insert(code);
      state.inserted = code;
      repl.update();
    }

    status(` ${ptr}   ${where ?? ''}   ${code}${note ? '   [' + note + ']' : ''}`);
  };

  const filterInput = makeInputFilter(
    ev => {
      if(!ev.press || (ev.button != 'left' && ev.button != 'right')) return;
      if(!show.last) return status('no show() output to click');

      state.pending = ev;
      os.clearTimeout(state.timer);
      state.timer = os.setTimeout(() => {
        if(state.pending) {
          state.pending = null;
          status('the terminal did not report the cursor position');
        }
      }, 500);
      out('\x1b[6n');
    },
    (row, col) => {
      const ev = state.pending;
      state.pending = null;
      if(ev) finish(ev, row);
    },
  );

  const handleByte = repl.handleByte.bind(repl);
  repl.handleByte = byte => {
    for(const b of filterInput(byte)) handleByte(b);
  };

  /* a command being evaluated scrolls the earlier output away */
  const evalStart = repl.evalAndPrintStart.bind(repl);
  repl.evalAndPrintStart = (...args) => {
    show.last = null;
    state.inserted = '';
    return evalStart(...args);
  };

  const modes = on => out(on ? '\x1b[?1000h\x1b[?1006h' : '\x1b[?1006l\x1b[?1000l');

  return {
    state,
    on() {
      if(state.on) return;
      state.on = true;
      modes(true);
      reserve(true);
    },
    off() {
      if(!state.on) return;
      modes(false);
      release();
      state.on = false;
    },
    suspend() {
      if(!state.on) return;
      modes(false);
      out('\x1b[r');
    },
    resume() {
      if(!state.on) return;
      modes(true);
      reserve(false);
    },
  };
}

/** The text of the REPL's `\help` directive: each global with what it holds right now, then examples. */
function helpText(vars, color) {
  const paint = color ? (code, t) => (code == null ? t : `\x1b[38;5;${code}m${t}\x1b[0m`) : (code, t) => t;
  const count = (v, noun) => (v == null ? 'not set' : `${v.length ?? v.size} ${noun}`);
  const rows = {
    data: [
      ['files', count(vars.files, 'files'), 'the input files, as given (directories expanded)'],
      ['ids', count(vars.ids, 'identifiers'), 'Map name -> { name, declaration, prototype[], references[] }; every position has file, line, column, offset, end, endLine, endColumn and, in a function, `in`'],
      ['records', count(vars.records, 'records'), 'the JSON IR that gets written; reassign it to change the output'],
      ['chunks', count(vars.chunks, 'chunks'), 'the text output (-l lines, function sources); reassign to change it'],
      ['dead', vars.dead ? count(vars.dead, 'identifiers') : 'null (needs -d)', 'identifiers no walk from the entry points reaches'],
      ['repl', 'the REPL', '`repl._` is the last result'],
    ],
    'call graph': [
      ['callees(name)', '', 'call sites in name\'s body that reach defined functions: [{ callee, file, line, column }]'],
      ['callers(name)', '', 'functions whose body refers to name: [{ caller, file, line, column }]'],
      ['reachable(name, up?)', '', 'Set of names reachable through calls (up: through callers)'],
      ['callTree(name, { depth, up })', '', 'indented tree text; ↻ marks recursion; print it with console.log'],
      ['browse(name)', '', 'full-screen browser: click a highlighted call to descend, right-click back'],
    ],
    functions: [
      ['show(value, opts)', '', 'print records colored, with their source; value is a record, an `ids` entry or an array of them; opts: color, width, lines, refs, items'],
      ['format(value, opts)', '', 'the same as a string'],
      ['findFunctions(src, file)', '', '[{ name, line, column, start, end }] of the function definitions'],
      ['findTypes(src, file)', '', 'struct/union/class/enum/typedef records with field offsets and sizes (LP64)'],
      ['findDefines(src, file)', '', '#define records: kind "define" or "macro", params, value'],
      ['findIdentifiers(src, file, ids?, pre?)', '', 'adds every identifier of src to ids (a new Map by default); pre is the preprocessed text'],
    ],
  };
  const examples = [
    `show(ids.get('main'))`,
    `records = records.filter(r => r.references === 0)`,
    `[...ids.values()].filter(e => e.declaration?.kind == 'function' && !e.references.length)`,
    `show(dead.slice(0, 5))`,
    `\\filter kind function name ^js_ file src/   // then: show(records)`,
  ];
  const out = [];

  for(const [title, list] of Object.entries(rows)) {
    out.push(paint(COLOR.label.declaration, title));
    for(const [name, now, what] of list) out.push(`  ${paint(COLOR.code.keyword, name.padEnd(38))} ${now ? paint(COLOR.pos, now) + '  ' : ''}${paint(COLOR.dim, what)}`);
  }

  out.push(paint(COLOR.label.declaration, 'directives'));
  for(const [name, what] of [['\\help', 'this text'], ['\\banner', 'the startup banner again'], ['\\browse [function]', 'the call-graph browser (default main)'], ['\\mouse [on|off]', 'clicks on show() output: left = pointer + location to status row, prompt and clipboard; right = open in editor'], ['\\editor CMD {loc}', 'editor template ({loc} {file} {line} {column}); default subl/sublime_text'], ['\\clip CMD', 'clipboard command (default DISPLAY=:0 xclip -in)'], ['\\filter [file RE] [kind K,..] [name RE]', 'show() only records matching all given parts; `\\filter clear` resets, `\\filter` shows it']]) out.push(`  ${paint(COLOR.code.keyword, name.padEnd(38))} ${paint(COLOR.dim, what)}`);
  out.push(paint(COLOR.label.declaration, 'examples'), ...examples.map(e => '  ' + highlightC(e, [], paint, 200).join('')));
  return out.join('\n') + '\n';
}

/**
 * Opens a REPL on `vars`, set as properties of globalThis, and resolves when it is left
 * (Ctrl-D, `\\q`); the caller reads the properties back, so reassigning them there changes
 * what is written.
 */
async function openRepl(vars) {
  const { REPL } = await import('repl');

  Object.assign(globalThis, vars);
  show.ids = vars.ids;
  const banner = () => std.err.puts(`extract-c.js: REPL, globalThis has ${Object.keys(vars).join(', ')}; Ctrl-D writes the output (\\help for details)\n`);
  banner();

  const repl = (globalThis.repl = new REPL('extract-c ', false));

  const printEvalResult = repl.printEvalResult;

  /* records print with show(), everything else as usual */
  repl.printEvalResult = function(result) {
    if(result !== SHOWN && !showable(result)) return printEvalResult.call(this, result);

    this.evalTime = Date.now() - this.evalStartTime;
    if(result !== SHOWN) show(result);
    this._ = result;
    this.handleCmdEnd();
  };

  const mouse = installMouse(repl, () => ({ records: globalThis.records, dead: globalThis.dead, ids: vars.ids }));
  const say = text => std.out.puts(text + '\n');

  repl.directives.mouse = [arg => (arg == 'off' ? mouse.off() : mouse.on()), 'clicks on show() output: \\mouse [on|off]'];
  repl.directives.editor = [(...cmd) => (cmd.length ? (mouse.state.editor = cmd.join(' ')) : say(`editor: ${mouse.state.editor ?? 'none'}`)), 'right click opens the location: \\editor CMD {loc}'];
  repl.directives.clip = [(...cmd) => (cmd.length ? (mouse.state.clip = cmd.join(' ')) : say(`clipboard: ${mouse.state.clip}`)), 'left click copies the location: \\clip CMD'];
  repl.directives.browse = [name => {
    const start = name ?? (isDefined(vars.ids, 'main') ? 'main' : null);
    mouse.suspend();
    try {
      if(!start || !browse(vars.ids, start)) say(`browse: ${start ?? 'no main'}: no function body in the scanned files; usage: \\browse NAME`);
    } finally {
      mouse.resume();
    }
  }, 'call-graph browser (mouse): \\browse [function]'];
  repl.directives.filter = [(...args) => std.out.puts(setFilter(args)), 'limit show() to file/kind/name (\\filter clear to reset)'];
  repl.directives.banner = [banner, 'show the startup banner again'];
  repl.directives.help = [() => std.out.puts(helpText(vars, isatty(1))), 'globals and functions of this session'];

  repl.exit = function() {
    for(const handler of this.cleanupHandlers) handler.call(this);
    this.running = false;
  };
  repl.loadSaveOptions();
  repl.historyLoad();

  mouse.on();
  try {
    await repl.run();
  } finally {
    mouse.off();
  }
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
                        chunks (the text output), dead (with -d), show()/format()
                        (records, printed colored with their source) and the find*
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
    await openRepl({ files, ids, records: irs, chunks, dead, findFunctions, findTypes, findDefines, findIdentifiers, show, format, callees: n => callees(ids, n), callers: n => callers(ids, n), reachable: (n, up) => reachable(ids, n, up), callTree: (n, o) => callTree(ids, n, o), browse: n => browse(ids, n) });
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
