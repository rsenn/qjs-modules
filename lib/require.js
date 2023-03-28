import { evalScript, getenv, loadFile, puts } from 'std';
import { realpath, S_IFDIR, S_IFREG, stat } from 'os';
import * as path from 'path';
import { memoize } from 'util';

const dsoModules = ['std', 'os', 'fs', 'child_process', 'deep', 'inspect', 'lexer', 'mmap', 'path', 'pointer', 'predicate', 'repeater', 'tree_walker'];
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
      }
    },
    top: { get },
    value: {
      get() {
        let value = [...this];
        if(get() !== undefined) value.push(get());

        return value;
      }
    },
    size: {
      get() {
        return this.value.length;
      }
    }
  });
  return stack;
}

const filenameStack = new Stack(
  value => (require.filename = value),
  () => require.filename
);
const exportsStack = new Stack(
  value => (globalThis.exports = value),
  () => globalThis.exports
);
const moduleStack = new Stack(
  value => ((globalThis.module = value), value?.exports ? (globalThis.exports = value.exports) : delete globalThis.exports),
  () => globalThis.module
);

if(globalThis.scriptArgs && scriptArgs[0]) filenameStack.push(scriptArgs[0]);

function ErrorSubst(error, module) {
  const { message, stack } = error;

  error = new Error(message.replace(/<evalScript>/, module));
  error.stack = stack;
  return error;
}

class CJSModule {
  constructor(id, m) {
    this.id = id;
    this.path = m;
    this._failed = null;
    this._loaded = false;
    this.exports = {};
  }

  load() {
    const __file = this.path ?? this.id;
    const __dir = path.dirname(__file);
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
      //evalScript(_script);
    } catch(e) {
      error = ErrorSubst(e);
    }
    moduleStack.pop();
    filenameStack.pop();

    if(error) throw error;

    this.exports = ctx.exports;
    this._loaded = true;
    return true;
  }
}

