import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { ReadableStream, WritableStream } from 'stream';
import { Blob } from 'blob';
import { toString } from 'util';
import { FileSystemReadableFileStream, FileSystemWritableFileStream, FileSystemReadableStream, StreamReadIterator } from '../lib/streams.js';
import fs from 'fs';

('use strict');
('use math');

export async function consume(x, t = a => console.log(`async consume =`, a)) {
  for await(let n of x) await t(n);
}
export async function* transform(x, t = a => a) {
  for await(let n of x) yield await t(n);
}

export async function reduce(x, t = (a, n) => ((a ??= []).push(n), a), c) {
  for await(let n of await x) c = await t(c, n);
  return c;
}

function main(...args) {
  console = globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 8,
      maxStringLength: 50,
      maxArrayLength: 64,
      compact: 1,
      showHidden: false
    }
  });
  let fd = fs.openSync('quickjs-misc.c', fs.O_RDONLY);

  let st = new FileSystemReadableStream(fd, 1024);

  /* (async function test() {
    for await(let chunk of await StreamReadIterator(st)) {
      console.log('test chunk', chunk);
    }
    console.log('test done');
  })();*/ let read = new FileSystemReadableFileStream('tests/test1.xml');

  reduce(transform(StreamReadIterator(read), toString), (acc, n) => acc + n, '').then(r => console.log('result', r));
  //& consume(StreamReadIterator(st),  r=>console.log('result',r));

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
