#!/usr/bin/env qjsm
/* move-modules.js: moves JS source files and rewrites every relative import of them.
 * depends on: module-lexer (specifier ranges), util (getOptHelp), fs, path, child_process.
 * rule: all moves are resolved against the original tree first, then files are rewritten, then moved.
 */
import { chmodSync, existsSync, lstatSync, mkdirSync, readFileSync, readdirSync, renameSync, statSync, unlinkSync, writeFileSync } from 'fs';
import * as path from 'path';
import { spawnSync } from 'child_process';
import { getOptHelp } from 'util';
import { parse } from 'module-lexer';
import ECMAScriptLexer from 'lexer/ecmascript.js';

const SCAN_EXT = /\.(m?js)$/;
const TEXT_EXT = /\.(m?js|cjs|jsx|ts|tsx|json|json5|md|markdown|html?|css|sh|bash|txt|ya?ml|toml|cfg|conf|ini|cmake|mk|makefile)$/i;
const DEFAULT_EXCLUDES = ['node_modules', '.git', 'attic', 'vendor', 'lib'];
const MAX_TEXT = 4 << 20;

/* a glob over one path segment, * and ? only: node_modules, *.min.js */
const segmentGlob = pat => new RegExp('^' + pat.replace(/[.+^${}()|[\]\\]/g, '\\$&').replace(/\*/g, '.*').replace(/\?/g, '.') + '$');

/* every file under dir whose name passes keep(), skipping excluded segments and symlinked directories */
function walk(dir, excluded, keep, out = []) {
  let names;

  try {
    names = readdirSync(dir).sort();
  } catch(e) {
    return out;
  }

  for(const name of names) {
    if(name == '.' || name == '..' || excluded.some(re => re.test(name))) continue;

    const p = path.join(dir, name);
    let st;

    try {
      st = lstatSync(p);
    } catch(e) {
      continue;
    }

    if(st.isDirectory()) walk(p, excluded, keep, out);
    else if(st.isFile() && keep(name)) out.push(p);
  }

  return out;
}

const isFile = p => {
  try {
    return statSync(p).isFile();
  } catch(e) {
    return false;
  }
};

/* the specifier from file dir `from` to file `to`: ./ for the same dir, ../ upward */
function specifier(from, to) {
  const rel = path.relative(from, to);

  return rel.startsWith('../') ? rel : './' + rel;
}

