import { realpath, S_IFDIR, S_IFREG, stat } from 'os';
import { dirname, join, normalize } from 'path';
import { define, memoize } from 'util';
import { evalScript, getenv, loadFile, puts } from 'std';
import { argv } from 'process';
import NODE from 'loaders/nodeNames';

let modules = {};
let paths = [];
let required = [];

let getPackagePaths;

let debug = arg => puts(arg + '\n');

{
  let debugOptions = getenv('DEBUG');
  if(typeof debugOptions == 'undefined' || debugOptions.split(',').indexOf('require') === -1) {
    debug = function() {};
  }
}

function Stack(set, get) {
  let stack = [];
  stack.push = function(value) {
    let top = get();
    if(top) Array.prototype.push.call(this, top);
    set(value);
    return this;
  };
  stack.pop = function() {
    if(this.length) {
      let value = Array.prototype.pop.call(this);
      set(value);
    } else if(get() !== undefined) {
      set(undefined);
    }
    return this;
  };

  Object.defineProperties(stack, {
    array: {
      get() {
        return [...this].concat([get()]);
      },
    },
    top: { get },
    value: {
      get() {
        let value = [...this];
        if(get() !== undefined) value.push(get());

        return value;
      },
    },
    size: {
      get() {
        return this.value.length;
      },
    },
  });
  return stack;
}

const filenameStack = new Stack(
  value => (require.filename = value),
  () => require.filename,
);
const exportsStack = new Stack(
  value => (globalThis.exports = value),
  () => globalThis.exports,
);
const moduleStack = new Stack(
  value => ((globalThis.module = value), value?.exports ? (globalThis.exports = value.exports) : delete globalThis.exports),
  () => globalThis.module,
);

if(argv[1]) filenameStack.push(argv[1]);

class Module {
  constructor(id, m) {
    this.id = id;
    this.path = m;
    this._failed = null;
    this._loaded = false;
    this.exports = {};
  }
}

define(Module.prototype, { [Symbol.toStringTag]: 'Module' });

class CJSModule extends Module {
  load() {
    const __file = this.path ?? this.id;
    const __dir = dirname(__file);
    const _require = require;

    let ctx = { exports: {} };
    // Prevents modules from changing exports
    // Object.seal(ctx);

    //const _mark = '##SCRIPT##';
    //let _loaderTemplate = _mark => `(function _loader (exports, require, module) {${_mark}})(ctx.exports, _require, ctx);`;
    let _loaderTemplate = _mark => new Function('exports', 'require', 'module', `${_mark}`);

    let _script = loadFile(__file);
    this._failed = _script === null;
    if(this._failed) return new Error(`Can't load script ${__file}`);

    let error;
    _script = _loaderTemplate(_script);
    //debug('load', _script.slice(-100));

    filenameStack.push(__file);
    moduleStack.push(ctx);
    try {
      _script(ctx.exports, _require, ctx);
    } catch(e) {
      error = e;
    }
    moduleStack.pop();
    filenameStack.pop();

    if(error) throw error;

    this.exports = ctx.exports;
    this._loaded = true;
    return true;
  }
}

define(CJSModule.prototype, { [Symbol.toStringTag]: 'CJSModule' });

class ESModule extends Module {
  load() {
    const __file = this.path ?? this.id;
    const __dir = dirname(__file);
    const _require = require;

    let ctx = { exports: {} };

    let error;

    filenameStack.push(__file);
    moduleStack.push(ctx);
    try {
      let m = loadModule(__file);
      const exports = getModuleNS(m);

      ctx.exports = exports;
    } catch(e) {
      error = e;
    }
    moduleStack.pop();
    filenameStack.pop();

    if(error) throw error;

    this.exports = ctx.exports;
    this._loaded = true;
    return true;
  }
}

define(ESModule.prototype, { [Symbol.toStringTag]: 'ESModule' });

function statPath(path) {
  const [fstat, err] = stat(path);
  let ret = {
    errno: err,
    isFile: fstat && fstat.mode & S_IFREG && true,
    isDir: fstat && fstat.mode & S_IFDIR && true,
  };

  // if(!err) debug(`statPath('${path}') ${err ? '-1' : ret.isFile ? 'file' : ret.isDir ? 'dir' : ''}`);

  return ret;
}

function dirExists(path) {
  let st = statPath(path);
  return st?.isDir;
}

function fileExists(path) {
  let st = statPath(path);
  return st?.isFile;
}

function exists(path) {
  let st = statPath(path);
  return st?.isFile || st?.isDir;
}

function loadJSON(path) {
  let data;
  try {
    let json = loadFile(path, 'utf-8');
    data = JSON.parse(json);
  } catch(e) {
    throw e;
  }
  return data;
}

