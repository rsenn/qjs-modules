import * as std from 'std';
import * as os from 'os';
import * as path from 'path';

const dsoModules = ['std', 'os', 'fs', 'child_process', 'deep', 'inspect', 'lexer', 'mmap', 'path', 'pointer', 'predicate', 'repeater', 'tree_walker'];
let modules = {};
let paths = [];

let debug = (...args) => console.log(console.config({ compact: 10 }), ...args);
{
  let debugOptions = std.getenv('DEBUG');
  if(typeof debugOptions == 'undefined' || debugOptions.split(',').indexOf('require') === -1) {
    debug = function() {};
  }
}

function Stack(set, get) {
  let stack = [];
  return {
    push(value) {
      let top = get();
      if(top) stack.push(top);
      set(value);
      return this;
    },
    pop() {
      if(stack.length) {
        let value = stack.pop();
        set(value);
      } else if(get() !== undefined) {
        set(undefined);
      }
      return this;
    },
    /* prettier-ignore */ get top() {
      return  get();
    },
    /* prettier-ignore */ get value() {
      let value = [...stack];
      if(get() !== undefined)
        value.push(get());

      return value;
    },
    /* prettier-ignore */ get size() {
      return this.value.length;
    }
  };
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
    Object.seal(ctx);

    //const _mark = '##SCRIPT##';
    //let _loaderTemplate = _mark => `(function _loader (exports, require, module) {${_mark}})(ctx.exports, _require, ctx);`;
    let _loaderTemplate = _mark => new Function('exports', 'require', 'module', `${_mark}`);

    let _script = std.loadFile(__file);
    this._failed = _script === null;
    if(this._failed) return new Error(`Can't load script ${__file}`);

    let error;
    _script = _loaderTemplate(_script);
    //debug('load', _script.slice(-100));

    filenameStack.push(__file);
    moduleStack.push(ctx);
    try {
      _script(ctx.exports, _require, ctx);
      //std.evalScript(_script);
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
  const [fstat, err] = os.stat(path);
  return {
    errno: err,
    isFile: fstat && fstat.mode & os.S_IFREG && true,
    isDir: fstat && fstat.mode & os.S_IFDIR && true
  };
}

function fileExists(path) {
  return statPath(path)?.isFile;
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
    if((module = findModule(m))) {
      debug(`loadDSO(${m})# loaded '${getModuleName(module)}'`);
      return getModuleExports(module);
    }
  }

  const instance = {};
  const handler = {
    get(target, prop, receiver) {
      console.log('get', { target, prop });
      if(prop in target) return Reflect.get(target, prop, receiver);
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
  import(m)
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
    let json = std.loadFile(path, 'utf-8');
    data = JSON.parse(json);
  } catch(e) {
    throw e;
  }
  return data;
}

/*function lookupPackage(name) {
  debug(`lookupPackage(${name})#`);
  let pkg = findPackage(name);
  let json = std.loadFile(pkg + '/package.json', 'utf-8');
  let data = JSON.parse(json ?? '{}');
  if(data.main)
    return data.main.startsWith('.') ? pkg + data.main.slice(1) : pkg + '/' + data.main;
}*/

function loadModule(m) {
  debug(`loadModule[${moduleStack.size}]# Module ${m}`);
  const [id, err] = os.realpath(m);
  if(err) {
    throw new Error(`Module require error: Can't get real module m for ${m}`);
    return;
  }

  let module;

  debug(`loadModule[${moduleStack.size}]# id ${id}`);
  if(modules.hasOwnProperty(id)) {
    module = modules[id];
  } else {
    module = new CJSModule(id, m);
    modules[id] = module;

    let result = module.load();
    if(result !== true) {
      debug(`loadModule[${moduleStack.size}]# error`, result);
      throw result;
      return;
    }
  }
  debug(`loadModule[${moduleStack.size}]# success path:`, module.path);
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
    //debug(`packagePaths(${p})# Looking for packages in ${dir}`);
    let fstat = statPath(dir + '/package.json');

    if(fstat.isFile) {
      for(let subdir of ['quickjs_modules', 'node_modules']) {
        let dstat = statPath(dir + '/' + subdir);
        if(dstat.isDir) add(dir + '/' + subdir);
      }
    }
  }
  return paths;
}

function lookupModule(m) {
  let fstat = statPath(m);

  debug(`lookupModule# Looking for ${m}`);
  // Path found
  if(fstat.isFile) {
    if(m[0] != '/') packagePaths(path.dirname(m));
    debug(`lookupModule# Found module file '${m}'`);
    return m;
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
    let pkg = JSON.parse(std.loadFile(`${m}/package.json`));
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

  debug(`require('${m}')# moduleStack.size`, moduleStack.size);
  debug(`require('${m}')# `, filenameStack.value);

  if(typeof file == 'undefined') {
    debug(`require('${m}')# Calling from main script`);
  } else {
    debug(`require('${m}')# Calling from ${file} parent module`);
  }
  const dir = path.dirname(file);

  if(dir[0] != '/') packagePaths(dir);

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
    throw _path;
    return;
  }

  if(/\.json$/i.test(m)) return loadJSON(m);

  try {
    let _module = loadModule(_path);

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

globalThis.require = require;

export default require;
