/* loaders/node.js: resolves `node:<name>` imports for qjsm, via registerHooks().
 * depends on: loaders/nodeNames, the qjsm globals registerHooks, builtins
 * and locateModule.
 * rule: src/qjsm.c knows nothing of `node:`; this hook is the only place.
 */
import NODE from 'loaders/nodeNames';

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
 *
 * a bare Node name counts as node:<name> as in Node, but only where Node
 * code imports it, so qjsm scripts keep qjs-modules' module of that name:
 *
 *   util from a file below node_modules     node:util
 *   buffer from anywhere                    node:buffer, qjs-modules has no buffer
 *   util from a qjsm script                 util (lib/util.js)
 */
const layers = new Set((globalThis.builtins ?? []).map(b => b.name).filter(n => n.startsWith('node_')));

const builtin = name => (globalThis.builtins ?? []).some(b => b.name == name);

/* true when a bare Node name is to load as node:<name>, see above */
function nodeBare(name, parentURL) {
  if(!NODE.has(name)) return false;
  if(/\/node_modules\//.test(parentURL ?? '')) return true;

  return layers.has('node_' + name) && !builtin(name) && !globalThis.locateModule?.(name);
}

registerHooks({
  resolve(specifier, context, nextResolve) {
    if(nodeBare(specifier, context.parentURL)) specifier = 'node:' + specifier;
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
