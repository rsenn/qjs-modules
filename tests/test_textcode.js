import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { TextDecoder, TextEncoder } from 'textcode';
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
  console.log('TextDecoder', TextDecoder);

  let decoder8 = new TextDecoder('utf-8');
  let decoder16 = new TextDecoder('utf-16');
  console.log('decoder16', Object.getOwnPropertyNames(Object.getPrototypeOf(decoder16)));
  console.log('decoder16', decoder16);
  console.log('decoder16.encoding', decoder16.encoding);
  let u8 = new Uint8Array([0xc3, 0xa4, 0xc3, 0xb6, 0x75, 0x0a]);
  let u16 = new Uint16Array([0x2606, 0x2a00, 0x2600, 0x262f, 0x000a]);
  console.log(`misc.toArrayBuffer('TEST\xc3', 0, 5) =`, misc.toArrayBuffer('TEST\xc3', 0, 5));
  console.log(`misc.toArrayBuffer(u8) =`, misc.toArrayBuffer(u8, 1, -1));

  Decode(decoder8, u8);
  Decode(decoder16, u16);

  function Decode(decoder, arr) {
    let buf = arr.buffer;
    let bits = arr.BYTES_PER_ELEMENT * 8;
    console.log('decoder' + bits + '.decode(', buf, ')', decoder.decode(buf));
    console.log('decoder' + bits + '.buffered', decoder.buffered);

    console.log('decoder' + bits + '.end()', decoder.end());
    console.log('decoder' + bits + '.buffered', decoder.buffered);
  }

  let encoder8 = new TextEncoder('utf-8');
  let encoder16 = new TextEncoder('utf-16');
  let s1 = '☆⨀☀☯';
  let s2 = 'äöüàéèïë';

  console.log('encoder16', Object.getOwnPropertyNames(Object.getPrototypeOf(encoder16)));
  console.log('encoder16', encoder16);
  console.log('encoder16.encoding', encoder16.encoding);
  Encode(8, s1);
  Encode(8, s2);
  Encode(16, s1);
  Encode(16, s2);
  Encode(32, s1);
  Encode(32, s2);

  function Encode(bits, str) {
    let encoder = new TextEncoder('utf-'+bits);
    //let bits = encoder.encoding.replace(/.*-/, '');

    console.log('encoder' + bits + '.encode(', str, ')', encoder.encode(str));
    console.log('encoder' + bits + '.buffered', encoder.buffered);

    console.log('encoder' + bits + '.end()', encoder.end());
    console.log('encoder' + bits + '.buffered', encoder.buffered);
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
