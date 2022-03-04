import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { TextDecoder, TextEncoder } from 'textcode';
import { toArrayBuffer, quote, concat } from 'util';
import { TextEncoderStream } from '../lib/streams.js';

async function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 256,
      compact: 2,
      showHidden: false,
      numberBase: 16
    }
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
    chunk = await reader.read();
    console.log('chunk', chunk);
  } while(chunk && !chunk.done);

  await reader.releaseLock();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
}
