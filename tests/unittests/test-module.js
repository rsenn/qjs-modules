/* lib/module.js: Node's node:module subset */
import module, { builtinModules, createRequire, isBuiltin, registerHooks } from 'module';
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
    write('sub/obj.cjs', 'exports.x = 1;\n');
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

  'teardown'() {
    os.exec(['rm', '-rf', dir]);
  },
});
