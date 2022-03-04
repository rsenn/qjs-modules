import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { TextDecoder, TextEncoder } from 'textcode';
import { toArrayBuffer, quote } from 'util';

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
      showHidden: false,
      numberBase: 16
    }
  });
  console.log('TextDecoder', TextDecoder);

  function Decode(bits, arr) {
    let decoder = new TextDecoder('utf-' + bits);

    let buf = arr.buffer;
    console.log('decoder' + bits + '.decode(', buf, `)`);

    /*console.log('decoder' + bits + '.buffered', decoder.buffered);*/

    let result = decoder.decode(buf);
    console.log('result =', result);
    /*console.log*/ /*'decoder' + bits + '.end()',*/ decoder.end();
    /* console.log('decoder' + bits + '.buffered', decoder.buffered);*/
  }

  let s1 = '🅇☆⨀☀☯🅧𝚡𝘅𝘹𝐱𝐗💨𞥞𑙣𑗊𑗐𑗐';
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

    console.log(('encoder' + bits + '.encode(' + quote(str, "'") + ')').padEnd(30));

    let result = encoder.encode(str);
    console.log('result =', result);

    /*   console.log*/ /*'encoder' + bits + '.end()',*/ encoder.end();
    /* console.log('encoder' + bits + '.buffered', encoder.buffered);*/
    return result;
  }
  let u8 = new Uint8Array([
    0xc3, 0xa4, 0xc3, 0xb6, 0xc3, 0xbc, 0xc3, 0xa0, 0xc3, 0xa9, 0xc3, 0xa8, 0xc3, 0xaf, 0xc3, 0xab
  ]);
  let u16 = new Uint8Array([0x3c, 0xd8, 0x47, 0xdd, 0x06, 0x26, 0x00, 0x2a, 0x00, 0x26, 0x2f, 0x26, 0x3c, 0xd8, 0x67, 0xdd, 0x35, 0xd8, 0xa1, 0xde, 0x35, 0xd8, 0x05, 0xde, 0x35, 0xd8, 0x39, 0xde, 0x35, 0xd8, 0x31, 0xdc, 0x35, 0xd8, 0x17, 0xdc, 0x3d, 0xd8, 0xa8, 0xdc, 0x3a, 0xd8, 0x5e, 0xdd, 0x05, 0xd8, 0x63, 0xde, 0x05, 0xd8, 0xca, 0xdd, 0x05, 0xd8, 0xd0, 0xdd, 0x05, 0xd8, 0xd0, 0xdd, 0x0a, 0x00
]);
  console.log(`toArrayBuffer('TEST\xc3', 0, 5) =`, toArrayBuffer('TEST\xc3', 0, 5));
  console.log(`toArrayBuffer(u8) =`, toArrayBuffer(u8, 1, -1));

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
