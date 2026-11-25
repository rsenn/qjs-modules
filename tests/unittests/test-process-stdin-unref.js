/* process.stdin.unref(): the process exits while the stdin handler is the only thing left, as in Node.
 * Run in a child qjsm whose stdin is a pipe that stays open for 2 seconds (sleep).
 * qjsm only. The exit tests need an engine with os.unrefHandler(); without it unref() records the flag only. */
import process from 'process';
import * as os from 'os';
import { Blob } from 'blob';
import { assert, eq, tests } from '../../lib/tinytest.js';

const Bun = globalThis.Bun ?? (await import('../../lib/bun.js')).default;
const engine = typeof os.unrefHandler == 'function';

/* the script's stdout and the ms it ran, with a stdin that closes after 2 s (and gets feed first) */
async function run(script, feed = '') {
  const sh = `(printf '%s' "$FEED"; sleep 2) | "$QJSM" -e "$SCRIPT"`;
  const p = Bun.spawn(['sh', '-c', sh], { env: { ...process.env, FEED: feed, QJSM: process.execPath, SCRIPT: script }, stdout: 'pipe', stderr: 'pipe' });
  const out = [];

  for await(const chunk of p.stdout) out.push(...chunk);

  const text = new TextDecoder().decode(new Uint8Array(out));
  const m = /^elapsed (\d+)$/m.exec(text);

  return { code: await p.exited, text, ms: m ? +m[1] : -1 };
}

/* the child: a 200 ms timer, a data listener, and what the test adds; prints its run time on exit */
const child = body => `const t0 = Date.now(); process.on('exit', () => console.log('elapsed ' + (Date.now() - t0))); setTimeout(() => console.log('timer'), 200); process.stdin.on('data', d => console.log('data ' + d.toString().trim())); ${body}`;

tests({
  'hasRef() follows ref() and unref()'() {
    const stdin = process.stdin;

    eq(true, stdin.hasRef());
    eq(stdin, stdin.unref());
    eq(false, stdin.hasRef());
    eq(stdin, stdin.ref());
    eq(true, stdin.hasRef());
  },

  async "a stdin that is not unref'd keeps the process until EOF"() {
    const r = await run(child(''));

    assert(r.text.includes('timer'), r.text);
    assert(r.ms >= 1500, `exited after ${r.ms} ms`);
  },

  async "an unref'd stdin lets the process exit after the timer"() {
    if(!engine) return;

    const r = await run(child('process.stdin.unref();'));

    assert(r.text.includes('timer'), r.text);
    assert(r.ms >= 0 && r.ms < 1000, `exited after ${r.ms} ms`);
    eq(0, r.code);
  },

  async 'ref() after unref() keeps the process alive again'() {
    const r = await run(child('process.stdin.unref(); process.stdin.ref();'));

    assert(r.ms >= 1500, `exited after ${r.ms} ms`);
  },

  async 'unref() before the first listener applies when reading starts'() {
    if(!engine) return;

    const r = await run(`const t0 = Date.now(); process.on('exit', () => console.log('elapsed ' + (Date.now() - t0))); process.stdin.unref(); setTimeout(() => process.stdin.on('data', d => console.log('data ' + d.toString().trim())), 50);`);

    assert(r.ms >= 0 && r.ms < 1000, `exited after ${r.ms} ms`);
  },

  async 'data still arrives while the loop runs for another reason'() {
    const r = await run(child('process.stdin.unref();'), 'hi');

    assert(r.text.includes('data hi'), r.text);
    assert(r.text.includes('timer'), r.text);
  },

  async 'unref() on a stdin nobody reads does not hold the process'() {
    const r = await run(`const t0 = Date.now(); process.on('exit', () => console.log('elapsed ' + (Date.now() - t0))); process.stdin.unref();`);

    assert(r.ms >= 0 && r.ms < 1000, `exited after ${r.ms} ms`);
  },
});
