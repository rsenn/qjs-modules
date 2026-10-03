/* bun.js: Bun.plugin(), on top of registerHooks().
 * https://bun.com/docs/runtime/plugins
 * depends on: the qjsm global registerHooks (no imports, so it can be precompiled).
 * rule: plugins run in registration order and the first non-undefined result wins.
 *
 *   Bun.plugin({ name, setup(build) {} })
 *   build.onResolve({ filter, namespace? }, ({ path, importer, namespace, kind }) => { path, namespace? })
 *   build.onLoad({ filter, namespace? }, ({ path, namespace, loader }) => { contents?, loader?, exports? })
 *   build.module(specifier, () => ({ contents?, loader?, exports? }))
 *
 * loaders: js, json, text; the others (ts, tsx, jsx, toml, ...) need a
 * transpiler this engine does not have and throw TypeError.
 */

const resolvers = []; // { filter, namespace, callback }
const loaders = []; // { filter, namespace, callback }
const modules = new Map(); // specifier -> callback
const exportsKey = Symbol.for('qjsm.bun.exports');
const exportsStore = (globalThis[exportsKey] = new Map());
let hooks, nextExportsId = 0;

const NAMESPACE = /^([a-z][a-z0-9+.-]*):(.*)$/is;
const MODULE_SCHEME = 'bun-module';

/* url -> { namespace, path }; "file:///a/b.js" -> { 'file', '/a/b.js' },
   "yaml:./x.yaml" -> { 'yaml', './x.yaml' }, "./x.js" -> { 'file', './x.js' } */
function parseUrl(url) {
  if(url.startsWith('file://')) return { namespace: 'file', path: decodeURIComponent(url.slice(7)) };

  const m = NAMESPACE.exec(url);

  if(m && !/^(node|data|https?)$/i.test(m[1])) return { namespace: m[1], path: m[2] };

  return { namespace: 'file', path: url };
}

function toUrl(path, namespace) {
  if(namespace && namespace !== 'file') return `${namespace}:${path}`;

  return path.startsWith('/') ? 'file://' + encodeURI(path).replace(/[?#]/g, c => '%' + c.charCodeAt(0).toString(16).toUpperCase()) : path;
}

function loaderOf(path) {
  const m = /\.([a-z0-9]+)$/i.exec(path);
  const ext = m ? m[1].toLowerCase() : '';

  return ({ mjs: 'js', cjs: 'js', js: 'js', json: 'json', txt: 'text' })[ext] ?? ext;
}

function text(contents) {
  return typeof contents == 'string' ? contents : new TextDecoder().decode(contents);
}

/* an onLoad/module result as a load hook result */
function toLoadResult(result, path) {
  if('exports' in result) {
    const id = nextExportsId++;
    let source = `const e = globalThis[Symbol.for('qjsm.bun.exports')].get(${id});\n`;

    exportsStore.set(id, result.exports);

    for(const key of Object.keys(result.exports)) {
      if(key == 'default') source += 'export default e.default;\n';
      else if(/^[A-Za-z_$][\w$]*$/.test(key)) source += `export const ${key} = e[${JSON.stringify(key)}];\n`;
    }

    return { format: 'module', source, shortCircuit: true };
  }

  const loader = result.loader ?? loaderOf(path);

  switch (loader) {
    case 'js':
      return { format: 'module', source: result.contents, shortCircuit: true };
    case 'json':
      return { format: 'json', source: result.contents, shortCircuit: true };
    case 'text':
      return { format: 'module', source: `export default ${JSON.stringify(text(result.contents))};`, shortCircuit: true };
    default:
      throw new TypeError(`Bun.plugin: loader '${loader}' is not supported (for '${path}')`);
  }
}

function resolve(specifier, context, nextResolve) {
  const importer = context.parentURL ? parseUrl(context.parentURL) : { namespace: 'file', path: '' };

  if(modules.has(specifier)) return { url: `${MODULE_SCHEME}:${specifier}`, shortCircuit: true };

  for(const { filter, namespace, callback } of resolvers) {
    if(namespace !== importer.namespace || !filter.test(specifier)) continue;

    const result = callback({ path: specifier, importer: importer.path, namespace: importer.namespace, kind: 'import-statement' });

    if(result == null) continue;

    return { url: toUrl(result.path, result.namespace), shortCircuit: true };
  }

  return nextResolve(specifier, context);
}

function load(url, context, nextLoad) {
  if(url.startsWith(MODULE_SCHEME + ':')) {
    const result = modules.get(url.slice(MODULE_SCHEME.length + 1))();

    if(result != null) return toLoadResult(result, url);
  }

  const { namespace, path } = parseUrl(url);

  for(const entry of loaders) {
    if(entry.namespace !== namespace || !entry.filter.test(path)) continue;

    const result = entry.callback({ path, namespace, loader: loaderOf(path), defer: () => Promise.resolve() });

    if(result == null) continue;

    return toLoadResult(result, path);
  }

  return nextLoad(url, context);
}

function constraints(name, c, callback) {
  if(!(c?.filter instanceof RegExp)) throw new TypeError(`${name}: constraints must be { filter: RegExp, namespace?: string }`);
  if(typeof callback != 'function') throw new TypeError(`${name}: callback must be a function`);

  return { filter: c.filter, namespace: c.namespace ?? 'file', callback };
}

const build = {
  onResolve(c, callback) {
    resolvers.push(constraints('onResolve', c, callback));
    return this;
  },
  onLoad(c, callback) {
    loaders.push(constraints('onLoad', c, callback));
    return this;
  },
  module(specifier, callback) {
    if(typeof callback != 'function') throw new TypeError('module: callback must be a function');
    modules.set(specifier, callback);
    return this;
  },
  onStart() {
    return this;
  },
  onEnd() {
    return this;
  },
  onBeforeParse() {
    return this;
  },
};

/** Registers `plugin.setup(build)`; returns whatever setup returns (a Promise if async). */
export function plugin(p) {
  if(typeof p?.setup != 'function') throw new TypeError('Bun.plugin: expected { name?, setup(build) }');

  hooks ??= registerHooks({ resolve, load });

  return p.setup(build);
}

/** Removes every plugin registered so far. */
plugin.clearAll = function clearAll() {
  resolvers.length = loaders.length = 0;
  modules.clear();
  hooks?.deregister();
  hooks = undefined;
};

export default { plugin };
