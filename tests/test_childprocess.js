import { signal, spawn, WNOHANG } from 'child_process';
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

  /* console.log('child', child);*/

  let [stdin, stdout, stderr] = child.stdio;
  /*console.log('stdio:', { stdin, stdout, stderr });*/

  let buf = new ArrayBuffer(4096);
  let ret;

  os.setReadHandler(stdout, () => {
    ret = os.read(stdout, buf, 0, buf.byteLength);
    /*console.log('buf.byteLength:', buf.byteLength);
    console.log('ret:', ret);*/

    if(ret > 0) {
      const chunk = buf.slice(0, ret);
      const str = toString(chunk);
      /* console.log('chunk:', str);*/
      data += str;
    }

    if(ret <= 0 || ret < buf.byteLength) {
      console.log('stdout', stdout);
      os.setReadHandler(stdout, null);
    }
  });

  /* while((ret = os.read(stdout, buf, 0, buf.byteLength)) > 0) {
    let chunk = toString(buf.slice(0, ret));
    // console.log('chunk:', chunk);
    data += chunk;
  }*/

  let retval = child.wait();
  console.log('retval', retval);

  console.log('child', child);

  return data;
}

function main(...args) {
  globalThis.console = new Console({
    inspectOptions_: {
      maxArrayLength: 10,
      maxStringLength: 200,
      compact: 2,
    },
  });

  signal(() => {
    const errfn = a => ({ pid: a[0] >= 0 ? a[0] : -1, errno: a[0] < 0 ? -a[0] : 0, status: a[1] });
    console.log('SIGCHLD');
    while(true) {
      const { pid, errno, status } = errfn(os.waitpid(-1, os.WNOHANG));
      console.log('waitpid() = ', pid, pid == -1 ? ` errno = ${std.strerror(errno)}` : ` status = ${status}`);
      if(pid == -1) break;
    }
  });

  let data = ReadChild(['sh', '-c', 'ls -latr | tail -n10']);

  console.log('data:', data);

  /*  data = ReadChild('lz4', '-9', '-f', '/etc/services', 'services.lz4');

  console.log('data:', data.slice(0, 100));
  data = ReadChild('lz4', '-dc', 'services.lz4');

  console.log('data:', data.slice(0, 100));*/
}

try {
  main(...scriptArgs.slice(1));
  //std.exit(0);
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
