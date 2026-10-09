import * as os from 'os';
import { chdir, platform } from 'os';
import { absolute, getcwd } from 'path';
import { declare, define, memoize, properties } from 'util';
import { exec, getCommandLine, getegid, geteuid, getExecutable, getgid, getpid, getppid, getRelease, getuid, hrtime, initgroups as initgroups_, kill, setegid as setegid_, seteuid as seteuid_, setgid as setgid_, setgroups as setgroups_, setuid as setuid_, signum, uname, umask as umask_ } from 'misc';
import { err as stderr, exit as exit_, getenviron, in as stdin, loadFile, out as stdout, open, setenv, unsetenv } from 'std';
import { EventEmitter } from 'node:events';

(proto => {
  const { write, puts } = proto;
  proto.write = function(...args) {
    return (typeof args[0] == 'string' ? puts : write).call(this, ...args);
  };
})(Object.getPrototypeOf(stdout));

const numDefault =
  (invalid = -1) =>
  (i, def) =>
    i == invalid ? def : i;

/* qjsm only defines the global `scriptArgs` for file/script execution, not for `-e` expr eval */
const { scriptArgs = [] } = globalThis;
const scriptIndex = (cl = getCommandLine(), a = scriptArgs) => numDefault()(cl.indexOf(a[0]), cl.length - a.length);

const process = define(
  {
    cwd: getcwd,
    chdir,
    kill,
    stdin,
    stdout,
    stderr,
    exit(code) {
      checkExitCode(code);
      exit_(Number(emitExit(code)));
    },
    nextTick(fn, ...args) {
      if(typeof fn != 'function') throw new TypeError('The "callback" argument must be of type function');
      Promise.resolve().then(() => fn(...args));
    },
    /* rss is real (/proc/self/statm); QuickJS exposes no heap figures, so the rest are 0 */
    memoryUsage() {
      const pages = Number((readFile('/proc/self/statm') ?? '0 0').split(' ')[1]);

      return { rss: pages * 4096, heapTotal: 0, heapUsed: 0, external: 0, arrayBuffers: 0 };
    },
    title: 'qjsm',
    emitWarning(warning, type, code, ctor) {
      let detail;

      if(type !== null && typeof type == 'object') ({ type, code, detail } = type);
      else if(typeof type == 'function') type = code = undefined;
      else if(typeof code == 'function') code = undefined;

      if(typeof warning == 'string') {
        const message = warning;

        warning = new Error(message);
        warning.name = String(type ?? 'Warning');
        if(code !== undefined) warning.code = code;
        if(detail !== undefined) warning.detail = detail;
      } else if(!(warning instanceof Error)) {
        throw new TypeError('The "warning" argument must be of type string or an instance of Error');
      }

      process.nextTick(() => {
        process.emit('warning', warning);
        stderr.puts(`(${process.title}:${process.pid}) ${warning.code ? `[${warning.code}] ` : ''}${warning.name}: ${warning.message}\n${warning.detail ? `${warning.detail}\n` : ''}`);
      });
    },
    hrtime,
    platform,

    /* prettier-ignore */ get uid() { return getuid(); },
    /* prettier-ignore */ get gid() { return getgid(); },
    /* prettier-ignore */ get euid() { return geteuid(); },
    /* prettier-ignore */ get egid() { return getegid(); },

    /* prettier-ignore */ set uid(v) { setuid_(v); },
    /* prettier-ignore */ set gid(v) { setgid_(v); },
    /* prettier-ignore */ set euid(v) { seteuid_(v); },
    /* prettier-ignore */ set egid(v) { setegid_(v); },

    get pid() { return getpid(); },
    get ppid() { return getppid(); },
  },
  properties(
    {
      env: () => envProxy(getenviron()),
      /* prettier-ignore */ argv() { return [this.execPath].concat(scriptArgs.length ? [absolute(scriptArgs[0]), ...scriptArgs.slice(1)] : []); },
      /* prettier-ignore */ argv0() { return scriptArgs[-1] ?? getCommandLine()[0] ?? this.execPath; },
      execArgv: ((cl, idx) => ((idx = scriptIndex(cl)), () => cl.slice(1, scriptIndex(cl))))(getCommandLine()),
      execPath: () => getExecutable() ?? 'qjsm',
      arch() {
        const { machine } = uname();

        switch (machine) {
          case 'aarch64':
            return 'arm64';
          case 'x86_64':
            return 'x64';
          case 'i386':
          case 'i486':
          case 'i586':
          case 'i686':
            return 'x32';
          default:
            return machine;
        }
      },
    },
    { memoize: true, enumerable: true },
  ),
);

