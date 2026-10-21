/* loaders/packages.js: Node's ESM package resolution as a registerHooks()
 * resolve hook: node_modules, package.json exports, imports, main.
 * depends on: std (loadFile), os (stat, realpath, getcwd), the qjsm global
 * registerHooks. Only std and os: a compiled-in builtin imports builtins.
 * rule: the algorithm of nodejs.org/api/esm.html, error codes included,
 * on paths. Only the hook deals in URLs.
 *
 *   import 'pkg'            node_modules/pkg, its "exports" or "main"
 *   import '@scope/pkg/sub' node_modules/@scope/pkg, "exports" "./sub"
 *   import '#internal'      "imports" of the nearest package.json
 *   import 'self/x'         the package that holds the importer, by "name"
 *
 * A bare name no node_modules holds goes on to the next resolver, qjsm's
 * module search path. Conditions are the hook context's, qjsm passes node
 * and import, -C adds more, default always matches.
 */
import * as std from 'std';
import * as os from 'os';

const has = (o, k) => Object.prototype.hasOwnProperty.call(o, k);
const packages = new Map();

/* resolve: the registerHooks() resolve hook, Node's shape.
 *
 *   resolve(specifier, { parentURL, conditions }, nextResolve)
 *
 *   returns  { url, shortCircuit: true } for a # import or a package
 *            in node_modules, else what nextResolve returns
 *   throws   the errors of packageResolve and packageImportsResolve
 */
export function resolve(specifier, context, nextResolve) {
  if(specifier[0] == '#' || isBare(specifier)) {
    const parent = context.parentURL ? toPath(context.parentURL) : cwd() + '/[eval]';
    const conditions = context.conditions ?? ['node', 'import'];
    const file = specifier[0] == '#' ? packageImportsResolve(specifier, parent, conditions) : packageResolve(specifier, parent, conditions);

    if(file !== undefined) return { url: toURL(file), shortCircuit: true };
  }

  return nextResolve(specifier, context);
}

/* packageResolve: the file a bare specifier names, Node's PACKAGE_RESOLVE.
 *
 *   packageResolve('@jridgewell/sourcemap-codec', '/x/dist/index.mjs')
 *     // '/x/node_modules/@jridgewell/sourcemap-codec/dist/sourcemap-codec.mjs'
 *
 *   string    specifier   pkg, pkg/sub, @scope/pkg, @scope/pkg/sub
 *   string    parent      path of the importing file
 *   string[]  conditions  default ['node', 'import'], default always matches
 *
 *   returns  the path, undefined when no package of that name is found
 *   throws   TypeError ERR_INVALID_MODULE_SPECIFIER for a bad name,
 *            Error ERR_PACKAGE_PATH_NOT_EXPORTED, ERR_MODULE_NOT_FOUND
 */
export function packageResolve(specifier, parent, conditions = ['node', 'import']) {
  let slash = specifier.indexOf('/');

  if(specifier[0] == '@') slash = slash < 0 ? -2 : specifier.indexOf('/', slash + 1);

  const name = slash == -2 ? '' : slash < 0 ? specifier : specifier.slice(0, slash);
  const subpath = '.' + (slash < 0 ? '' : specifier.slice(slash));

  if(!name || name[0] == '.' || /[\\%]/.test(name) || subpath.endsWith('/'))
    throw fail(TypeError, 'ERR_INVALID_MODULE_SPECIFIER', `Invalid module "${specifier}" is not a valid package name imported from ${parent}`);

  /* the package of the importer, by its own name */
  const scope = lookupPackageScope(parent),
    own = scope && readPackageJson(scope);

  if(own?.name == name && own.exports != null) return packageExportsResolve(scope, subpath, own.exports, conditions, parent);

  for(let dir = dirname(parent); ; dir = dirname(dir)) {
    const pkg = join(dir, 'node_modules/' + name);

    if(kind(pkg) == 'dir') {
      const pjson = readPackageJson(pkg);

      if(pjson?.exports != null) return packageExportsResolve(pkg, subpath, pjson.exports, conditions, parent);
      if(subpath == '.') return legacyMainResolve(pkg, pjson, parent);

      return finalize(join(pkg, subpath));
    }

    if(dir == '/') return undefined;
  }
}

/* packageImportsResolve: the file of a #name specifier, from the "imports"
 * of the package.json nearest to parent, Node's PACKAGE_IMPORTS_RESOLVE.
 *
 *   returns  the path
 *   throws   TypeError ERR_PACKAGE_IMPORT_NOT_DEFINED when no entry
 *            matches, ERR_INVALID_MODULE_SPECIFIER for # and #/x
 */
export function packageImportsResolve(specifier, parent, conditions = ['node', 'import']) {
  if(specifier == '#' || specifier.startsWith('#/'))
    throw fail(TypeError, 'ERR_INVALID_MODULE_SPECIFIER', `Invalid module "${specifier}" is not a valid internal imports specifier name imported from ${parent}`);

  const scope = lookupPackageScope(parent),
    imports = scope && readPackageJson(scope).imports;

  if(imports && typeof imports == 'object') {
    const file = packageImportsExportsResolve(specifier, imports, scope, true, conditions, parent);

    if(file != null) return file;
  }

  throw fail(TypeError, 'ERR_PACKAGE_IMPORT_NOT_DEFINED', `Package import specifier "${specifier}" is not defined${scope ? ` in package ${scope}/package.json` : ''} imported from ${parent}`);
}

