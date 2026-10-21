/* lib/loaders/commonjs.js: the 'commonjs' format loader, qjsm only */
import installCommonJS from '../../lib/loaders/commonjs.js';
import * as os from 'os';
import * as std from 'std';
import { assert, eq, tests } from '../../lib/tinytest.js';

const dir = `/tmp/qjsm-test-commonjs-${os.getpid()}`;

function write(name, text) {
  const f = std.open(`${dir}/${name}`, 'w');

  f.puts(text);
  f.close();
}

let hooks;

tests({
  'setup'() {
    os.mkdir(dir);
    write('dep.cjs', 'module.exports = { v: 41 };\n');
    write('lib.cjs', "const dep = require('./dep.cjs');\nexports.sum = dep.v + 1;\nexports.file = __filename;\nexports.dir = __dirname;\n");
    write('fn.cjs', 'module.exports = function twice(n) { return n * 2; };\nmodule.exports.tag = "t";\n');
    write('num.cjs', 'module.exports = 41;\n');
    write('empty.cjs', '');
    write('shebang.cjs', '#!/usr/bin/env qjsm\nexports.ok = true;\n');
    write('throws.cjs', 'throw new RangeError("from cjs");\n');
    write('bad-id.cjs', 'exports["not valid"] = 1; exports.valid = 2;\n');
    write('builtin.cjs', "exports.readFileSync = typeof require('fs').readFileSync;\nexports.join = typeof require('path').join;\n");

    /* .js by package.json "type" and syntax, bare require() from node_modules */
    for(const d of ['typeless', 'cjs', 'esm', 'node_modules', 'node_modules/pkg', 'node_modules/pkg/lib', 'node_modules/pkg/node_modules', 'node_modules/pkg/node_modules/dep']) os.mkdir(`${dir}/${d}`);
    write('typeless/package.json', '{}');
    write('typeless/plain.js', "module.exports = { kind: 'cjs' };\n");
    write('typeless/esm.js', "export const kind = 'esm';\n");
    write('cjs/package.json', '{"type": "commonjs"}');
    write('cjs/a.js', "exports.kind = 'cjs';\n");
    write('esm/package.json', '{"type": "module"}');
    write('esm/a.js', "export const kind = 'esm';\n");
    write('node_modules/pkg/package.json', '{"main": "lib/main"}');
    write('node_modules/pkg/lib/main.js', "module.exports = { dep: require('dep'), rel: require('./rel') };\n");
    write('node_modules/pkg/lib/rel.js', 'module.exports = 2;\n');
    write('node_modules/pkg/node_modules/dep/index.js', 'module.exports = 1;\n');
    write('use-pkg.cjs', "module.exports = require('pkg');\n");
    write('core.cjs', "const fs = require('fs');\nObject.defineProperty(fs, 'qjsmTest', { value: 1, configurable: true });\nmodule.exports = [typeof require('util').promisify, fs === require('node:fs'), typeof require('fs/promises').readFile, typeof require('buffer').Buffer, fs.qjsmTest].join();\ndelete fs.qjsmTest;\n");
    write('rethrow.cjs', "module.exports = require('./thrower.cjs');\n");
    write('thrower.cjs', 'throw new RangeError("thrower");\n');

    hooks = installCommonJS();
  },

  'installing sets the global require'() {
    eq('function', typeof globalThis.require);
  },

  async 'named exports come from module.exports keys'() {
    const m = await import(`${dir}/lib.cjs`);

    eq(42, m.sum);
  },

  async 'require() inside a module resolves relative to it'() {
    eq(42, (await import(`${dir}/lib.cjs`)).sum);
  },

  async '__filename and __dirname are set'() {
    const m = await import(`${dir}/lib.cjs`);

    eq(`${dir}/lib.cjs`, m.file);
    eq(dir, m.dir);
  },

  async 'default is module.exports'() {
    const m = await import(`${dir}/lib.cjs`);

    eq(42, m.default.sum);
  },

  async 'module.exports may be a function with properties'() {
    const m = await import(`${dir}/fn.cjs`);

    eq(6, m.default(3));
    eq('t', m.tag);
  },

  async 'module.exports may be a primitive'() {
    eq(41, (await import(`${dir}/num.cjs`)).default);
  },

  async 'an empty module exports an empty object'() {
    eq(0, Object.keys((await import(`${dir}/empty.cjs`)).default).length);
  },

  async 'a shebang line is ignored'() {
    eq(true, (await import(`${dir}/shebang.cjs`)).ok);
  },

  async 'only identifier keys become named exports'() {
    const m = await import(`${dir}/bad-id.cjs`);

    eq(2, m.valid);
    eq(1, m.default['not valid']);
    assert(!('not valid' in m), 'invalid identifier exported');
  },

  async 'an exception in the module rejects the import'() {
    let error;

    try {
      await import(`${dir}/throws.cjs`);
    } catch(e) {
      error = e;
    }

    assert(error && /from cjs/.test(error.message), String(error));
  },

  async 'require of a builtin returns its namespace'() {
    const m = await import(`${dir}/builtin.cjs`);

    eq('function', m.readFileSync);
    eq('function', m.join);
  },

  async 'a .js file follows the type of the nearest package.json'() {
    eq('cjs', (await import(`${dir}/cjs/a.js`)).kind);
    eq('esm', (await import(`${dir}/esm/a.js`)).kind);
  },

  async 'a .js file without a type is CommonJS unless it only parses as a module'() {
    eq('cjs', (await import(`${dir}/typeless/plain.js`)).kind);
    eq('esm', (await import(`${dir}/typeless/esm.js`)).kind);
  },

  async 'require() of a bare name searches the node_modules above the requiring file'() {
    const m = await import(`${dir}/use-pkg.cjs`);

    eq(1, m.dep);
    eq(2, m.rel);
  },

  async 'require() of a Node name gives the Node module object'() {
    eq('function,true,function,function,1', (await import(`${dir}/core.cjs`)).default);
  },

  async 'an error in a required module reaches the caller as is'() {
    let error;

    try {
      await import(`${dir}/rethrow.cjs`);
    } catch(e) {
      error = e;
    }

    assert(error instanceof RangeError && error.message == 'thrower', String(error));
  },

  'teardown'() {
    hooks.deregister();
    eq('undefined', typeof globalThis.require);
    os.exec(['rm', '-rf', dir]);
  },
});