/*function lookupPackage(name) {
  debug(`lookupPackage(${name})#`);
  let pkg = findPackage(name);
  let json = loadFile(pkg + '/package.json', 'utf-8');
  let data = JSON.parse(json ?? '{}');
  if(data.main)
    return data.main.startsWith('.') ? pkg + data.main.slice(1) : pkg + '/' + data.main;
}*/

function requireModule(m) {
  debug(`requireModule[${moduleStack.size}]# Module ${m}`);
  const [id, err] = realpath(m);
  if(err) {
    throw new Error(`Module require error: Can't get real module m for ${m}`);
    return;
  }

  let module;

  debug(`requireModule[${moduleStack.size}]# id ${id}`);
  if(modules.hasOwnProperty(id)) {
    module = modules[id];
    debug(`requireModule[${moduleStack.size}]# ${module} (cached)`);
  } else {
    modules[id] = module = new CJSModule(id, m);
    required.push(m);
    let result;
    try {
      result = module.load();
      debug(`requireModule[${moduleStack.size}]# ${m} (CJS) ${result}`);
    } catch(e) {
      debug(`requireModule[${moduleStack.size}]# ${m} \x1b[1;31mERROR\x1b[0m ${e.message + '\n' + e.stack}`);

      /* only source that does not compile as CommonJS is retried as a
         module, an error the module throws reaches the caller as is */
      if(!(e instanceof SyntaxError)) {
        delete modules[id];
        required.pop();
        throw e;
      }

      modules[id] = module = new ESModule(id, m);

      try {
        result = module.load();
      } catch(e) {
        delete modules[id];
        required.pop();
        throw e;
      }

      debug(`requireModule[${moduleStack.size}]# ${m} (ESM) ${result}`);
    }
    required.pop();
    if(result !== true) {
      debug(`requireModule[${moduleStack.size}]# error ${result}`);
      throw result;
      return;
    }
  }
  debug(`requireModule[${moduleStack.size}]# success path: ${m}`);
  return module;
}

function packagePaths(p) {
  let add = dir => {
    if(paths.indexOf(dir) == -1) {
      paths.push(dir);
      debug(`packagePaths(${p})# ${paths}`);
    }
  };

  for(let dir = p, prev; dir && dir != '/' && dir !== prev; prev = dir, dir = dirname(dir)) {
    let fstat = statPath(dir + '/package.json');

    if(fstat.isFile) {
      debug(`packagePaths(${p})# package.json in ${dir}`);
      for(const subdir of ['quickjs_modules', 'node_modules']) {
        let dstat = statPath(dir + '/' + subdir);
        if(dstat.isDir) add(dir + '/' + subdir);
      }
      break;
    }
  }
  debug(`packagePaths(${p})# paths=${paths}`);
  return paths;
}

/* the node_modules directories a bare name is searched in, nearest first,
 * Node's NODE_MODULES_PATHS: nodeModulesPaths('/a/b') is
 * ['/a/b/node_modules', '/a/node_modules', '/node_modules'], the ones that exist */
function nodeModulesPaths(dir) {
  const out = [];

  for(let d = dir, prev; d && d !== prev; prev = d, d = dirname(d)) if(!d.endsWith('/node_modules') && dirExists(d + '/node_modules')) out.push((d == '/' ? '' : d) + '/node_modules');

  return out;
}

/* the file m names, Error when there is none; a bare m is searched in
 * the node_modules directories above dir, the requiring file's directory */
