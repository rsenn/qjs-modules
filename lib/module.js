import { loadFile } from 'std';
import { dirname, extname, isAbsolute, join, normalize } from 'path';

/**
 * Node's `node:module` API (https://nodejs.org/api/module.html), backed by this
 * engine's own builtin-module registry (`globalThis.builtins`, native - see
 * jsm_builtins() in src/qjsm.c) rather than a hand-maintained list - matches how
 * Bun/Deno alias `node:module` to *their own* module system, not to literal Node
 * internals. `import ... from 'node:module'` resolves here too (src/qjsm.c strips a
 * leading `node:` before builtin-name lookup).
 *
 * Only the subset actually implementable on top of what this engine already has:
 * `builtinModules`, `isBuiltin()`, `createRequire()`. Node's `Module` class,
 * `register()` (loader hooks), `syncBuiltinESMExports()` and `SourceMap` aren't -
 * flagged in TODO.md rather than stubbed out with fake behavior.
 *
 * createRequire() doesn't reuse lib/require.js: this file is precompiled to bytecode
 * at build time (BUILTINS_COMPILED), and that compile step can only resolve *native*
 * module imports, not a cross-reference to another lib/*.js file - every other
 * successfully-precompiled builtin only imports natives too (checked: fs.js,
 * process.js, assert.js, etc.). So this is a small, self-contained, synchronous CJS
 * loader using only 'std'/'path', deliberately simpler than require.js's full
 * algorithm: it resolves relative/absolute paths only, no node_modules or
 * package.json lookup. Good enough for createRequire()'s most common real use (load
 * a local .js/.json file from ESM code), not a full require() replacement - use
 * `require` (lib/require.js) directly for that.
 */
export const builtinModules = (globalThis.builtins ?? []).map(b => b.name).sort();

export function isBuiltin(name) {
  if(typeof name !== 'string') return false;
  if(name.startsWith('node:')) name = name.slice(5);
  return builtinModules.includes(name);
}

function loadCommonJS(resolvedPath) {
  const source = loadFile(resolvedPath);
  if(source == null) throw new Error(`Cannot find module '${resolvedPath}'`);

  if(extname(resolvedPath) === '.json') return JSON.parse(source);

  const mod = { exports: {} };
  const wrapper = new Function('exports', 'require', 'module', '__filename', '__dirname', source);

  wrapper(mod.exports, id => resolveAndLoad(id, resolvedPath), mod, resolvedPath, dirname(resolvedPath));

  return mod.exports;
}

function resolveAndLoad(specifier, fromFile) {
  let resolved = specifier;

  if(specifier.startsWith('.') || specifier.startsWith('/')) {
    resolved = isAbsolute(specifier) ? specifier : normalize(join(dirname(fromFile), specifier));
    if(extname(resolved) === '') resolved += '.js';
  }

  return loadCommonJS(resolved);
}

/** Returns a require() function resolving relative to `filename`, matching Node's `createRequire()`. */
export function createRequire(filename) {
  return function required(id) {
    return resolveAndLoad(id, filename);
  };
}

export default { builtinModules, isBuiltin, createRequire };