function statPath(path) {
  const [fstat, err] = stat(path);
  let ret = {
    errno: err,
    isFile: fstat && fstat.mode & S_IFREG && true,
    isDir: fstat && fstat.mode & S_IFDIR && true
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

function findPackage(name) {
  debug(`findPackage(${name})#`);
  for(let dir of paths) {
    let fstat = statPath(dir + '/' + name);
    if(fstat.isDir) return dir + '/' + name;
  }
}

function loadDSO(m) {
  let module;
  if(globalThis.findModule) {
    if((module = findModule(m)) /*|| (module = loadModule(m))*/) {
      debug(`loadDSO(${m})# loaded '${getModuleName(module)}'`);
      return getModuleExports(module);
    }
  }

  const instance = {};
  const handler = {
    get(target, prop, receiver) {
      console.log('get', { target, prop });
      if(prop in target) return Reflect.get(target, prop, receiver);
      console.log('module', module);
      if(module !== undefined && module[prop]) return module[prop]; //(target[prop] = Reflect.get(module, prop, receiver));

      return function(...args) {
        if(typeof module == 'undefined') throw new Error(`Module '${m}' not loaded`);
        if(typeof module[prop] != 'function') throw new Error(`Module '${m}' method '${prop}' not a function`);
        return module[prop].call(this, ...args);
      };
    },
    enumerate(target) {
      if(module !== undefined) return Object.keys(module);
      return Reflect.ownKeys(target);
    },
    ownKeys(target) {
      if(module !== undefined) return Object.keys(module);
      return Reflect.ownKeys(target);
    }
  };
  debug(`loadDSO(${m})# loading...`);

  return import(m)
    .then(m => {
      Object.assign(instance, (module = m));
      debug(`loadDSO(${m})# loaded`);
    })
    .catch(err => debug(`loadDSO(${m})# ERROR:`, err));
  return new Proxy(instance, handler);
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
  } else {
    module = new CJSModule(id, m);
    modules[id] = module;
    required.push(m);
    let result = module.load();
    required.pop();
    if(result !== true) {
      debug(`requireModule[${moduleStack.size}]# error`, result);
      throw result;
      return;
    }
  }
  debug(`requireModule[${moduleStack.size}]# success path:`, m);
  return module;
}

function packagePaths(p) {
  let add = dir => {
    if(paths.indexOf(dir) == -1) {
      paths.push(dir);
      debug(`packagePaths(${p})#`, paths);
    }
  };

  for(let dir = p; dir && dir != '/'; dir = path.dirname(dir)) {
    let fstat = statPath(dir + '/package.json');

    if(fstat.isFile) {
      debug(`packagePaths(${p})# package.json in ${dir}`);
      for(let subdir of ['quickjs_modules', 'node_modules']) {
        let dstat = statPath(dir + '/' + subdir);
        if(dstat.isDir) add(dir + '/' + subdir);
      }
    }
  }
  debug(`packagePaths(${p})# paths=${paths}`);
  return paths;
}

function lookupPath(p) {
  let ext = path.extname(p);
  let st = statPath(p);

  if(ext == '' && fileExists(p + '.js')) {
    p += '.js';
    st = statPath(p);
  } else if(st.isDir && fileExists(p + '/index.js')) {
    p += '/index.js';
    st = statPath(p);
  }
  let r = p;
  if(st.errno) r = null;
  debug(`lookupPath('${p}') = ${r}`);

  return r;
}

function lookupModule(m) {
  let paths,
    fstat = statPath(m);

  debug(`lookupModule# Looking for ${m}`);
  // Path found
  if(fstat.isFile) {
    debug(`lookupModule# Found module file '${m}'`);
    return m;
  }

  if(m[0] == '@') {
    let f = 'packages/' + m.slice(1).replace(/\//g, '-');

    /*    if(fileExists(f)) */ m = lookupModule(f);
    fstat = statPath(m);
  } else if(m[0] != '/' && m[0] != '.') {
    let l = getPackagePaths(path.dirname(m));

    for(let p of l) {
      let x = p + '/' + m;
      let f = lookupPath(x);
      if(f) {
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
    if(!m.endsWith('.js') && path.exists(`${m}.js`)) {
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
    if(pkg && Object.keys(pkg).indexOf('main') !== -1 && pkg.main !== '' && statPath(`${m}/${pkg.main}`).isFile) {
      tryOthers = false;
      modulePath = `${m}/${pkg.main}`;
      debug(`lookupModule# Found package main script!`);
    }
  }
  let file = path.join(m, `index.js`);
  // Try other options
  if(tryOthers && statPath(file).isFile) {
    tryOthers = false;
    modulePath = file;
    debug(`lookupModule# Found package index.js file`);
  }
  file = path.join(m, 'main.js');
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

export function require(m) {
  if(dsoModules.indexOf(m) != -1) {
    return loadDSO(m);
  }

  const { filename: file = scriptArgs[0] } = require;

  getPackagePaths ??= memoize(packagePaths);

  getPackagePaths(m);

  if(typeof file == 'undefined') {
    const { url, main } = import.meta ?? {};
    debug(`\x1b[1;31mrequire('${m}')\x1b[0m# Calling from main script '${scriptFile ?? scriptArgs[0]}'`);
  } else {
    debug(`\x1b[1;31mrequire('${m}')\x1b[0m# Calling from ${file} parent module`);
  }
  const dir = path.dirname(file);

  if(dir[0] != '/') getPackagePaths(dir);

  if(/^\./.test(m)) {
    m = path.join(dir, m);
    m = path.canonical(m);
    //debug(`require('${m}')# concat(${dir},${m}) =`, m);
  } else if(!/[\.\/]/.test(m)) {
    let pkg = findPackage(m);
    debug(`require('${m}')# pkg`, pkg);
    if(pkg) m = pkg;
  }

  let _path = lookupModule(m);

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

    return _module.exports;
  } catch(e) {
    debug(
      `require('${m}')# error[${moduleStack.size}]`,
      e.message,
      e.stack.split(/\n/g).find(frame => !/require.js/.test(frame))
    );
  }

  ///* if(moduleStack.size == 0)*/ throw new Error(`require[${moduleStack.size}]: error loading '${m}'`);
}

require.main = {};

Object.defineProperties(require.main, {
  paths: { get: memoize(packagePaths) }
});

globalThis.require = require;

export default require;
