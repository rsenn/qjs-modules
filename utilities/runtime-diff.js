#!/usr/bin/env qjsm

// runtime-diff.js - compares class/object/module structure across JS runtimes.
//
// Generates a small, dependency-free probe script (lib/describe-class.js's and
// lib/describe-object.js's source inlined directly, so it needs no module resolution
// beyond the names being probed) that:
//   - dynamically imports each given module name and describeClass()/describeObject()s
//     every export
//   - looks up each given global identifier on globalThis and describes it the same way
//   - prints one line, `##RUNTIME_DIFF##<json>`, with the whole result
//
// Runs that script through each selected runtime's CLI (qjsm/node/deno/bun - browser
// isn't supported: it can't be shelled out to and captured like the others), captures
// the marker line, and:
//   - pretty-prints each described export/global back into readable (non-executable,
//     bodies are empty) JS - a class/object/function skeleton, not the raw JSON
//   - if more than one runtime ran, diffs the member-name sets per name across
//     runtimes and reports which are missing/present unevenly
//
// Caveats: describeClass()/describeObject() don't preprocess macro-generated members,
// this only sees own + prototype-chain-walked members (not e.g. Proxy traps), and the
// diff is member-*names* only (a method present in every runtime but with a different
// arity/kind isn't flagged - only presence/absence is).

import { puts, exit, loadFile, open, tmpfile, SEEK_SET } from 'std';
import { exec } from 'os';
import { getOpt, isMainModule } from 'util';

const RUNTIMES = {
  qjsm: ['qjsm'],
  node: ['node'],
  deno: ['deno', 'run', '--allow-all'],
  bun: ['bun', 'run'],
};

const RESULT_MARKER = '##RUNTIME_DIFF##';

// --- process helpers ---------------------------------------------------------------

// Captures via a temp file, not a pipe: piped stdout on Node/Bun is written
// asynchronously, and a large result line (>64KB, the Linux pipe buffer size) can
// still be in flight - or even get a short write the runtime doesn't retry - when the
// process exits, silently truncating the JSON. A regular file has no such buffering
// hazard, at the cost of only reading it after the child has fully exited (block: true).
function runCapture(args) {
  const tmp = tmpfile();
  if(!tmp) throw new Error('failed to create temp file');

  exec(args, { stdout: tmp.fileno(), block: true });

  tmp.seek(0, SEEK_SET);
  const out = tmp.readAsString();
  tmp.close();

  return out;
}

function isRuntimeAvailable(bin) {
  const devnull = open('/dev/null', 'w');
  if(!devnull) return false;

  const status = exec([bin, '--version'], { block: true, stdout: devnull.fileno(), stderr: devnull.fileno() });
  devnull.close();

  return status !== 127;
}

// --- probe script generation ---------------------------------------------------------

function stripExport(src) {
  return src.replace(/^export\s+/m, '');
}

