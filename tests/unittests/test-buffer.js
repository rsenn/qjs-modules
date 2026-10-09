// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/buffer.js: Buffer, as in Node; the same assertions must hold on node, bun and deno */
import { Buffer, atob, btoa, constants, isAscii, isUtf8, kMaxLength } from 'node:buffer';
import { assert, eq, tests } from '../../lib/tinytest.js';

const esc = s => JSON.stringify(s).replace(/[^\x00-\x7f]/g, c => '\\u' + c.charCodeAt(0).toString(16).padStart(4, '0'));
const arr = b => Array.from(b).join(',');
const code = fn => {
  try {
    fn();
  } catch(e) {
    return e.code;
  }
};

tests({
  'from(string) in every encoding'() {
    eq(arr(Buffer.from('héllo')), '104,195,169,108,108,111');
    eq(arr(Buffer.from('héllo', 'latin1')), '104,233,108,108,111');
    eq(arr(Buffer.from('hi€', 'utf16le')), '104,0,105,0,172,32');
    eq(arr(Buffer.from('68656c6c6f', 'hex')), '104,101,108,108,111');
    eq(arr(Buffer.from('68656c6c6', 'hex')), '104,101,108,108');
    eq(arr(Buffer.from('68zz', 'hex')), '104');
    eq(arr(Buffer.from('aGVsbG8=', 'base64')), '104,101,108,108,111');
    eq(arr(Buffer.from('aGVsbG8', 'base64')), '104,101,108,108,111');
    eq(arr(Buffer.from('a-_b', 'base64url')), arr(Buffer.from('a+/b', 'base64')));
    eq(arr(Buffer.from('aGV s\nbG8=', 'base64')), '104,101,108,108,111');
    eq(arr(Buffer.from('é', 'ascii')), '233');
    eq(arr(Buffer.from('a\ud800b')), '97,239,191,189,98');
  },
  'toString in every encoding'() {
    const b = Buffer.from('héllo€');

    eq(b.toString(), 'héllo€');
    eq(b.toString('utf8', 1, 3), 'é');
    eq(b.toString('hex'), '68c3a96c6c6fe282ac');
    eq(b.toString('base64'), 'aMOpbGxv4oKs');
    eq(b.toString('base64url'), 'aMOpbGxv4oKs');
    eq(Buffer.from([0xfb, 0xff]).toString('base64'), '+/8=');
    eq(Buffer.from([0xfb, 0xff]).toString('base64url'), '-_8');
    eq(Buffer.from([104, 233]).toString('latin1'), 'hé');
    eq(Buffer.from([104, 233]).toString('ascii'), 'hi');
    eq(Buffer.from('hi€', 'utf16le').toString('utf16le'), 'hi€');
    eq(b.toString('utf8', 100), '');
    eq(b.toString(undefined, 0, 2), 'h�');
  },
  'invalid UTF-8 decodes to replacement characters like Node'() {
    eq(esc(Buffer.from([0x68, 0xff, 0x69]).toString()), '"h\\ufffdi"');
    eq(esc(Buffer.from([0xe2, 0x82]).toString()), '"\\ufffd"');
    eq(esc(Buffer.from([0xf0, 0x9f, 0x98]).toString()), '"\\ufffd"');
    eq(esc(Buffer.from([0xc0, 0x80]).toString()), '"\\ufffd\\ufffd"');
    eq(esc(Buffer.from([0xef, 0xbb, 0xbf, 0x41]).toString()), '"\\ufeffA"');
  },
  'unknown encodings throw'() {
    eq(code(() => Buffer.from('x', 'nope')), 'ERR_UNKNOWN_ENCODING');
    eq(code(() => Buffer.from('x').toString('nope')), 'ERR_UNKNOWN_ENCODING');
    assert(Buffer.isEncoding('UTF-8') && Buffer.isEncoding('ucs2') && !Buffer.isEncoding('nope'));
  },
  'from(array, arrayBuffer, buffer, typed array, objects)'() {
    eq(arr(Buffer.from([1, 2, 258, -1])), '1,2,2,255');
    eq(arr(Buffer.from({ length: 2, 0: 7, 1: 8 })), '7,8');
    eq(arr(Buffer.from({ type: 'Buffer', data: [1, 2] })), '1,2');
    eq(arr(Buffer.from(new Uint16Array([1, 258]))), '1,2');
    eq(arr(Buffer.from(new String('ab'))), '97,98');

    const ab = new Uint8Array([1, 2, 3, 4]).buffer;
    const v = Buffer.from(ab, 1, 2);

    eq(arr(v), '2,3');
    ab && (new Uint8Array(ab)[1] = 9);
    eq(v[0], 9);

    const copy = Buffer.from(v);

    v[0] = 5;
    eq(copy[0], 9);
    eq(code(() => Buffer.from(ab, 10)), 'ERR_BUFFER_OUT_OF_BOUNDS');
    eq(code(() => Buffer.from(5)), 'ERR_INVALID_ARG_TYPE');
    eq(code(() => Buffer.from(null)), 'ERR_INVALID_ARG_TYPE');
  },
  'alloc, allocUnsafe, fill and the constructor'() {
    eq(arr(Buffer.alloc(3)), '0,0,0');
    eq(arr(Buffer.alloc(5, 'ab')), '97,98,97,98,97');
    eq(arr(Buffer.alloc(4, 'ff', 'hex')), '255,255,255,255');
    eq(arr(Buffer.alloc(3, 258)), '2,2,2');
    eq(Buffer.allocUnsafe(4).length, 4);
    eq(code(() => Buffer.alloc(-1)), 'ERR_OUT_OF_RANGE');
    eq(code(() => Buffer.alloc('3')), 'ERR_INVALID_ARG_TYPE');
    eq(Buffer.from('ab') instanceof Uint8Array, true);
    eq(Buffer.isBuffer(Buffer.alloc(1)), true);
    eq(Buffer.isBuffer(new Uint8Array(1)), false);
    eq(Buffer.poolSize, 8192);
    eq(typeof kMaxLength, 'number');
    eq(constants.MAX_LENGTH, kMaxLength);
  },
  'fill ranges and patterns'() {
    eq(arr(Buffer.alloc(6).fill('xyz', 1, 5)), '0,120,121,122,120,0');
    eq(arr(Buffer.alloc(4).fill(Buffer.from([1, 2, 3]))), '1,2,3,1');
    eq(arr(Buffer.alloc(3).fill('')), '0,0,0');
    eq(arr(Buffer.alloc(3).fill('a', 'utf8')), '97,97,97');
    eq(code(() => Buffer.alloc(2).fill('zz', 'hex')), 'ERR_INVALID_ARG_VALUE');
  },
  'byteLength'() {
    eq(Buffer.byteLength('héllo'), 6);
    eq(Buffer.byteLength('héllo', 'latin1'), 5);
    eq(Buffer.byteLength('abcd', 'hex'), 2);
    eq(Buffer.byteLength('aGVsbG8=', 'base64'), 5);
    eq(Buffer.byteLength(new ArrayBuffer(7)), 7);
    eq(Buffer.byteLength(new Uint16Array(3)), 6);
    eq(code(() => Buffer.byteLength(5)), 'ERR_INVALID_ARG_TYPE');
  },
  'concat and compare'() {
    const c = Buffer.concat([Buffer.from('ab'), Buffer.from('cd'), new Uint8Array([101])]);

    eq(c.toString(), 'abcde');
    eq(Buffer.concat([Buffer.from('abc'), Buffer.from('def')], 4).toString(), 'abcd');
    eq(Buffer.concat([Buffer.from('a')], 3).length, 3);
    eq(Buffer.concat([]).length, 0);
    eq(code(() => Buffer.concat('x')), 'ERR_INVALID_ARG_TYPE');
    eq(code(() => Buffer.concat([1])), 'ERR_INVALID_ARG_TYPE');
    eq(Buffer.compare(Buffer.from('a'), Buffer.from('b')), -1);
    eq(Buffer.compare(Buffer.from('b'), Buffer.from('a')), 1);
    eq(Buffer.compare(Buffer.from('ab'), Buffer.from('abc')), -1);
    eq(Buffer.compare(Buffer.from('abc'), Buffer.from('abc')), 0);
    eq(Buffer.from('abc').compare(Buffer.from('xbcx'), 1, 3, 1, 3), 0);
    eq(Buffer.from('abc').equals(Buffer.from('abc')), true);
    eq(Buffer.from('abc').equals(new Uint8Array([97, 98, 99])), true);
    eq(Buffer.from('abc').equals(Buffer.from('abd')), false);
    eq(code(() => Buffer.from('a').equals('a')), 'ERR_INVALID_ARG_TYPE');
  },
  'copy'() {
    const src = Buffer.from('hello'), dst = Buffer.alloc(8, '-');

    eq(src.copy(dst, 1, 1, 4), 3);
    eq(dst.toString(), '-ell----');
    eq(src.copy(dst, 6), 2);
    eq(dst.toString(), '-ell--he');
    eq(src.copy(dst, 10), 0);
    eq(Buffer.from('abcdef').copy(new Uint8Array(3)), 3);

    const overlap = Buffer.from('abcdef');

    overlap.copy(overlap, 2, 0, 4);
    eq(overlap.toString(), 'ababcd');
    eq(code(() => src.copy(dst, -1)), 'ERR_OUT_OF_RANGE');
    eq(code(() => src.copy('x')), 'ERR_INVALID_ARG_TYPE');
  },
  'slice and subarray share memory'() {
    const b = Buffer.from('abcdef');
    const s = b.slice(1, 4);

    eq(s.toString(), 'bcd');
    assert(Buffer.isBuffer(s));
    s[0] = 120;
    eq(b.toString(), 'axcdef');
    eq(b.subarray(-2).toString(), 'ef');
    eq(b.slice(-3, -1).toString(), 'de');
    eq(s.parent === b.buffer, true);
    eq(s.offset - b.offset, 1);
    assert(Buffer.isBuffer(b.map(x => x)));
    assert(Buffer.isBuffer(b.filter(x => x > 100)));
  },
  'indexOf, lastIndexOf, includes'() {
    const b = Buffer.from('this is a buffer, a buffer');

    eq(b.indexOf('is'), 2);
    eq(b.indexOf('is', 3), 5);
    eq(b.indexOf('a', -4), -1);
    eq(b.indexOf('a', -9), 18);
    eq(b.indexOf(Buffer.from('buffer')), 10);
    eq(b.indexOf(97), 8);
    eq(b.indexOf('nope'), -1);
    eq(b.indexOf(''), 0);
    eq(b.indexOf('', 5), 5);
    eq(b.indexOf('', 100), b.length);
    eq(b.lastIndexOf('buffer'), 20);
    eq(b.lastIndexOf('buffer', 19), 10);
    eq(b.lastIndexOf('a', -10), 8);
    eq(b.lastIndexOf('nope'), -1);
    eq(b.includes('buffer'), true);
    eq(b.includes('nope'), false);
    eq(Buffer.from('€x').indexOf('x'), 3);
    eq(Buffer.from('abc').indexOf('63', 'hex'), 2);
    eq(Buffer.from('abc').indexOf('bc', 'utf8'), 1);
    eq(Buffer.from('abc').slice(1).indexOf('c'), 1);
    eq(code(() => b.indexOf({})), 'ERR_INVALID_ARG_TYPE');
  },
  'write'() {
    const b = Buffer.alloc(8);

    eq(b.write('héllo'), 6);
    eq(b.toString('utf8', 0, 6), 'héllo');
    eq(Buffer.alloc(4).write('héllo', 2), 1);
    eq(Buffer.alloc(3).write('€€'), 3);
    eq(Buffer.alloc(2).write('€'), 0);
    eq(Buffer.alloc(4).write('abcd', 1, 2), 2);
    eq(Buffer.alloc(4).write('ffee', 'hex'), 2);
    eq(Buffer.alloc(4).write('ffee', 1, 'hex'), 2);
    eq(Buffer.alloc(4).write('hi', 0, 4, 'utf16le'), 4);
    eq(code(() => Buffer.alloc(2).write('a', 5)), 'ERR_OUT_OF_RANGE');
    eq(code(() => Buffer.alloc(2).write(5)), 'ERR_INVALID_ARG_TYPE');
  },
  'fixed-size integer reads and writes'() {
    const b = Buffer.alloc(8);

    eq(b.writeUInt8(255, 0), 1);
    eq(b.writeUInt16BE(0x0102, 1), 3);
    eq(b.writeUInt16LE(0x0102, 3), 5);
    eq(arr(b.subarray(0, 5)), '255,1,2,2,1');
    eq(b.readUInt8(0), 255);
    eq(b.readUInt16BE(1), 258);
    eq(b.readUInt16LE(3), 258);
    eq(b.readUint16BE(1), 258);
    eq(b.readInt8(0), -1);
    b.writeInt32LE(-2, 0);
    eq(b.readInt32LE(0), -2);
    eq(b.readUInt32LE(0), 0xfffffffe);
    b.writeInt16BE(-300, 4);
    eq(b.readInt16BE(4), -300);
    b.writeUInt32BE(0xdeadbeef, 0);
    eq(b.readUInt32BE(0), 0xdeadbeef);
    eq(b.readInt32BE(0), 0xdeadbeef - 2 ** 32);
  },
  'bigint and float reads and writes'() {
    const b = Buffer.alloc(16);

    eq(b.writeBigUInt64LE(2n ** 63n + 5n, 0), 8);
    eq(b.readBigUInt64LE(0), 2n ** 63n + 5n);
    eq(b.readBigInt64LE(0), -(2n ** 63n) + 5n);
    b.writeBigInt64BE(-7n, 8);
    eq(b.readBigInt64BE(8), -7n);
    eq(b.readBigUint64BE(8), 2n ** 64n - 7n);
    b.writeFloatLE(1.5, 0);
    eq(b.readFloatLE(0), 1.5);
    b.writeDoubleBE(-2.25, 8);
    eq(b.readDoubleBE(8), -2.25);
    b.writeFloatBE(0.1, 0);
    eq(Math.fround(0.1), b.readFloatBE(0));
    eq(code(() => b.writeBigInt64LE(2n ** 63n, 0)), 'ERR_OUT_OF_RANGE');
  },
  'variable-size integers'() {
    const b = Buffer.alloc(6);

    eq(b.writeUIntBE(0x123456789a, 0, 5), 5);
    eq(arr(b.subarray(0, 5)), '18,52,86,120,154');
    eq(b.readUIntBE(0, 5), 0x123456789a);
    eq(b.readUIntLE(0, 5), 0x9a78563412);
    b.writeIntLE(-5, 0, 3);
    eq(b.readIntLE(0, 3), -5);
    eq(b.readUIntLE(0, 3), 2 ** 24 - 5);
    b.writeIntBE(-129, 0, 2);
    eq(b.readIntBE(0, 2), -129);
    b.writeUIntLE(0xffffffffffff, 0, 6);
    eq(b.readUIntLE(0, 6), 0xffffffffffff);
    eq(code(() => b.readUIntLE(0, 7)), 'ERR_OUT_OF_RANGE');
    eq(code(() => b.readUIntLE(0)), 'ERR_INVALID_ARG_TYPE');
    eq(code(() => b.writeUIntBE(256, 0, 1)), 'ERR_OUT_OF_RANGE');
  },
  'range and type errors of reads and writes'() {
    const b = Buffer.alloc(4);

    eq(code(() => b.readUInt32LE(1)), 'ERR_OUT_OF_RANGE');
    eq(code(() => b.readUInt8(4)), 'ERR_OUT_OF_RANGE');
    eq(code(() => b.readUInt8(-1)), 'ERR_OUT_OF_RANGE');
    eq(code(() => b.readUInt8(1.5)), 'ERR_OUT_OF_RANGE');
    eq(code(() => b.readUInt8('1')), 'ERR_INVALID_ARG_TYPE');
    eq(code(() => Buffer.alloc(0).readUInt8(0)), 'ERR_BUFFER_OUT_OF_BOUNDS');
    eq(code(() => b.writeUInt8(256, 0)), 'ERR_OUT_OF_RANGE');
    eq(code(() => b.writeInt8(-129, 0)), 'ERR_OUT_OF_RANGE');
    eq(code(() => b.writeUInt16LE(1, 3)), 'ERR_OUT_OF_RANGE');
    eq(b.writeUInt8(7), 1);
  },
  'swap'() {
    eq(arr(Buffer.from([1, 2, 3, 4]).swap16()), '2,1,4,3');
    eq(arr(Buffer.from([1, 2, 3, 4]).swap32()), '4,3,2,1');
    eq(arr(Buffer.from([1, 2, 3, 4, 5, 6, 7, 8]).swap64()), '8,7,6,5,4,3,2,1');
    let threw = false;

    try {
      Buffer.from([1, 2, 3]).swap16();
    } catch(e) {
      threw = e instanceof RangeError;
    }

    eq(threw, true);
  },
  'toJSON and JSON.stringify'() {
    eq(JSON.stringify(Buffer.from('hi')), '{"type":"Buffer","data":[104,105]}');
    eq(arr(Buffer.from(JSON.parse(JSON.stringify(Buffer.from('hi'))))), '104,105');
  },
  'atob, btoa, isUtf8, isAscii'() {
    eq(btoa('hello'), 'aGVsbG8=');
    eq(atob('aGVsbG8='), 'hello');
    eq(atob('aGk'), 'hi');
    eq(code(() => atob('a')), 5);
    eq(code(() => btoa('€')), 5);
    eq(isUtf8(Buffer.from('héllo')), true);
    eq(isUtf8(new Uint8Array([0xff])), false);
    eq(isUtf8(new Uint8Array([0xc0, 0x80])), false);
    eq(isUtf8(new Uint8Array([0xed, 0xa0, 0x80])), false);
    eq(isAscii(Buffer.from('abc')), true);
    eq(isAscii(Buffer.from('é')), false);
  },
  'Buffer and new Buffer are callable like Node'() {
    eq(Buffer(3).length, 3);
    eq(new Buffer(3).length, 3);
    eq(new Buffer('hi').toString(), 'hi');
    eq(Buffer([104, 105]).toString(), 'hi');
    assert(Buffer.from('x') instanceof Buffer);
  },
  'iteration, entries and typed array behaviour are kept'() {
    const b = Buffer.from('abc');

    eq([...b].join(), '97,98,99');
    eq(b.length, 3);
    eq(Object.prototype.toString.call(b), '[object Uint8Array]');
    eq(Array.from(b.entries()).length, 3);
    b[1] = 300;
    eq(b[1], 44);
  },
});
