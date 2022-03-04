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
 
  function Decode(bits, arr) {
    let decoder = new TextDecoder('utf-' + bits);
     console.log('decoder' + bits + '.decode(', arr, `) = '${decoder.decode(arr)}'`);
    console.log('decoder' + bits + '.buffered', decoder.buffered);

    console.log('decoder' + bits + '.end()', decoder.end());
    console.log('decoder' + bits + '.buffered', decoder.buffered);
  }

  let s1 = '☆⨀☀☯';
  let s2 = 'äöüàéèïë';


  Encode(8, s1);
  Encode(8, s2);
  Encode(16, s1);
  Encode(16, s2);
  Encode(32, s1);
  Encode(32, s2);

  function Encode(bits, str) {
    let encoder = new TextEncoder('utf-' + bits);
    //let bits = encoder.encoding.replace(/.*-/, '');

    console.log('encoder' + bits + '.encode(', str, ')', encoder.encode(str));
    console.log('encoder' + bits + '.buffered', encoder.buffered);

    console.log('encoder' + bits + '.end()', encoder.end());
    console.log('encoder' + bits + '.buffered', encoder.buffered);
  }
 let u8 = new Uint8Array([0xc3, 0xa4, 0xc3, 0xb6, 0x75, 0x0a]);
  let u16 = new Uint16Array([0x2606, 0x2a00, 0x2600, 0x262f, 0x000a]);
  console.log(`misc.toArrayBuffer('TEST\xc3', 0, 5) =`, misc.toArrayBuffer('TEST\xc3', 0, 5));
  console.log(`misc.toArrayBuffer(u8) =`, misc.toArrayBuffer(u8, 1, -1));

  Decode(8, u8);
  Decode(16, u16);

  const encoder = new TextEncoder();
  const view = encoder.encode('€');
  console.log(`encoder.encode('€')`, view); // Uint8Array(3) [226, 130, 172]
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
