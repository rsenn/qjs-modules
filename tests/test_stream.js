import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { ReadableStream, WritableStream } from 'stream';
import { FileSystemReadableFileStream, FileSystemWritableFileStream } from '../lib/streams.js';

('use strict');
('use math');

async function ReadStream(stream) {
  let reader = stream.getReader();
  console.log('ReadStream', { reader });
  let chunks = [];
  do {
    let chunk = reader.read();
    console.log('ReadStream', { chunk });
    chunk = await chunk;
    chunks.push(chunk);
  } while(chunk);

  reader.releaseLock();
  return chunks;
}

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 256,
      compact: 2,
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
