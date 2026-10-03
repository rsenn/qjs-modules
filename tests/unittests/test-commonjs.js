/* lib/commonjs.js: the 'commonjs' format loader, qjsm only */
import installCommonJS from '../../lib/commonjs.js';
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

  'teardown'() {
    hooks.deregister();
    os.exec(['rm', '-rf', dir]);
  },
});