function buildProbeScript(moduleNames, globalNames) {
  const describeClassSrc = stripExport(loadFile('lib/describe-class.js'));
  const describeObjectSrc = stripExport(loadFile('lib/describe-object.js'));

  return `${describeClassSrc}
${describeObjectSrc}

function isClassLike(fn) {
  if(typeof fn !== 'function') return false;
  if(/^class[\\s{]/.test(Function.prototype.toString.call(fn))) return true;
  return Object.getOwnPropertyNames(fn.prototype || {}).length > 1;
}

function describeAny(val) {
  if(val === null) return { kind: 'null' };
  if(typeof val === 'function') return isClassLike(val) ? { kind: 'class', ...describeClass(val) } : { kind: 'function', ...describeObject(val) };
  if(typeof val === 'object') return { kind: 'object', ...describeObject(val) };
  return { kind: typeof val, value: val };
}

const result = { modules: {}, globals: {}, builtins: null };
const moduleNames = ${JSON.stringify(moduleNames)};
const globalNames = ${JSON.stringify(globalNames)};

// Node/Deno/Bun all implement node:module's builtinModules (Deno/Bun for Node
// compat). qjsm has no node:module, but does expose its own registry as the native
// \`builtins\` global (jsm_builtins() in src/qjsm.c) - {name, native|compiled}[]
// instead of a flat string array, so it's mapped to match.
try {
  const { builtinModules } = await import('node:module');
  result.builtins = builtinModules.filter(m => !m.startsWith('_')).sort();
} catch(e) {
  if(typeof globalThis.builtins !== 'undefined') result.builtins = globalThis.builtins.map(b => b.name).sort();
}

// Bun exposes no registry API for its own bun:* modules (confirmed by direct probing
// - no Bun.builtinModules or equivalent) - manually maintained, will go stale as Bun
// adds/removes bun:* modules; update by hand when that's noticed.
if(typeof Bun !== 'undefined') {
  const BUN_MODULES = ['bun:sqlite', 'bun:ffi', 'bun:jsc', 'bun:test', 'bun:wrap'];
  result.builtins = [...new Set([...(result.builtins ?? []), ...BUN_MODULES])].sort();
}

for(const name of moduleNames) {
  try {
    const mod = await import(name);
    const exports = {};
    for(const key of Object.keys(mod)) exports[key] = describeAny(mod[key]);
    result.modules[name] = { ok: true, exports };
  } catch(e) {
    result.modules[name] = { ok: false, error: String((e && e.message) || e) };
  }
}

for(const name of globalNames) {
  try {
    if(!(name in globalThis)) throw new Error('not defined');
    result.globals[name] = { ok: true, ...describeAny(globalThis[name]) };
  } catch(e) {
    result.globals[name] = { ok: false, error: String((e && e.message) || e) };
  }
}

// describeObject()'s fields capture the raw property value verbatim, including
// non-primitives - real modules have self-referential ones (Node's path.win32.win32
// === path.win32), which would otherwise throw JSON.stringify out of the whole
// result, not just that one field. Also some runtimes (Bun's node:repl shim) expose
// live objects with getters that throw NotImplementedError on access - JSON.stringify
// invokes those itself while walking own properties, before a replacer ever sees the
// value, so a replacer alone can't guard it. Pre-sanitize into a plain, JSON-safe tree
// first (catching per-property access and breaking cycles/depth), then stringify that.
function sanitize(v, seen, depth) {
  if(typeof v === 'function') return undefined;
  if(typeof v === 'bigint') return v.toString() + 'n';
  if(v === null || typeof v !== 'object') return v;
  if(seen.has(v)) return '[Circular]';
  if(depth > 8) return '[MaxDepth]';
  seen.add(v);

  const out = Array.isArray(v) ? [] : {};
  let keys;
  try {
    keys = Object.keys(v);
  } catch(e) {
    return '[Unreadable]';
  }
  for(const k of keys) {
    try {
      out[k] = sanitize(v[k], seen, depth + 1);
    } catch(e) {
      out[k] = '[Unreadable: ' + e.message + ']';
    }
  }
  return out;
}

function safeStringify(v) {
  return JSON.stringify(sanitize(v, new WeakSet(), 0));
}

// console.log() on a piped (non-tty) stdout is async under Node/Bun - a large result
// line (>64KB, the Linux pipe buffer size) can still be in flight when the process
// exits, truncating it. fs.writeSync(1, ...) is a real blocking write, but a single
// call can still return short on a pipe (observed: Bun's writeSync stops at the 64KB
// pipe-buffer boundary instead of looping) - loop on the byte count it reports until
// everything's out. Assumes ASCII-only output (true for this probe's JSON), since the
// returned count is bytes but slicing below is by UTF-16 code unit.
const resultLine = ${JSON.stringify(RESULT_MARKER)} + safeStringify(result) + '\\n';
try {
  const fs = await import('fs');
  let offset = 0;
  while(offset < resultLine.length) {
    const n = fs.writeSync(1, resultLine.slice(offset));
    if(!n) break;
    offset += n;
  }
} catch(e) {
  console.log(resultLine);
}
`;
}

function extractResult(output) {
  const idx = output.indexOf(RESULT_MARKER);
  if(idx === -1) return { ok: false, error: 'no result marker in output', raw: output };

  const jsonText = output.slice(idx + RESULT_MARKER.length).split('\n')[0];

  try {
    return { ok: true, data: JSON.parse(jsonText) };
  } catch(e) {
    return { ok: false, error: `failed to parse result: ${e.message}`, raw: output };
  }
}

