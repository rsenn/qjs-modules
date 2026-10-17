/* lib/loaders/file.js: `with { type: 'file' }` imports, qjsm only */
import installFileLoader from '../../lib/loaders/file.js';
import * as os from 'os';
import * as std from 'std';
import { assert, eq, tests } from '../../lib/tinytest.js';

const dir = `/tmp/qjsm-test-file-loader-${os.getpid()}`;
let hooks;

tests({
  'setup'() {
    os.mkdir(dir);
    const f = std.open(`${dir}/hello.c`, 'w');

    f.puts('int main(void) { return +; }\n');
    f.close();
    hooks = installFileLoader();
  },

  async 'default export is the absolute path, the file is not parsed'() {
    const m = await import(`${dir}/hello.c`, { with: { type: 'file' } });

    eq(`${dir}/hello.c`, m.default);
  },

  async 'a percent-encoded path (space) is decoded'() {
    os.mkdir(`${dir}/a b`);
    std.open(`${dir}/a b/x.txt`, 'w').close();

    const m = await import(`${dir}/a b/x.txt`, { with: { type: 'file' } });

    eq(`${dir}/a b/x.txt`, m.default);
  },

  async 'without the attribute the hook passes through'() {
    const f = std.open(`${dir}/plain.js`, 'w');

    f.puts('export const v = 7;\n');
    f.close();

    eq(7, (await import(`${dir}/plain.js`)).v);
  },

  'teardown'() {
    hooks.deregister();
    os.exec(['rm', '-rf', dir]);
  },
});
