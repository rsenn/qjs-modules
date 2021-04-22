import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect';
import child_process from 'child_process';
import Console from '../lib/console.js';
import { toString } from 'mmap';

('use strict');

globalThis.inspect = inspect;

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log('Wrote "' + file + '": ' + data.length + ' bytes');
}

function DumpChildProcess(cp) {
  const { file, cwd, args, env, stdio, pid, exitcode, termsig } = cp;

  console.log(`ChildProcess`, { file, cwd, args, env, stdio, pid, exitcode, termsig });
}
const inspectOptions = {
  colors: true,
  showHidden: false,
  customInspect: true,
  showProxy: false,
  getters: false,
  depth: 4,
  maxArrayLength: 10,
  maxStringLength: 200,
  compact: 2,
  hideKeys: ['loc', 'range', 'inspect', Symbol.for('nodejs.util.inspect.custom')]
};

function ReadChild(...args) {
  let cmd = args.shift();
  let cp = child_process.spawn(cmd, args, { stdio: 'pipe' });
  let data = '';
  DumpChildProcess(cp);

  let [stdin, stdout, stderr] = cp.stdio;
  console.log('stdio:', { stdin, stdout, stderr });

  let buf = new ArrayBuffer(4096);
  let ret;

  while((ret = os.read(stdout, buf, 0, buf.byteLength)) > 0) {
    let chunk = toString(buf.slice(0, ret));
    console.log('chunk:', chunk);
    data += chunk;
  }
  cp.wait();

  DumpChildProcess(cp);
  return data;
}

function main(...args) {
  console = new Console(inspectOptions);

  let data = ReadChild('ls', '-la');

  console.log('data:', data);

  data = ReadChild('lz4', '-9', '-f', '/etc/services', 'services.lz4');

  console.log('data:', data);
  data = ReadChild('lz4', '-dc', 'services.lz4');

  console.log('data:', data);
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
