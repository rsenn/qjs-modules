/* loaders/commonjs.js: loads format 'commonjs' modules (`.cjs`, or any hook result with
 * format: 'commonjs'), on top of registerHooks().
 * runs on qjsm only: registerHooks and the compiled-in 'require' builtin are qjs-modules'.
 * rule: the module runs at load time; the ES module handed to the engine only
 * re-exports module.exports (default, plus one named export per identifier key).
 *
 * ```js
 * import installCommonJS from 'loaders/commonjs';
 * installCommonJS();                    // also sets globalThis.require; deregister() removes it
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
const SHIM = "const e = globalThis[Symbol.for('qjsm.commonjs.exports')]";

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
  /* the store is shared by every copy of this module, so are the ids */
  const id = store.size;
  let source = `${SHIM}.get(${id});\nexport default e;\n`;

  store.set(id, exports);

  if(exports !== null && (typeof exports == 'object' || typeof exports == 'function'))
    for(const name of Object.keys(exports)) if(name != 'default' && /^[A-Za-z_$][\w$]*$/.test(name)) source += `export const ${name} = e[${JSON.stringify(name)}];\n`;

  return source;
}

/** Registers the hooks; returns the registerHooks() result, whose `deregister()` also removes globalThis.require. */
export default function installCommonJS() {
  const hooks = registerHooks({
    resolve(specifier, context, nextResolve) {
      const result = nextResolve(specifier, context);

      return /\.cjs$/.test(result.url) ? { ...result, format: 'commonjs' } : result;
    },
    load(url, context, nextLoad) {
      const result = nextLoad(url, context);

      if(result.format != 'commonjs' && context.format != 'commonjs') return result;

      const source = typeof result.source == 'string' ? result.source : new TextDecoder().decode(result.source);

      /* already converted by a second install of this loader */
      if(source.startsWith(SHIM)) return result;

      return { format: 'module', source: shim(run(toPath(url), source)), shortCircuit: true };
    },
  });
  const deregister = hooks.deregister;

  hooks.deregister = function() {
    if(globalThis.require === require) delete globalThis.require;

    return deregister.call(this);
  };

  return hooks;
}
