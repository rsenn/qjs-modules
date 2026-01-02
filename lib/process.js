import { chdir } from 'os';
import { platform } from 'os';
import { absolute } from 'path';
import { getcwd } from 'path';
import { isRelative } from 'path';
import { join } from 'path';
import { normalize } from 'path';
import { define } from 'util';
import { memoize } from 'util';
import { properties } from 'util';
import { getCommandLine } from 'misc';
import { getegid } from 'misc';
import { geteuid } from 'misc';
import { getExecutable } from 'misc';
import { getgid } from 'misc';
import { getpid } from 'misc';
import { getppid } from 'misc';
import { getuid } from 'misc';
import { hrtime } from 'misc';
import { kill } from 'misc';
import { setegid } from 'misc';
import { seteuid } from 'misc';
import { setgid } from 'misc';
import { setuid } from 'misc';
import { uname } from 'misc';
import { err as stderr } from 'std';
import { exit } from 'std';
import { getenviron } from 'std';
import { in as stdin } from 'std';
import { out as stdout } from 'std';
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
      code ??= this.exitCode;
      exit(code);
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

export default process;