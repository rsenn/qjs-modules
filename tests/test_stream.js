import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { ReadableStream, WritableStream } from 'stream';
import { Blob } from 'blob';
import { toString } from 'util';
import { FileSystemReadableFileStream, FileSystemWritableFileStream, FileSystemReadableStream, StreamReadIterator } from '../lib/streams.js';
import fs from 'fs';
import { AsyncGeneratorPrototype, extendAsyncGenerator } from '../lib/extendAsyncGenerator.js';

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

  extendAsyncGenerator(Object.getPrototypeOf(iter));
  //  extendAsyncGenerator(AsyncGeneratorPrototype);

  let tfrm = iter.map(n => toString(n));
  extendAsyncGenerator(Object.getPrototypeOf(tfrm));

  let p = [Object.getPrototypeOf(iter), AsyncGeneratorPrototype, Object.getPrototypeOf(tfrm)];

  /*console.log('p', p);
  console.log('p', p.map(pr => pr.constructor));

  console.log('Object.getPrototypeOf(iter)', Object.getPrototypeOf(iter));
  console.log('Object.getPrototypeOf(tfrm)', Object.getPrototypeOf(tfrm));
  console.log('AsyncGeneratorPrototype', AsyncGeneratorPrototype);

  console.log('iter', iter);
  console.log('iter.reduce', iter.reduce);
  console.log('tfrm.reduce', tfrm.reduce);
*/
  console.log('AsyncGeneratorPrototype', AsyncGeneratorPrototype.reduce);

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
