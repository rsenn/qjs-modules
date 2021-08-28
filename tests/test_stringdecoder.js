import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { StringDecoder } from 'stringdecoder';
import * as misc from 'misc';

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

  let decoder = new StringDecoder('utf-16');
  console.log('decoder', Object.getOwnPropertyNames(Object.getPrototypeOf(decoder)));
  console.log('decoder', decoder);
  console.log('decoder.encoding', decoder.encoding);
  let u8 = new Uint8Array([0,10,65535,1024,100,200,300]);
  console.log(`misc.toArrayBuffer('TEST\xc3', 0, 5) =`, misc.toArrayBuffer('TEST\xc3', 0, 5));
  console.log(`misc.toArrayBuffer(u8) =`, misc.toArrayBuffer(u8, 1,-1));

  for(let i = 0; i < 4; i++) {
    let buf = misc.toArrayBuffer('TEST\xc3', 0, 5); // new Uint8Array([...['T', 'E', 'S', 'T'].map(ch => ch.charCodeAt(0)), 0xc3]).buffer;
    console.log(`[${i}]`, 'decoder.write(buf)', decoder.write(buf));
    console.log(`[${i}]`, 'decoder.buffered', decoder.buffered);

    buf = new Uint8Array(misc.toArrayBuffer('_BLAH'));
    buf[0] = 0xa4; //  ...['B', 'L', 'A', 'H'].map(ch => ch.charCodeAt(0))]).buffer;
    console.log(`[${i}]`, 'decoder.end(buf)', decoder.end(buf));
    console.log(`[${i}]`, 'decoder.buffered', decoder.buffered);
    break;
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
