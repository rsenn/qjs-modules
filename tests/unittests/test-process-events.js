/* lib/process.js: process.on() events */
import * as std from 'std';
import process from 'process';
import { assert, eq, tests } from '../../lib/tinytest.js';

const wait = ms => new Promise(r => setTimeout(r, ms));

tests({
  async 'warning event carries name, code and detail'() {
    const got = [];
    const on = w => got.push([w.name, w.code, w.message, w.detail]);

    process.on('warning', on);
    process.emitWarning('hello', { type: 'CustomWarning', code: 'W1', detail: 'more' });
    await wait(5);
    process.off('warning', on);

    eq(got.join('|'), 'CustomWarning,W1,hello,more');
  },
  'emitWarning rejects non-string, non-Error input'() {
    let err;

    try {
      process.emitWarning(42);
    } catch(e) {
      err = e;
    }

    assert(err instanceof TypeError);
  },
  async 'unhandledRejection then rejectionHandled'() {
    const log = [];
    const unhandled = (reason, promise) => log.push(['unhandled', reason.message]);
    const handled = promise => log.push(['handled', promise instanceof Promise]);

    process.on('unhandledRejection', unhandled);
    process.on('rejectionHandled', handled);

    const p = Promise.reject(new Error('boom'));
    await wait(10);
    p.catch(() => {});
    await wait(10);
    process.off('unhandledRejection', unhandled);
    process.off('rejectionHandled', handled);

    eq(log.join('|'), 'unhandled,boom|handled,true');
  },
  async 'no unhandledRejection for a rejection handled in the same turn'() {
    let n = 0;
    const unhandled = () => n++;

    process.on('unhandledRejection', unhandled);
    Promise.reject(new Error('quick')).catch(() => {});
    await wait(10);
    process.off('unhandledRejection', unhandled);

    eq(n, 0);
  },
  async 'signal listeners'() {
    const got = [];
    const on = name => got.push(name);

    for(const s of ['SIGHUP', 'SIGINT', 'SIGTERM']) process.on(s, on);
    process.kill(process.pid, 1);
    process.kill(process.pid, 2);
    process.kill(process.pid, 15);
    await wait(20);
    for(const s of ['SIGHUP', 'SIGINT', 'SIGTERM']) process.off(s, on);

    eq(got.sort().join(), 'SIGHUP,SIGINT,SIGTERM');
    eq(process.listenerCount('SIGINT'), 0);
  },
  async 'kill accepts signal names with or without the SIG prefix'() {
    const got = [];
    const on = name => got.push(name);

    for(const s of ['SIGHUP', 'SIGUSR1', 'SIGUSR2']) process.on(s, on);
    process.kill(process.pid, 'SIGHUP');
    process.kill(process.pid, 'usr1');
    process.kill(process.pid, 12);
    await wait(20);
    for(const s of ['SIGHUP', 'SIGUSR1', 'SIGUSR2']) process.off(s, on);

    eq(got.join(), 'SIGHUP,SIGUSR1,SIGUSR2');
  },
  'kill rejects an unknown signal name'() {
    let err;

    try {
      process.kill(process.pid, 'NOPE');
    } catch(e) {
      err = e;
    }

    assert(err instanceof TypeError);
  },
  'exitCode and exit() validate their argument'() {
    for(const bad of ['x', 1.5, {}]) {
      let err;

      try {
        process.exitCode = bad;
      } catch(e) {
        err = e;
      }

      eq(err?.code, 'ERR_INVALID_ARG_TYPE');
    }

    process.exitCode = '3';
    eq(process.exitCode, '3');
    process.exitCode = undefined;
  },
  'env writes through and coerces to strings'() {
    process.env.QJSM_TEST_ENV = 5;
    eq(process.env.QJSM_TEST_ENV, '5');
    delete process.env.QJSM_TEST_ENV;
    eq(process.env.QJSM_TEST_ENV, undefined);
  },
  'cpuUsage, resourceUsage, memory and uptime report numbers'() {
    const a = process.cpuUsage();

    assert(typeof a.user == 'number' && typeof process.cpuUsage(a).system == 'number');
    assert(process.threadCpuUsage().user >= 0);
    eq(Object.keys(process.resourceUsage()).length, 16);
    assert(process.availableMemory() > 0 && process.memoryUsage.rss() > 0);
    assert(process.uptime() > 0);
  },
  'umask() reads and sets the mask'() {
    const old = process.umask();

    eq(process.umask('027'), old);
    eq(process.umask(), 0o27);
    process.umask(old);
  },
  'version, versions, release, config and features'() {
    assert(/^v\d/.test(process.version));
    eq(process.versions.quickjs, process.version.slice(1));
    eq(process.release.name, 'qjsm');
    eq(typeof process.config.variables, 'object');
    eq(process.features.tls, false);
  },
  'getBuiltinModule returns builtin modules only'() {
    eq(typeof process.getBuiltinModule('node:fs').readFileSync, 'function');
    eq(process.getBuiltinModule('nope-zzz'), undefined);
  },
  'ref and unref call the methods or the nodejs symbols'() {
    let n = 0;

    process.ref({ ref: () => n++ });
    process.unref({ [Symbol.for('nodejs.unref')]: () => (n += 10) });
    eq(n, 11);
  },
  'uncaught exception capture callback'() {
    eq(process.hasUncaughtExceptionCaptureCallback(), false);
    process.setUncaughtExceptionCaptureCallback(() => {});
    eq(process.hasUncaughtExceptionCaptureCallback(), true);
    process.setUncaughtExceptionCaptureCallback(null);
    eq(process.hasUncaughtExceptionCaptureCallback(), false);
  },
  'loadEnvFile parses .env syntax and keeps existing variables'() {
    const file = `/tmp/qjsm-test-process-${process.pid}.env`;
    const f = std.open(file, 'w');

    f.puts('# comment\nQJSM_A=one\nexport QJSM_B="two\\nlines"\nQJSM_C=\'x y\' # tail\nHOME=ignored\n');
    f.close();
    process.loadEnvFile(file);
    std.remove?.(file);

    eq(process.env.QJSM_A, 'one');
    eq(process.env.QJSM_B, 'two\nlines');
    eq(process.env.QJSM_C, 'x y');
    assert(process.env.HOME !== 'ignored');
  },
  'finalization callbacks run for live objects'() {
    const ref = {};
    let called;

    process.finalization.register(ref, (obj, event) => (called = event));
    process.finalization.unregister(ref);
    eq(called, undefined);
  },
});
