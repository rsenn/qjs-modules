/* process.stdin (ref, unref, 'readable', read()) and process.stdout / process.stderr as
 * Node writables (write, end, 'finish', events), run in a child qjsm that reads a pipe.
 * qjsm only: Node has Readable and Writable classes, here it is the std file with their surface. */
import process from 'process';
import { Blob } from 'blob';
import { assert, eq, tests } from '../../lib/tinytest.js';

const Bun = globalThis.Bun ?? (await import('../../lib/bun.js')).default;

/* the output of a qjsm running script with input on its stdin */
async function run(script, input = '') {
  const p = Bun.spawn([process.execPath, '-e', script], { stdin: new Blob([input]), stdout: 'pipe', stderr: 'pipe' });
  const out = [],
    err = [];

  for await(const chunk of p.stdout) out.push(...chunk);
  for await(const chunk of p.stderr) err.push(...chunk);

  const text = a => new TextDecoder().decode(new Uint8Array(a));

  return { code: await p.exited, out: text(out), err: text(err) };
}

tests({
  /* stdin */

  'ref and unref record the flag and return stdin'() {
    eq(true, process.stdin.hasRef());
    eq(process.stdin, process.stdin.unref());
    eq(false, process.stdin.hasRef());
    eq(process.stdin, process.stdin.ref());
    eq(true, process.stdin.hasRef());
  },

  async 'readable and read() take the buffered data, null at the end'() {
    const r = await run(`const got = []; process.stdin.on('readable', () => { let c; while((c = process.stdin.read()) !== null) got.push(c.length + c.constructor.name); }); process.stdin.on('end', () => console.log('end', got.join()));`, 'hello');

    eq('end 5Buffer\n', r.out);
  },

  async 'read(size) waits for size bytes until the end'() {
    const r = await run(`const got = []; process.stdin.on('readable', () => { let c; while((c = process.stdin.read(2)) !== null) got.push(c.toString()); }); process.stdin.on('end', () => console.log(got.join()));`, 'hello');

    eq('he,ll,o\n', r.out);
  },

  async 'read() gives a string after setEncoding'() {
    const r = await run(`process.stdin.setEncoding('utf8'); process.stdin.on('readable', () => { const c = process.stdin.read(); if(c !== null) console.log(typeof c, c); });`, 'héllo');

    eq('string héllo\n', r.out);
  },

  async 'readableLength, readable and readableEnded follow the buffer'() {
    const r = await run(`const s = process.stdin; s.on('readable', () => { if(s.readableLength) console.log('len', s.readableLength, s.readable); s.read(); }); s.on('end', () => console.log('ended', s.readable, s.readableEnded));`, 'abc');

    eq('len 3 true\nended false true\n', r.out);
  },

  async 'a data listener still sees what read() returns'() {
    const r = await run(`let n = 0; process.stdin.on('data', d => n += d.length); process.stdin.on('readable', () => { process.stdin.read(); }); process.stdin.on('end', () => console.log(n));`, 'hello');

    eq('5\n', r.out);
  },

  async 'removing the readable listener lets the process exit'() {
    const r = await run(`const f = () => {}; process.stdin.on('readable', f); process.stdin.off('readable', f); console.log('done', process.stdin.listenerCount('readable'));`, 'x');

    eq('done 0\n0', `${r.out}${r.code}`);
  },

  /* stdout and stderr */

  async 'stdout has the Writable surface'() {
    const r = await run(`const o = process.stdout; console.log(['on', 'once', 'off', 'emit', 'write', 'end', 'cork', 'uncork', 'destroy', 'setDefaultEncoding'].map(k => typeof o[k]).join(), o.writable, o.writableEnded, o.writableLength, o.writableHighWaterMark, o.destroyed, o._isStdio);`);

    eq('function,function,function,function,function,function,function,function,function,function true false 0 16384 false true\n', r.out);
  },

  async 'write takes strings, typed arrays and encodings and returns true'() {
    const r = await run(`const o = process.stdout; const r = [o.write('a'), o.write(new Uint8Array([98])), o.write(Buffer.from('c')), o.write('64', 'hex'), o.write(new DataView(new Uint8Array([101]).buffer)), o.write('é')]; o.write('\\n' + r.join() + '\\n');`);

    eq('abcdeé\ntrue,true,true,true,true,true\n', r.out);
  },

  async 'write calls the callback after the write'() {
    const r = await run(`let at = 'before'; process.stdout.write('x\\n', () => console.log('cb', at)); at = 'after';`);

    eq('x\ncb after\n', r.out);
  },

  async 'stderr is a writable too'() {
    const r = await run(`process.stderr.write('e1\\n'); process.stderr.write(new Uint8Array([101, 50, 10]));`);

    eq('e1\ne2\n', r.err);
    eq('', r.out);
  },

  async 'write rejects a number and null'() {
    const r = await run(`for(const v of [7, null, {}]) try { process.stdout.write(v); } catch(e) { console.log(e.code); }`);

    eq('ERR_INVALID_ARG_TYPE\nERR_STREAM_NULL_VALUES\nERR_INVALID_ARG_TYPE\n'.split('\n').sort().join(), r.out.split('\n').sort().join());
  },

  async 'end writes the last chunk and emits finish'() {
    const r = await run(`const o = process.stdout; o.once('finish', () => console.log('finish', o.writableFinished, o.writableEnded, o.writable)); console.log('before', o.writableFinished); o.end('bye\\n');`);

    eq('before false\nbye\nfinish true true false\n', r.out);
  },

  async 'a write after end gives the callback an error and emits error'() {
    const r = await run(`const o = process.stdout; o.on('error', e => console.log('error', e.code)); o.end(); o.write('late', e => console.log('cb', e.code));`);

    eq('cb ERR_STREAM_WRITE_AFTER_END\nerror ERR_STREAM_WRITE_AFTER_END\n', r.out);
  },

  async 'console.log keeps working after end'() {
    const r = await run(`process.stdout.end(); setTimeout(() => console.log('still'), 5);`);

    eq('still\n', r.out);
  },

  async 'a resize listener on a pipe does nothing and does not keep the process alive'() {
    const r = await run(`process.stdout.on('resize', () => {}); console.log(process.stdout.listenerCount('resize'));`);

    eq('1\n0', `${r.out}${r.code}`);
  },

  async 'once runs a listener one time'() {
    const r = await run(`const o = process.stdout; let n = 0; o.once('x', () => n++); o.emit('x'); o.emit('x'); console.log(n, o.listenerCount('x'));`);

    eq('1 0\n', r.out);
  },

  async 'an error without a listener is thrown'() {
    const r = await run(`try { process.stdout.emit('error', new Error('boom')); } catch(e) { console.log('thrown', e.message); }`);

    eq('thrown boom\n', r.out);
  },

  'the std file write(arrayBuffer, offset, length) still works'() {
    const buf = new Uint8Array([79, 75, 10]).buffer;

    process.stdout.write(buf, 0, 3);
    assert(true);
  },
});
