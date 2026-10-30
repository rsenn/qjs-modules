/* extract-c-split.js: splitLib(), the engine behind extract-c.js --split-lib.
 * depends on: lex() and findFunctions() of extract-c.js, passed in by the caller.
 * rule: mechanical only, anything that needs a decision is reported, not guessed. */
import * as fs from 'fs';
import * as path from 'path';
import * as std from 'std';

const COMMENT = new Set(['singleLineComment', 'multiLineComment']);
const TYPE_KW = new Set(['typedef', 'struct', 'union', 'enum']);

/* libc call names per system header, a header not listed here is kept everywhere */
const SYS_NEEDS = {
  'stdlib.h': /^(malloc|calloc|realloc|free|abs|labs|exit|abort|atoi|atol|strtol|strtoul|getenv|qsort|bsearch|rand|srand|system)$/,
  'ctype.h': /^(is[a-z]+|toupper|tolower)$/,
  'string.h': /^(mem[a-z]+|str[a-z]+)$/,
  'stdio.h': /^(f?printf|sprintf|snprintf|puts|putchar|fputs|fopen|fclose|fgets|fflush|perror|sscanf|stdin|stdout|stderr|FILE)$/,
  'errno.h': /^(errno|E[A-Z0-9]+)$/,
};
const LIBC_CALL = /^(mem[a-z]+|str[a-z]+|malloc|calloc|realloc|free|f?printf|sprintf|snprintf|puts|putchar|fputs|fopen|fclose|fgets|fflush|perror|sscanf|is[a-z]+|toupper|tolower|ato[ilf]|getenv|abs|qsort|bsearch)$/;

const idents = toks => toks.filter(t => t.type == 'identifier').map(t => t.lexeme);

/* splits the text of one gap between functions into file-scope items */
function parseItems(lex, text, base) {
  const toks = lex(text, 'gap');
  const items = [];
  let pending = [], cur = [], depth = 0;

  const flush = (last, kind) => {
    const first = cur[0] ?? last;
    const lead = pending.length && text.slice(pending[pending.length - 1].charPos + pending[pending.length - 1].charLength, first.charPos).split('\n').length <= 2 ? pending : [];
    const from = (lead[0] ?? first).charPos;
    const body = text.slice(first.charPos, last.charPos + last.charLength);
    items.push({ kind, offset: base + from, text: text.slice(from, last.charPos + last.charLength), toks: cur.slice(), body, lead });
    pending = pending.filter(c => !lead.includes(c));
    cur = [];
  };

  for(const t of toks) {
    if(!cur.length && t.type == 'preprocessor') {
      cur = [t];
      flush(t, 'pp');
      continue;
    }
    if(!cur.length && COMMENT.has(t.type)) {
      pending.push(t);
      continue;
    }
    cur.push(t);
    if(t.type == 'lbrace' || t.type == 'lparen' || t.type == 'lbrack') depth++;
    else if(t.type == 'rbrace' || t.type == 'rparen' || t.type == 'rbrack') depth--;
    else if(t.type == 'semi' && depth == 0) flush(t, 'decl');
  }

  return { items, stray: pending.map(c => base + c.charPos), rest: cur.length };
}

function describeItem(it) {
  const t = it.toks;
  it.static = t.some(x => x.type == 'static');
  const first = t.find(x => x.type != 'static' && x.type != 'extern' && x.type != 'const');
  const stop = t.findIndex(x => ['equal', 'lbrack', 'semi', 'lparen'].includes(x.type));

  if(t.some(x => x.type == 'lparen') && !t.some(x => x.type == 'equal') && !t.some(x => x.lexeme == 'typedef')) {
    it.kind = 'proto';
    return;
  }

  if(TYPE_KW.has(t[0]?.lexeme) && (t[0].lexeme == 'typedef' || t.some(x => x.type == 'lbrace'))) {
    const last = t.filter((x, i) => x.type == 'identifier' && t.slice(i).every(y => y.type != 'lbrace')).pop();
    const id = t[0].lexeme == 'typedef' ? idents(t).pop() : t[1]?.type == 'identifier' ? t[1].lexeme : last?.lexeme;
    it.kind = 'type';
    it.name = id;
    return;
  }

  it.kind = 'var';
  it.name = idents(t.slice(0, stop < 0 ? t.length : stop)).pop();
}