function lookupModule(m, dir) {
  let paths,
    fstat = statPath(m);

  //if(fstat.isDir) m += '/index';

  debug(`lookupModule# Looking for ${m}`);

  // Path found
  if(fstat.isFile) {
    debug(`lookupModule# Found module file '${m}'`);
    return m;
  }

  if(m[0] == '@' && false) {
    let f = 'packages/' + m.slice(1).replace(/\//g, '-');

    /*    if(fileExists(f)) */ m = lookupModule(f);
    fstat = statPath(m);
  } else if(m[0] != '/' && m[0] != '.') {
    for(const p of nodeModulesPaths(dir)) {
      const f = p + '/' + m;

      if(fileExists(f)) return f;
      if(fileExists(f + '.js')) return f + '.js';

      /* a package directory: package.json main or index.js, below */
      if(dirExists(f)) {
        m = f;
        fstat = statPath(m);
        break;
      }
    }
  }

  // Path not found
  if(fstat.errno) {
    debug(`lookupModule# Not found module file '${m}'`);
    // Try with '.js' extension
    if(!m.endsWith('.js') && fileExists(`${m}.js`)) {
      debug(`lookupModule# Found appending .js to file name`);
      return `${m}.js`;
    }
    return new Error(`Error: Module ${m} not found!`);
  }

  // Path found and it isn't a dir
  if(!fstat.isDir) {
    return new Error(`Error: Module file type not supported for ${m}`);
  }

  // Path it's a dir
  let modulePath = null; // Real m to module
  let tryOthers = true; // Keep trying?

  debug(`lookupModule# ${m} is a directory, trying options...`);
  // Try with package.json for NPM or YARN modules
  if(statPath(`${m}/package.json`).isFile) {
    debug(`lookupModule# ${m}/package.json exists, looking for main script...`);
    let pkg = JSON.parse(loadFile(`${m}/package.json`));
    /* main may leave out .js or name a directory, as Node allows */
    const main = pkg?.main && [`${m}/${pkg.main}`, `${m}/${pkg.main}.js`, `${m}/${pkg.main}/index.js`].find(fileExists);

    if(main) {
      tryOthers = false;
      modulePath = main;
      debug(`lookupModule# Found package main script!`);
    }
  }
  let file = join(m, `index.js`);
  // Try other options
  if(tryOthers && statPath(file).isFile) {
    tryOthers = false;
    modulePath = file;
    debug(`lookupModule# Found package index.js file`);
  }
  file = join(m, 'main.js');
  if(tryOthers && statPath(file).isFile) {
    tryOthers = false;
    modulePath = file;
    debug(`lookupModule# Found package main.js file`);
  }

  if(modulePath === null) {
    return new Error(`Error: Module ${m} is a directory, but not a package`);
  }

  debug(`lookupModule# Found module file: ${modulePath}`);
  // Returns what it founded
  return modulePath;
}

/* a Node name or a qjsm builtin, loaded as import loads it: require('fs')
 * and require('node:fs') give the fs module object, the default export,
 * else the namespace. node:<name> is tried first, it fails under -N or
 * where qjsm has no Node module of that name. */
function requireBuiltin(name) {
  const bare = name.replace(/^node:/, '');
  let ns;

  if(NODE.has(bare))
    try {
      ns = globalThis.requireModule('node:' + bare);
    } catch(e) {}

  ns ??= globalThis.requireModule(bare);

  return ns.default ?? ns;
}

export function require(m) {
  if(m.startsWith('node:') || NODE.has(m) || globalThis.builtins?.some(b => b.name == m)) return requireBuiltin(m);

  const { filename: file = argv[1] } = require;

  getPackagePaths ??= memoize(packagePaths);

  getPackagePaths(__dirname ?? process.cwd());

  if(typeof file == 'undefined') {
    let meta;
    try {
      meta = import.meta;
    } catch(e) {
      meta = {};
    }
    const { url, main } = meta;
    debug(`\x1b[1;33mrequire('${m}')\x1b[0m# Calling from main script '${scriptFile ?? scriptArgs[0]}'`);
  } else {
    debug(`\x1b[1;33mrequire('${m}')\x1b[0m# Calling from ${file} parent module`);
  }
  const dir = dirname(file);

  if(dir[0] != '/') getPackagePaths(dir);

  if(/^\./.test(m)) {
    let tmp;
    tmp = join(dir, m);
    tmp = normalize(tmp);
    debug(`require('${m}')# concat(${dir},${m}) = ${tmp}`);
    m = tmp;
  }

  let _path = lookupModule(m, dir),
    tmp;

  /* a bare name no node_modules holds: qjsm's module path */
  if(_path instanceof Error && !/^[./]/.test(m) && (tmp = globalThis.locateModule?.(m))) _path = lookupModule((m = tmp), dir);

  // Module not found
  if(_path instanceof Error) {
    debug(`ERROR loading ${m}: ${_path.message}`);
    throw _path;
    return;
  }

  if(/\.json$/i.test(m)) return loadJSON(m);

  debug(`LOAD: ${_path}`);

  try {
    let _module = requireModule(_path);

    return _module.exports.default ?? _module.exports;
  } catch(e) {
    debug(`require('${m}')# error[${moduleStack.size}]`, e.message, ...(typeof e.stack == 'string' ? [e.stack.split(/\n/g).find(frame => !/require.js/.test(frame))] : []));
    throw e;
  }

  ///* if(moduleStack.size == 0)*/ throw new Error(`require[${moduleStack.size}]: error loading '${m}'`);
}

require.main = Object.defineProperties(
  {},
  {
    paths: { get: memoize(packagePaths) },
  },
);

Object.defineProperties(require, { cache: { value: modules } });

globalThis.require = require;

export default require;
