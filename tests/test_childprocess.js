import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect';
import child_process from 'child_process';
import Console from '../lib/console.js';
import { toString } from 'util';

('use strict');

globalThis.inspect = inspect;

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log('Wrote "' + file + '": ' + data.length + ' bytes');
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
  hideKeys: ['loc', 'range', 'inspect', Symbol.toStringTag, Symbol.for('nodejs.util.inspect.custom')]
};

function ReadChild(...args) {
  let cmd = args.shift();
  let child = child_process.spawn(cmd, args, { stdio: 'pipe' });
  let data = '';
  console.log('child', child);

  let [stdin, stdout, stderr] = child.stdio;
  console.log('stdio:', { stdin, stdout, stderr });

  let buf = new ArrayBuffer(4096);
  let ret;

  os.setReadHandler(stdout, () => {
    ret = os.read(stdout, buf, 0, buf.byteLength);
    //    console.log('buf.byteLength:', buf.byteLength);
    console.log('ret:', ret);

    if(ret > 0) {
      let chunk = toString(buf.slice(0, ret));
      //  console.log('chunk:', chunk);
      data += chunk;
      //console.log('data:', data);
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
  //child.wait();

  console.log('child', child);
  return data;
}

function main(...args) {
  globalThis.console = new Console({ inspectOptions });

  let data = ReadChild('/opt/diet/bin-x86_64/ls', '-la');

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
