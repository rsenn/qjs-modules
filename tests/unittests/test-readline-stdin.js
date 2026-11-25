/* readline.createInterface({ input: process.stdin }) reads through the events of process.stdin,
 * not the raw file: pause, resume and other listeners work. Run in a child qjsm that reads a pipe.
 * qjsm only: process.stdin is the std file with Node's readable surface. */
import process from 'process';
import { Blob } from 'blob';
import { assert, eq, tests } from '../../lib/tinytest.js';

const Bun = globalThis.Bun ?? (await import('../../lib/bun.js')).default;

/* the output of a qjsm running script with input on its stdin */
async function run(script, input = '') {
  const p = Bun.spawn([process.execPath, '-e', script], { stdin: new Blob([input]), stdout: 'pipe', stderr: 'pipe' });
  const out = [];

  for await(const chunk of p.stdout) out.push(...chunk);

  return { code: await p.exited, out: new TextDecoder().decode(new Uint8Array(out)) };
}

const rl = `const readline = await import('readline'); `;

tests({
  async 'the lines of process.stdin'() {
    const r = await run(`(async () => { ${rl} const got = []; for await(const l of readline.createInterface({ input: process.stdin })) got.push(l); console.log(got.join('|')); })();`, 'a\nb\r\n€uro\nlast');

    eq('a|b|€uro|last\n', r.out);
  },

  async 'a second listener on process.stdin sees the same bytes'() {
    const r = await run(`(async () => { ${rl} let bytes = 0, lines = 0; process.stdin.on('data', d => bytes += d.length); const i = readline.createInterface({ input: process.stdin }); i.on('line', () => lines++); i.on('close', () => console.log(bytes, lines)); })();`, 'one\ntwo\nthree\n');

    eq('14 3\n', r.out);
  },

  async 'closing the interface pauses stdin and the process exits'() {
    const r = await run(`(async () => { ${rl} const i = readline.createInterface({ input: process.stdin }); i.on('line', l => { console.log('line', l); i.close(); }); i.on('close', () => console.log('closed', process.stdin.isPaused(), process.stdin.listenerCount('data'), process.stdin.listenerCount('end'))); })();`, 'a\nb\nc\n');

    /* as in Node, the other lines of the chunk are still emitted after close() */
    assert(r.out.startsWith('line a\nclosed true 0 0\n'), r.out);
    eq(0, r.code);
  },

  async 'a second interface after the first closed gets the next lines'() {
    const r = await run(`(async () => { ${rl} const first = readline.createInterface({ input: process.stdin }); first.on('line', l => { console.log('first', l); first.close(); const second = readline.createInterface({ input: process.stdin }); second.on('line', m => console.log('second', m)); }); })();`, 'a\n');

    eq('first a\n', r.out);
  },

  async 'a string input after setEncoding'() {
    const r = await run(`(async () => { ${rl} process.stdin.setEncoding('utf8'); const got = []; for await(const l of readline.createInterface({ input: process.stdin })) got.push(l); console.log(got.join('|')); })();`, 'é\nü\n');

    eq('é|ü\n', r.out);
  },

  async 'pause and resume of the interface reach process.stdin'() {
    const r = await run(`(async () => { ${rl} const i = readline.createInterface({ input: process.stdin }); i.pause(); console.log(process.stdin.isPaused()); i.resume(); console.log(process.stdin.isPaused()); i.close(); })();`, 'x\n');

    eq('true\nfalse\n', r.out);
  },

  async 'question on process.stdin answers from the first line'() {
    const r = await run(`(async () => { ${rl} const i = readline.createInterface({ input: process.stdin }); i.question('name? ', a => { console.log('hello', a); i.close(); }); })();`, 'Ada\n');

    eq('hello Ada\n', r.out);
  },
});