if(typeof globalThis.BigInt == 'function')
  process.hrtime.bigint = () => {
    let [s, ns] = process.hrtime();
    return BigInt(s) * BigInt(1e9) + BigInt(ns);
  };

Object.setPrototypeOf(process, EventEmitter.prototype);

let exiting = false;

/* emits 'exit' once; an explicit `code` wins over process.exitCode, listeners may still change it */
function emitExit(code) {
  if(code !== undefined) process.exitCode = code;

  if(!exiting) {
    exiting = true;
    runFinalizers('exit');
    process.emit('exit', Number(process.exitCode ?? 0));
  }

  return Number(process.exitCode ?? 0);
}

/* 'uncaughtExceptionMonitor' always fires; true when an 'uncaughtException' listener took the error */
const isObject = v => v !== null && (typeof v == 'object' || typeof v == 'function');
/* errors given to 'uncaughtException': the engine also rejects internal promises with them */
const handledErrors = new WeakSet();

function emitUncaught(err, origin) {
  if(isObject(err)) handledErrors.add(err);

  process.emit('uncaughtExceptionMonitor', err, origin);

  for(const fn of [...captureCallbacks].reverse()) if(fn(err) === true || captureCallbacks.length == 1) return true;

  if(captureCallbacks.length) return true;

  if(!process.listenerCount('uncaughtException')) return false;

  process.emit('uncaughtException', err, origin);
  return true;
}

/* rejections still unhandled one timer turn after they happened, and those already reported */
const rejections = new Map();
const reported = new WeakSet();

function checkRejections() {
  for(const [promise, reason] of rejections) {
    rejections.delete(promise);

    if(isObject(reason) && handledErrors.has(reason)) continue;

    if(process.listenerCount('unhandledRejection')) {
      reported.add(promise);
      process.emit('unhandledRejection', reason, promise);
    } else if(!emitUncaught(reason, 'unhandledRejection')) {
      stderr.puts(`Possibly unhandled promise rejection: ${reason?.stack ?? reason}\n`);
      process.exit(1);
    }
  }
}

/* called from src/qjsm.c; a true result from `rejection` and `uncaught` means "handled, don't crash" */
declare(process, {
  __qjsm_hooks__: {
    rejection(promise, reason, handled) {
      if(handled) {
        if(!rejections.delete(promise) && reported.has(promise)) {
          reported.delete(promise);
          process.emit('rejectionHandled', promise);
        }

        return false;
      }

      if(!process.listenerCount('unhandledRejection') && !process.listenerCount('uncaughtException') && !captureCallbacks.length) return false;

      if(!rejections.size) os.setTimeout(checkRejections, 0);
      rejections.set(promise, reason);
      return true;
    },
    uncaught: err => emitUncaught(err, 'uncaughtException'),
    beforeExit() {
      runFinalizers('beforeExit');

      if(!process.listenerCount('beforeExit')) return false;

      process.emit('beforeExit', process.exitCode ?? 0);
      return true;
    },
    exit(code) {
      if(process.exitCode === undefined && code) process.exitCode = code;

      return emitExit();
    },
  },
});

/* --- helpers for the Node API surface below --- */

/* whole file as a string, or null; unlike readFile() it also reads /proc files, which report size 0 */
function readFile(path) {
  const f = open(path, 'r');

  if(!f) return null;

  const text = f.readAsString();

  f.close();
  return text;
}

const isInt = v => (typeof v == 'number' && Number.isInteger(v)) || (typeof v == 'string' && /^-?\d+$/.test(v));
const received = v => (v === null ? 'null' : typeof v == 'object' ? `an instance of ${v.constructor?.name ?? 'Object'}` : `type ${typeof v} (${String(v)})`);
const argError = (name, type, v) => Object.assign(new TypeError(`The "${name}" argument must be ${type}. Received ${received(v)}`), { code: 'ERR_INVALID_ARG_TYPE' });

