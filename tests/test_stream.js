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

/*async function ReadStream(stream) {
  let reader = stream.getReader();
  let chunk,
    chunks = [];
  while((chunk = await reader.read())) {
    let value = toString(chunk),
      done = chunk === null;
    if(done) break;
    chunks.push(value);
  }
  let blob = new Blob(chunks);
  reader.releaseLock();
  return blob.arrayBuffer();
}

async function WriteStream(stream, fn = writer => {}) {
  let writer = stream.getWriter();
  fn(writer);
  writer.releaseLock();
}*/

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

  let st = new FileSystemReadableStream(fd, 1024 * 128);

  (async function test() {
    for await(let chunk of await StreamReadIterator(st)) {
      console.log('test chunk', chunk);
    }
    console.log('test done');
  })();

  /*  let read = new FileSystemReadableFileStream('tests/test1.xml');
  let write = new FileSystemWritableFileStream('/tmp/out.txt');
 
  return ReadStream(read).then(result => {
    let str = toString(result);
     WriteStream(write, async writer => {
       await writer.write(result);
      await writer.close();
    });
  });*/
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