export function splitLib({ cSource, hSource, name, libDir }, { lex, findFunctions }) {
  const warnings = [];
  const notes = [];
  const files = new Map();
  const NAME = name.toUpperCase();
  const funcsRaw = findFunctions(cSource, name + '.c');

  /* header: guard, includes, prototypes in order */
  const htoks = lex(hSource, name + '.h');
  const hincludes = [];
  const hitems = [];
  const hcur = [];
  let hpending = [], hdepth = 0, hbody = false;
  const hflush = last => {
    const first = hcur[0];
    const lead = hpending.filter(() => true);
    const from = (lead[0] ?? first).charPos;
    const text = hSource.slice(from, last.charPos + last.charLength);
    const hasParen = hcur.some(x => x.type == 'lparen');
    const typedef = hcur.some(x => x.lexeme == 'typedef');
    const idx = hcur.findIndex(x => x.type == 'lparen');
    const proto = hasParen && !typedef && !hbody && !hcur.some(x => x.type == 'static');
    hitems.push({ kind: proto ? 'proto' : 'verbatim', name: proto ? hcur[idx - 1]?.lexeme : undefined, text, lead: lead.length > 0, toks: hcur.slice() });
    hcur.length = 0;
    hpending = [];
    hbody = false;
  };

  let guardSeen = false;
  htoks.forEach((t, i) => {
    if(!hcur.length && t.type == 'preprocessor') {
      const m = /^#\s*(\w+)\s*(.*)$/s.exec(t.lexeme);
      if(!guardSeen && m?.[1] == 'ifndef') return void (guardSeen = true);
      if(guardSeen && m?.[1] == 'define' && hitems.length == 0 && hincludes.length == 0 && !t.seenGuard) {
        guardSeen = 'done';
        return;
      }
      if(m?.[1] == 'endif' && htoks.slice(i + 1).every(x => COMMENT.has(x.type))) return;
      const inc = /^#\s*include\s+"([^"]+)"/.exec(t.lexeme);
      if(inc) return void hincludes.push(inc[1].replace(/^lib\//, ''));
      hcur.push(t);
      hflush(t);
      return;
    }
    if(!hcur.length && COMMENT.has(t.type)) return void hpending.push(t);
    hcur.push(t);
    if(['lbrace', 'lparen', 'lbrack'].includes(t.type)) {
      if(t.type == 'lbrace' && hdepth == 0 && hcur[hcur.length - 2]?.type == 'rparen') hbody = true;
      hdepth++;
    } else if(['rbrace', 'rparen', 'rbrack'].includes(t.type)) {
      hdepth--;
      if(t.type == 'rbrace' && hdepth == 0 && hbody) hflush(t);
    } else if(t.type == 'semi' && hdepth == 0) hflush(t);
  });

  const protoNames = new Set(hitems.filter(i => i.kind == 'proto').map(i => i.name));

  /* c file: includes, functions, file-scope items */
  const cincludes = [];
  const gaps = [];
  let at = 0;
  for(const f of funcsRaw) {
    gaps.push([at, f.start]);
    at = f.end;
  }
  gaps.push([at, cSource.length]);

  const nodes = [];
  const sysIncludes = [];

  gaps.forEach(([a, b], gi) => {
    const { items, stray, rest } = parseItems(lex, cSource.slice(a, b), a);
    for(const it of items) {
      if(it.kind == 'pp') {
        const m = /^#\s*include\s*([<"])([^>"]+)[>"]/.exec(it.text);
        if(m) (m[1] == '<' ? sysIncludes : cincludes).push(m[2]);
        else {
          const d = /^#\s*define\s+(\w+)/.exec(it.text);
          if(d) nodes.push({ kind: 'macro', name: d[1], offset: it.offset, text: it.text, ids: idents(lex(it.text.replace(/^#\s*define\s+\w+/, ''), 'm')), static: true });
          else warnings.push(`file-scope directive left out: ${it.text.split('\n')[0]}`);
        }
        continue;
      }
      describeItem(it);
      if(it.kind == 'proto') {
        notes.push(`forward declaration dropped: ${it.body.replace(/\s+/g, ' ')}`);
        continue;
      }
      nodes.push({ kind: it.kind, name: it.name, offset: it.offset, text: it.text, ids: idents(lex(it.text, 'i')), static: it.static });
    }
    if(stray.length || rest) warnings.push(gi == gaps.length - 1 ? 'a comment or text trailing the whole file is not placed' : 'a banner or stray comment before a function is not placed');
  });

  funcsRaw.forEach(f => {
    let text = cSource.slice(f.start, f.end);
    let banner;
    while((banner = /^\s*(\/\*[\s\S]*?\*\/|\/\/[^\n]*)[ \t]*\n[ \t]*\n/.exec(text))) {
      warnings.push(`banner comment above ${f.name} is not placed: ${banner[1].split('\n')[0]}`);
      text = text.slice(banner[0].length);
    }
    const toks = lex(text, f.name);
    const at = toks.findIndex(t => t.type == 'identifier' && t.lexeme == f.name);
    const isStatic = toks.slice(0, at).some(t => t.type == 'static');
    nodes.push({
      kind: 'func',
      name: f.name,
      offset: f.start,
      text,
      toks,
      ids: idents(toks),
      static: isStatic,
      calls: toks.filter((t, i) => t.type == 'identifier' && toks[i + 1]?.type == 'lparen' && LIBC_CALL.test(t.lexeme)).map(t => t.lexeme),
      startOff: toks.find(t => !COMMENT.has(t.type))?.charPos ?? 0,
    });
  });

  const byName = new Map(nodes.map(n => [n.name, n]));
  const anyProto = protoNames.size > 0;
  for(const n of nodes) {
    n.inHeader = protoNames.has(n.name);
    n.root = (n.kind == 'func' && (anyProto ? n.inHeader : !n.static)) || (n.kind == 'var' && !n.static);
    if(n.kind == 'func' && !n.root && !n.static) n.unexported = true;
  }
  for(const n of nodes) n.refs = [...new Set(n.ids)].filter(id => id != n.name && byName.has(id)).map(id => byName.get(id));

  /* roots reaching each private node */
  const users = new Map(nodes.filter(n => !n.root).map(n => [n, new Set()]));
  for(const r of nodes.filter(n => n.root)) {
    const seen = new Set();
    const walk = n => {
      for(const d of n.refs) {
        if(seen.has(d) || d.root) continue;
        seen.add(d);
        users.get(d).add(r);
        walk(d);
      }
    };
    walk(r);
  }

  /* groups of roots that share one file */
  const groupOf = new Map(nodes.filter(n => n.root).map(n => [n, n]));
  const find = n => (groupOf.get(n) == n ? n : find(groupOf.get(n)));
  const internal = [];
  const placed = new Map();
  const ownFile = new Set();

  for(const [p, us] of users) {
    if(!us.size) {
      warnings.push(`${p.name}: nothing public uses it, dropped`);
      continue;
    }
    if(us.size > 1 && [...us].every(u => u.kind == 'var')) {
      const [a, ...rest] = [...us];
      for(const u of rest) groupOf.set(find(u), find(a));
      continue;
    }
    if(us.size > 1) {
      warnings.push(`${p.name}: used by ${[...us].map(u => u.name).join(', ')}: ${p.kind == 'func' ? 'own file plus internal header' : p.kind == 'var' ? 'shared data NOT placed, decide' : 'internal header'}`);
      if(p.kind == 'func') ownFile.add(p);
      else if(p.kind != 'var') internal.push(p);
      continue;
    }
  }

  for(const n of nodes) if(n.root) placed.set(n, []);
  for(const [p, us] of users) {
    if(us.size != 1 && !(us.size > 1 && [...us].every(u => u.kind == 'var'))) continue;
    placed.get(find([...us][0]))?.push(p);
  }
  /* roots merged into a group: their helpers move with the group */
  for(const [r, list] of [...placed]) {
    const g = find(r);
    if(g != r) {
      placed.get(g).push(...list, r);
      placed.delete(r);
    }
  }

  /* output files */
  const rewriteInc = h => (h.startsWith('lib/') ? '../' + h.slice(4) : '../../' + h);
  const hdrIncludes = new Set(hincludes);
  const allSys = [...new Set(sysIncludes)];

  const addInclude = (list, h) => list.includes(h) || list.push(h);

  /* header: add headers its declarations need */
  const libHeaders = fs.existsSync(libDir) ? fs.readdirSync(libDir).filter(f => f.endsWith('.h') && !f.endsWith('_internal.h')).map(f => f.slice(0, -2)) : [];
  const hids = new Set(hitems.flatMap(i => idents(i.toks)));
  const hinc = hincludes.map(h => h.replace(/\.h$/, ''));
  for(const lh of libHeaders) {
    if(lh == name || hinc.includes(lh)) continue;
    if([...hids].some(id => id == lh || id.startsWith(lh + '_'))) {
      hinc.push(lh);
      notes.push(`header: added #include "${lh}.h"`);
    }
  }

  const header = [`#ifndef ${NAME}_H`, `#define ${NAME}_H`, ''];
  if(hinc.length) header.push(...hinc.map(h => `#include "${h}.h"`), '');
  const parts = hitems.map(i => (i.kind == 'proto' && !i.lead ? `/* */\n${i.text}` : i.text));
  header.push(parts.join('\n\n'), '', `#endif /* defined ${NAME}_H */`, '');
  files.set(path.join(libDir, name + '.h'), header.join('\n').replace(/\n\n\n+/g, '\n\n'));

  const funcsOut = [];
  const emit = (root, list) => {
    const body = [...list, root].sort((a, b) => a.offset - b.offset);
    const roots = body.filter(n => n.root);
    const ordered = [...body.filter(n => !n.root), ...roots];
    const ids = new Set(ordered.flatMap(n => n.ids));
    const calls = new Set(ordered.flatMap(n => n.calls ?? []));
    const incs = [`#include "../${name}.h"`];
    const libIncs = [];

    for(const inc of cincludes) {
      const base = inc.replace(/^lib\//, '').replace(/\.h$/, '');
      if(inc == name + '.h' || hinc.includes(base)) continue;
      if(inc.startsWith('lib/')) {
        if([...ids].some(id => id == base || id.startsWith(base + '_') || id.startsWith(base.toUpperCase() + '_'))) addInclude(libIncs, rewriteInc(inc));
      } else {
        warnings.push(`${root.name}: include "${inc}" kept as ${rewriteInc(inc)}, check`);
        addInclude(libIncs, rewriteInc(inc));
      }
    }
    incs.push(...libIncs.map(h => `#include "${h}"`));
    for(const s of allSys) {
      const re = SYS_NEEDS[s];
      if(!re) {
        warnings.push(`${root.name}: system header <${s}> unknown, kept`);
        incs.push(`#include <${s}>`);
      } else if([...ids].some(id => re.test(id))) incs.push(`#include <${s}>`);
    }

    const texts = ordered.map(n => {
      if(n.kind == 'func' && !n.root && n.unexported) {
        const t = n.text;
        return t.slice(0, n.startOff) + 'static ' + t.slice(n.startOff);
      }
      return n.text.trim();
    });
    const file = path.join(libDir, name, root.name + '.c');
    files.set(file, incs.join('\n') + '\n\n' + texts.join('\n\n') + '\n');
    funcsOut.push({ file, calls: [...calls] });
    for(const n of ordered)
      if(n.unexported && n.kind == 'func' && !n.root) notes.push(`${n.name}: not in the header, moved into ${path.basename(file)} as static`);
  };

  for(const [root, list] of placed) {
    if(root.kind == 'func' && !(root.name == name || root.name.startsWith(name + '_'))) {
      warnings.push(`${root.name}: does not start with ${name}_, not written`);
      continue;
    }
    if(root.kind == 'var') warnings.push(`${root.name}: global variable${placed.get(root).length ? ', group with ' + placed.get(root).map(n => n.name).join(', ') : ''}, written to ${root.name}.c, decide`);
    emit(root, list);
  }

  for(const p of ownFile) {
    p.root = true;
    p.text = p.text;
    emit(p, []);
    notes.push(`${p.name}: shared helper, declare it in ${name}_internal.h (static removed)`);
  }

  if(internal.length || ownFile.size) {
    const lines = [`#ifndef ${NAME}_INTERNAL_H`, `#define ${NAME}_INTERNAL_H`, '', `#include "${name}.h"`, ''];
    for(const n of internal) lines.push(n.text.trim(), '');
    for(const p of ownFile) lines.push(`${p.text.slice(p.startOff, p.text.indexOf('{')).replace(/^static\s+/, '').trim()};`, '');
    lines.push(`#endif /* defined ${NAME}_INTERNAL_H */`, '');
    files.set(path.join(libDir, name + '_internal.h'), lines.join('\n'));
  }

  const libc = funcsOut.map(f => `${path.relative(libDir, f.file)}: ${f.calls.join(' ')}`).filter(l => !l.endsWith(': '));
  return { files, warnings, notes, libc };
}

/* follow-ups after the split: cmake lists, includes of the old header, git */
export function followUps({ name, libDir, root, apply, cFile, hFile }) {
  const out = [];
  const act = (what, fn) => {
    out.push(`${apply ? 'did' : 'would'}: ${what}`);
    if(apply) fn();
  };
  const sh = cmd => {
    const f = std.popen(cmd + ' 2>&1', 'r');
    const r = f.readAsString();
    f.close();
    return r;
  };
  const edit = (file, fn, what) => {
    if(!fs.existsSync(file)) return void out.push(`skip: ${file} does not exist`);
    const old = fs.readFileSync(file, 'utf8');
    const text = fn(old);
    if(text !== old) act(`${what} (${file})`, () => fs.writeFileSync(file, text));
  };

  edit(
    path.join(libDir, 'CMakeLists.txt'),
    t =>
      t.replace(/(set\(MODSUBDIRS)([\s\S]*?)\)/, (m, a, body) => {
        const words = body.split(/\s+/).filter(Boolean);
        if(words.includes(name)) return m;
        const indent = /\n([ \t]+)\S/.exec(body)?.[1] ?? '    ';
        const i = words.findIndex(w => w > name);
        words.splice(i < 0 ? words.length : i, 0, name);
        return a + '\n' + indent + words.join('\n' + indent) + ')';
      }),
    `add ${name} to MODSUBDIRS`,
  );

  const users = new Set();
  edit(
    path.join(root, 'CMakeLists.txt'),
    t =>
      t.replace(/^set\((\w+)_SOURCES([^)]*)\)\n(\n?)/gm, (m, prog, body, nl) => {
        const words = body.split(/\s+/).filter(Boolean);
        const kept = words.filter(w => w != name + '.c' && w != name + '.h');
        if(kept.length == words.length) return m;
        users.add(prog);
        return kept.length ? `set(${prog}_SOURCES ${kept.join(' ')})\n${nl}` : '';
      }),
    `remove ${name}.c/${name}.h from set(<prog>_SOURCES)`,
  );

  const rewritten = [];
  const walk = dir => {
    for(const e of fs.readdirSync(dir)) {
      if(e == '.git' || e.startsWith('build') || e == 'node_modules') continue;
      const p = path.join(dir, e);
      if(p == libDir || p == path.resolve(cFile) || p == path.resolve(hFile)) continue;
      if(fs.statSync(p).isDirectory()) walk(p);
      else if(/\.(c|h|cpp)$/.test(e)) rewritten.push(p);
    }
  };
  walk(root);

  for(const f of rewritten) {
    const text = fs.readFileSync(f, 'utf8');
    if(!text.includes(name + '.h')) continue;
    const re = new RegExp(`#(\\s*)include(\\s*)"((?:\\.\\./)*)${name}\\.h"`, 'g');
    const next = text.replace(re, (m, a, b, up) => (path.resolve(path.dirname(f), up + name + '.h') == path.resolve(root, name + '.h') ? `#${a}include${b}"${up}lib/${name}.h"` : m));
    if(next !== text) {
      const base = path.basename(f, path.extname(f));
      if(path.dirname(f) == root && fs.existsSync(path.join(root, 'CMakeLists.txt'))) users.add(base);
      act(`rewrite include of ${name}.h to lib/${name}.h (${f})`, () => fs.writeFileSync(f, next));
    }
  }

  if(users.size)
    edit(
      path.join(root, 'CMakeLists.txt'),
      t => {
        for(const u of users) t = t.replace(new RegExp(`^(\\s*program\\(${u})(\\s)(?![^)]*\\b${name}\\b)`, 'm'), `$1 ${name}$2`);
        return t;
      },
      `add ${name} to program(${[...users].join(', ')})`,
    );

  const oldFiles = [cFile, hFile].filter(f => f && path.resolve(f) == path.resolve(root, path.basename(f)));
  if(oldFiles.length) act(`git rm ${oldFiles.join(' ')}`, () => out.push(sh(`git -C '${root}' rm -q ${oldFiles.map(f => `'${f}'`).join(' ')}`)));
  else out.push(`skip: git rm, the input files are not ${root}/${name}.c and ${name}.h`);
  act(`git add lib/${name}.h lib/${name}/`, () => out.push(sh(`git -C '${root}' add '${libDir}/${name}.h' '${libDir}/${name}'`)));
  out.push(`remind: add tests/CMakeLists.txt link_libraries entry if tests use ${name}, then rerun "cmake ." in the build dir`);
  return out;
}

/* reads lib/<name>.h and lib/<name>/*.c back: header text, and per function file its includes and body */
function gatherLib(libDir, name) {
  const warnings = [];
  const hText = fs.readFileSync(path.join(libDir, name + '.h'), 'utf8').replace(/^\/\* \*\/\n/gm, '');
  const dir = path.join(libDir, name);
  const INC = /^[ \t]*#[ \t]*include[ \t]*([<"])([^>"\n]+)[>"][^\n]*\n?/gm;
  const libHeaders = new Set(fs.readdirSync(libDir).filter(f => f.endsWith('.h')));

  const units = fs
    .readdirSync(dir)
    .filter(f => f.endsWith('.c'))
    .map(f => {
      const src = fs.readFileSync(path.join(dir, f), 'utf8');
      const incs = [];
      const body = src.replace(INC, (m, q, h) => {
        if(q == '<') incs.push({ sys: true, h });
        else if(h == `../${name}.h`) return '';
        else if(h == `../${name}_internal.h`) warnings.push(`${f}: includes ${name}_internal.h, merge its contents by hand`);
        else if(h.startsWith('../../')) incs.push({ h: h.slice(6) });
        else if(h.startsWith('../')) incs.push({ h: 'lib/' + h.slice(3), local: h.slice(3) });
        else incs.push({ h });
        return '';
      });
      const fn = f.slice(0, -2);
      const at = hText.search(new RegExp(`\\b${fn}\\s*\\(`));
      return { file: f, fn, incs, body: body.trim(), at: at < 0 ? Infinity : at };
    })
    .sort((a, b) => a.at - b.at || a.file.localeCompare(b.file));

  for(const u of units) if(u.at == Infinity) warnings.push(`${u.file}: not declared in ${name}.h, placed last`);
  const names = new Map();
  for(const u of units)
    for(const m of u.body.matchAll(/^(\w+)\([^;{]*\)\s*\{/gm)) {
      if(names.has(m[1])) warnings.push(`${m[1]} defined in ${names.get(m[1])} and ${u.file}`);
      else names.set(m[1], u.file);
    }
  return { hText, units, warnings, libHeaders };
}

/* the includes of all units, de-duped in first-seen order, own and header-included ones removed */
function mergedIncludes(units, skip, rewrite) {
  const lib = [];
  const sys = [];
  for(const u of units)
    for(const i of u.incs) {
      if(i.sys) addUnique(sys, i.h);
      else if(!skip.has(i.h) && !skip.has(i.local)) addUnique(lib, rewrite(i));
    }
  return [...lib.map(h => `#include "${h}"`), ...sys.map(h => `#include <${h}>`)];
}

const addUnique = (list, v) => list.includes(v) || list.push(v);

/* reverse of splitLib: lib/<name>.h + lib/<name>/*.c -> <name>.h and <name>.c text */
export function joinLib({ libDir, name }) {
  const { hText, units, warnings, libHeaders } = gatherLib(libDir, name);
  const hinc = new Set([...hText.matchAll(/#\s*include\s*"([^"]+)"/g)].map(m => m[1]));
  const header = hText.replace(/#(\s*)include(\s*)"([^"/]+\.h)"/g, (m, a, b, h) => (libHeaders.has(h) ? `#${a}include${b}"lib/${h}"` : m));
  const incs = mergedIncludes(units, hinc, i => i.h);
  const c = [`#include "${name}.h"`, ...incs, '', units.map(u => u.body).join('\n\n'), ''].join('\n');
  return { header, c, warnings };
}

/* header, then #ifdef NAME_IMPL with all includes and all functions of lib/<name>/ */
export function amalgamate({ libDir, name, guard }) {
  const { hText, units, warnings } = gatherLib(libDir, name);
  const IMPL = name.toUpperCase() + '_IMPL';
  const hinc = new Set([...hText.matchAll(/#\s*include\s*"([^"]+)"/g)].map(m => m[1]));
  let header = hText;

  if(guard) {
    const g = /#\s*ifndef\s+(\w+)/.exec(hText)?.[1];
    if(g) header = hText.replace(new RegExp(`\\b${g}\\b`, 'g'), guard);
    else warnings.push('no include guard found in the header, --guard ignored');
  }

  const incs = mergedIncludes(units, hinc, i => i.local ?? i.h);
  return { text: [header.trimEnd(), '', `#ifdef ${IMPL}`, ...incs, '', units.map(u => u.body).join('\n\n'), '', `#endif /* defined ${IMPL} */`, ''].join('\n'), warnings };
}