function checkExitCode(code) {
  if(code !== undefined && code !== null && !isInt(code)) throw argError('code', 'of type number', code);
}

/* process.env writes through to the real environment; values become strings */
function envProxy(env) {
  return new Proxy(env, {
    set(target, key, value) {
      if(typeof key != 'string') return false;

      target[key] = String(value);
      setenv(key, String(value));
      return true;
    },
    defineProperty(target, key, desc) {
      if('value' in desc && typeof key == 'string') setenv(key, (desc.value = String(desc.value)));

      return Reflect.defineProperty(target, key, desc);
    },
    deleteProperty(target, key) {
      if(typeof key == 'string') unsetenv(key);

      return Reflect.deleteProperty(target, key);
    },
  });
}

/* fields of /proc/<path>/stat by their number in proc(5): field(14) is utime */
function procStat(path = 'self') {
  const text = readFile(`/proc/${path}/stat`);

  if(text === null) return () => 0;

  const rest = text.slice(text.lastIndexOf(')') + 2).split(' ');

  return n => Number(rest[n - 3]);
}

/* `Key:  value` lines of /proc/self/status or /proc/meminfo as { Key: 'value' } */
function procFields(path) {
  const ret = {};

  for(const line of (readFile(path) ?? '').split('\n')) {
    const i = line.indexOf(':');

    if(i > 0) ret[line.slice(0, i)] = line.slice(i + 1).trim();
  }

  return ret;
}

/* a user or group given by name or number as its number; names come from `file` (/etc/passwd, /etc/group) */
function lookupId(value, file, what) {
  if(isInt(value)) return Number(value);

  if(typeof value == 'string') {
    for(const line of (readFile(file) ?? '').split('\n')) {
      const f = line.split(':');

      if(f[0] === value) return Number(f[2]);
    }

    throw Object.assign(new Error(`${what} identifier does not exist: ${value}`), { code: 'ERR_UNKNOWN_CREDENTIAL' });
  }

  throw argError('id', 'one of type number or string', value);
}

const userId = v => lookupId(v, '/etc/passwd', 'User');
const groupId = v => lookupId(v, '/etc/group', 'Group');

function userName(value) {
  if(typeof value == 'string' && !isInt(value)) return value;

  for(const line of (readFile('/etc/passwd') ?? '').split('\n')) {
    const f = line.split(':');

    if(Number(f[2]) === Number(value)) return f[0];
  }

  throw Object.assign(new Error(`User identifier does not exist: ${value}`), { code: 'ERR_UNKNOWN_CREDENTIAL' });
}

/* CPU times are in clock ticks of 1/100 s, reported in microseconds */
const TICK_US = 10000;

function cpuTimes(stat, prev) {
  if(prev !== undefined) {
    if(prev === null || typeof prev != 'object') throw argError('prevValue', 'of type object', prev);
    for(const k of ['user', 'system']) if(typeof prev[k] != 'number') throw argError(`prevValue.${k}`, 'of type number', prev[k]);
  }

  const ret = { user: stat(14) * TICK_US, system: stat(15) * TICK_US };

  if(prev) {
    ret.user -= prev.user;
    ret.system -= prev.system;
  }

  return ret;
}

const qjsVersion = (getRelease().sourceUrl.match(/quickjs-(.*)\.tar/) ?? [])[1] ?? '';
const isTTY = fd => (os.isatty(fd) ? true : undefined);

