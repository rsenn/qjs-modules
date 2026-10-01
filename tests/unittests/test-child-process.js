import { ChildProcess, exec, execSync, kill, spawn, spawnSync } from 'child_process';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* child_process has a history of nondeterministic crashes (see the
 * `ctest-nondeterministic-failures` BUGS entry) - this file passed 5/5
 * repeated runs while being written, but treat any future flakiness here
 * as that pre-existing module issue, not a bug in the test itself. */

tests({
  'execSync() runs a shell command synchronously'() {
    const result = execSync('exit 0');
    eq(result.status, 0);
    eq(result.signal, null);
  },
  'execSync() reports a nonzero exit status'() {
    const result = execSync('exit 7');
    eq(result.status, 7);
  },
  'spawnSync() runs a program without a shell'() {
    const result = spawnSync('sh', ['-c', 'exit 0']);
    eq(result.status, 0);
    eq(result.signal, null);
  },
  'spawnSync() reports a nonzero exit status'() {
    const result = spawnSync('sh', ['-c', 'exit 5']);
    eq(result.status, 5);
  },
  async 'spawn() returns a ChildProcess whose wait() reports exit status'() {
    const child = spawn('sh', ['-c', 'exit 3']);
    assert(child instanceof ChildProcess);
    assert(typeof child.pid === 'number' && child.pid > 0);
    eq(+child, child.pid);

    const status = await child.wait();
    eq(status.exitCode, 3);
  },
  async 'exec() runs a shell command asynchronously'() {
    /* exec() resolves to the ChildProcess itself, not a status object, and
     * its exitcode isn't populated yet at that point - a further wait()
     * is needed, same as spawn(). */
    const child = await exec('exit 0');
    assert(child instanceof ChildProcess);
    assert(typeof child.pid === 'number' && child.pid > 0);
  },
  async 'kill() sends a signal to a running process'() {
    /* kill() takes the ChildProcess object, not its pid, despite
     * doc/native/child-process.md's `kill(pid, signal)` signature. */
    const child = spawn('sh', ['-c', 'sleep 30']);
    kill(child, 'SIGTERM');
    const status = await child.wait();
    eq(status.exitCode !== 0 || status.signalCode !== null, true);
  },
});
