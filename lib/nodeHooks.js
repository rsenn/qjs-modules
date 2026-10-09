/* nodeHooks.js: resolves `node:<name>` imports for qjsm, via registerHooks().
 * depends on: the qjsm globals registerHooks and builtins.
 * rule: src/qjsm.c knows nothing of `node:`; this hook is the only place.
 */

/* node:<name> | resolves to
 *
 *   node:child_process   builtin node_child_process (lib/node/child_process.js)
 *   node:fs              fs, the engine's own module of that name
 *   node:fs/promises     fs/promises, mapped to fsPromises by the engine
 *   node:os              left as is: qjsm's os is not Node's os
 */
const layers = new Set((globalThis.builtins ?? []).map(b => b.name).filter(n => n.startsWith('node_')));

registerHooks({
  resolve(specifier, context, nextResolve) {
    if(!specifier.startsWith('node:')) return nextResolve(specifier, context);

    const name = specifier.slice(5);

    if(layers.has('node_' + name)) return nextResolve('node_' + name, context);
    if(name == 'os') return nextResolve(specifier, context);

    return nextResolve(name, context);
  },
});