// --- deserialize back into readable JS ----------------------------------------------

function emptyMembers() {
  return { methods: [], getters: [], setters: [], fields: [] };
}

function safeJSON(v) {
  try {
    return JSON.stringify(v);
  } catch(e) {
    return String(v);
  }
}

function methodPrefix(kind) {
  return kind === 'async' ? 'async ' : kind === 'generator' ? '*' : kind === 'async-generator' ? 'async *' : kind === 'class' ? 'class ' : '';
}

// A member name is only ever safe to print bare (`foo() {}`, `foo: 1,`) when it's a
// valid JS identifier - describeClass()/describeObject() also hand back names like
// "node:module" (a real export key) or "Symbol.iterator" (from their own symbolName()
// helper), which would otherwise come out as invalid syntax (`node:module: ...`) or a
// wrong-meaning one (a literal string key instead of the actual well-known symbol).
function propKey(name) {
  const s = String(name);
  if(/^[A-Za-z_$][A-Za-z0-9_$]*$/.test(s)) return s;
  if(/^Symbol\.[A-Za-z0-9_$]+$/.test(s)) return `[${s}]`;
  return JSON.stringify(s);
}

function renderClassBody(members, indent, prefix = '') {
  const lines = [];
  for(const f of members.fields) lines.push(`${indent}${prefix}${propKey(f.name)} = ${safeJSON(f.value)}; // ${f.type}`);
  for(const g of members.getters) lines.push(`${indent}${prefix}get ${propKey(g)}() {}`);
  for(const s of members.setters) lines.push(`${indent}${prefix}set ${propKey(s)}(v) {}`);
  // safeStringify()'s depth cap (utilities/runtime-diff.js's probe script) can replace
  // a deeply-nested params array with the literal string '[MaxDepth]' - guard against
  // that not being an array rather than crashing the whole render.
  for(const m of members.methods) lines.push(`${indent}${prefix}${methodPrefix(m.kind)}${propKey(m.name)}(${Array.isArray(m.params) ? m.params.join(', ') : ''}) {}`);
  return lines;
}

function renderClass(desc) {
  const own = desc.prototypeChain[0] ?? emptyMembers();
  const staticsOwn = desc.staticChain[0] ?? emptyMembers();
  const parent = desc.prototypeChain[1]?.constructorName;
  const header = `class ${desc.name}${parent && parent !== '(anonymous)' ? ` extends ${parent}` : ''} {`;

  return [header, ...renderClassBody(staticsOwn, '  ', 'static '), ...renderClassBody(own, '  '), '}'].join('\n');
}

function renderFunction(desc, name) {
  const params = desc.constructorParams;
  return `function ${name}(${Array.isArray(params) ? params.join(', ') : ''}) {}`;
}

function renderObjectLiteral(desc) {
  const lines = [`const ${desc.name} = {`];
  for(const f of desc.fields ?? []) lines.push(`  ${propKey(f.name)}: ${safeJSON(f.value)}, // ${f.type}`);
  for(const g of desc.getters ?? []) lines.push(`  get ${propKey(g)}() {},`);
  for(const s of desc.setters ?? []) lines.push(`  set ${propKey(s)}(v) {},`);
  for(const m of desc.methods ?? []) lines.push(`  ${methodPrefix(m.kind)}${propKey(m.name)}(${Array.isArray(m.params) ? m.params.join(', ') : ''}) {},`);
  lines.push('};');
  return lines.join('\n');
}

function renderAny(entry, name) {
  if(!entry) return `// ${name}: (no result)`;
  // Only the module/global *wrapper* entries carry an explicit `ok` - a bare
  // describeAny() result (a module's per-export entry) never sets it, so this must
  // check `=== false`, not just falsiness, or every real result looks like an error.
  if(entry.ok === false) return `// ${name}: ERROR - ${entry.error}`;

  // Always label by the requested binding name (module export key / global
  // identifier), not entry.name - describeObject() derives that from
  // obj.constructor.name, which for a plain object like Math is "Object", not "Math".
  if(entry.kind === 'class') return renderClass({ ...entry, name });
  if(entry.kind === 'function') return renderFunction(entry, name);
  if(entry.kind === 'object') return renderObjectLiteral({ ...entry, name });

  return `const ${name} = ${safeJSON(entry.value)}; // ${entry.kind}`;
}

