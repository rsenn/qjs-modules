/* typescript.js: loads `.ts`, `.mts` and `.tsx` modules (format 'module-typescript')
 * by transpiling with the external `swc` CLI, on top of registerHooks().
 * runs on qjsm only; needs `swc` (npm i -g @swc/cli @swc/core) in PATH.
 * rule: type stripping only, no type checking; target es2022.
 *
 * ```js
 * import installTypeScript from 'typescript';
 * installTypeScript();
 * const { A } = await import('./a.ts');
 * ```
 *
 * `.cts` / 'commonjs-typescript' is not handled.
 */
import { popen } from 'std';
import { stat } from 'os';

const SUFFIXES = ['.ts', '.tsx', '.mts', '/index.ts'];
const IS_TS = /\.(ts|tsx|mts)$/;

function toPath(url) {
  return url.startsWith('file://') ? decodeURIComponent(url.slice(7)) : url;
}

function isFile(path) {
  const [st, err] = stat(path);
  return !err && (st.mode & 0o170000) == 0o100000;
}

/* swc <path> on stdout; throws Error if swc fails or yields nothing */
function transpile(path) {
  const quoted = `'${path.replace(/'/g, `'\\''`)}'`;
  const tsx = path.endsWith('.tsx') ? ' -C jsc.parser.tsx=true' : '';
  const f = popen(`swc -C jsc.target=es2022 -C jsc.parser.syntax=typescript${tsx} ${quoted} 2>/dev/null`, 'r');

  if(!f) throw new Error(`cannot run swc for '${path}'`);

  const source = f.readAsString();

  if(f.close() != 0 || !source) throw new Error(`swc failed to transpile '${path}'`);

  return source;
}

/** Registers the hooks; returns the registerHooks() result (`deregister()`). */
export default function installTypeScript() {
  return registerHooks({
    resolve(specifier, context, nextResolve) {
      let result = nextResolve(specifier, context);
      let path = toPath(result.url);

      if(path[0] == '/' && !isFile(path)) {
        const found = SUFFIXES.map(s => path + s).find(isFile);

        if(found) result = { ...result, url: 'file://' + found };
        path = found ?? path;
      }

      return IS_TS.test(path) ? { ...result, format: 'module-typescript' } : result;
    },
    load(url, context, nextLoad) {
      if(context.format != 'module-typescript') return nextLoad(url, context);

      return { format: 'module', source: transpile(toPath(url)), shortCircuit: true };
    },
  });
}

export { installTypeScript };
