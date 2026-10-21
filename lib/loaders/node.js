/* loaders/node.js: resolves `node:<name>` imports for qjsm, via registerHooks().
 * depends on: the qjsm globals registerHooks and builtins.
 * rule: src/qjsm.c knows nothing of `node:`; this hook is the only place.
 */

/* node:<name> | resolves to
 *
 *   node:child_process   builtin node_child_process (lib/node/child_process.js)
 *   node:vfs             vfs (lib/vfs.js), node 26.4+'s virtual file system
 *   node:fs              fs, the engine's own module of that name
 *   node:fs/promises     fs/promises, mapped to fsPromises by the engine
 *   node:path/posix      path/posix, mapped to path by the engine
 *   node:os              left as is: qjsm's os is not Node's os
 *   node:deep            itself, and loading it throws as on Node and Bun:
 *                        Error ERR_UNKNOWN_BUILTIN_MODULE, deep is no Node module
 *   node:sqlite          the same, Node's sqlite is not qjsm's
 */
const layers = new Set((globalThis.builtins ?? []).map(b => b.name).filter(n => n.startsWith('node_')));

/* Node's builtinModules that also load without the prefix, plus vfs */
const NODE = new Set(
  (
    'assert assert/strict async_hooks buffer child_process cluster console constants crypto dgram diagnostics_channel dns ' +
    'dns/promises domain events fs fs/promises http http2 https inspector inspector/promises module net os path ' +
    'path/posix path/win32 perf_hooks process punycode querystring readline readline/promises repl stream ' +
    'stream/consumers stream/promises stream/web string_decoder sys timers timers/promises tls trace_events tty url ' +
    'util util/types v8 vm vfs wasi worker_threads zlib'
  ).split(' '),
);

registerHooks({
  resolve(specifier, context, nextResolve) {
    if(!specifier.startsWith('node:')) return nextResolve(specifier, context);

    const name = specifier.slice(5);

    if(layers.has('node_' + name)) return nextResolve('node_' + name, context);
    if(!NODE.has(name)) return { url: specifier, shortCircuit: true };
    if(name == 'os') return nextResolve(specifier, context);

    return nextResolve(name, context);
  },

  load(url, context, nextLoad) {
    if(url.startsWith('node:') && !NODE.has(url.slice(5))) {
      const error = new Error(`No such built-in module: ${url}`);

      error.code = 'ERR_UNKNOWN_BUILTIN_MODULE';
      throw error;
    }

    return nextLoad(url, context);
  },
});
