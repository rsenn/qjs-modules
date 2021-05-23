import { getenviron, exit, in as stdin, out as stdout, err as stderr } from 'std';
import { getcwd, chdir, kill, SIGTERM } from 'os';
import { hrtime } from 'misc';

const process = {
  get env() {
    return getenviron();
  },
  argv: ['qjsm', ...scriptArgs],
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
  hrtime
};

export default process;
