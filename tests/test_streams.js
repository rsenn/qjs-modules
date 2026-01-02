import { toString } from 'util';
import { TextEncoderStream } from '../lib/streams.js';
import { Console } from 'console';
async function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 256,
      compact: 2,
      showHidden: false,
      numberBase: 16,
    },
  });
  let chunk, reader, writer, stream;

  stream = new TextEncoderStream('utf-16le');

  writer = await stream.writable.getWriter();

  await writer.write('This is a test!\n');
  await writer.write('The second test!\n');
  await writer.close();
  await writer.releaseLock();

  reader = await stream.readable.getReader();
  console.log('reader', reader);

  do {
    chunk = reader.read();
    console.log('chunk', chunk);
    chunk = await chunk;
    console.log('chunk', toString(chunk.value));
  } while(chunk && !chunk.done);

  await reader.releaseLock();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
}