/**
 * Node's `node:module` API (https://nodejs.org/api/module.html), backed by this
 * engine's own builtin-module registry (`globalThis.builtins`, native - see
 * jsm_builtins() in src/qjsm.c) rather than a hand-maintained list - matches how
 * Bun/Deno alias `node:module` to *their own* module system, not to literal Node
 * internals. `import ... from 'node:module'` resolves here too (src/qjsm.c strips a
 * leading `node:` before builtin-name lookup).
 *
 * Only the subset actually implementable on top of what this engine already has:
 * `builtinModules`, `isBuiltin()`, `createRequire()`, `registerHooks()`. Node's
 * `Module` class, async `register()` hooks, `syncBuiltinESMExports()` and `SourceMap` aren't -
 * flagged in TODO.md rather than stubbed out with fake behavior.
 *
 * createRequire() is backed by the compiled-in `require` builtin (lib/require.js),
 * loaded on first use so that importing 'module' does not itself install the
 * global `require`.
 */
export const builtinModules = (globalThis.builtins ?? []).map(b => b.name).sort();

export function isBuiltin(name) {
  if(typeof name !== 'string') return false;
  if(name.startsWith('node:')) name = name.slice(5);
  return builtinModules.includes(name);
}

/**
 * Node's `module.createRequire(filename)`: a `require()` resolving relative to
 * `filename` (a path or `file://` URL), with require.js's full lookup
 * (`node_modules`, `package.json`, `.json`, `.js`).
 *
 * ```js
 * const require = createRequire(import.meta.url);
 * const cfg = require('./config.json');
 * ```
 */
export function createRequire(filename) {
  if(typeof filename != 'string') throw new TypeError('createRequire: filename must be a string or file: URL');

  if(filename.startsWith('file://')) filename = decodeURIComponent(filename.slice(7));

  return function required(id) {
    const { default: require } = globalThis.requireModule('require');
    const saved = require.filename;

    require.filename = filename;

    try {
      return require(id);
    } finally {
      require.filename = saved;
    }
  };
}

/**
 * Node's `module.registerHooks()`: the qjsm global of the same name, `undefined`
 * where the engine is not qjsm.
 */
export const registerHooks = globalThis.registerHooks;

export default { builtinModules, isBuiltin, createRequire, registerHooks };
