#!/usr/bin/env qjsm
import * as fs from 'fs';
import * as path from 'path';
import { getOpt, isMainModule } from 'util';
import CLexer from 'lexer/c.js';

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

/** Yields the `;`-terminated statements at depth 0; function definitions yield their signature with `.fn = true`. */
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
        j = matching(ts, j);
        if(fn) {
          sig.fn = true;
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

const mk = (name, type, size, align, line, extra) => ({ name, type, size, align, line, methods: [], getters: [], setters: [], fields: [], prototypeChain: [], ...extra });

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
  const line = ts[k].loc.line;
  let desc;

  if(kind == 'enum') {
    desc = mk(tag, kind, 4, 4, line, { fields: splitTop(body, ',').map(p => ({ name: p[0].lexeme })) });
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
    desc = mk(tag, kind, l.size, l.align, line, { fields: l.fields, methods });
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
      out.push(mk(dd.name, 'typedef', m.size, m.align, s[0].loc.line, { target: m.type }));
    }
  }

  return out;
}

function main(...args) {
  let pattern, list, types, splitDir, output;

  const params = getOpt(
    {
      pattern: [true, v => (pattern = new RegExp(v)), 'p'],
      list: [false, () => (list = true), 'l'],
      types: [false, () => (types = true), 't'],
      split: [true, v => (splitDir = v), 's'],
      output: [true, v => (output = v), 'o'],
      help: [false, null, 'h'],
      '@': 'files',
    },
    args,
  );
  const files = params['@'];

  if(params.help || !files.length) {
    console.log(`Usage: c-extract.js [-p REGEXP] [-l] [-t] [-s DIR] [-o FILE] FILE...

  -p, --pattern REGEXP  only functions/types whose name matches (default: all)
  -l, --list            print "file:line: name" instead of the source / IR
  -t, --types           emit struct/union/class/enum/typedef/using as JSON IR
                        (describeObject()-shaped; fields carry byte offset and
                        size, assuming LP64) instead of functions
  -s, --split DIR       write each function to DIR/<name>.c, prefixed with the
                        text preceding the file's first function (#includes etc.)
  -o, --output FILE     write to FILE instead of stdout`);
    return params.help ? 0 : 1;
  }

  if(splitDir) fs.mkdirSync(splitDir, { recursive: true });

  const chunks = [],
    irs = [];

  for(const file of files) {
    const source = fs.readFileSync(file, 'utf8');

    if(types) {
      for(const d of findTypes(source, file).filter(d => !pattern || pattern.test(d.name ?? ''))) {
        if(list) chunks.push(`${file}:${d.line}: ${d.type} ${d.name}\n`);
        else irs.push(d);
      }
      continue;
    }

    const all = findFunctions(source, file);
    const funcs = pattern ? all.filter(f => pattern.test(f.name)) : all;
    const preamble = all.length ? source.slice(0, all[0].start).trimEnd() : '';

    for(const f of funcs) {
      const text = source.slice(f.start, f.end);

      if(splitDir) {
        const dest = path.join(splitDir, f.name + '.c');
        fs.writeFileSync(dest, (preamble ? preamble + '\n\n' : '') + text + '\n');
        console.log(`${file}:${f.line}: ${f.name} -> ${dest}`);
      } else if(list) chunks.push(`${file}:${f.line}: ${f.name}\n`);
      else chunks.push(text + '\n\n');
    }
  }

  if(irs.length) chunks.push(JSON.stringify(irs, null, 2));

  if(chunks.length) {
    const out = chunks.join('');
    if(output) fs.writeFileSync(output, out);
    else console.log(out.trimEnd());
  }

  return 0;
}

if(isMainModule(import.meta.url)) process.exit(main(...scriptArgs.slice(1)));
