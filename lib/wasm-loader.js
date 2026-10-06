/* wasm-loader.js: loads `.wasm` modules (format 'module-wasm') as ES modules,
 * on top of registerHooks(); the WebAssembly ESM integration, instance phase.
 * runs on qjsm only; needs the `wasm` native module.
 * rule: each import module is a static import, so it is evaluated first.
 *
 * ```js
 * import installWasmLoader from 'wasm-loader';
 * installWasmLoader();
 * const { add } = await import('./add.wasm');
 * ```
 *
 * `import source` and `with { type: 'wasm' }` are not handled.
 */
import * as wasm from 'wasm';
import { readFileSync } from 'fs';

const IS_WASM = /\.wasm$/;

function toPath(url) {
  return url.startsWith('file://') ? decodeURIComponent(url.slice(7)) : url;
}

/* the module source for one .wasm file; it reads the bytes again at run time
 *
 * ```js
 * import * as m0 from "env";            // one per imported module name
 * const e = instantiate(m0);            // { env: m0 }
 * export const add = e.add;             // global exports hold their value
 * ```
 */
function glue(path) {
  const module = new wasm.Module(readFileSync(path));
  const names = [...new Set(wasm.Module.imports(module).map(i => i.module))];
  const exports = wasm.Module.exports(module);
  const q = JSON.stringify;

  return [
    `import * as wasm from 'wasm';`,
    `import { readFileSync } from 'fs';`,
    ...names.map((n, i) => `import * as m${i} from ${q(n)};`),
    `const url = import.meta.url;`,
    `const path = decodeURIComponent(url.startsWith('file://') ? url.slice(7) : url);`,
    `const imports = { ${names.map((n, i) => `${q(n)}: m${i}`).join(', ')} };`,
    `const e = new wasm.Instance(new wasm.Module(readFileSync(path)), imports).exports;`,
    `const v = x => (x instanceof wasm.Global ? x.value : x);`,
    ...exports.map(({ name }, i) => `const x${i} = v(e[${q(name)}]); export { x${i} as ${q(name)} };`),
  ].join('\n');
}

/** Registers the hooks; returns the registerHooks() result (`deregister()`). */
export default function installWasmLoader() {
  return registerHooks({
    resolve(specifier, context, nextResolve) {
      const result = nextResolve(specifier, context);

      return IS_WASM.test(result.url) ? { ...result, format: 'module-wasm' } : result;
    },
    load(url, context, nextLoad) {
      if(context.format != 'module-wasm') return nextLoad(url, context);

      return { format: 'module', source: glue(toPath(url)), shortCircuit: true };
    },
  });
}

export { installWasmLoader };
