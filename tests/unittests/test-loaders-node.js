// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/loaders/node.js: node:<name> loads Node's modules only, as on Node, Bun and Deno */
import { eq, tests } from '../../lib/tinytest.js';

/* the error of import(specifier), or null when it loads */
async function failure(specifier) {
  try {
    await import(specifier);
    return null;
  } catch(e) {
    return e;
  }
}

/* only qjsm has globalThis.builtins */
const qjsm = Array.isArray(globalThis.builtins);

tests({
  async 'a Node module loads with the prefix'() {
    eq('function', typeof (await import('node:fs')).readFileSync);
    eq('function', typeof (await import('node:path/posix')).join);
  },

  async 'a name Node does not have throws when it loads'() {
    eq('No such built-in module: node:deep', (await failure('node:deep'))?.message);
  },

  'import.meta.resolve() leaves an unknown node: name alone'() {
    eq('node:deep', import.meta.resolve('node:deep'));
  },

  async 'a bare Node name in a package below node_modules is the Node module'() {
    eq('function,function,function,EventEmitter', (await import('../fixtures/node-modules/node_modules/bare-node/index.js')).default);
  },

  async 'a bare Node name qjs-modules has no module for is the Node module'() {
    eq('function', typeof (await import('buffer')).Buffer.from);
  },

  /* deno says ERR_MODULE_NOT_FOUND, node and bun ERR_UNKNOWN_BUILTIN_MODULE */
  async 'qjsm: the error has Node code, and node:sqlite is not qjsm sqlite'() {
    if(!qjsm) return;

    eq('ERR_UNKNOWN_BUILTIN_MODULE', (await failure('node:deep'))?.code);
    eq('ERR_UNKNOWN_BUILTIN_MODULE', (await failure('node:sqlite'))?.code);
  },
});
