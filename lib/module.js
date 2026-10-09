/* module.js: Node's `node:module` (https://nodejs.org/api/module.html).
 * depends on: fs, path (builtins), the qjsm globals builtins/registerHooks/evalBuf.
 * rule: real behavior or a documented refusal, no pretend success.
 *
 * `import ... from 'node:module'` resolves here too (lib/nodeHooks.js maps `node:` to the bare name).
 * Not supported: native addons (.node), async register() hooks, compile cache,
 * stack-trace source maps (SourceMap itself works). createRequire() is backed by
 * the compiled-in `require` builtin (lib/require.js), loaded on first use.
 */
import * as fs from 'fs';

const env = () => globalThis.process?.env ?? {};
const isString = v => typeof v == 'string';

/* posix path helpers (the path builtin mishandles the root: resolve('/') and dirname('/x')) */
const path = {
  resolve(...parts) {
    let result = '';

    for(let i = parts.length - 1; i >= 0 && result[0] != '/'; i--) if(parts[i]) result = result ? parts[i] + '/' + result : parts[i];

    if(result[0] != '/') result = (globalThis.process?.cwd() ?? '/') + '/' + result;

    const out = [];

    for(const part of result.split('/'))
      if(part == '..') out.pop();
      else if(part && part != '.') out.push(part);

    return '/' + out.join('/');
  },
  dirname(p) {
    const i = p.replace(/\/+$/, '').lastIndexOf('/');

    return i < 0 ? '.' : i == 0 ? '/' : p.slice(0, i);
  },
  basename: p => p.replace(/\/+$/, '').slice(p.replace(/\/+$/, '').lastIndexOf('/') + 1),
  extname(p) {
    const base = path.basename(p);
    const i = base.lastIndexOf('.');

    return i > 0 ? base.slice(i) : '';
  },
  join: (...parts) => path.resolve(...parts),
};

function fail(Type, code, message) {
  const error = new Type(message);

  error.code = code;
  return error;
}

function toPath(v) {
  if(v && typeof v == 'object' && isString(v.href)) v = v.href;
  if(isString(v) && v.startsWith('file://')) return decodeURIComponent(v.slice(7));
  return v;
}

/* --- builtins --- */

const SUBPATHS = { fsPromises: 'fs/promises', timersPromises: 'timers/promises', readlinePromises: 'readline/promises' };

export const builtinModules = (globalThis.builtins ?? []).map(b => SUBPATHS[b.name] ?? b.name).sort();

export function isBuiltin(name) {
  if(!isString(name)) return false;
  if(name.startsWith('node:')) name = name.slice(5);
  return builtinModules.includes(name);
}

/* --- constants --- */

export const constants = { compileCacheStatus: { FAILED: 0, ENABLED: 1, ALREADY_ENABLED: 2, DISABLED: 3 } };

/* --- filesystem helpers --- */

/* 0 file, 1 directory, -2 missing */
function stat(filename) {
  try {
    return fs.statSync(filename).isDirectory() ? 1 : 0;
  } catch(e) {
    return -2;
  }
}

function readPackage(dir) {
  try {
    return { exists: true, ...JSON.parse(fs.readFileSync(path.join(dir, 'package.json'), 'utf8')) };
  } catch(e) {
    return { exists: false };
  }
}

function realpath(filename) {
  try {
    return fs.realpathSync(filename);
  } catch(e) {
    return filename;
  }
}

/* --- Module --- */

const parents = new WeakMap();
const wrapper = ['(function (exports, require, module, __filename, __dirname) { ', '\n});'];
let statFn = stat;
let readPackageFn = readPackage;
let debugLog;

function updateChildren(parent, child) {
  const children = parent?.children;

  if(children && !children.includes(child)) children.push(child);
}

function debug(...args) {
  debugLog ??= (env().NODE_DEBUG ?? '').split(',').includes('module');
  if(debugLog) console.error('MODULE %d:', globalThis.process?.pid, ...args);
}

