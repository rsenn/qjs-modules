import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { ReadableStream, WritableStream } from 'stream';
import { toString, define } from 'util';
import { FileSystemReadableFileStream, LineStreamIterator, FileSystemWritableFileStream, FileSystemReadableStream, StreamReadIterator } from '../lib/streams.js';
import fs from 'fs';
import { extendAsyncGenerator } from '../lib/extendAsyncGenerator.js';

('use strict');
('use math');

extendAsyncGenerator();

async function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      maxStringLength: 50,
      maxArrayLength: Infinity,
      compact: false
    }
  });
  let fd = fs.openSync('quickjs-misc.c', fs.O_RDONLY);

  let st = new FileSystemReadableStream(fd, 1024);

  let read = new FileSystemReadableFileStream('tests/test1.xml');

  let iter = await StreamReadIterator(st);
  /*  let tfrm=  LineStreamIterator(iter);*/
  let result = await iter.reduce(
    (buf => (acc, n) => {
      buf += toString(n);
      if(buf.indexOf('\n') != -1) {
        let lines = buf.split('\n');
        buf = lines.pop();
        (acc ??= []).push(...lines);
      }
      return acc;
    })('')
  );

  /*let linestream=await LineStreamIterator(iter);
console.log('linestream',  linestream );
let result = await  linestream.reduce((acc, n) => (acc.push(n), acc), [])*/

  console.log('result', result);
  /*
  let write = new FileSystemWritableFileStream('/tmp/out.txt');
  */
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
