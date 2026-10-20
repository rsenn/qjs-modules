/* loaders/typescript.js: loads `.ts` and `.mts` modules (format 'module-typescript') by
 * stripping types with swc's strip-only mode, on top of registerHooks().
 * runs on qjsm only; needs node 22.13+ in PATH, its module.stripTypeScriptTypes is swc strip-only.
 * rule: every type is overwritten with spaces, newlines kept, so line:column stay exact.
 *
 * ```js
 * import installTypeScript from 'loaders/typescript';
 * installTypeScript();
 * const { A } = await import('./a.ts');
 * ```
 *
 * only erasable syntax: enum, namespace with values, parameter properties and `import x = require()`
 * make the load throw. `.tsx` (JSX) and `.cts` / 'commonjs-typescript' are not handled.
 */
import { popen } from 'std';
import { stat } from 'os';

const SUFFIXES = ['.ts', '.mts', '/index.ts'];
const IS_TS = /\.(ts|mts)$/;

function toPath(url) {
  return url.startsWith('file://') ? decodeURIComponent(url.slice(7)) : url;
}

function isFile(path) {
  const [st, err] = stat(path);
  return !err && (st.mode & 0o170000) == 0o100000;
}

const STRIP = `const{stripTypeScriptTypes:s}=require('node:module');process.stdout.write(s(require('fs').readFileSync(process.argv[1],'utf8')))`;

/* path with its types blanked out; throws Error if node refuses the syntax or yields nothing */
function strip(path) {
  const quoted = `'${path.replace(/'/g, `'\\''`)}'`;
  const f = popen(`node --disable-warning=ExperimentalWarning -e "${STRIP}" ${quoted} 2>&1`, 'r');

  if(!f) throw new Error(`cannot run node for '${path}'`);

  const source = f.readAsString();

  if(f.close() != 0 || !source) throw new Error(`cannot strip types of '${path}': ${source.split('\n').find(l => /Error/.test(l)) ?? 'node failed'}`);

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

      return { format: 'module', source: strip(toPath(url)), shortCircuit: true };
    },
  });
}
