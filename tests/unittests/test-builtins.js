import * as std from 'std';
import * as os from 'os';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* run under qjsm; the tests that need compiled-in builtins skip otherwise.
 * Run with QUICKJS_MODULE_PATH=/nonexistent to prove no builtin reads the disk. */
const have = typeof builtins != 'undefined' ? [...builtins].map(b => b.name) : [];

const exe = os.readlink('/proc/self/exe')[0];

function run(args, code) {
  const f = std.popen(`${exe} ${args} ${code.endsWith('.mjs') ? code : `-e '${code}'`} 2>&1`, 'r');
  const out = f.readAsString().split("\n").filter(l => !/^TypeError: not an object/.test(l)).join("\n");
  f.close();
  return out;
}

tests({
  async 'every builtin imports'() {
    const failed = [];

    for(const name of have) {
      try {
        await import(name);
      } catch(e) {
        failed.push(name + ': ' + String(e.message).split('\n')[0]);
      }
    }
    eq(failed.join('\n'), '');
  },

  'disabled builtin is reported by its importer'() {
    if(!have.includes('abort') || !have.includes('globals') || !std.getenv('HOME')) return;
    const out = run('--disable-builtin abort', 'import("globals").catch(e=>print(e.message))');
    assert(/imports 'abort', which is disabled/.test(out), out);
  },

  'hooks do not see builtin names by default'() {
    if(!have.includes('path') || !have.includes('module') || std.getenv('QUICKJS_MODULE_PATH') != undefined) return;
    const file = `/tmp/test-builtins-${os.getpid?.() ?? 0}.mjs`;
    const src = `import { registerHooks } from "node:module";
registerHooks({ resolve(s, c, n) { if(s == "path") console.log("HOOK"); return n(s, c); } });
await import("path"); console.log("done");`;
    const w = std.open(file, 'w');
    w.puts(src);
    w.close();
    try {
      eq(run('', file).trim(), 'done');
      eq(run('--hooks-on-builtins', file).trim(), 'HOOK\ndone');
    } finally {
      os.remove(file);
    }
  },
});