/* packageExportsResolve: the file a subpath ('.', './sub') of the package
 * in directory pkg maps to through its "exports", Node's
 * PACKAGE_EXPORTS_RESOLVE.
 *
 *   returns  the path
 *   throws   Error ERR_PACKAGE_PATH_NOT_EXPORTED when nothing matches
 */
export function packageExportsResolve(pkg, subpath, exports, conditions = ['node', 'import'], parent = pkg) {
  const keys = exports && typeof exports == 'object' && !Array.isArray(exports) ? Object.keys(exports) : [];
  const dotted = keys.filter(k => k[0] == '.').length;

  if(dotted && dotted != keys.length)
    throw fail(Error, 'ERR_INVALID_PACKAGE_CONFIG', `Invalid package config ${pkg}/package.json. "exports" cannot contain some keys starting with '.' and some not.`);

  let file;

  /* "exports": "./x.js", [...] and { import, default } are the main export */
  if(subpath == '.') {
    const main = dotted ? exports['.'] : exports;

    if(main !== undefined) file = packageTargetResolve(pkg, main, null, false, conditions, parent);
  } else if(dotted) {
    file = packageImportsExportsResolve(subpath, exports, pkg, false, conditions, parent);
  }

  if(file != null) return file;

  throw fail(
    Error,
    'ERR_PACKAGE_PATH_NOT_EXPORTED',
    subpath == '.' ? `No "exports" main defined in ${pkg}/package.json` : `Package subpath '${subpath}' is not defined by "exports" in ${pkg}/package.json imported from ${parent}`,
  );
}

/* legacyMainResolve: the main file of a package without "exports":
 * main, main.js, main.json, main/index.js, main/index.json, then index.js
 * and index.json. Node's list also has .node addons, which qjsm cannot load.
 *
 *   returns  the path
 *   throws   Error ERR_MODULE_NOT_FOUND when none of them is a file
 */
export function legacyMainResolve(pkg, pjson, parent = pkg) {
  const main = typeof pjson?.main == 'string' && pjson.main ? [pjson.main, '.js', '.json', '/index.js', '/index.json'].map((s, i) => (i ? pjson.main + s : s)) : [];

  for(const name of [...main, 'index.js', 'index.json']) {
    const file = join(pkg, name);

    if(kind(file) == 'file') return finalize(file);
  }

  throw fail(Error, 'ERR_MODULE_NOT_FOUND', `Cannot find package '${pkg}' imported from ${parent}`);
}

/* readPackageJson: the parsed package.json in dir, null when there is
 * none. Cached per directory for the life of the process.
 *
 *   throws  Error ERR_INVALID_PACKAGE_CONFIG for a file that is no JSON
 */
export function readPackageJson(dir) {
  if(packages.has(dir)) return packages.get(dir);

  const file = join(dir, 'package.json');
  let pjson = null;

  if(kind(file) == 'file') {
    try {
      pjson = JSON.parse(std.loadFile(file));
    } catch(e) {
      throw fail(Error, 'ERR_INVALID_PACKAGE_CONFIG', `Invalid package config ${file}: ${e.message}`);
    }
  }

  packages.set(dir, pjson);
  return pjson;
}

/* the directory of the package.json nearest above file, null at a
 * node_modules directory or the root, Node's LOOKUP_PACKAGE_SCOPE
 */
function lookupPackageScope(file) {
  for(let dir = dirname(file); ; dir = dirname(dir)) {
    if(basename(dir) == 'node_modules') return null;
    if(readPackageJson(dir)) return dir;
    if(dir == '/') return null;
  }
}

/* the entry of an "exports" or "imports" object that matchKey selects:
 * the key itself, else the longest pattern key with one *, whose match
 * replaces the * in the target. Returns null when none matches.
 */
function packageImportsExportsResolve(matchKey, matchObj, pkg, isImports, conditions, parent) {
  if(has(matchObj, matchKey) && !matchKey.includes('*')) return packageTargetResolve(pkg, matchObj[matchKey], null, isImports, conditions, parent);

  const patterns = Object.keys(matchObj)
    .filter(k => k.includes('*') && k.indexOf('*') == k.lastIndexOf('*'))
    .sort(patternKeyCompare);

  for(const key of patterns) {
    const star = key.indexOf('*'),
      base = key.slice(0, star),
      trailer = key.slice(star + 1);

    if(matchKey.startsWith(base) && matchKey != base && (!trailer || (matchKey.endsWith(trailer) && matchKey.length >= key.length)))
      return packageTargetResolve(pkg, matchObj[key], matchKey.slice(base.length, matchKey.length - trailer.length), isImports, conditions, parent);
  }

  return null;
}

