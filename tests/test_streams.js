import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { TextDecoder, TextEncoder } from 'textcode';
import { toArrayBuffer, quote, concat } from 'util';
import { TextEncoderStream } from '../lib/streams.js';

function main(...args) {
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

  let encoderStream = new TextEncoderStream('utf-16');

  let writer;
  writer = encoderStream.writable.getWriter();

  writer.write('This is a test!\n');
  writer.releaseLock();

  let reader;
  reader = encoderStream.readable.getReader();

  let chunk = reader.read();
  console.log('chunk', chunk);
  reader.releaseLock();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
}
