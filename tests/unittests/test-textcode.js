import { TextDecoder, TextEncoder } from 'textcode';
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'TextDecoder decode() decodes UTF-8 bytes to a string'() {
    const dec = new TextDecoder();
    const bytes = new Uint8Array([72, 101, 108, 108, 111]);
    eq(dec.decode(bytes), 'Hello');
  },
  'TextDecoder decode() handles multi-byte UTF-8 sequences'() {
    const dec = new TextDecoder();
    const enc = new TextEncoder();
    const bytes = enc.encode('héllo€');
    eq(dec.decode(bytes), 'héllo€');
  },
  'TextDecoder default encoding is UTF-8'() {
    const dec = new TextDecoder();
    eq(dec.encoding, 'UTF-8');
  },
  'TextDecoder(label) sets encoding'() {
    eq(new TextDecoder('utf-8').encoding, 'UTF-8');
    eq(new TextDecoder('utf-16').encoding, 'UTF-16');
    eq(new TextDecoder('utf-32').encoding, 'UTF-32');
  },
  'TextDecoder endian defaults to little-endian (falsy)'() {
    const dec = new TextDecoder('utf-16');
    assert(!dec.endian);
  },
  'TextDecoder(label) with be suffix sets big-endian'() {
    const dec = new TextDecoder('utf-16be');
    assert(dec.endian);
  },
  'TextDecoder buffered starts empty'() {
    const dec = new TextDecoder();
    eq(dec.buffered, 0);
  },

  'TextEncoder encode() returns a Uint8Array of UTF-8 bytes'() {
    const enc = new TextEncoder();
    const out = enc.encode('Hi');
    assert(out instanceof Uint8Array);
    eq(out.length, 2);
    eq(out[0], 72);
    eq(out[1], 105);
  },
  'TextEncoder default encoding is UTF-8'() {
    eq(new TextEncoder().encoding, 'UTF-8');
  },
  'TextEncoder(label) sets encoding'() {
    eq(new TextEncoder('utf-16').encoding, 'UTF-16');
    eq(new TextEncoder('utf-32').encoding, 'UTF-32');
  },
  'TextEncoder encodeInto() writes into an existing typed array'() {
    const enc = new TextEncoder();
    const dest = new Uint8Array(16);
    const result = enc.encodeInto('Hi', dest);
    eq(result.read, 2);
    eq(result.written, 2);
    eq(dest[0], 72);
    eq(dest[1], 105);
  },
  'TextEncoder encodeInto() truncates when dest is too small'() {
    const enc = new TextEncoder();
    const dest = new Uint8Array(2);
    const result = enc.encodeInto('Hello', dest);
    assert(result.written <= 2);
  },
});
