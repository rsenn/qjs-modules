import { getenviron, exit, in as stdin, out as stdout, err as stderr } from 'std';
import { getcwd, chdir, kill, SIGTERM } from 'os';
import {
  hrtime,
  uname,
  getpid,
  getuid,
  getgid,
  geteuid,
  getegid,
  setuid,
  setgid,
  seteuid,
  setegid
} from 'misc';

const process = {
  /* prettier-ignore */ get env() {
    return getenviron();
  },
  argv: ['qjsm', ...(globalThis.scriptArgs || [])],
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
  /* prettier-ignore */ get arch() {
    const { machine } = uname();

    switch (machine) {
      case 'aarch64': return 'arm64';
      case 'x86_64':
        return 'x64';
      case 'i386':
      case 'i486':
      case 'i586':
      case 'i686':
        return 'x32';
      default: return machine;
    }
  },
  /* prettier-ignore */ get pid() {
    return getpid();
  },
  /* prettier-ignore */ get ppid() {
    return getppid();
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
