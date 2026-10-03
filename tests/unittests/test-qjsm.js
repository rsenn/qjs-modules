/* the module globals installed by src/qjsm.c, qjsm only */
import 'path';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* the getters follow the running script, so read them while it is the top frame */
const [file, dir] = [__filename, __dirname];

tests({
  'every module global is a function'() {
    for(const name of ['findModule', 'findModuleIndex', 'loadModule', 'requireModule', 'resolveModule', 'normalizeModule', 'locateModule', 'registerHooks', 'evalBuf', 'evalFile'])
      eq('function', typeof globalThis[name]);
  },

  'builtins lists name and kind'() {
    const path = builtins.find(b => b.name == 'path');

    assert(path, 'path missing');
    eq(true, path.native);
  },

  'builtins includes compiled JS modules'() {
    const util = builtins.find(b => b.name == 'util');

    assert(util, 'util missing');
  },

  'moduleList includes what has been imported'() {
    assert(moduleList.some(m => m.name == 'path'), 'path not in moduleList');
  },

  'moduleEntries is non-empty'() {
    assert(moduleEntries.length > 0, 'empty');
  },

  'normalizeModule resolves relative specifiers against the importer'() {
    eq('/a/b/d.js', normalizeModule('/a/b/c.js', './d.js'));
    eq('/a/d.js', normalizeModule('/a/b/c.js', '../d.js'));
  },

  'normalizeModule leaves builtin names bare and strips node:'() {
    eq('path', normalizeModule('/x.js', 'path'));
    eq('path', normalizeModule('/x.js', 'node:path'));
  },

  'findModule returns null for an unknown module'() {
    eq(null, findModule('nonexistent-xyz'));
  },

  'findModule finds a loaded module'() {
    assert(findModule('path') !== null, 'path not found');
  },

  'findModuleIndex is -1 when unknown, an index when loaded'() {
    eq(-1, findModuleIndex('nonexistent-xyz'));
    assert(findModuleIndex('path') >= 0, 'path index');
  },

  'locateModule returns null for an unknown module'() {
    eq(null, locateModule('nonexistent-xyz'));
  },

  'requireModule returns the namespace'() {
    eq('function', typeof requireModule('path').join);
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

  'evalBuf evaluates a script'() {
    eq(3, evalBuf('1+2'));
  },

  '__filename and __dirname describe the running script'() {
    assert(file.endsWith('test-qjsm.js'), file);
    assert(dir.endsWith('unittests'), dir);
  },
});
