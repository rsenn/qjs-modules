import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { TextDecoder, TextEncoder } from 'textcode';
import { toArrayBuffer, quote } from 'util';

function Decode(encoding, ...chunks) {
  let decoder = new TextDecoder(encoding);
  console.log('decoder(' + encoding + ')', decoder);
  let result = [];
  for(let buf of chunks) {
    console.log('decoder(' + encoding + ').decode(', buf, `)`);
    result.push(decoder.decode(buf));
  }
  let r = decoder.end();
  if(r) result.push(r);
  console.log(`result = '${result.join('')}'`);
  return result;
}

function Encode(encoding, ...chunks) {
  let encoder = new TextEncoder(encoding);
  console.log('encoder(' + encoding + ')', encoder);
  let result = [];
  for(let str of chunks) {
    console.log(('encoder(' + encoding + ').encode(' + quote(str, "'") + ')').padEnd(30));
    result.push(encoder.encode(str));
  }
  let r = encoder.end();
  if(r) result.push(r);
  console.log('result =', ...result);
  return result;
}

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

  let s1 = '🅇☆⨀☀☯🅧𝚡𝘅𝘹𝐱𝐗💨𞥞𑙣𑗊𑗐𑗐';
  let s2 = 'äöüàéèïë';

  let u8 = new Uint8Array([
    0xc3, 0xa4, 0xc3, 0xb6, 0xc3, 0xbc, 0xc3, 0xa0, 0xc3, 0xa9, 0xc3, 0xa8, 0xc3, 0xaf, 0xc3, 0xab
  ]);
  let u16 = new Uint16Array([
    0xcd8, 0x7dd, 0x626, 0x2a, 0x26, 0xf26, 0xcd8, 0x7dd, 0x5d8, 0x1de, 0x5d8, 0x5de, 0x5d8, 0x9de, 0x5d8, 0x1dc, 0x5d8,
    0x7dc, 0xa00
  ]);
  let u32 = new Uint32Array(
    [
      0x47f1, 0x626, 0x2a, 0x26, 0x2f26, 0x67f1, 0xa1d6, 0x5d6, 0x39d6, 0x31d4, 0x17d4, 0xa8f4, 0x5ee9, 0x6316, 0xca15,
      0xd015, 0xd015
    ] /*[0x1f147, 0x2606, 0x2a00, 0x2600, 0x262f, 0x1f167, 0x1d6a1, 0x1d605, 0x1d639, 0x1d431, 0x1d417, 0x1f4a8, 0x1e95e, 0x11663, 0x115ca, 0x115d0, 0x115d0]*/
  );
  /* console.log(`toArrayBuffer('TEST\xc3', 0, 5) =`, toArrayBuffer('TEST\xc3', 0, 5));
  console.log(`toArrayBuffer(u8) =`, toArrayBuffer(u8, 1, -1));*/

  for(let s of [s1 /*,s2*/]) {
    /*Encode(8, s);*/
    Encode('UTF16LE', s);
    Encode('UTF16BE', s);
    Encode('UTF32LE', s);
    Encode('UTF32BE', s);
  }

  Decode('UTF8', u8);
  Decode('UTF16', u16);
  Decode('UTF32', u32.buffer.slice(0, -1), u32.buffer.slice(-1));

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
