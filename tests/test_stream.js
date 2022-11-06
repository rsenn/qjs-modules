import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { WritableStream } from 'stream';
import { toString } from 'util';
import { FileSystemReadableFileStream, FileSystemWritableFileStream, FileSystemReadableStream, StreamReadIterator } from '../lib/streams.js';
import fs from 'fs';
import { extendAsyncGenerator, } from '../lib/extendAsyncGenerator.js';

('use strict');
('use math');

extendAsyncGenerator();

async function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      maxStringLength: 50,
      maxArrayLength: Infinity,
      compact: false
    }
  });
  let fd = fs.openSync('quickjs-misc.c', fs.O_RDONLY);

  let st = new FileSystemReadableStream(fd, 1024);

  let read = new FileSystemReadableFileStream('tests/test1.xml');

  let iter = await StreamReadIterator(st);

  let tfrm = iter.map(n => toString(n));

  let result = await tfrm.reduce((acc, n) => acc + n, ''); //await tfrm.reduce((a, n) => ((a ??= []).push(n), a), [])

  // console.log('result', { result });
  console.log('result', result.split(/\n/g)); /*  
  let write = new FileSystemWritableFileStream('/tmp/out.txt');
  */
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
