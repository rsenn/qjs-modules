/* a bare import of a name that is a native module of this tree is never
 * served by the lib/<name>.js polyfill of the same name */
import * as std from 'std';
import * as os from 'os';
import { eq, tests } from '../../lib/tinytest.js';

const lib = new URL('../../lib', import.meta.url).pathname;
const native = f => String(f).includes('[native code]');

/* runs code in a child qjsm whose module path is only dir, prints its output */
function run(dir, code) {
  const file = `${std.getenv('TMPDIR') ?? '/tmp'}/native-guard-${os.getpid()}.txt`;
  const fd = os.open(file, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644);
  const env = std.getenviron();

  env.QUICKJS_MODULE_PATH = dir;
  os.exec([process.execPath, '-e', code], { stdout: fd, stderr: fd, env, block: true });
  os.close(fd);

  const out = std.loadFile(file);

  os.remove(file);
  return out.trim();
}

const probe = name => `import('${name}').then(m => console.log('OK'), e => console.log('FAIL'))`;

tests({
  'list resolves to the native module'() {
    return import('list').then(({ List }) => eq(native(List), true));
  },

  'tree_walker resolves to the native module'() {
    return import('tree_walker').then(({ TreeWalker }) => eq(native(TreeWalker), true));
  },

  'the polyfill alone is refused for a native name'() {
    eq(run(lib, probe('list')), 'FAIL');
  },

  'a JS-only module still loads from lib'() {
    eq(run(lib, probe('tinytest')), 'OK');
  },
});
