/* lib/node/child_process.js: Node's child_process on node:child_process */
import { execFile, execFileSync, execSync, spawn, spawnSync } from 'node:child_process';
import { assert, eq, tests } from '../../lib/tinytest.js';

const once = (emitter, type) => new Promise(resolve => emitter.once(type, (...args) => resolve(args)));

tests({
  'execSync returns stdout as a Buffer, a string with encoding'() {
    eq(execSync('echo hi').toString(), 'hi\n');
    eq(execSync('echo hi', { encoding: 'utf8' }), 'hi\n');
  },
  'execSync throws on a non-zero exit'() {
    let err;
    try {
      execSync('echo oops >&2; exit 3', { stdio: 'pipe' });
    } catch(e) {
      err = e;
    }
    eq(err.status, 3);
    eq(err.message.split('\n')[0], 'Command failed: echo oops >&2; exit 3');
  },
  'execFileSync passes arguments'() {
    eq(execFileSync('echo', ['a', 'b'], { encoding: 'utf8' }), 'a b\n');
  },
  'spawnSync feeds input and reports ENOENT, shell and timeout'() {
    eq(spawnSync('cat', { input: 'abc', encoding: 'utf8' }).stdout, 'abc');
    eq(spawnSync('/nonexistent/x').error.code, 'ENOENT');
    eq(spawnSync('echo $((1+2))', { shell: true, encoding: 'utf8' }).stdout, '3\n');
    eq(spawnSync('sleep', ['1'], { timeout: 50 }).error.code, 'ETIMEDOUT');
  },
  'spawnSync reads two large pipes without deadlock'() {
    const r = spawnSync('sh', ['-c', 'head -c 300000 /dev/zero; head -c 300000 /dev/zero >&2']);
    eq(r.stdout.length + r.stderr.length, 600000);
  },
  async 'spawn emits spawn, data, exit and close'() {
    const c = spawn('sh', ['-c', 'echo out; exit 2']);
    let out = '';
    c.stdout.setEncoding('utf8');
    c.stdout.on('data', d => (out += d));
    const [code, signal] = await once(c, 'close');
    eq([code, signal, out].join('|'), '2||out\n');
  },
  async 'spawn: stdin end() reaches the child, kill() sets signalCode'() {
    const c = spawn('cat');
    let out = '';
    c.stdout.on('data', d => (out += d));
    c.stdin.end('hello');
    await once(c, 'close');
    eq(out, 'hello');

    const k = spawn('sleep', ['5']);
    setTimeout(() => k.kill('SIGINT'), 20);
    const [, sig] = await once(k, 'close');
    eq(sig, 'SIGINT');
    assert(k.killed);
  },
  async 'spawn of a missing file emits error ENOENT'() {
    const [e] = await once(spawn('/nonexistent/y'), 'error');
    eq(e.code, 'ENOENT');
  },
  async 'execFile calls back with (err, stdout, stderr)'() {
    const ok = await new Promise(r => execFile('sh', ['-c', 'echo a; echo b >&2'], (e, so, se) => r([e, so, se])));
    eq(JSON.stringify(ok), '[null,"a\\n","b\\n"]');
    const bad = await new Promise(r => execFile('sh', ['-c', 'exit 4'], e => r(e)));
    eq(bad.code, 4);
  },
});