function nodeModulePaths(from) {
  const parts = path.resolve(from).split('/').filter(Boolean);
  const paths = [];

  for(let i = parts.length; i > 0; i--) if(parts[i - 1] != 'node_modules') paths.push('/' + parts.slice(0, i).join('/') + '/node_modules');

  paths.push('/node_modules');
  return paths;
}

function initPaths() {
  const home = env().HOME;
  const execPath = globalThis.process?.execPath;
  const prefix = execPath ? path.dirname(path.dirname(execPath)) : '/usr/local';
  const paths = [path.resolve(prefix, 'lib', 'node')];

  if(home) paths.unshift(path.resolve(home, '.node_libraries'), path.resolve(home, '.node_modules'));
  if(env().NODE_PATH) paths.unshift(...env().NODE_PATH.split(':').filter(Boolean));

  globalPaths.length = 0;
  globalPaths.push(...paths);
}

export const globalPaths = [];

function tryFile(filename) {
  return statFn(filename) == 0 ? realpath(filename) : false;
}

function tryExtensions(base, exts) {
  for(const ext of exts) {
    const found = tryFile(base + ext);

    if(found) return found;
  }
  return false;
}

function tryPackage(dir, exts) {
  const main = readPackageFn(dir).main;

  if(isString(main) && main) {
    const target = path.resolve(dir, main);
    const found = tryFile(target) || tryExtensions(target, exts) || tryExtensions(path.join(target, 'index'), exts);

    if(found) return found;
  }

  return tryExtensions(path.join(dir, 'index'), exts);
}

function findPath(request, paths, isMain) {
  const absolute = request[0] == '/';

  if(absolute) paths = [''];
  else if(!paths?.length) return false;

  const key = request + '\x00' + paths.join('\x00');
  const cached = Module._pathCache[key];

  if(cached) return cached;

  const exts = Object.keys(Module._extensions);
  const trailingSlash = request.endsWith('/');

  for(const dir of paths) {
    if(dir && statFn(dir) != 1) continue;

    const base = path.resolve(dir, request);
    let found = false;

    if(!trailingSlash) {
      const s = statFn(base);

      if(s == 0) found = realpath(base);
      else if(s == 1) found = tryPackage(base, exts);

      found ||= tryExtensions(base, exts);
    } else if(statFn(base) == 1) {
      found = tryPackage(base, exts);
    }

    if(found) {
      Module._pathCache[key] = found;
      return found;
    }
  }

  return false;
}

function resolveLookupPaths(request, parent) {
  if(isBuiltin(request)) return null;

  const relative = request == '.' || request == '..' || request.startsWith('./') || request.startsWith('../');

  if(!relative) {
    const paths = [...(parent?.paths ?? []), ...globalPaths];

    return paths.length ? paths : null;
  }

  if(!parent?.id || !parent.filename) return ['.'];

  return [path.dirname(parent.filename)];
}

function resolveFilename(request, parent, isMain, options) {
  if(isBuiltin(request)) return request;

  let paths;

  if(options && typeof options == 'object' && Array.isArray(options.paths)) {
    const relative = request.startsWith('./') || request.startsWith('../') || request == '.' || request == '..';

    paths = relative ? options.paths : options.paths.flatMap(p => [p, ...nodeModulePaths(p)]);
  } else {
    paths = resolveLookupPaths(request, parent);
  }

  const filename = findPath(request, paths, isMain);

  if(filename) return filename;

  const requireStack = [];

  for(let cursor = parent; cursor; cursor = cursor.parent) requireStack.push(cursor.filename || cursor.id);

  let message = `Cannot find module '${request}'`;

  if(requireStack.length) message += '\nRequire stack:\n- ' + requireStack.join('\n- ');

  const error = fail(Error, 'MODULE_NOT_FOUND', message);

  error.requireStack = requireStack;
  throw error;
}

