/* Bun.spawn() and Bun.spawnSync() of lib/bun/spawn.js; expected values come from bun 1.4.2.
 * runs under bun as well: bun tests/unittests/test-bun-spawn.js */
import * as fs from 'fs';
import { assert, eq, tests } from '../../lib/tinytest.js';

const Bun = globalThis.Bun ?? (await import('../../lib/bun.js')).default;
const ROOT = fs.mkdtempSync(`${globalThis.process?.env?.TMPDIR ?? '/tmp'}/qjs-bun-spawn-`);
const sh = (script, options) => Bun.spawn(['sh', '-c', script], options);
async function text(stream) {
  const decoder = new TextDecoder();
  let out = '';

  for await(const chunk of stream) out += decoder.decode(chunk, { stream: true });

  return out + decoder.decode();
}

const show = (...a) => a.map(String).join(' ');

async function fails(fn) {
  try {
    await fn();
  } catch(e) {
    return e;
  }

  return null;
}

tests({
  'exports spawn and spawnSync'() {
    eq('function', typeof Bun.spawn);
    eq('function', typeof Bun.spawnSync);
  },

  'a process: pid, stdout as a stream, exited and exitCode'() {
    const p = sh('echo out; exit 3', { stderr: 'ignore' });

    assert(p.pid > 0, 'pid');
    eq(null, p.exitCode);
    eq(null, p.signalCode);
    eq('undefined', typeof p.stdin);
    eq('undefined', typeof p.stderr);
    eq('function', typeof p.stdout.getReader);
    assert(p.readable === p.stdout, 'readable is stdout');

    return (async () => {
      eq('out\n', await text(p.stdout));
      eq(3, await p.exited);
      eq(3, p.exitCode);
      eq(null, p.signalCode);
      eq(true, p.killed);
    })();
  },

  async 'stdout and stderr as pipes'() {
    const p = sh('echo hi; echo e >&2', { stderr: 'pipe' });

    eq('hi\ne\n0', [await text(p.stdout), await text(p.stderr), await p.exited].join(''));
  },

  async 'a stream has text(), json(), bytes() on it'() {
    eq('hi\n', await sh('echo hi').stdout.text());
    eq('{"a":1}', JSON.stringify(await sh('echo \'{"a":1}\'').stdout.json()));
    eq('hi', new TextDecoder().decode(await sh('printf hi').stdout.bytes()));
  },

  async 'a stream is async iterable and a big output arrives whole'() {
    const p = sh('head -c 300000 /dev/zero | tr "\\0" x');
    let n = 0;

    for await(const chunk of p.stdout) {
      assert(chunk instanceof Uint8Array, 'chunk');
      n += chunk.length;
    }

    eq(300000, n);
    eq(0, await p.exited);
  },

  async 'kill() gives 143 for SIGTERM and 137 for SIGKILL, exitCode stays null'() {
    for(const [signal, code, name] of [[undefined, 143, 'SIGTERM'], ['SIGKILL', 137, 'SIGKILL'], [9, 137, 'SIGKILL']]) {
      const p = sh('sleep 5');

      p.kill(signal);
      eq(`${code} null ${name} true`, show(await p.exited, p.exitCode, p.signalCode, p.killed));
    }
  },

  async 'a process that kills itself'() {
    const p = sh('kill -9 $$');

    eq('137 null SIGKILL', show(await p.exited, p.exitCode, p.signalCode));
  },

  async 'timeout kills with killSignal, SIGTERM by default'() {
    const a = sh('sleep 5', { timeout: 100, killSignal: 'SIGKILL' });
    const b = sh('sleep 5', { timeout: 100 });

    eq('137 SIGKILL', [await a.exited, a.signalCode].join(' '));
    eq('143 SIGTERM', [await b.exited, b.signalCode].join(' '));
  },

  async 'an AbortSignal kills the process'() {
    const controller = new AbortController();
    const p = sh('sleep 5', { signal: controller.signal });

    controller.abort();
    eq('143 SIGTERM', [await p.exited, p.signalCode].join(' '));
  },

  async 'stdin pipe: a FileSink with write() and end()'() {
    const p = sh('cat', { stdin: 'pipe' });

    eq('FileSink', p.stdin.constructor.name);
    eq(3, p.stdin.write('abc'));
    eq(2, p.stdin.write(new Uint8Array([100, 10])));
    eq('number', typeof p.stdin.end());
    eq('abcd\n', await text(p.stdout));
    eq(0, await p.exited);
  },

  async 'stdin from a Blob, a Response, bytes, a stream and a file'() {
    const file = `${ROOT}/in.txt`;

    fs.writeFileSync(file, 'from file');

    for(const [input, want] of [
      [new Blob(['from blob']), 'from blob'],
      [new TextEncoder().encode('from bytes'), 'from bytes'],
      [new Blob(['from stream']).stream(), 'from stream'],
      [Bun.file(file), 'from file'],
    ])
      eq(want, await text(sh('cat', { stdin: input }).stdout));
  },

  async 'a megabyte through cat does not block'() {
    const input = 'x'.repeat(1 << 20);
    const p = sh('cat', { stdin: new Blob([input]) });

    eq(1 << 20, (await text(p.stdout)).length);
    eq(0, await p.exited);
  },

  async 'env replaces the environment, cwd changes the directory'() {
    eq('bar:/tmp\n', await text(sh('echo $FOO:$PWD', { env: { FOO: 'bar' }, cwd: '/tmp' }).stdout));
    eq('unset\n', await text(sh('echo ${FOO-unset}', { env: { PATH: '/usr/bin:/bin' } }).stdout));
  },

  async 'onExit gets the process, exit code, signal code and error'() {
    let args;
    const p = sh('exit 4', { onExit: (...a) => (args = a) });

    await p.exited;
    await new Promise(resolve => setTimeout(resolve, 10));
    eq('4 null', show(args[1], args[2]));
    assert(args[0] === p, 'the process');
  },

  async 'stdout to a file, and the stdio array'() {
    const file = `${ROOT}/out.txt`;
    const p = sh('echo tofile', { stdout: Bun.file(file) });

    await p.exited;
    eq('undefined', typeof p.stdout);
    eq('tofile\n', fs.readFileSync(file, 'utf8'));

    const q = Bun.spawn(['sh', '-c', 'echo a; echo b >&2'], { stdio: ['ignore', 'pipe', 'pipe'] });

    eq('a\n b\n', (await text(q.stdout)) + ' ' + (await text(q.stderr)));
  },

  async 'a command that does not exist throws ENOENT'() {
    const e = await fails(() => Bun.spawn(['nonexistent-cmd-xyz']));

    eq('Error ENOENT -2 Executable not found in $PATH: "nonexistent-cmd-xyz"', `${e.name} ${e.code} ${e.errno} ${e.message}`);
    eq('ENOENT', (await fails(() => Bun.spawn(['true'], { cwd: '/nonexistent' }))).code);
  },

  async 'cmd must be a non-empty array'() {
    eq('cmd must not be empty', (await fails(() => Bun.spawn([]))).message);
    eq('cmd must be an array', (await fails(() => Bun.spawn('ls'))).message);
    eq('cmd must be an array', (await fails(() => Bun.spawn())).message);
    eq(0, await Bun.spawn({ cmd: ['true'] }).exited);
  },

  'spawnSync returns exitCode, stdout, stderr, success and pid'() {
    const r = Bun.spawnSync(['sh', '-c', 'echo out; echo err >&2; exit 2']);

    eq('exitCode,stdout,stderr,success,pid', Object.keys(r).filter(k => k != 'resourceUsage').join());
    eq(2, r.exitCode);
    eq(false, r.success);
    eq('out\n', r.stdout.toString());
    eq('err\n', r.stderr.toString());
    assert(r.stdout instanceof Uint8Array, 'stdout bytes');
    assert(r.pid > 0, 'pid');
    eq(true, Bun.spawnSync(['true']).success);
  },

  'spawnSync takes input, env and cwd, and reports a signal'() {
    eq('in', Bun.spawnSync({ cmd: ['cat'], stdin: new TextEncoder().encode('in') }).stdout.toString());
    eq('v\n', Bun.spawnSync(['sh', '-c', 'echo $X'], { env: { X: 'v' } }).stdout.toString());
    eq('/tmp\n', Bun.spawnSync(['pwd'], { cwd: '/tmp' }).stdout.toString());

    const r = Bun.spawnSync(['sh', '-c', 'sleep 3'], { timeout: 100 });

    eq('null SIGTERM false', show(r.exitCode, r.signalCode, r.success));
  },

  async 'spawnSync with stdout inherit has no stdout, a missing command throws'() {
    const r = Bun.spawnSync(['true'], { stdout: 'inherit' });

    eq('undefined', typeof r.stdout);
    eq('ENOENT', (await fails(() => Bun.spawnSync(['nonexistent-cmd-xyz']))).code);
  },
});
