/* lib/bun.js: Bun's network globals from qjs-lws, only when it is installed, qjsm only */
import * as os from 'os';
import * as std from 'std';
import { eq, tests } from '../../lib/tinytest.js';

const NAMES = ['fetch', 'Request', 'Response', 'Headers', 'FormData', 'URL', 'URLSearchParams', 'AbortController', 'AbortSignal'];

/* stdout of qjsm -e code, run with the default module path */
function child(code) {
  const file = `${std.getenv('TMPDIR') ?? '/tmp'}/bun-network-${os.getpid()}.txt`;
  const fd = os.open(file, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644);
  const env = std.getenviron();

  delete env.QUICKJS_MODULE_PATH;
  os.exec([process.execPath, '-e', code], { stdout: fd, env, block: true });
  os.close(fd);

  const out = std.loadFile(file);

  os.remove(file);
  return out.trim();
}

tests({
  'no global when qjs-lws is not in the module path'() {
    if(locateModule('fetch')) return;

    for(const name of NAMES) eq(false, name in globalThis, name);
    eq(undefined, Bun.serve);
    eq(undefined, Bun.fetch);
  },

  /* skipped when qjs-lws is not installed in the default module path */
  'qjs-lws globals load on first use, not at startup'() {
    const out = child(`
      if(locateModule('fetch')) {
        const before = moduleList.some(m => m.name.endsWith('serve.js'));
        const r = new Request(new URL('http://x/y?q=1'), { headers: new Headers({ a: '1' }) });

        console.log(before, r.url, r.headers.get('a'), Bun.fetch === fetch, typeof Bun.serve, 'path' in globalThis);
      }
    `);

    if(out) eq('false http://x/y?q=1 1 true function false', out);
  },

  'URL and URLSearchParams are not enumerable, as in Bun'() {
    const out = child(`
      if(locateModule('lws/url'))
        console.log(Object.keys(globalThis).filter(k => /^(URL|URLSearchParams|fetch)$/.test(k)).join());
    `);

    if(out) eq('fetch', out);
  },
});
