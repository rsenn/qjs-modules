import { chdir, platform } from 'os';
import { absolute, getcwd, isRelative, join, normalize } from 'path';
import { define, memoize, properties } from 'util';
import { getCommandLine, getegid, geteuid, getExecutable, getgid, getpid, getppid, getuid, hrtime, kill, setegid, seteuid, setgid, setuid, uname } from 'misc';
import { err as stderr, exit, getenviron, in as stdin, loadFile, out as stdout } from 'std';
import { EventEmitter } from 'events';
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
const scriptArgs = typeof globalThis.scriptArgs != 'undefined' ? globalThis.scriptArgs : [];

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
      code ??= this.exitCode ?? 0;
      this.exitCode = code;

      if(!this._exiting) {
        this._exiting = true;
        this.emit('exit', code);
      }

      exit(this.exitCode);
    },
    nextTick(fn, ...args) {
      if(typeof fn != 'function') throw new TypeError('The "callback" argument must be of type function');
      Promise.resolve().then(() => fn(...args));
    },
    /* rss is real (/proc/self/statm); QuickJS exposes no heap figures, so the rest are 0 */
    memoryUsage() {
      const pages = Number((loadFile('/proc/self/statm') ?? '0 0').split(' ')[1]);

      return { rss: pages * 4096, heapTotal: 0, heapUsed: 0, external: 0, arrayBuffers: 0 };
    },
    uptime: (start => () => (Date.now() - start) / 1000)(Date.now()),
    title: 'qjsm',
    emitWarning(warning, type = 'Warning') {
      const name = typeof warning == 'string' ? type : warning.name;
      const message = typeof warning == 'string' ? warning : warning.message;

      stderr.puts(`(${name}) ${message}\n`);
    },
    hrtime,
    async importModule(p) {
      if(/^\.\.?\//.test(p) && isRelative(p)) {
        p = join(__dirname ?? getcwd(), p);
        p = absolute(p);
        p = normalize(p);
        console.log('importModule', { p });
      }
      let g = p.indexOf('*') == 0;
      let m = await import((p = p.slice(g ? 1 : 0)));
      g ? Object.assign(globalThis, m) : (globalThis[p.slice(p.lastIndexOf('/') + 1).replace(/\.[^\/.]+$/g, '')] = m);
      return m;
    },
    platform,

    /* prettier-ignore */ get uid() { return getuid(); },
    /* prettier-ignore */ get gid() { return getgid(); },
    /* prettier-ignore */ get euid() { return geteuid(); },
    /* prettier-ignore */ get egid() { return getegid(); },

    /* prettier-ignore */ set uid(v) { setuid(v); },
    /* prettier-ignore */ set gid(v) { setgid(v); },
    /* prettier-ignore */ set euid(v) { seteuid(v); },
    /* prettier-ignore */ set egid(v) { setegid(v); },
  },
  properties(
    {
      env: getenviron,
      /* prettier-ignore */ argv() { return [this.execPath].concat(scriptArgs); },
      /* prettier-ignore */ argv0() { return scriptArgs[-1] ?? this.execPath; },
      execArgv: ((cl, idx) => ((idx = scriptIndex(cl)), () => cl.slice(1, scriptIndex(cl))))(getCommandLine()),
      execPath: () => getExecutable() ?? 'qjsm',
      pid: getpid,
      ppid: getppid,
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
    { memoize: true },
  ),
);

if(typeof globalThis.BigInt == 'function')
  process.hrtime.bigint = () => {
    let [s, ns] = process.hrtime();
    return BigInt(s) * BigInt(1e9) + BigInt(ns);
  };

Object.setPrototypeOf(process, EventEmitter.prototype);

export default process;
