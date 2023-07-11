import { absolute, getcwd, isRelative, join, normalize } from 'path';
import { err as stderr, exit, getenviron, in as stdin, out as stdout } from 'std';
import { chdir, SIGTERM } from 'os';
import { getCommandLine, getCurrentWorkingDirectory, getExecutable, getFileDescriptor, hrtime, uname, kill, getpid } from 'misc';

(proto => {
  const { write, puts } = proto;

  proto.write = function(...args) {
    return (typeof args[0] == 'string' ? puts : write).call(this, ...args);
  };
})(Object.getPrototypeOf(stdout));

const scriptArgv = () => [...(globalThis.scriptArgs || [])];

const scriptIndex = () => {
  let index,
    scriptv = scriptArgv(),
    commandLine = getCommandLine();

  if((index = commandLine.indexOf(scriptv[0])) == -1) index = commandLine.length - scriptv.length;
  return index;
};

const getExecPath = () => getExecutable() ?? 'qjsm';
const getExecArgv = () => getCommandLine().slice(1, scriptIndex());
const getArgv0 = () => getExecPath();
const getArgv = () => [getArgv0()].concat(scriptArgv());

const process = {
  get env() {
    return getenviron();
  },
  get argv() {
    return getArgv();
  },
  get argv0() {
    return getArgv0();
  },
  get execPath() {
    return getExecPath();
  },
  get execArgv() {
    return getExecArgv();
  },
  /*  argv: getArgv(),
  argv0: getArgv0(),
  execPath: getExecPath(),
  execArgv: getExecArgv(),*/
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
  get arch() {
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
  get pid() {
    return getpid();
  },
  /*  getuid,
  getgid,
  geteuid,
  getegid,
  setuid,
  setgid,
  seteuid,
  setegid,*/
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
  }
};

if(typeof globalThis.BigInt == 'function')
  process.hrtime.bigint = () => {
    let [s, ns] = process.hrtime();
    return BigInt(s) * BigInt(1e9) + BigInt(ns);
  };

export default process;