/* the move map from -m FILE lines and SRC DST pairs: Map of absolute source to absolute destination */
function readMoves(file, pairs, errors) {
  const list = [];

  if(file)
    readFileSync(file, 'utf8')
      .split('\n')
      .forEach((line, i) => {
        line = line.replace(/#.*$/, '').trim();
        if(!line) return;

        const w = line.split(/\s+/);

        if(w.length == 2) list.push([w[0], w[1], `${file}:${i + 1}`]);
        else errors.push(`${file}:${i + 1}: expected 'old new', got '${line}'`);
      });

  if(pairs.length % 2) errors.push('odd number of SRC DST arguments');
  for(let i = 0; i + 1 < pairs.length; i += 2) list.push([pairs[i], pairs[i + 1], 'command line']);

  const moves = new Map();
  const dsts = new Map();

  for(const [s, d, where] of list) {
    const src = path.resolve(s);
    const dst = path.resolve(d);

    if(src == dst) errors.push(`${where}: source and destination are the same: ${s}`);
    else if(moves.has(src)) errors.push(`${where}: source listed twice: ${s}`);
    else if(dsts.has(dst)) errors.push(`${where}: ${s} and ${dsts.get(dst)} both move to ${d}`);
    else {
      moves.set(src, dst);
      dsts.set(dst, s);
    }
  }

  return moves;
}

/* the specifier edits of one file: { edits: [{ start, end, from, to, kind }], missing: [spec], refs: [[start, end]] }
 * The file is looked at in its original place, a relative specifier is resolved there, then rewritten
 * for the new place of the importer and of the target. */
function planFile(file, text, moves) {
  const dir = path.dirname(file);
  const newDir = path.dirname(moves.get(file) ?? file);
  const edits = [];
  const missing = [];
  const handled = [];

  const consider = (start, end, kind) => {
    const raw = text.slice(start, end);

    handled.push([start, end]);

    if(!/^\.\.?\//.test(raw)) return;
    if(raw.includes('\\')) return missing.push(`${raw} (escape sequence, left alone)`);

    const target = path.resolve(dir, raw);

    if(!isFile(target)) return missing.push(raw);

    const to = specifier(newDir, moves.get(target) ?? target);

    if(to != raw) edits.push({ start, end, from: raw, to, kind });
  };

  const { imports } = parse(text);

  for(const r of imports) {
    if(r.kind != 'import-statement' && r.kind != 'dynamic-import') continue;
    if(r.path == null || !r.pathRange) continue;
    consider(r.pathRange[0], r.pathRange[1], r.kind == 'dynamic-import' ? 'import()' : 'import');
  }

  /* new URL('./x.js', import.meta.url) */
  for(const m of text.matchAll(/new\s+URL\(\s*(['"])([^'"\n]*)\1\s*,\s*import\.meta\.url\s*\)/g)) {
    const start = m.index + m[0].indexOf(m[1]) + 1;

    consider(start, start + m[2].length, 'URL');
  }

  edits.sort((a, b) => a.start - b.start);
  return { edits, missing, handled };
}

const applyEdits = (text, edits) => edits.reduceRight((t, e) => t.slice(0, e.start) + e.to + t.slice(e.end), text);

/* writes file.new, then renames it over file, keeping its mode */
function replaceFile(file, text) {
  const mode = statSync(file).mode & 0o7777;
  const temp = file + '.new';

  writeFileSync(temp, text);

  try {
    chmodSync(temp, mode);
    renameSync(temp, file);
  } catch(e) {
    try {
      unlinkSync(temp);
    } catch(e2) {}
    throw e;
  }
}

/* regions of a JS file where a path may be mentioned: comments, strings and the static parts of template
 * literals, as { start, end, kind, edgeL, edgeR }. edgeL/edgeR mark a template part that touches a ${ }
 * substitution on that side, a candidate running into it is not a whole path. Regex literals are skipped. */
function jsRegions(text) {
  const astral = /[\ud800-\udfff]/.test(text);
  const map = [];

  if(astral)
    for(let i = 0, cp = 0; i < text.length; i++, cp++) {
      map[cp] = i;
      if((text.charCodeAt(i) & 0xfc00) == 0xd800) i++;
    }

  const at = cp => (astral ? map[cp] : cp);
  const lexer = new ECMAScriptLexer(text, 'move-modules');
  const out = [];

  for(let tok; (tok = lexer.nextToken()); ) {
    const { type } = tok;
    const kind = type == 'comment' ? 'comment' : /^(stringLiteral|templateLiteral(Head|Part|Tail)?)$/.test(type) ? 'string' : null;

    if(!kind) continue;

    const start = at(tok.loc.charOffset);
    const end = at(tok.loc.charOffset + tok.charLength);

    out.push({ start, end, kind, edgeL: type == 'templateLiteralPart' || type == 'templateLiteralTail', edgeR: type == 'templateLiteralHead' || type == 'templateLiteralPart' });
  }

  return out;
}

const COMPONENTS = p => p.split('/').filter(Boolean).length;
const MENTION_EXT = /\.(js|mjs|json|css|html|md)$/;
const RUN = /[A-Za-z0-9_.@*\/-]+/g;

/* what one mention c of a path in file (original place) means, or null when it names nothing that moves:
 *   { rewrite: { to, interp, target, notice } }  provably one moved file
 *   { ambiguous: reason }                        two targets, or a look-alike that is not in the map
 *   { warnString: reason }                       string whose cwd or file reading cannot be told apart
 *   { urlLike: true }                            /client/x.js style, a browser URL probably */
function resolveMention(c, file, kind, ctx) {
  const { moves, cwd, movedBase, strings } = ctx;
  const newOf = p => moves.get(p) ?? p;
  const hits = [];
  const other = [];

  if(c.startsWith('/')) {
    if(moves.has(c)) hits.push({ i: 'abs', t: c });
    else {
      const tail = c.slice(1);

      if(moves.has(path.resolve(cwd, tail)) || [...moves.keys()].some(k => k.endsWith('/' + tail))) return { urlLike: true };
      return null;
    }
  } else {
    const a = path.resolve(path.dirname(file), c);
    const b = path.resolve(cwd, c);

    if(ctx.isFile(a)) (moves.has(a) || moves.has(file) ? hits : other).push({ i: 'a', t: a });
    if(ctx.isFile(b)) (moves.has(b) ? hits : other).push({ i: 'b', t: b });
    if(kind != 'string' && !c.startsWith('.')) for(const k of moves.keys()) if(k.endsWith('/' + c)) hits.push({ i: 'c', t: k });
  }

  const targets = new Set(hits.map(h => h.t));
  const strangers = other.filter(o => movedBase.has(path.basename(o.t)) && !targets.has(o.t));

  if(targets.size > 1) return { ambiguous: `names ${[...targets].map(ctx.rel).join(' and ')}` };
  if(strangers.length) return { ambiguous: `${ctx.rel(strangers[0].t)} exists and is not moved, but its name is a moved one` };
  if(!targets.size) return null;

  if(kind == 'string') {
    if(strings == 'warn') return { warnString: `would become ${ctx.rel(newOf(hits[0].t))}` };
    if(!hits.some(h => h.i == 'b' || h.i == 'abs')) return { warnString: 'relative to this file, or to the cwd? left alone' };
  }

  const [target] = targets;
  const pick = hits.filter(h => h.i != 'c');
  const hit =
    kind == 'string' ? hits.find(h => h.i == 'b' || h.i == 'abs') : (/^\.\.?\//.test(c) ? pick.find(h => h.i == 'a') : pick.find(h => h.i == 'b')) ?? pick[0] ?? hits[0];
  const dot = r => (!r.startsWith('../') && c.startsWith('./') ? './' + r : r);
  let to, notice;

  if(hit.i == 'a') to = dot(path.relative(path.dirname(newOf(file)), newOf(target)));
  else if(hit.i == 'b') to = dot(path.relative(cwd, newOf(target)));
  else if(hit.i == 'abs') to = newOf(target);
  else {
    const parts = newOf(target).split('/');
    const first = c.split('/')[0];
    const keep = first && !first.startsWith('.') ? parts.indexOf(first, 1) : -1;

    /* keep the mention's first directory, then whatever the move added after it */
    if(keep > 0 && keep < parts.length - 1) to = parts.slice(keep).join('/');
    else {
      let k = COMPONENTS(c);

      while(k < parts.length && ctx.newPaths.filter(p => p.endsWith('/' + parts.slice(-k).join('/'))).length > 1) k++;

      to = parts.slice(-k).join('/');
      notice = `${first} no longer in the path, shortest unique tail`;
    }
  }

  return to == c ? null : { rewrite: { to, interp: hit.i, target, notice } };
}

/* a directory or glob mention of a folder that loses files: left alone, reported */
function dirMention(d, file, kind, ctx) {
  const { movedDirs, cwd } = ctx;

  if(!d) return false;
  if(movedDirs.has(path.resolve(path.dirname(file), d)) || movedDirs.has(path.resolve(cwd, d))) return true;

  return kind != 'string' && !d.startsWith('.') && d.includes('/') && [...movedDirs].some(m => m.endsWith('/' + d));
}

/* the mention edits of one file: { edits, ambiguous, warnStrings, urlLike, dirs } */
function planMentions(file, text, regions, specRanges, ctx) {
  const edits = [];
  const out = { edits, ambiguous: [], warnStrings: [], urlLike: [], dirs: [] };
  const lines = ctx.lineStarts(text);

  for(const r of regions) {
    if(specRanges.some(([s, e]) => s >= r.start && e <= r.end)) continue;

    const chunk = text.slice(r.start, r.end);
    let m;

    RUN.lastIndex = 0;

    while((m = RUN.exec(chunk))) {
      let run = m[0];
      let at = r.start + m.index;

      if(run.startsWith('//')) continue;
      if(r.kind == 'string' && text[at - 1] == '\\' && /^[ntrfbv]/.test(run)) {
        run = run.slice(1);
        at++;
      }

      const line = ctx.lineOf(lines, at);
      const where = `${ctx.rel(file)}:${line}`;

      if(r.edgeR && at + run.length >= r.end - 2) continue;

      if(run.includes('*')) {
        const star = run.indexOf('*');
        const slash = run.lastIndexOf('/', star);

        if(slash > 0 && dirMention(run.slice(0, slash), file, r.kind, ctx)) out.dirs.push([where, run]);
        continue;
      }

      if(run.endsWith('/')) {
        if(dirMention(run.slice(0, -1), file, r.kind, ctx)) out.dirs.push([where, run]);
        continue;
      }

      run = run.replace(/\.+$/, '');

      if(!run.includes('/') || !MENTION_EXT.test(run)) continue;
      if(r.edgeR && at + run.length >= r.end - 2) continue;
      if(r.edgeL && at == r.start) continue;

      const res = resolveMention(run, file, r.kind, ctx);

      if(!res) continue;
      if(res.urlLike) out.urlLike.push([where, run]);
      else if(res.ambiguous) out.ambiguous.push([where, run, res.ambiguous]);
      else if(res.warnString) out.warnStrings.push([where, run, res.warnString]);
      else edits.push({ start: at, end: at + run.length, from: run, to: res.rewrite.to, kind: r.kind == 'string' ? 'string' : r.kind, interp: res.rewrite.interp, notice: res.rewrite.notice, line });
    }
  }

  return out;
}

function main(args) {
  const roots = [];
  const excludes = [...DEFAULT_EXCLUDES];
  const opts = {};

  const params = getOptHelp(
    {
      map: ['FILE', v => (opts.map = v), 'm'],
      root: ['DIR', v => roots.push(v), 'r'],
      exclude: ['PATTERN', v => excludes.push(v), 'x'],
      'no-default-excludes': [false, () => excludes.splice(0, DEFAULT_EXCLUDES.length), 'X'],
      'dry-run': [false, () => (opts.dry = true), 'n'],
      git: [false, () => (opts.git = true), 'g'],
      strict: [false, () => (opts.strict = true), 's'],
      strings: ['MODE', v => (opts.strings = v), 'S'],
      'no-refs': [false, () => (opts.noRefs = true), 'R'],
      '@': '*pairs',
    },
    args,
    {
      map: `file with one 'old/path.js new/path.js' per line (# comments allowed)`,
      root: `directory or file to scan for importers, repeatable (default: current directory)`,
      exclude: `skip directories and files whose name matches PATTERN (* and ?), repeatable, added to the defaults: ${DEFAULT_EXCLUDES.join(' ')}`,
      'no-default-excludes': `drop the default exclude list (-x patterns stay)`,
      'dry-run': `print the plan, change nothing`,
      git: `move with git mv, so history follows`,
      strict: `a relative specifier to a missing file is an error, not a warning`,
      strings: `rewrite (default) or warn: path mentions in string literals`,
      'no-refs': `do not look at path mentions in comments, strings and text, only at imports`,
    },
  );

  const errors = [];

  if(opts.strings && !['rewrite', 'warn'].includes(opts.strings)) errors.push(`--strings: expected rewrite or warn, got '${opts.strings}'`);

  const moves = readMoves(opts.map, params['@'], errors);
  const cwd = path.getcwd?.() ?? process.cwd();
  const rel = p => path.relative(cwd, p) || '.';

  if(!moves.size && !errors.length) errors.push('no moves given: pass SRC DST pairs or -m FILE');

  /* idempotent: everything already moved */
  const srcGone = [...moves].filter(([s, d]) => !existsSync(s) && existsSync(d));

  if(!errors.length && srcGone.length == moves.size) {
    console.log('nothing to do: all destinations exist and no source remains');
    return 0;
  }

  for(const [s, d] of moves) {
    if(srcGone.length && srcGone.some(([s2]) => s2 == s)) errors.push(`${rel(s)}: already moved, but other moves of the map are not (refusing a half-applied map)`);
    else if(!isFile(s)) errors.push(`${rel(s)}: source is not an existing file`);
    else if(existsSync(d)) errors.push(`${rel(d)}: destination exists`);
  }

  if(errors.length) {
    for(const e of errors) console.error('error: ' + e);
    return 1;
  }

  const excluded = excludes.map(segmentGlob);
  const scanRoots = (roots.length ? roots : ['.']).map(r => path.resolve(r));
  const scan = new Set();
  const textFiles = new Set();

  for(const r of scanRoots) {
    if(isFile(r)) {
      if(SCAN_EXT.test(r)) scan.add(r);
      textFiles.add(r);
    } else {
      for(const f of walk(r, excluded, n => SCAN_EXT.test(n))) scan.add(f);
      for(const f of walk(r, excluded, n => TEXT_EXT.test(n) || n == 'Makefile')) textFiles.add(f);
    }
  }

  if(opts.map) textFiles.delete(path.resolve(opts.map));

  /* moved files are scanned even in excluded places: their own imports change */
  for(const s of moves.keys()) if(SCAN_EXT.test(s)) scan.add(s);

  const plans = new Map();
  const warnings = [];
  const specRanges = new Map();
  const planOf = (f, text) => plans.get(f) ?? (plans.set(f, { file: f, text, edits: [] }), plans.get(f));

  for(const f of [...scan].sort()) {
    const text = readFileSync(f, 'utf8');
    let plan;

    try {
      plan = planFile(f, text, moves);
    } catch(e) {
      warnings.push(`${rel(f)}: cannot lex: ${e.message}`);
      continue;
    }

    specRanges.set(f, plan.handled);
    for(const m of plan.missing) warnings.push(`${rel(f)}: specifier to a missing file: ${m}`);
    if(plan.edits.length) planOf(f, text).edits.push(...plan.edits.map(e => ({ ...e, cat: 'specifier' })));
  }

  if(opts.strict && warnings.some(w => w.includes('missing file'))) {
    for(const w of warnings) console.error('error: ' + w);
    return 1;
  }

  const found = { ambiguous: [], warnStrings: [], urlLike: [], dirs: [] };

  if(!opts.noRefs) {
    const everyone = new Set([...scan, ...textFiles, ...moves.keys()]);
    const isFileCached = new Map();
    const ctx = {
      moves,
      cwd,
      rel,
      strings: opts.strings ?? 'rewrite',
      movedBase: new Set([...moves.keys()].map(k => path.basename(k))),
      movedDirs: new Set([...moves.keys()].map(k => path.dirname(k))),
      newPaths: [...everyone].map(p => moves.get(p) ?? p),
      isFile: p => isFileCached.get(p) ?? (isFileCached.set(p, isFile(p)), isFileCached.get(p)),
      lineStarts: text => {
        const l = [0];

        for(let i = text.indexOf('\n'); i >= 0; i = text.indexOf('\n', i + 1)) l.push(i + 1);
        return l;
      },
      lineOf: (l, at) => {
        let lo = 0,
          hi = l.length - 1;

        while(lo < hi) {
          const mid = (lo + hi + 1) >> 1;

          if(l[mid] <= at) lo = mid;
          else hi = mid - 1;
        }

        return lo + 1;
      },
    };

    for(const f of [...everyone].sort()) {
      let text;

      try {
        if(!isFile(f) || statSync(f).size > MAX_TEXT) continue;
        text = readFileSync(f, 'utf8');
      } catch(e) {
        continue;
      }

      let regions = [{ start: 0, end: text.length, kind: 'text' }];

      if(SCAN_EXT.test(f)) {
        try {
          regions = jsRegions(text);
        } catch(e) {
          warnings.push(`${rel(f)}: cannot lex for comments and strings: ${e.message}`);
          continue;
        }
      }

      const r = planMentions(f, text, regions, specRanges.get(f) ?? [], ctx);

      for (const k of Object.keys(found)) found[k].push(...r[k]);
      if(r.edits.length) planOf(f, text).edits.push(...r.edits.map(e => ({ ...e, cat: e.kind == 'string' ? 'string' : 'text' })));
    }
  }

  for(const p of plans.values()) p.edits.sort((x, y) => x.start - y.start);

  const count = cat => [...plans.values()].reduce((n, p) => n + p.edits.filter(e => e.cat == cat).length, 0);
  const lineAt = (text, at) => text.slice(0, at).split('\n').length;

  console.log(`moves (${moves.size}):`);
  for(const [s, d] of moves) console.log(`  ${rel(s)} -> ${rel(d)}`);

  for(const p of [...plans.values()].sort((x, y) => x.file.localeCompare(y.file))) {
    console.log(`${rel(p.file)}${moves.has(p.file) ? ` (moves to ${rel(moves.get(p.file))})` : ''}:`);
    for(const e of p.edits) console.log(`  ${e.line ?? lineAt(p.text, e.start)}: ${e.kind}${e.interp ? ` (${e.interp})` : ''}: ${e.from} -> ${e.to}${e.notice ? ` [${e.notice}]` : ''}`);
  }

  for(const w of warnings) console.log('warning: ' + w);

  const section = (title, list, fmt) => {
    if(!list.length) return;
    console.log(`${title} (${list.length}):`);
    for(const x of list) console.log('  ' + fmt(x));
  };

  section('ambiguous, not changed', found.ambiguous, ([w, c, why]) => `${w}: ${c}: ${why}`);
  section('strings left alone', found.warnStrings, ([w, c, why]) => `${w}: ${c}: ${why}`);
  section('url-like, not changed', found.urlLike, ([w, c]) => `${w}: ${c}`);

  if(found.dirs.length) {
    const byFile = new Map();

    for(const [w, c] of found.dirs) {
      const [f, l] = [w.slice(0, w.lastIndexOf(':')), w.slice(w.lastIndexOf(':') + 1)];

      if(!byFile.has(f)) byFile.set(f, new Map());
      if(!byFile.get(f).has(c)) byFile.get(f).set(c, []);
      byFile.get(f).get(c).push(l);
    }

    console.log(`directory/glob mentions, fix by hand (${found.dirs.length}):`);
    for(const [f, m] of byFile) for(const [c, l] of m) console.log(`  ${f}: ${c} (line ${l.join(', ')})`);
  }

  const total = count('specifier');
  const nText = count('text');
  const nString = count('string');

  if(!opts.dry) {
    for(const p of plans.values()) replaceFile(p.file, applyEdits(p.text, p.edits));

    for(const [s, d] of moves) {
      mkdirSync(path.dirname(d), { recursive: true });

      if(opts.git) {
        const r = spawnSync('git', ['-C', path.dirname(s), 'mv', s, d]);

        if(r.status !== 0) throw new Error(`git mv ${rel(s)} ${rel(d)} failed (status ${r.status})`);
      } else renameSync(s, d);
    }
  }

  const w = opts.dry ? 'would ' : '';

  console.log(`${w}${opts.dry ? 'move' : 'moved'} ${moves.size} files, ${w}${opts.dry ? 'rewrite' : 'rewrote'} ${total} specifiers, ${nText} comment/text mentions and ${nString} strings in ${plans.size} files`);
  console.log(`left: ${found.ambiguous.length} ambiguous, ${found.warnStrings.length} strings, ${found.urlLike.length} url-like, ${found.dirs.length} directory/glob mentions`);
  return 0;
}

process.exit(main(process.argv.slice(2)));
