import { spawn } from 'child_process';
import * as os from 'os';
import { toString } from 'util';
import Console from '../lib/console.js';
import * as std from 'std';
function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log('Wrote "' + file + '": ' + data.length + ' bytes');
}

function ReadChild(args) {
  let cmd = args.shift();
  let child = spawn(cmd, args, { stdio: ['inherit', 'pipe', 'inherit'] });
  let data = '';

  let [stdin, stdout, stderr] = child.stdio;

  let buf = new ArrayBuffer(4096);
  let ret;

  os.setReadHandler(stdout, () => {
    ret = os.read(stdout, buf, 0, buf.byteLength);

    if(ret > 0) {
      const chunk = buf.slice(0, ret);
      const str = toString(chunk);
      console.log('chunk:', str);
      data += str;
    }

    if(ret <= 0 || ret < buf.byteLength) {
      console.log('stdout', stdout);
      os.setReadHandler(stdout, null);
    }
  });

  console.log('child', child);

  return [child, data];
}

function main(...args) {
  globalThis.console = new Console({
    inspectOptions_: {
      maxArrayLength: 10,
      maxStringLength: 200,
      compact: 2,
    },
  });

  let [child, data] = ReadChild(['sh', '-c', 'ls -latr | tail -n10']);

  console.log('ReadChild', { child, data });
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}