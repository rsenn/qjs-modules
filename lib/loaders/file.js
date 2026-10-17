/* loaders/file.js: `import path from './x.c' with { type: 'file' }`, as in Bun.
 * runs on qjsm only: registerHooks() is qjs-modules'.
 * rule: the file is never read or compiled; the default export is its absolute path.
 *
 * ```js
 * import installFileLoader from 'loaders/file';
 * installFileLoader();
 * const { default: path } = await import('./hello.c', { with: { type: 'file' } });
 * // "/abs/dir/hello.c"
 * ```
 *
 * only the load hook sees `importAttributes`, so the hook short-circuits there.
 */

function toPath(url) {
  return url.startsWith('file://') ? decodeURIComponent(url.slice(7)) : url;
}

/** Registers the hook; returns the registerHooks() result (`deregister()`). */
export default function installFileLoader() {
  return registerHooks({
    load(url, context, nextLoad) {
      if(context.importAttributes?.type != 'file') return nextLoad(url, context);

      return { format: 'module', source: `export default ${JSON.stringify(toPath(url))};\n`, shortCircuit: true };
    },
  });
}
