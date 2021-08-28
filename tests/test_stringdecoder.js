import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { StringDecoder } from 'stringdecoder';

('use strict');
('use math');

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
  console.log('StringDecoder', StringDecoder);

  let decoder = new StringDecoder('utf-8');
  console.log('decoder', Object.getPrototypeOf(decoder));
  console.log('decoder', Object.getOwnPropertyNames(Object.getPrototypeOf(decoder)));
  console.log('decoder', decoder);
  for(let i = 0; i < 4; i++) {
    let buf = new Uint8Array([...['T', 'E', 'S', 'T'].map(ch => ch.charCodeAt(0)), 0xc3]).buffer;
    console.log('decoder.write(buf)', decoder.write(buf));
    buf = new Uint8Array([0xa4, ...['B', 'L', 'A', 'H'].map(ch => ch.charCodeAt(0))]).buffer;
    console.log('decoder.end(buf)', decoder.end(buf));
  }
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
