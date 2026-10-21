/* lazyGlobal.js: defines a property that loads its value from a module on
 * first read, for the runtime globals of lib/node.js and lib/bun.js.
 * depends on: the qjsm globals builtins, locateModule, requireModule.
 * rule: nothing is loaded before the first read, qjs-lws costs ~50 ms.
 *
 * ```js
 * lazyGlobal(globalThis, 'URL', ['lws/url', 'url'], false);
 * URL;   // requireModule('lws/url').URL, else requireModule('url').URL
 * ```
 */

const found = new Map();

/* spec is a qjsm builtin or a file in the module path */
function available(spec) {
  if(!found.has(spec)) found.set(spec, !!(globalThis.builtins?.some(b => b.name == spec) || globalThis.locateModule?.(spec)));
  return found.get(spec);
}

/* defines obj[name] as export key of the first module in specs that loads.
 *
 *   object           obj         where to define it, globalThis or Bun
 *   string           name        the property
 *   string|string[]  specs       modules to try in order
 *   boolean          enumerable
 *   string           key         the export, default name
 *
 * never throws: no getter when name exists or no spec is found, and when
 * every module fails to load obj[name] is deleted and reads as undefined.
 */
export default function lazyGlobal(obj, name, specs, enumerable, key = name) {
  specs = [specs].flat().filter(available);

  if(name in obj || !specs.length) return;

  const set = value => Object.defineProperty(obj, name, { value, writable: true, enumerable, configurable: true });

  Object.defineProperty(obj, name, {
    get() {
      let value;

      for(const spec of specs) {
        try {
          value = requireModule(spec)[key];
        } catch(e) {}

        if(value !== undefined) break;
      }

      if(value === undefined) delete obj[name];
      else set(value);

      return value;
    },
    set,
    enumerable,
    configurable: true,
  });
}
