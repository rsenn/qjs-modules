/* loaders/commonjs.js: loads format 'commonjs' modules on top of registerHooks(), as Node does:
 *
 *   .cjs                                   CommonJS
 *   .js, nearest package.json "commonjs"   CommonJS
 *   .js, nearest package.json "module"     ES module
 *   .js, no "type"                         CommonJS unless it only parses as a module
 *   any hook result with format 'commonjs' CommonJS
 *
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
import * as std from 'std';

const key = Symbol.for('qjsm.commonjs.exports');
const store = (globalThis[key] ??= new Map());
const SHIM = "const e = globalThis[Symbol.for('qjsm.commonjs.exports')]";

const types = new Map();

function toPath(url) {
  return url.startsWith('file://') ? decodeURIComponent(url.slice(7)) : url;
}

/* the "type" of the package.json nearest above file, Node's package scope:
 * 'module', 'commonjs' or undefined, also when a node_modules directory or
 * the root comes first */
function packageType(file) {
  const dir = dirname(file);

  if(!types.has(dir)) {
    let type, json;

    if(dir.endsWith('/node_modules') || dir == '/' || dir == '.') type = undefined;
    else if((json = std.loadFile(dir + '/package.json')) !== null) {
      try {
        type = JSON.parse(json).type;
      } catch(e) {}
    } else type = packageType(dir);

    types.set(dir, type);
  }

  return types.get(dir);
}

/* source compiled as the body of a CommonJS module wrapper, throws
 * SyntaxError for source that is an ES module */
function compile(source) {
  return new Function('exports', 'require', 'module', '__filename', '__dirname', source.replace(/^#!.*/, ''));
}

/* runs wrapper as a CommonJS module and returns its module.exports */
function run(path, wrapper) {
  const module = { id: path, filename: path, exports: {}, loaded: false };
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

      if(result.format || !/^file:.*\.c?js$/.test(result.url)) return result;
      if(/\.cjs$/.test(result.url) || packageType(toPath(result.url)) == 'commonjs') return { ...result, format: 'commonjs' };

      return result;
    },
    load(url, context, nextLoad) {
      const result = nextLoad(url, context);
      const commonjs = result.format == 'commonjs' || context.format == 'commonjs';

      /* a .js file without a package "type" is ambiguous */
      if(!commonjs && (context.format || !/^file:.*\.js$/.test(url) || packageType(toPath(url)) !== undefined)) return result;

      const source = typeof result.source == 'string' ? result.source : new TextDecoder().decode(result.source);
      let wrapper;

      /* already converted by a second install of this loader */
      if(source.startsWith(SHIM)) return result;

      try {
        wrapper = compile(source);
      } catch(e) {
        if(commonjs || !(e instanceof SyntaxError)) throw e;

        return result;
      }

      return { format: 'module', source: shim(run(toPath(url), wrapper)), shortCircuit: true };
    },
  });
  const deregister = hooks.deregister;

  hooks.deregister = function() {
    if(globalThis.require === require) delete globalThis.require;

    return deregister.call(this);
  };

  return hooks;
}
