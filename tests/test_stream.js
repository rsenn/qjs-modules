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
/*
async function consume(x, t = a => console.log(`async consume =`, a)) {
  for await(let n of x) await t(n);
}*/

async function* transform(x, t = a => a) {
  for await(let n of await x) yield await t(n);
}

async function reduce(x, t = (a, n) => ((a ??= []).push(n), a), c) {
  for await(let n of await x) c = await t(c, n);
  return c;
}

async function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      maxStringLength: 100
    }
  });
  let fd = fs.openSync('quickjs-misc.c', fs.O_RDONLY);

  let st = new FileSystemReadableStream(fd, 1024);

  let read = new FileSystemReadableFileStream('tests/test1.xml');

  let iter = await StreamReadIterator(st);
  let tfrm = await transform(iter, toString);
  console.log('tfrm', tfrm);

  let result = await reduce(tfrm, (acc, n) => acc + n, '');

  console.log('result', { result });
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