/* .env syntax: KEY=value, `export KEY=value`, '..' and ".." quotes, # comments */
function parseEnv(text) {
  const ret = {};

  for(const line of text.split(/\r?\n/)) {
    const m = /^\s*(?:export\s+)?([\w.-]+)\s*=\s*(.*?)\s*$/.exec(line);

    if(!m) continue;

    let [, key, value] = m;
    const q = value[0];

    if((q == '"' || q == "'" || q == '`') && value.lastIndexOf(q) > 0) {
      value = value.slice(1, value.lastIndexOf(q));
      if(q == '"') value = value.replace(/\\n/g, '\n');
    } else {
      value = value.replace(/\s+#.*$/, '');
    }

    ret[key] = value;
  }

  return ret;
}

/* --- finalization: callbacks for objects still alive at 'exit' / 'beforeExit' --- */

const finalizers = new Set();

function runFinalizers(event) {
  for(const entry of [...finalizers]) {
    if(entry.event != event) continue;

    const ref = entry.ref.deref();

    if(ref === undefined) finalizers.delete(entry);
    else entry.callback(ref, event);
  }
}

const register = event => (ref, callback) => {
  if(!isObject(ref)) throw argError('ref', 'of type object or function', ref);
  if(typeof callback != 'function') throw argError('callback', 'of type function', callback);

  finalizers.add({ ref: new WeakRef(ref), callback, event });
};

let exitCodeValue;
const captureCallbacks = [];
let sourceMapsFlag = false;

const refSymbol = Symbol.for('nodejs.ref');
const unrefSymbol = Symbol.for('nodejs.unref');

/* --- the Node API surface --- */

Object.assign(process, {
  assert(value, message) {
    if(!value) throw Object.assign(new Error(message ?? 'assertion error'), { code: 'ERR_ASSERTION' });
  },
  binding(name) {
    throw new Error(`No such module: ${name}`);
  },
  abort() {
    kill(getpid(), 6);
  },
  reallyExit(code) {
    exit_(Number(code ?? process.exitCode ?? 0));
  },
  openStdin() {
    return stdin;
  },

  getuid,
  getgid,
  geteuid,
  getegid,
  setuid: id => setuid_(userId(id)),
  setgid: id => setgid_(groupId(id)),
  seteuid: id => seteuid_(userId(id)),
  setegid: id => setegid_(groupId(id)),
  getgroups() {
    const groups = (procFields('/proc/self/status').Groups ?? '').split(' ').filter(Boolean).map(Number);

    if(!groups.includes(getegid())) groups.push(getegid());
    return groups;
  },
  setgroups(groups) {
    if(!Array.isArray(groups)) throw argError('groups', 'an instance of Array', groups);

    setgroups_(groups.map(groupId));
  },
  initgroups(user, extraGroup) {
    initgroups_(userName(user), groupId(extraGroup));
  },
  umask(mask) {
    if(mask === undefined) {
      const old = umask_(0);

      umask_(old);
      return old;
    }

    if(typeof mask == 'string') {
      if(!/^[0-7]+$/.test(mask)) throw argError('mask', 'a valid octal string', mask);
      mask = parseInt(mask, 8);
    }

    if(!Number.isInteger(mask)) throw argError('mask', 'of type number', mask);

    return umask_(mask);
  },

  cpuUsage: prev => cpuTimes(procStat('self'), prev),
  threadCpuUsage: prev => cpuTimes(procStat('thread-self'), prev),
  resourceUsage() {
    const stat = procStat('self');
    const status = procFields('/proc/self/status');
    const io = procFields('/proc/self/io');
    const kb = v => parseInt(v ?? '0') || 0;

    return {
      userCPUTime: stat(14) * TICK_US,
      systemCPUTime: stat(15) * TICK_US,
      maxRSS: kb(status.VmHWM),
      sharedMemorySize: 0,
      unsharedDataSize: kb(status.VmData),
      unsharedStackSize: kb(status.VmStk),
      minorPageFault: stat(10),
      majorPageFault: stat(12),
      swappedOut: 0,
      fsRead: kb(io.syscr),
      fsWrite: kb(io.syscw),
      ipcSent: 0,
      ipcReceived: 0,
      signalsCount: 0,
      voluntaryContextSwitches: kb(status.voluntary_ctxt_switches),
      involuntaryContextSwitches: kb(status.nonvoluntary_ctxt_switches),
    };
  },
  availableMemory() {
    return (parseInt(procFields('/proc/meminfo').MemAvailable) || 0) * 1024;
  },
  constrainedMemory() {
    for(const f of ['/sys/fs/cgroup/memory.max', '/sys/fs/cgroup/memory/memory.limit_in_bytes']) {
      const text = readFile(f)?.trim();
      const limit = Number(text);

      if(text && Number.isFinite(limit) && limit < 2 ** 60) return limit;
    }

    return 0;
  },
  uptime() {
    const system = Number((readFile('/proc/uptime') ?? '0').split(' ')[0]);

    return system - procStat('self')(22) / 100;
  },

  getActiveResourcesInfo: () => [],

  getBuiltinModule(id) {
    if(typeof id != 'string') throw argError('id', 'of type string', id);

    const name = id.replace(/^node:/, '');

    return requireModule('module').isBuiltin(name) ? requireModule(name) : undefined;
  },
  dlopen(module, filename, flags) {
    if(!isObject(module)) throw argError('module', 'of type object', module);

    module.exports = requireModule(String(filename));
  },
  execve(file, args = [], env) {
    if(typeof file != 'string') throw argError('file', 'of type string', file);
    if(!Array.isArray(args)) throw argError('args', 'an instance of Array', args);

    const saved = { ...getenviron() };

    if(env !== undefined) {
      for(const k of Object.keys(saved)) unsetenv(k);
      for(const [k, v] of Object.entries(env)) setenv(k, String(v));
    }

    try {
      exec(file, args.map(String));
    } finally {
      if(env !== undefined) {
        for(const k of Object.keys(getenviron())) unsetenv(k);
        for(const [k, v] of Object.entries(saved)) setenv(k, v);
      }
    }
  },

  loadEnvFile(path = '.env') {
    const text = readFile(String(path));

    if(text === null) throw Object.assign(new Error(`ENOENT: no such file or directory, open '${path}'`), { code: 'ENOENT', syscall: 'open', path: String(path) });

    for(const [key, value] of Object.entries(parseEnv(text))) if(!(key in process.env)) process.env[key] = value;
  },

  ref(obj) {
    if(isObject(obj)) (obj[refSymbol] ?? obj.ref)?.call(obj);
  },
  unref(obj) {
    if(isObject(obj)) (obj[unrefSymbol] ?? obj.unref)?.call(obj);
  },

  hasUncaughtExceptionCaptureCallback: () => captureCallbacks.length > 0,
  setUncaughtExceptionCaptureCallback(fn) {
    if(fn === null) return void (captureCallbacks.length = 0);
    if(typeof fn != 'function') throw argError('fn', 'of type function or null', fn);
    if(captureCallbacks.length) throw Object.assign(new Error('`process.setupUncaughtExceptionCapture()` was called while a capture callback was already active'), { code: 'ERR_UNCAUGHT_EXCEPTION_CAPTURE_ALREADY_SET' });

    captureCallbacks.push(fn);
  },
  addUncaughtExceptionCaptureCallback(fn) {
    if(typeof fn != 'function') throw argError('fn', 'of type function', fn);

    captureCallbacks.push(fn);
  },

  setSourceMapsEnabled(val) {
    if(typeof val != 'boolean') throw argError('val', 'of type boolean', val);

    sourceMapsFlag = val;
  },

  debugPort: 9229,
  domain: null,
  mainModule: undefined,
  allowedNodeEnvironmentFlags: new Set(),
  finalization: Object.freeze({
    register: register('exit'),
    registerBeforeExit: register('beforeExit'),
    unregister(ref) {
      for(const entry of finalizers) if(entry.ref.deref() === ref) finalizers.delete(entry);
    },
  }),
});

process.memoryUsage.rss = () => process.memoryUsage().rss;

define(
  process,
  properties(
    {
      exitCode: [
        () => exitCodeValue,
        code => {
          checkExitCode(code);
          exitCodeValue = code;
        },
      ],
      sourceMapsEnabled: () => sourceMapsFlag,
      moduleLoadList: () => Object.values(globalThis.moduleList ?? {}).map(m => `${m.builtin ? 'NativeModule' : 'Module'} ${m.name}`),
    },
    { enumerable: true, configurable: true },
  ),
  {
    version: `v${qjsVersion}`,
    versions: Object.freeze({ quickjs: qjsVersion, qjsm: qjsVersion }),
    release: Object.freeze({ ...getRelease(), name: 'qjsm' }),
    config: Object.freeze({
      target_defaults: Object.freeze({ cflags: [], default_configuration: 'Release', defines: [], include_dirs: [], libraries: [] }),
      variables: Object.freeze({ host_arch: process.arch, target_arch: process.arch, quickjs_version: qjsVersion }),
    }),
    features: Object.freeze({ inspector: false, debug: false, uv: false, ipv6: true, tls_alpn: false, tls_ocsp: false, tls_sni: false, tls: false, cached_builtins: true, require_module: true, typescript: false }),
    report: {
      compact: false,
      directory: '',
      filename: '',
      reportOnFatalError: false,
      reportOnSignal: false,
      reportOnUncaughtException: false,
      signal: 'SIGUSR2',
      excludeNetwork: false,
      getReport(err) {
        return {
          header: {
            reportVersion: 3,
            event: 'JavaScript API',
            trigger: 'GetReport',
            filename: null,
            dumpEventTime: new Date().toISOString(),
            processId: process.pid,
            cwd: process.cwd(),
            commandLine: process.argv,
            nodejsVersion: process.version,
            arch: process.arch,
            platform: process.platform,
            host: uname().nodename,
          },
          javascriptStack: { message: err ? String(err) : 'No stack.', stack: String((err ?? new Error()).stack ?? '').split('\n').slice(1) },
          resourceUsage: process.resourceUsage(),
          environmentVariables: { ...process.env },
        };
      },
      writeReport(file, err) {
        if(typeof file != 'string') [file, err] = [`report.${new Date().toISOString().replace(/[-:.]/g, '')}.${process.pid}.json`, file];

        const f = open(file, 'w');

        f.puts(JSON.stringify(process.report.getReport(err), null, process.report.compact ? 0 : 2));
        f.close();
        return file;
      },
    },
  },
);

/* stdio: Node's tty-stream properties on the std file objects */
for(const [stream, fd] of [[stdin, 0], [stdout, 1], [stderr, 2]]) {
  define(
    stream,
    properties(
      {
        fd: () => fd,
        isTTY: () => isTTY(fd),
        columns: () => (os.isatty(fd) ? os.ttyGetWinSize(fd)?.[0] : undefined),
        rows: () => (os.isatty(fd) ? os.ttyGetWinSize(fd)?.[1] : undefined),
      },
      { configurable: true },
    ),
  );

  if(fd) {
    stream.getWindowSize = () => os.ttyGetWinSize(fd) ?? [80, 24];
    stream.getColorDepth = (env = process.env) => (!os.isatty(fd) || 'NO_COLOR' in env ? 1 : /truecolor|24bit/i.test(env.COLORTERM ?? '') ? 24 : /256/.test(env.TERM ?? '') ? 8 : env.TERM && env.TERM != 'dumb' ? 4 : 1);
    stream.hasColors = (count = 16, env = process.env) => 2 ** stream.getColorDepth(env) >= count;
  }
}


/* process.on('SIGINT', fn) installs an os.signal() handler for as long as a listener exists */
const signals = new Set();
const signalNumber = name => {
  try {
    return signum(name);
  } catch(e) {}
};
const isSignal = type => typeof type == 'string' && /^SIG[A-Z0-9]+$/.test(type) && type != 'SIGKILL' && type != 'SIGSTOP' && signalNumber(type) !== undefined;

process.on('newListener', type => {
  if(!isSignal(type) || signals.has(type)) return;

  signals.add(type);
  os.signal(signalNumber(type), () => process.emit(type, type));
});

process.on('removeListener', type => {
  if(!signals.has(type) || process.listenerCount(type)) return;

  signals.delete(type);
  os.signal(signalNumber(type), undefined);
});

export { chdir, getcwd as cwd, getegid, geteuid, getgid, getuid, hrtime, kill, platform, stderr, stdin, stdout };
export const { abort, allowedNodeEnvironmentFlags, arch, argv, argv0, assert, availableMemory, binding, config, constrainedMemory, cpuUsage, debugPort, dlopen, domain, emitWarning, env, execArgv, execPath, execve, exit, exitCode, features, finalization, getActiveResourcesInfo, getBuiltinModule, getgroups, hasUncaughtExceptionCaptureCallback, initgroups, loadEnvFile, mainModule, memoryUsage, moduleLoadList, nextTick, openStdin, pid, ppid, reallyExit, ref, release, report, resourceUsage, setSourceMapsEnabled, setUncaughtExceptionCaptureCallback, addUncaughtExceptionCaptureCallback, setegid, seteuid, setgid, setgroups, setuid, sourceMapsEnabled, threadCpuUsage, title, umask, unref, uptime, version, versions } = process;
export default process;