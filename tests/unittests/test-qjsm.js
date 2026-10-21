/* the module globals installed by src/qjsm.c, qjsm only */
import 'path';
import * as os from 'os';
import * as std from 'std';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* the getters follow the running script, so read them while it is the top frame */
const [file, dir] = [__filename, __dirname];

tests({
  'every module global is a function'() {
    for(const name of ['findModule', 'loadModule', 'requireModule', 'resolveModule', 'normalizeModule', 'locateModule', 'getModule', 'getModuleName', 'getModuleMetaObject', 'getModuleNS', 'registerHooks', 'disableBuiltin', 'evalBuf', 'evalFile'])
      eq('function', typeof globalThis[name]);
  },

  'builtins lists name and kind'() {
    const path = builtins.find(b => b.name == 'path');

    assert(path, 'path missing');
    eq('native', path.kind);
  },

  'builtins includes compiled JS modules'() {
    const util = builtins.find(b => b.name == 'util');

    assert(util, 'util missing');
  },

  'moduleList includes what has been imported'() {
    assert(moduleList.some(m => m.name == 'path'), 'path not in moduleList');
  },

  'moduleList is non-empty'() {
    assert(moduleList.length > 0, 'empty');
  },

  'normalizeModule resolves relative specifiers against the importer'() {
    eq('/a/b/d.js', normalizeModule('/a/b/c.js', './d.js'));
    eq('/a/d.js', normalizeModule('/a/b/c.js', '../d.js'));
  },

  'normalizeModule leaves builtin names bare and strips node:'() {
    eq('path', normalizeModule('/x.js', 'path'));
    eq('path', normalizeModule('/x.js', 'node:path'));
  },

  'findModule returns the module index'() {
    const index = findModule('path');

    eq('number', typeof index);
    assert(index >= 0, 'path not found');
    eq(index, moduleList.findIndex(m => m.name == 'path'));
  },

  'moduleList items describe the module'() {
    const item = moduleList.find(m => m.name == 'path');

    eq('native', item.kind);
    eq(true, item.builtin);
    eq(undefined, item.hooked);
    eq(moduleList.findIndex(m => m.name == 'path'), item.index);
  },

  'getModule returns the moduleList item by name or index'() {
    const item = moduleList.find(m => m.name == 'path');

    eq(JSON.stringify(item), JSON.stringify(getModule('path')));
    eq(JSON.stringify(item), JSON.stringify(getModule(item.index)));
  },

  'getModule is null for an unknown module'() {
    eq(null, getModule('nonexistent-xyz'));
  },

  'getModuleName returns the module name'() {
    eq('path', getModuleName(findModule('path')));
  },

  'getModuleNS returns the namespace'() {
    eq('function', typeof getModuleNS(findModule('path')).join);
  },

  'getModuleMetaObject returns an object'() {
    eq('object', typeof getModuleMetaObject(findModule('path')));
  },

  'getModuleName of an unknown index throws'() {
    let error;

    try {
      getModuleName(1e6);
    } catch(e) {
      error = e;
    }

    assert(error instanceof TypeError, 'expected TypeError');
  },

  'findModule is -1 when unknown'() {
    eq(-1, findModule('nonexistent-xyz'));
  },

  'findModule starts searching at an offset'() {
    const index = findModule('path');

    eq(-1, findModule('path', index + 1));
  },

  'QUICKJS_MODULE_PATH splits on ";" and, off Windows, on ":"'() {
    const base = `${std.getenv('TMPDIR') ?? '/tmp'}/qjsm-test-mp-${os.getpid()}`;
    const old = std.getenv('QUICKJS_MODULE_PATH');

    os.mkdir(base);
    os.mkdir(`${base}/a`);
    os.mkdir(`${base}/b`);
    std.open(`${base}/b/sepmod.js`, 'w').close();

    try {
      std.setenv('QUICKJS_MODULE_PATH', `${base}/a;${base}/b`);
      eq(`${base}/b/sepmod.js`, locateModule('sepmod'));

      if(os.platform != 'win32') {
        std.setenv('QUICKJS_MODULE_PATH', `${base}/a:${base}/b`);
        eq(`${base}/b/sepmod.js`, locateModule('sepmod'));
      }
    } finally {
      if(old === undefined) std.unsetenv('QUICKJS_MODULE_PATH');
      else std.setenv('QUICKJS_MODULE_PATH', old);
      os.exec(['rm', '-rf', base]);
    }
  },

  'locateModule returns null for an unknown module'() {
    eq(null, locateModule('nonexistent-xyz'));
  },

  'requireModule returns the namespace'() {
    eq('function', typeof requireModule('path').join);
  },

  'requireModule does not bind the module on globalThis'() {
    delete globalThis.misc;
    requireModule('misc');
    eq(false, 'misc' in globalThis);
  },

  'loadModule binds the module onto globalThis'() {
    loadModule('path', 'qjsmTestPath');

    try {
      eq('function', typeof globalThis.qjsmTestPath.join);
    } finally {
      delete globalThis.qjsmTestPath;
    }
  },

  'loadModule of an unknown module throws'() {
    let error;

    try {
      loadModule('nonexistent-xyz');
    } catch(e) {
      error = e;
    }

    assert(error && /nonexistent-xyz/.test(error.message), String(error));
  },

  'disableBuiltin switches a builtin off at run time'() {
    assert(builtins.some(b => b.name == 'iterator'), 'iterator missing');

    eq('iterator', disableBuiltin('iterator', 'no-such-builtin', 'iterator')[0]);
    eq(0, disableBuiltin('iterator', 'no-such-builtin').length);
    assert(!builtins.some(b => b.name == 'iterator'), 'iterator still listed');
  },

  'disableBuiltin takes the Node subpath names'() {
    eq('fsPromises', disableBuiltin('fs/promises')[0]);
  },

  'disableBuiltin rejects what is not a string'() {
    let error;

    try {
      disableBuiltin(42);
    } catch(e) {
      error = e;
    }

    assert(error instanceof TypeError, String(error));
  },

  'evalBuf evaluates a script'() {
    eq(3, evalBuf('1+2'));
  },

  '__filename and __dirname describe the running script'() {
    assert(file.endsWith('test-qjsm.js'), file);
    assert(dir.endsWith('unittests'), dir);
  },
});
