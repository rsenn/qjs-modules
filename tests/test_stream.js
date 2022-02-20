import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { ReadableStream, WritableStream } from 'stream';
import { Blob } from 'blob';
import { FileSystemReadableFileStream, FileSystemWritableFileStream } from '../lib/streams.js';

('use strict');
('use math');

async function ReadStream(stream) {
  let reader = stream.getReader();
  console.log('ReadStream(0)', { reader });
  let chunk,
    chunks = [];
  while((chunk = reader.read())) {
    let { value, done } = await chunk;
    // chunk = chunk.then(res => (console.log('chunk resolved', res), res));
    console.log('ReadStream(1)', { done, value });
    if(done) break;
    chunks.push(value);
  }

  console.log('ReadStream(2)', { chunks });
  let blob = new Blob(chunks);
  console.log('ReadStream(3)', { blob });

  reader.releaseLock();
  return blob;
}

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 64,
      compact: 1,
      showHidden: false
    }
  });

  let read = new FileSystemReadableFileStream('tests/test1.xml');
  let write = new FileSystemWritableFileStream('/tmp/out.txt');

  console.log('read', read);
  console.log('write', write);

  ReadStream(read).then(result => console.log('read', { result }));
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
