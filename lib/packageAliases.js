/* packageAliases.js: the "_moduleAliases" map of ./package.json, as a registerHooks() resolve hook.
 * depends on: std (loadFile), the qjsm global registerHooks.
 * rule: registers nothing unless package.json has the map, so a run without one has no hook.
 *
 * ```json
 * { "_moduleAliases": { "lib/util": "./vendor/util.js", "@app": "./src" } }
 * ```
 *
 * `import 'lib/util'` and `import './lib/util'` then resolve to ./vendor/util.js. A builtin by
 * its name is resolved by the loader before any hook runs, so an alias can never replace one.
 */
import * as std from 'std';

let aliases;

try {
  aliases = JSON.parse(std.loadFile('package.json') ?? 'null')?._moduleAliases;
} catch(e) {}

if(aliases && typeof aliases == 'object') {
  const has = Object.prototype.hasOwnProperty;

  registerHooks({
    resolve(specifier, context, nextResolve) {
      const key = specifier.replace(/^(\.\/)+/, '');

      if(has.call(aliases, key) && typeof aliases[key] == 'string') return nextResolve(aliases[key], context);

      return nextResolve(specifier, context);
    },
  });
}
