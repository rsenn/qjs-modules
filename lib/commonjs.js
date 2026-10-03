/* commonjs.js: loads format 'commonjs' modules (`.cjs`, or any hook result with
 * format: 'commonjs'), on top of registerHooks().
 * runs on qjsm only: registerHooks and the compiled-in 'require' builtin are qjs-modules'.
 * rule: the module runs at load time; the ES module handed to the engine only
 * re-exports module.exports (default, plus one named export per identifier key).
 *
 * ```js
 * import installCommonJS from 'commonjs';
 * installCommonJS();                    // also sets globalThis.require
 * const { x } = await import('./lib.cjs');
 * ```
 *
 * only results of hooks registered before this one are seen; a hook added
 * later that short-circuits with format 'commonjs' bypasses it.
 */
import require from 'require';
import { dirname } from 'path';

const key = Symbol.for('qjsm.commonjs.exports');
const store = (globalThis[key] ??= new Map());
let nextId = 0;

function toPath(url) {
  return url.startsWith('file://') ? decodeURIComponent(url.slice(7)) : url;
}

/* runs `source` as a CommonJS module and returns its module.exports */
function run(path, source) {
  const module = { id: path, filename: path, exports: {}, loaded: false };
  const wrapper = new Function('exports', 'require', 'module', '__filename', '__dirname', source.replace(/^#!.*/, ''));
  const filename = require.filename;

  require.filename = path;

  try {
    wrapper.call(module.exports, module.exports, require, module, path, dirname(path));
  } finally {
    require.filename = filename;
  }

  module.loaded = true;
  return module.exports;
}

/* ES module source re-exporting `exports` from the store */
function shim(exports) {
  const id = nextId++;
  let source = `const e = globalThis[Symbol.for('qjsm.commonjs.exports')].get(${id});\nexport default e;\n`;

  store.set(id, exports);

  if(exports !== null && (typeof exports == 'object' || typeof exports == 'function'))
    for(const name of Object.keys(exports)) if(name != 'default' && /^[A-Za-z_$][\w$]*$/.test(name)) source += `export const ${name} = e[${JSON.stringify(name)}];\n`;

  return source;
}

/** Registers the hooks; returns the registerHooks() result (`deregister()`). */
export default function installCommonJS() {
  return registerHooks({
    resolve(specifier, context, nextResolve) {
      const result = nextResolve(specifier, context);

      return /\.cjs$/.test(result.url) ? { ...result, format: 'commonjs' } : result;
    },
    load(url, context, nextLoad) {
      const result = nextLoad(url, context);

      if(result.format != 'commonjs' && context.format != 'commonjs') return result;

      const source = typeof result.source == 'string' ? result.source : new TextDecoder().decode(result.source);

      return { format: 'module', source: shim(run(toPath(url), source)), shortCircuit: true };
    },
  });
}

export { installCommonJS };