// --- cross-runtime diff --------------------------------------------------------------

function memberNamesOf(entry) {
  if(!entry || !entry.ok) return [];
  if(entry.exports) return Object.keys(entry.exports);

  if(entry.kind === 'class') {
    const own = entry.prototypeChain[0] ?? emptyMembers();
    return [...own.methods.map(m => m.name), ...own.getters, ...own.setters, ...own.fields.map(f => f.name)];
  }

  if(entry.kind === 'object' || entry.kind === 'function') {
    return [...(entry.methods ?? []).map(m => m.name), ...(entry.getters ?? []), ...(entry.setters ?? []), ...(entry.fields ?? []).map(f => f.name)];
  }

  return [];
}

function diffEntry(name, perRuntime) {
  const lines = [`## ${name}`];
  const memberSets = {};

  for(const [rt, entry] of Object.entries(perRuntime)) {
    if(!entry) {
      lines.push(`  ${rt}: (no result)`);
      memberSets[rt] = new Set();
    } else if(!entry.ok) {
      lines.push(`  ${rt}: ERROR - ${entry.error}`);
      memberSets[rt] = new Set();
    } else {
      const names = memberNamesOf(entry);
      memberSets[rt] = new Set(names);
      lines.push(`  ${rt}: ok (${names.length} members)`);
    }
  }

  const all = new Set();
  for(const s of Object.values(memberSets)) for(const n of s) all.add(n);

  const inconsistent = [...all]
    .filter(n => {
      const present = Object.values(memberSets).map(s => s.has(n));
      return present.some(Boolean) && !present.every(Boolean);
    })
    .sort();

  if(inconsistent.length) {
    lines.push('  differing members:');
    for(const n of inconsistent) {
      const where = Object.entries(memberSets)
        .filter(([, s]) => s.has(n))
        .map(([rt]) => rt);
      lines.push(`    ${n}: present in [${where.join(', ')}]`);
    }
  }

  return lines.join('\n');
}

/** Same shape as diffEntry(), but for a plain name-list per runtime (result.builtins)
 * instead of a describeAny()-shaped entry. */
function diffNameList(label, listsByRuntime) {
  const lines = [`## ${label}`];
  const sets = {};

  for(const [rt, list] of Object.entries(listsByRuntime)) {
    if(list == null) {
      lines.push(`  ${rt}: (not available)`);
      sets[rt] = new Set();
    } else {
      sets[rt] = new Set(list);
      lines.push(`  ${rt}: ${list.length} modules`);
    }
  }

  const all = new Set();
  for(const s of Object.values(sets)) for(const n of s) all.add(n);

  const inconsistent = [...all]
    .filter(n => {
      const present = Object.values(sets).map(s => s.has(n));
      return present.some(Boolean) && !present.every(Boolean);
    })
    .sort();

  if(inconsistent.length) {
    lines.push('  not in every runtime:');
    for(const n of inconsistent) {
      const where = Object.entries(sets)
        .filter(([, s]) => s.has(n))
        .map(([rt]) => rt);
      lines.push(`    ${n}: present in [${where.join(', ')}]`);
    }
  }

  return lines.join('\n');
}

// --- main ------------------------------------------------------------------------

const OPTIONS = {
  help: [false, printHelp, 'h'],
  global: [true, (v, prev) => (prev ?? []).concat(v), 'g'],
  runtime: [true, (v, prev) => (prev ?? []).concat(v), 'r'],
  all: [false, null, 'a'],
  builtins: [false, null, 'b'],
  output: [true, null, 'o'],
  '@': 'names',
};

