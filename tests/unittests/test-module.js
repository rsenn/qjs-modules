/* lib/module.js: Node's node:module subset */
import module, { Module, SourceMap, builtinModules, constants, createRequire, findPackageJSON, getCompileCacheDir, getSourceMapsSupport, isBuiltin, registerHooks, setSourceMapsSupport } from 'module';
import nodeModule from 'node:module';
import * as os from 'os';
import * as std from 'std';
import { assert, eq, tests } from '../../lib/tinytest.js';

const dir = `/tmp/qjsm-test-module-${os.getpid()}`;

function write(name, text) {
  const f = std.open(`${dir}/${name}`, 'w');

  f.puts(text);
  f.close();
}

tests({
  'setup'() {
    os.mkdir(dir);
    os.mkdir(`${dir}/sub`);
    write('sub/a.cjs', "module.exports = require('./b.cjs') + 1;\n");
    write('sub/b.cjs', 'module.exports = 41;\n');
    write('sub/data.json', '{"j": 1}\n');
    write('sub/obj.cjs', 'exports.x = 1; exports.same = this === exports; exports.file = __filename;\n');
    write('sub/bad.cjs', "throw new Error('boom');\n");
    write('package.json', '{"name": "root"}\n');
    os.mkdir(`${dir}/node_modules`);
    os.mkdir(`${dir}/node_modules/pkg`);
    write('node_modules/pkg/package.json', '{"name": "pkg", "main": "main.js"}\n');
    write('node_modules/pkg/main.js', "module.exports = 'pkgmain';\n");
  },

  'node:module and module are the same'() {
    eq(module, nodeModule);
  },

  'builtinModules is a sorted list of names'() {
    assert(builtinModules.includes('path'), 'path');
    assert(builtinModules.includes('fs'), 'fs');
    eq(builtinModules.join(), [...builtinModules].sort().join());
  },

  'isBuiltin knows builtins with and without node:'() {
    eq(true, isBuiltin('path'));
    eq(true, isBuiltin('node:path'));
    eq(false, isBuiltin('nonexistent-xyz'));
  },

  'isBuiltin rejects non-strings'() {
    eq(false, isBuiltin(5));
  },

  'registerHooks is the qjsm global'() {
    eq(globalThis.registerHooks, registerHooks);
  },

  'createRequire loads a CommonJS file relative to filename'() {
    const require = createRequire(`${dir}/sub/x.js`);

    eq(42, require('./a.cjs'));
  },

  'createRequire accepts a file: URL'() {
    const require = createRequire(`file://${dir}/sub/x.js`);

    eq(1, require('./obj.cjs').x);
  },

  'createRequire loads JSON'() {
    eq(1, createRequire(`${dir}/sub/x.js`)('./data.json').j);
  },

  'createRequire with a primitive module.exports'() {
    eq(41, createRequire(`${dir}/sub/x.js`)('./b.cjs'));
  },

  'two requires keep their own base directory'() {
    const a = createRequire(`${dir}/sub/x.js`);
    const b = createRequire(`${dir}/x.js`);

    eq(41, a('./b.cjs'));
    eq(41, b('./sub/b.cjs'));
  },

  'createRequire needs a string'() {
    let name;

    try {
      createRequire(5);
    } catch(e) {
      name = e.constructor.name;
    }

    eq('TypeError', name);
  },

  'default export is the Module class'() {
    eq(Module, module);
    eq(Module, Module.Module);
    eq(builtinModules, Module.builtinModules);
  },

  'builtinModules uses node subpath names'() {
    assert(!builtinModules.includes('fsPromises'), 'fsPromises');
    assert(builtinModules.includes('fs/promises'), 'fs/promises');
    eq(true, isBuiltin('node:fs/promises'));
  },

  'wrap and wrapper'() {
    eq('(function (exports, require, module, __filename, __dirname) { x\n});', Module.wrap('x'));
    eq(2, Module.wrapper.length);
  },

  '_nodeModulePaths walks up and skips node_modules'() {
    eq('/a/node_modules/b/c/node_modules,/a/node_modules/b/node_modules,/a/node_modules,/node_modules', Module._nodeModulePaths('/a/node_modules/b/c').join());
    eq('/node_modules', Module._nodeModulePaths('/').join());
  },

  'new Module has the CommonJS module fields'() {
    const m = new Module('/x/y.js', null);

    eq('/x/y.js', m.id);
    eq('/x', m.path);
    eq(false, m.loaded);
    eq(null, m.filename);
    eq(0, m.children.length);
  },

  'Module._load loads and caches a file'() {
    const m = new Module(`${dir}/main.js`, null);

    m.filename = m.id;
    m.paths = Module._nodeModulePaths(dir);
    eq(42, Module._load('./sub/a.cjs', m, false));
    assert(Module._cache[`${dir}/sub/a.cjs`], 'cached');
    eq(42, m.require('./sub/a.cjs'));
  },

  'modules see their own exports, filename and this'() {
    const m = new Module(`${dir}/main.js`, null);

    m.filename = m.id;
    m.paths = Module._nodeModulePaths(dir);

    const o = m.require('./sub/obj.cjs');

    eq(true, o.same);
    eq(`${dir}/sub/obj.cjs`, o.file);
  },

  'Module._load resolves node_modules packages through package.json main'() {
    const m = new Module(`${dir}/main.js`, null);

    m.filename = m.id;
    m.paths = Module._nodeModulePaths(dir);
    eq('pkgmain', m.require('pkg'));
  },

  'Module._load of a missing module throws MODULE_NOT_FOUND'() {
    const m = new Module(`${dir}/main.js`, null);
    let code;

    m.filename = m.id;
    m.paths = Module._nodeModulePaths(dir);

    try {
      m.require('./nope');
    } catch(e) {
      code = e.code;
    }

    eq('MODULE_NOT_FOUND', code);
  },

  'a module that throws is dropped from the cache and the parent'() {
    const m = new Module(`${dir}/main.js`, null);

    m.filename = m.id;
    m.paths = Module._nodeModulePaths(dir);

    try {
      m.require('./sub/bad.cjs');
    } catch(e) {}

    eq(undefined, Module._cache[`${dir}/sub/bad.cjs`]);
    eq(false, m.children.some(c => c.id.endsWith('bad.cjs')));
  },

  'Module._extensions has .js .json .node'() {
    eq('.js,.json,.node', Object.keys(Module._extensions).join());
  },

  '_compile runs CommonJS source'() {
    const m = new Module('/x/z.js');

    m._compile('module.exports = [typeof require, __filename];', '/x/z.js');
    eq('function,/x/z.js', m.exports.join());
  },

  'findPackageJSON finds the closest and the package one'() {
    eq(`${dir}/package.json`, findPackageJSON('./sub/a.cjs', `${dir}/main.js`));
    eq(`${dir}/node_modules/pkg/package.json`, findPackageJSON('pkg', `${dir}/main.js`));
    eq(undefined, findPackageJSON('nonexistent-xyz', `${dir}/main.js`));
  },

  'SourceMap decodes mappings'() {
    const map = new SourceMap({ version: 3, sources: ['a.ts'], names: ['foo'], mappings: 'AAAA,SAASA,GAAG;AACX' });

    eq('a.ts', map.findEntry(0, 10).originalSource);
    eq(9, map.findEntry(0, 10).originalColumn);
    eq('foo', map.findEntry(0, 10).name);
    eq(2, map.findOrigin(2, 1).lineNumber);
    eq('{}', JSON.stringify(map.findEntry(-1, 0)));
    eq('a.ts', map.payload.sources[0]);
  },

  'source maps support is recorded'() {
    setSourceMapsSupport(true, { nodeModules: true });
    eq(true, getSourceMapsSupport().enabled);
    eq(true, getSourceMapsSupport().nodeModules);
    eq(false, getSourceMapsSupport().generatedCode);
    setSourceMapsSupport(false);
    eq(false, getSourceMapsSupport().enabled);
  },

  'compile cache is reported as unavailable'() {
    eq(undefined, getCompileCacheDir());
    eq(constants.compileCacheStatus.FAILED, Module.enableCompileCache().status);
  },

  'register is refused'() {
    let code;

    try {
      Module.register('x');
    } catch(e) {
      code = e.code;
    }

    eq('ERR_FEATURE_UNAVAILABLE', code);
  },

  'teardown'() {
    os.exec(['rm', '-rf', dir]);
  },
});
