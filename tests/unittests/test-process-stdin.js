/* process.stdin of lib/process.js: 'data' and 'end' events, setEncoding(), async iteration, run in a child qjsm that reads a pipe.
 * qjsm only: Node's stdin is a Readable, here it is the std file with the same events. */
import process from 'process';
import { Blob } from 'blob';
import { assert, eq, tests } from '../../lib/tinytest.js';

const Bun = globalThis.Bun ?? (await import('../../lib/bun.js')).default;

/* the output of a qjsm running script with input on its stdin */
async function run(script, input) {
  const p = Bun.spawn([process.execPath, '-e', script], { stdin: new Blob([input]), stdout: 'pipe', stderr: 'pipe' });
  const chunks = [];

  for await(const chunk of p.stdout) chunks.push(...chunk);

  return { code: await p.exited, out: new TextDecoder().decode(new Uint8Array(chunks)) };
}

tests({
  async 'data events carry Buffers, end follows'() {
    const r = await run(`let n = 0, buf; process.stdin.on('data', d => { n += d.length; buf = d.constructor.name; }); process.stdin.on('end', () => console.log('end', n, buf));`, 'hello');

    eq('end 5 Buffer\n0', `${r.out}${r.code}`);
  },

  async 'setEncoding gives strings'() {
    const r = await run(`let s = ''; process.stdin.setEncoding('utf8'); process.stdin.on('data', d => s += d); process.stdin.on('end', () => console.log(JSON.stringify(s)));`, 'a\nb');

    eq('"a\\nb"\n', r.out);
  },

  async 'for await reads the whole input'() {
    const r = await run(`(async () => { let n = 0; for await(const c of process.stdin) n += c.length; console.log('n', n); })();`, 'x'.repeat(200000));

    eq('n 200000\n', r.out);
  },

  async 'a listener removed lets the process exit'() {
    const r = await run(`const f = () => {}; process.stdin.on('data', f); process.stdin.off('data', f); console.log('done', process.stdin.listenerCount('data'));`, 'x');

    eq('done 0\n0', `${r.out}${r.code}`);
  },

  'setRawMode exists only on a terminal'() {
    eq(!!process.stdin.isTTY, typeof process.stdin.setRawMode == 'function');
  },
});