function printHelp() {
  puts(
    `Usage: ${scriptArgs[0]} [OPTIONS] <module-name...>\n\n` +
      'Describes each given module (dynamically imported) and/or global identifier via\n' +
      'lib/describe-class.js/lib/describe-object.js, in each selected runtime, prints\n' +
      'the result back as readable JS, and diffs member-name sets across runtimes when\n' +
      'more than one ran. See the file header comment for the full method/caveats.\n\n' +
      '<module-name...>  module specifiers to import() and describe\n\n' +
      'Options:\n' +
      '  -g, --global <name>   a globalThis identifier to describe instead of/as well\n' +
      '                        as modules (repeatable)\n' +
      '  -r, --runtime <name>  qjsm|node|deno|bun to run against (repeatable, default:\n' +
      '                        qjsm only)\n' +
      '  -a, --all             run against every runtime with a detected binary\n' +
      '  -b, --builtins        list/diff each runtime\'s own builtin module names\n' +
      '                        (node:module\'s builtinModules on node/deno/bun;\n' +
      '                        globalThis.builtins on qjsm) instead of/as well as\n' +
      '                        describing specific names\n' +
      '  -o, --output <file>   write the raw per-runtime JSON results to file\n' +
      '  -h, --help            show this help\n',
  );
  exit(0);
}

function main(...args) {
  const params = getOpt(OPTIONS, args);
  const moduleNames = params['@'] ?? [];
  const globalNames = params.global ?? [];

  if(moduleNames.length === 0 && globalNames.length === 0 && !params.builtins) printHelp();

  let runtimeNames = params.all ? Object.keys(RUNTIMES).filter(isRuntimeAvailable) : (params.runtime ?? ['qjsm']);

  const unknown = runtimeNames.filter(n => !RUNTIMES[n]);
  if(unknown.length) {
    puts(`unknown runtime(s): ${unknown.join(', ')} (known: ${Object.keys(RUNTIMES).join(', ')})\n`);
    exit(1);
  }

  const script = buildProbeScript(moduleNames, globalNames);
  const probeFile = '.tmp/runtime-diff-probe.mjs';
  const pf = open(probeFile, 'w');
  pf.puts(script);
  pf.close();

  const results = {};

  for(const rt of runtimeNames) {
    if(!isRuntimeAvailable(RUNTIMES[rt][0])) {
      puts(`skipping ${rt}: binary not found\n`);
      continue;
    }

    puts(`running ${rt}...\n`);
    results[rt] = extractResult(runCapture([...RUNTIMES[rt], probeFile]));
    if(!results[rt].ok) puts(`  ${rt}: ${results[rt].error}\n`);
  }

  if(params.output) {
    const of = open(params.output, 'w');
    of.puts(JSON.stringify(results, null, 2));
    of.close();
  }

  const ran = Object.keys(results).filter(rt => results[rt].ok);

  for(const name of moduleNames) {
    puts(`\n=== module: ${name} ===\n`);
    for(const rt of ran) {
      const modEntry = results[rt].data.modules[name];
      puts(`--- ${rt} ---\n`);
      if(!modEntry.ok) {
        puts(`ERROR: ${modEntry.error}\n`);
        continue;
      }
      for(const [exportName, entry] of Object.entries(modEntry.exports)) puts(renderAny(entry, exportName) + '\n\n');
    }
  }

  for(const name of globalNames) {
    puts(`\n=== global: ${name} ===\n`);
    for(const rt of ran) puts(`--- ${rt} ---\n${renderAny(results[rt].data.globals[name], name)}\n`);
  }

  if(ran.length > 1) {
    puts(`\n=== diff across ${ran.join(', ')} ===\n`);

    for(const name of moduleNames) {
      const perRuntime = {};
      for(const rt of ran) perRuntime[rt] = results[rt].data.modules[name];
      puts(diffEntry(name, perRuntime) + '\n');
    }

    for(const name of globalNames) {
      const perRuntime = {};
      for(const rt of ran) perRuntime[rt] = results[rt].data.globals[name];
      puts(diffEntry(name, perRuntime) + '\n');
    }
  }

  if(params.builtins) {
    puts(`\n=== builtin modules ===\n`);

    if(ran.length > 1) {
      const listsByRuntime = {};
      for(const rt of ran) listsByRuntime[rt] = results[rt].data.builtins;
      puts(diffNameList('builtins', listsByRuntime) + '\n');
    } else {
      for(const rt of ran) {
        const list = results[rt].data.builtins;
        puts(`--- ${rt} ---\n`);
        puts(list == null ? '(not available on this runtime)\n' : `${list.length} modules:\n${list.join('\n')}\n`);
      }
    }
  }
}

if(isMainModule(import.meta.url)) main(...scriptArgs.slice(1));