function load(request, parent, isMain) {
  if(isBuiltin(request)) return createRequire(parent?.filename || path.join(globalThis.process?.cwd() ?? '/', 'x'))(request);

  const filename = resolveFilename(request, parent, isMain);
  const cached = Module._cache[filename];

  if(cached) {
    updateChildren(parent, cached);
    return cached.exports;
  }

  const module = new Module(filename, parent);

  if(isMain) {
    module.id = '.';
    if(globalThis.process) globalThis.process.mainModule = module;
  }

  Module._cache[filename] = module;

  try {
    module.load(filename);
  } catch(e) {
    delete Module._cache[filename];

    const index = parent?.children?.indexOf(module) ?? -1;

    if(index != -1) parent.children.splice(index, 1);

    throw e;
  }

  return module.exports;
}

function makeRequire(module) {
  const require = id => {
    if(!isString(id) || id === '') throw fail(TypeError, 'ERR_INVALID_ARG_VALUE', `The argument 'id' must be a non-empty string. Received ${JSON.stringify(id)}`);
    return module.require(id);
  };

  require.resolve = (request, options) => resolveFilename(request, module, false, options);
  require.resolve.paths = request => resolveLookupPaths(request, module);
  require.main = globalThis.process?.mainModule;
  require.extensions = Module._extensions;
  require.cache = Module._cache;
  return require;
}

function stripBOM(content) {
  return content.charCodeAt(0) == 0xfeff ? content.slice(1) : content;
}

export class Module {
  constructor(id = '', parent) {
    this.id = id;
    this.path = path.dirname(id);
    this.exports = {};
    this.filename = null;
    this.loaded = false;
    this.children = [];
    this.paths = undefined;
    parents.set(this, parent);
    updateChildren(parent, this);
  }

  get parent() {
    return parents.get(this);
  }

  set parent(value) {
    parents.set(this, value);
  }

  get isPreloading() {
    return false;
  }

  load(filename) {
    this.filename = filename;
    this.paths = nodeModulePaths(path.dirname(filename));

    const base = path.basename(filename);
    let ext = path.extname(filename);

    for(let i = base.indexOf('.', 1); i != -1; i = base.indexOf('.', i + 1)) {
      const candidate = base.slice(i);

      if(Module._extensions[candidate]) {
        ext = candidate;
        break;
      }
    }

    Module._extensions[Module._extensions[ext] ? ext : '.js'](this, filename);
    this.loaded = true;
  }

