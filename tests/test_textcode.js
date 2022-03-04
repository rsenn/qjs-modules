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
      numberBase: 16,
    }
  });
  console.log('TextDecoder', TextDecoder);
 
  function Decode(bits, arr) {
    let decoder = new TextDecoder('utf-' + bits);
     console.log('decoder' + bits + '.decode(', arr, `) = '${decoder.decode(arr)}'`);
    /*console.log('decoder' + bits + '.buffered', decoder.buffered);*/

    /*console.log*/(/*'decoder' + bits + '.end()',*/ decoder.end());
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

    console.log(('encoder' + bits + '.encode('+ quote(str, "'")+ ')').padEnd(30), encoder.encode(str));
    //console.log('encoder' + bits + '.buffered', encoder.buffered);

 /*   console.log*/(/*'encoder' + bits + '.end()',*/ encoder.end());
   /* console.log('encoder' + bits + '.buffered', encoder.buffered);*/
  }
 let u8 = new Uint8Array([ 0xc3, 0xa4, 0xc3, 0xb6, 0xc3, 0xbc, 0xc3, 0xa0, 0xc3, 0xa9, 0xc3, 0xa8, 0xc3, 0xaf, 0xc3, 0xab]);
  let u16 = new Uint16Array([ 0xd83c, 0xdd47, 0x2606, 0x2a00, 0x2600, 0x262f, 0xd83c, 0xdd67, 0xd835, 0xdea1, 0xd835, 0xde05, 0xd835, 0xde39, 0xd835, 0xdc31, 0xd835, 0xdc17, 0xd83d, 0xdca8, 0xd83a, 0xdd5e, 0xd805, 0xde63, 0xd805, 0xddca, 0xd805, 0xddd0, 0xd805, 0xddd0]);
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
