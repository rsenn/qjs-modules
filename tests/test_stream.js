import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { ReadableStream, WritableStream } from 'stream';
import { Blob } from 'blob';
import { toString, define } from 'util';
import { FileSystemReadableFileStream, FileSystemWritableFileStream, FileSystemReadableStream, StreamReadIterator } from '../lib/streams.js';
import fs from 'fs';
import { AsyncGeneratorPrototype, extendAsyncGenerator, AsyncGeneratorExtensions } from '../lib/extendAsyncGenerator.js';

('use strict');
('use math');

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

  Object.assign(Object.getPrototypeOf((async function* () {})()), AsyncGeneratorExtensions);
  extendAsyncGenerator(Object.getPrototypeOf(iter));
  extendAsyncGenerator(Object.getPrototypeOf((async function* () {})()));

  let tfrm = iter.map(n => toString(n));
  extendAsyncGenerator(Object.getPrototypeOf(tfrm));
  let p = [Object.getPrototypeOf(iter), AsyncGeneratorPrototype, Object.getPrototypeOf(tfrm)];
  /* console.log('AsyncGeneratorExtensions', AsyncGeneratorExtensions);

  console.log('tfrm.reduce', tfrm.reduce);*/

  let result = await tfrm.reduce((acc, n) => acc + n, '');

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