/* pattern keys longest base first, then longest key: Node's PATTERN_KEY_COMPARE */
function patternKeyCompare(a, b) {
  const baseA = a.indexOf('*'),
    baseB = b.indexOf('*');

  if(baseA != baseB) return baseB - baseA;

  return b.length - a.length;
}

/* segments a target may not hold after its ./ : empty, . , .. and node_modules */
const INVALID_SEGMENT = /(^|[\\/])(\.\.?|%2e%2e?|\.%2e|%2e\.|node_modules|node%5fmodules)?([\\/]|$)/i;

/* a target of "exports" or "imports", Node's PACKAGE_TARGET_RESOLVE.
 *
 *   target                 resolves to
 *   "./x.js"               pkg/x.js, * replaced by patternMatch
 *   "dep" (imports only)   packageResolve of dep from pkg
 *   [a, b]                 the first that is not an invalid target
 *   { import, default }    the first key that is default or a condition
 *   null                   null, the path is not exported
 *
 * Returns undefined when no condition of an object matches.
 */
function packageTargetResolve(pkg, target, patternMatch, isImports, conditions, parent) {
  if(typeof target == 'string') {
    const subst = patternMatch == null ? target : target.replaceAll('*', patternMatch);

    if(!target.startsWith('./')) {
      if(!isImports || /^(\.\.?\/|\/|[a-z][a-z0-9+.-]*:)/i.test(target)) throw invalidTarget(pkg, target, isImports, parent);

      return packageResolve(subst, join(pkg, 'package.json'), conditions) ?? notFound(subst, parent);
    }

    if(INVALID_SEGMENT.test(target.slice(2))) throw invalidTarget(pkg, target, isImports, parent);
    if(patternMatch != null && INVALID_SEGMENT.test(patternMatch))
      throw fail(TypeError, 'ERR_INVALID_MODULE_SPECIFIER', `Invalid module "${patternMatch}" request is not a valid match in pattern "${target}" in ${pkg}/package.json`);

    return finalize(join(pkg, subst.slice(2)));
  }

  if(Array.isArray(target)) {
    let last;

    for(const t of target) {
      try {
        const file = packageTargetResolve(pkg, t, patternMatch, isImports, conditions, parent);

        if(file !== undefined) return file;
      } catch(e) {
        if(e.code != 'ERR_INVALID_PACKAGE_TARGET') throw e;

        last = e;
      }
    }

    if(last) throw last;

    return null;
  }

  if(target && typeof target == 'object') {
    for(const key of Object.keys(target)) {
      if(key != 'default' && !conditions.includes(key)) continue;

      const file = packageTargetResolve(pkg, target[key], patternMatch, isImports, conditions, parent);

      if(file !== undefined) return file;
    }

    return undefined;
  }

  if(target === null) return null;

  throw invalidTarget(pkg, target, isImports, parent);
}

/* a resolved file through symlinks, as Node does; a missing one as is,
 * import.meta.resolve() reports it and the load fails
 */
function finalize(file) {
  const [real, err] = os.realpath(file);

  return err ? file : real;
}

function invalidTarget(pkg, target, isImports, parent) {
  return fail(
    TypeError,
    'ERR_INVALID_PACKAGE_TARGET',
    `Invalid "${isImports ? 'imports' : 'exports'}" target ${JSON.stringify(target)} in the package config ${pkg}/package.json imported from ${parent}`,
  );
}

function notFound(name, parent) {
  throw fail(Error, 'ERR_MODULE_NOT_FOUND', `Cannot find package '${name}' imported from ${parent}`);
}

function fail(Type, code, message) {
  const e = new Type(message);

  e.code = code;
  return e;
}

/* pkg, @scope/pkg/x: not ./x, ../x, /x, a URL or node:fs */
function isBare(specifier) {
  return !/^(\.\.?(\/|$)|\/|[a-z][a-z0-9+.-]*:)/i.test(specifier);
}

/* 'file' | 'dir' | undefined */
function kind(p) {
  const [st, err] = os.stat(p);

  if(!err) return (st.mode & os.S_IFMT) == os.S_IFDIR ? 'dir' : 'file';
}

function cwd() {
  return os.getcwd()[0];
}

/* file:///a%20b.js and /a b.js, escaped as qjsm's loader does */
function toPath(url) {
  return url.startsWith('file://') ? decodeURIComponent(url.slice(7)) : url;
}

function toURL(file) {
  return 'file://' + file.replace(/[\x00-\x20%#?\x7f]/g, c => '%' + c.charCodeAt(0).toString(16).toUpperCase().padStart(2, '0'));
}

function dirname(p) {
  const i = p.lastIndexOf('/');

  return i <= 0 ? '/' : p.slice(0, i);
}

function basename(p) {
  return p.slice(p.lastIndexOf('/') + 1);
}

/* dir and a relative path, . and .. resolved: join('/a/b', '../c') is /a/c */
function join(dir, rel) {
  const out = [];

  for(const part of (dir + '/' + rel).split('/'))
    if(part == '..') out.pop();
    else if(part && part != '.') out.push(part);

  return '/' + out.join('/');
}

registerHooks({ resolve });

export default resolve;
