import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { TextDecoder, TextEncoder } from 'textcode';
import { toArrayBuffer, quote, concat } from 'util';

function Decode(encoding, ...chunks) {
  let decoder = new TextDecoder(encoding);
  //console.log('decoder(' + encoding + ')', decoder);
  let result = [];
  for(let buf of chunks) {
    console.log('decoder(' + encoding + ').decode(', buf, `)`);
    result.push(decoder.decode(buf));
  }
  let r = decoder.end();
  if(r) result.push(r);
  console.log(`result = '${result.join('')}'\n`);
  return result;
}

function Encode(encoding, ...chunks) {
  let encoder = new TextEncoder(encoding);
  //  console.log('encoder(' + encoding + ')', encoder);
  let result = [];
  for(let str of chunks) {
    console.log((`encoder(` + encoding + `).encode('${str}')`).padEnd(30));
    result.push(encoder.encode(str));
  }
  let r = encoder.end();
  if(r) result.push(r);

  if(result.length == 1) result = result[0];
  else result = concat(...result.map(typedArr => typedArr.buffer));

  console.log(`result = '`, result, `'\n`);
  return result;
}

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
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
  let u32 = new Uint32Array([
    0x1f147, 0x2606, 0x2a00, 0x2600, 0x262f, 0x1f167, 0x1d6a1, 0x1d605, 0x1d639, 0x1d431, 0x1d417, 0x1f4a8, 0x1e95e,
    0x11663, 0x115ca, 0x115d0, 0x115d0
  ]);

  for(let s of [s1, s2]) {
    Encode('utf-8', s);
    Encode('utf-16le', s);
    Encode('utf-16be', s);
    Encode('utf-32le', s);
    Encode('utf-32be', s);
  }

  Decode('utf-8', u8);
  Decode('utf-16', u16);
  Decode('utf-32le', u32.buffer.slice(0, -1), u32.buffer.slice(-1));
  Decode(
    'utf-32be',
    new Uint32Array([
      0x47f10100, 0x6260000, 0x2a0000, 0x260000, 0x2f260000, 0x67f10100, 2715156736, 0x5d60100, 0x39d60100, 0x31d40100,
      0x17d40100, 2834563328, 0x5ee90100, 0x63160100, 3390374144, 3491037440, 3491037440
    ])
  );

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