  require(id) {
    if(!isString(id)) throw fail(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "id" argument must be of type string.');
    if(id === '') throw fail(TypeError, 'ERR_INVALID_ARG_VALUE', "The argument 'id' must be a non-empty string. Received ''");
    return Module._load(id, this, false);
  }

  _compile(content, filename, format) {
    content = stripBOM(content);

    if(content.startsWith('#!')) content = '//' + content;

    const source = Module.wrap(content);
    const fn = globalThis.evalBuf ? globalThis.evalBuf(source, filename) : (0, eval)(source);
    const dirname = path.dirname(filename);

    return fn.call(this.exports, this.exports, makeRequire(this), this, filename, dirname);
  }
}

Object.defineProperty(Module, 'Module', { value: Module, writable: true, configurable: true });

Module._cache = { __proto__: null };
Module._pathCache = { __proto__: null };
Module._extensions = {
  __proto__: null,
  '.js'(module, filename) {
    module._compile(fs.readFileSync(filename, 'utf8'), filename);
  },
  '.json'(module, filename) {
    try {
      module.exports = JSON.parse(stripBOM(fs.readFileSync(filename, 'utf8')));
    } catch(e) {
      e.message = filename + ': ' + e.message;
      throw e;
    }
  },
  '.node'(module, filename) {
    throw fail(Error, 'ERR_DLOPEN_FAILED', `native addons are not supported: ${filename}`);
  },
};

Module.globalPaths = globalPaths;
Module.builtinModules = builtinModules;
Module.constants = constants;
Module.wrapper = wrapper;
Module.wrap = script => wrapper[0] + script + wrapper[1];
Module.isBuiltin = isBuiltin;

Object.defineProperties(Module, {
  _stat: { get: () => statFn, set: v => (statFn = v), enumerable: true, configurable: true },
  _readPackage: { get: () => readPackageFn, set: v => (readPackageFn = v), enumerable: true, configurable: true },
});

export const _cache = Module._cache;
export const _pathCache = Module._pathCache;
export const _extensions = Module._extensions;
export const _debug = (Module._debug = debug);
export const _nodeModulePaths = (Module._nodeModulePaths = nodeModulePaths);
export const _initPaths = (Module._initPaths = initPaths);
export const _findPath = (Module._findPath = findPath);
export const _resolveLookupPaths = (Module._resolveLookupPaths = resolveLookupPaths);
export const _resolveFilename = (Module._resolveFilename = resolveFilename);
export const _load = (Module._load = load);

/** Module._preloadModules(requests): requires each request as if from the cwd. */
export function _preloadModules(requests) {
  if(!Array.isArray(requests)) return;

  const parent = new Module('internal/preload', null);

  parent.paths = nodeModulePaths(globalThis.process?.cwd() ?? '/');

  for(const request of requests) parent.require(request);
}
Module._preloadModules = _preloadModules;

initPaths();

/* --- createRequire --- */

/**
 * Node's `module.createRequire(filename)`: a `require()` resolving relative to
 * `filename` (a path, `file://` URL string or URL), with require.js's full lookup
 * (`node_modules`, `package.json`, `.json`, `.js`).
 *
 * ```js
 * const require = createRequire(import.meta.url);
 * const cfg = require('./config.json');
 * ```
 */
export function createRequire(filename) {
  filename = toPath(filename);

  if(!isString(filename)) throw new TypeError('createRequire: filename must be a string or file: URL');

  return function required(id) {
    const { default: require } = globalThis.requireModule('require');
    const saved = require.filename;

    require.filename = filename;

    try {
      return require(id);
    } finally {
      require.filename = saved;
    }
  };
}
Module.createRequire = createRequire;

/* --- hooks --- */

/** Node's `module.registerHooks()`: the qjsm global, `undefined` on other engines. */
export const registerHooks = globalThis.registerHooks;
Module.registerHooks = registerHooks;

/** Node's deprecated async `module.register()`; hooks run off-thread there, which this engine has no equivalent for. */
export function register(specifier, parentURL, options) {
  throw fail(Error, 'ERR_FEATURE_UNAVAILABLE', 'module.register() is not supported; use module.registerHooks()');
}
Module.register = register;

/** The builtin ES namespaces here are the module exports themselves, so there is nothing to sync. */
export function syncBuiltinESMExports() {}
Module.syncBuiltinESMExports = syncBuiltinESMExports;

/** Runs `main` (default: the script path) as the entry module. */
export function runMain(main = globalThis.process?.argv?.[1] ?? globalThis.scriptArgs?.[0]) {
  return Module._load(path.resolve(toPath(main)), null, true);
}
Module.runMain = runMain;

/* --- findPackageJSON --- */

/** Node's `module.findPackageJSON(specifier, base)`: path of the package.json for `specifier`, or undefined. */
export function findPackageJSON(specifier, base) {
  specifier = toPath(specifier);
  base = base === undefined ? undefined : toPath(base);

  if(!isString(specifier)) throw fail(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "specifier" argument must be of type string or an instance of URL.');

  const bare = !(specifier[0] == '.' || specifier[0] == '/');

  if(specifier[0] != '/' && !isString(base)) throw fail(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "base" argument must be provided for a non-absolute specifier.');

  const baseDir = base ? (statFn(base) == 1 ? base : path.dirname(base)) : '/';

  if(bare) {
    const [first, second] = specifier.split('/');
    const name = first[0] == '@' ? first + '/' + second : first;

    for(const dir of nodeModulePaths(baseDir)) {
      const file = path.join(dir, name, 'package.json');

      if(statFn(file) == 0) return file;
    }

    return undefined;
  }

  let dir = path.resolve(baseDir, specifier);

  if(statFn(dir) != 1) dir = path.dirname(dir);

  for(;;) {
    const file = path.join(dir, 'package.json');

    if(statFn(file) == 0) return file;

    const up = path.dirname(dir);

    if(up == dir) return undefined;
    dir = up;
  }
}
Module.findPackageJSON = findPackageJSON;

/* --- TypeScript --- */

/** Node's `module.stripTypeScriptTypes(code, options)`: strips types with the external `swc` CLI (see lib/typescript.js). */
export function stripTypeScriptTypes(code, options = {}) {
  if(!isString(code)) throw fail(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "code" argument must be of type string.');
  if((options.mode ?? 'strip') != 'strip') throw fail(Error, 'ERR_FEATURE_UNAVAILABLE', "only mode 'strip' is supported");

  const std = globalThis.requireModule('std');
  const file = `/tmp/qjsm-strip-${globalThis.process?.pid ?? 0}-${Date.now()}.ts`;
  const out = std.open(file, 'w');

  if(!out) throw fail(Error, 'ERR_FEATURE_UNAVAILABLE', 'cannot write a temporary file');

  out.puts(code);
  out.close();

  try {
    const proc = std.popen(`swc -C jsc.target=es2022 -C jsc.parser.syntax=typescript '${file}' 2>/dev/null`, 'r');
    const result = proc?.readAsString();

    if(!proc || proc.close() != 0 || !result) throw fail(Error, 'ERR_FEATURE_UNAVAILABLE', 'stripping types needs the swc CLI (npm i -g @swc/cli @swc/core)');

    return options.sourceUrl ? `${result}\n\n//# sourceURL=${options.sourceUrl}` : result;
  } finally {
    globalThis.requireModule('os').remove(file);
  }
}
Module.stripTypeScriptTypes = stripTypeScriptTypes;

/* --- compile cache (not supported) --- */

/** No code cache in this engine: reports FAILED with a message, never throws. */
export function enableCompileCache(options) {
  return { status: constants.compileCacheStatus.FAILED, message: 'the module compile cache is not supported' };
}

export function flushCompileCache() {}

export function getCompileCacheDir() {
  return undefined;
}

Module.enableCompileCache = enableCompileCache;
Module.flushCompileCache = flushCompileCache;
Module.getCompileCacheDir = getCompileCacheDir;

/* --- source maps --- */

const BASE64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
const sourceMapsSupport = { enabled: false, nodeModules: false, generatedCode: false };
const sourceMapCache = new Map();

function decodeMappings(mappings, sources, names, sourceRoot) {
  const entries = [];
  let line = 0;
  let srcIndex = 0;
  let srcLine = 0;
  let srcColumn = 0;
  let nameIndex = 0;
  let name;

  for(const group of mappings.split(';')) {
    let column = 0;

    for(const segment of group.split(',')) {
      if(!segment) continue;

      const fields = [];

      for(let i = 0, shift = 0, value = 0; i < segment.length; i++) {
        const digit = BASE64.indexOf(segment[i]);

        value += (digit & 31) << shift;

        if(digit & 32) {
          shift += 5;
        } else {
          fields.push(value & 1 ? -(value >> 1) : value >> 1);
          shift = 0;
          value = 0;
        }
      }

      column += fields[0];

      const entry = { generatedLine: line, generatedColumn: column };

      if(fields.length > 1) {
        srcIndex += fields[1];
        srcLine += fields[2];
        srcColumn += fields[3];
        entry.originalSource = (sourceRoot ?? '') + sources[srcIndex];
        entry.originalLine = srcLine;
        entry.originalColumn = srcColumn;

        if(fields.length > 4) {
          nameIndex += fields[4];
          name = names[nameIndex];
        }

        entry.name = name;
      }

      entries.push(entry);
    }

    line++;
  }

  return entries;
}

/**
 * Node's `module.SourceMap`: a parsed Source Map v3.
 *
 * ```js
 * const map = new SourceMap(payload);
 * map.findEntry(0, 5);      // { generatedLine, generatedColumn, originalSource, originalLine, originalColumn, name }
 * map.findOrigin(1, 6);     // { name, fileName, lineNumber, columnNumber }  (1-indexed)
 * ```
 */
export class SourceMap {
  #payload;
  #lineLengths;
  #entries;

  constructor(payload, { lineLengths } = {}) {
    if(!payload || typeof payload != 'object') throw fail(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "payload" argument must be of type object.');

    this.#payload = JSON.parse(JSON.stringify(payload));
    this.#lineLengths = lineLengths && [...lineLengths];

    const { mappings = '', sources = [], names = [], sourceRoot } = this.#payload;

    this.#entries = decodeMappings(mappings, sources, names, sourceRoot);
  }

  get payload() {
    return JSON.parse(JSON.stringify(this.#payload));
  }

  get lineLengths() {
    return this.#lineLengths && [...this.#lineLengths];
  }

  findEntry(lineOffset, columnOffset) {
    const entries = this.#entries;
    let lo = 0;
    let hi = entries.length - 1;
    let best = -1;

    while(lo <= hi) {
      const mid = (lo + hi) >> 1;
      const e = entries[mid];

      if(e.generatedLine < lineOffset || (e.generatedLine == lineOffset && e.generatedColumn <= columnOffset)) {
        best = mid;
        lo = mid + 1;
      } else {
        hi = mid - 1;
      }
    }

    const entry = entries[best];

    if(!entry || entry.originalSource === undefined) return {};

    const { generatedLine, generatedColumn, originalSource, originalLine, originalColumn, name } = entry;

    return { generatedLine, generatedColumn, originalSource, originalLine, originalColumn, name };
  }

  findOrigin(lineNumber, columnNumber) {
    const entry = this.findEntry(lineNumber - 1, columnNumber - 1);

    if(entry.originalSource === undefined) return {};

    /* the call site is offset from the mapped range's start; carry that offset over */
    return {
      name: entry.name,
      fileName: entry.originalSource,
      lineNumber: entry.originalLine + 1 + (lineNumber - 1 - entry.generatedLine),
      columnNumber: entry.originalColumn + 1 + (columnNumber - 1 - entry.generatedColumn),
    };
  }
}

/** Node's `module.getSourceMapsSupport()`; the flags are recorded, no stack trace uses them. */
export function getSourceMapsSupport() {
  return { ...sourceMapsSupport };
}

export function setSourceMapsSupport(enabled, options = {}) {
  if(typeof enabled != 'boolean') throw fail(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "enabled" argument must be of type boolean.');

  sourceMapsSupport.enabled = enabled;
  sourceMapsSupport.nodeModules = !!options.nodeModules;
  sourceMapsSupport.generatedCode = !!options.generatedCode;
}

/** Node's `module.findSourceMap(path)`: the map named by the file's `//# sourceMappingURL=` (a data: URL or a path), once support is enabled. */
export function findSourceMap(file) {
  if(!sourceMapsSupport.enabled || !isString(file)) return undefined;
  if(sourceMapCache.has(file)) return sourceMapCache.get(file);

  let map;

  try {
    const match = [...fs.readFileSync(file, 'utf8').matchAll(/^\/\/[#@] sourceMappingURL=(.+)$/gm)].pop();

    if(match) {
      const url = match[1].trim();
      const data = url.match(/^data:application\/json[^,]*;base64,(.*)$/);
      const json = data ? atob(data[1]) : fs.readFileSync(path.resolve(path.dirname(file), toPath(url)), 'utf8');

      map = new SourceMap(JSON.parse(json));
    }
  } catch(e) {}

  sourceMapCache.set(file, map);
  return map;
}

Module.SourceMap = SourceMap;
Module.getSourceMapsSupport = getSourceMapsSupport;
Module.setSourceMapsSupport = setSourceMapsSupport;
Module.findSourceMap = findSourceMap;

export default Module;
