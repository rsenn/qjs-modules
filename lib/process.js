import { getenviron, exit, in as stdin, out as stdout, err as stderr } from 'std';
import { getcwd, chdir, kill, SIGTERM } from 'os';
import { getCommandLine, getCurrentWorkingDirectory, getExecutable, getFileDescriptor, hrtime, uname, getpid, getuid, getgid, geteuid, getegid, setuid, setgid, seteuid, setegid } from 'misc';

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
  cwd() {
    const [str, err] = getcwd();
    if(!err) return str;
  },
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
  getuid,
  getgid,
  geteuid,
  getegid,
  setuid,
  setgid,
  seteuid,
  setegid
};

export default process;
