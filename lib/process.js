import { getenviron, exit, in as stdin, out as stdout, err as stderr } from 'std';
import { getcwd, chdir, kill, SIGTERM } from 'os';

const process = {
  get env() {
    return getenviron();
  },
  args: ['qjsm', ...scriptArgs],
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
  }
};

export default process;
