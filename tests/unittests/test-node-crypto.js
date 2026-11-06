// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* node:crypto createHash(); expected values come from node 23 */
import crypto, { Hash, Hmac, createHash, createHmac, getHashes, hash } from 'node:crypto';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* algorithm | digest of '' | of 'abc' | first 16 hex of 1000 times 'a' */
const VECTORS = [
  ['md5', 'd41d8cd98f00b204e9800998ecf8427e', '900150983cd24fb0d6963f7d28e17f72', 'cabe45dcc9ae5b66'],
  ['sha1', 'da39a3ee5e6b4b0d3255bfef95601890afd80709', 'a9993e364706816aba3e25717850c26c9cd0d89d', '291e9a6c66994949'],
  ['sha224', 'd14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f', '23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7', '4e8f0ce90b64661a'],
  ['sha256', 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855', 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad', '41edece42d63e8d9'],
  ['sha384', '38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b', 'cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7', 'f54480689c6b0b11'],
  ['sha512', 'cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e', 'ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f', '67ba5535a46e3f86'],
  ['sha512-224', '6ed0dd02806fa89e25de060c19d3ac86cabb87d6a0ddd05c333b84f4', '4634270f707b6a54daae7530460842e20e37ed265ceee9a43e8924aa', 'ffdfa284ae9e5622'],
  ['sha512-256', 'c672b8d1ef56ed28ab87c3622c5114069bdd3ad7b8f9737498d0c01ecef0967a', '53048e2681941ef99b2e29b76b4c7dabe4c2d0c634fc6d46e0e2f13107e7af23', '40eb4a70d4d69815'],
];

/* Deno differs from Node here: an unknown encoding throws, sha-256 is no name, the message has a suffix */
const deno = typeof Deno != 'undefined';

function fails(fn) {
  try {
    fn();
  } catch(e) {
    return e;
  }

  return null;
}

tests({
  'exports createHash, hash, getHashes, Hash and a default object'() {
    eq('function', typeof createHash);
    eq('function', typeof hash);
    eq('function', typeof getHashes);
    eq('function', typeof Hash);
    assert(crypto.createHash === createHash, 'default.createHash');
  },

  'every algorithm gives the known digests'() {
    for(const [algorithm, empty, abc, long] of VECTORS) {
      eq(empty, createHash(algorithm).update('').digest('hex'));
      eq(abc, createHash(algorithm).update('abc').digest('hex'));
      eq(long, createHash(algorithm).update('a'.repeat(1000)).digest('hex').slice(0, 16));
    }
  },

  'getHashes lists the algorithms of createHash'() {
    const list = getHashes();

    for(const [algorithm] of VECTORS) assert(list.includes(algorithm), algorithm);
  },

  'update chunks equal one update, at every block boundary'() {
    for(const n of [0, 1, 55, 56, 63, 64, 65, 111, 112, 127, 128, 129, 300]) {
      const text = 'x'.repeat(n);

      for(const [algorithm] of VECTORS) {
        const whole = createHash(algorithm).update(text).digest('hex');
        const parts = createHash(algorithm).update(text.slice(0, n >> 1)).update(text.slice(n >> 1)).digest('hex');

        eq(whole, parts);
      }
    }
  },

  'digest() without an encoding is a Buffer, with one a string'() {
    const h = () => createHash('sha1').update('abc');

    eq('a9993e364706816aba3e25717850c26c9cd0d89d', h().digest().toString('hex'));
    eq(20, h().digest().length);
    eq('qZk+NkcGgWq6PiVxeFDCbJzQ2J0=', h().digest('base64'));
    eq('qZk-NkcGgWq6PiVxeFDCbJzQ2J0', h().digest('base64url'));
    eq(20, h().digest('latin1').length);
    eq(20, h().digest('binary').length);
    eq('a9993e364706816aba3e25717850c26c9cd0d89d', h().digest('buffer').toString('hex'));
  },

  'update takes strings with an encoding, buffers, views'() {
    const hex = 'a9993e364706816aba3e25717850c26c9cd0d89d';

    eq(hex, createHash('sha1').update('616263', 'hex').digest('hex'));
    eq(hex, createHash('sha1').update('YWJj', 'base64').digest('hex'));
    eq(hex, createHash('sha1').update(Buffer.from('abc')).digest('hex'));
    eq(hex, createHash('sha1').update(new Uint8Array([97, 98, 99])).digest('hex'));
    eq(hex, createHash('sha1').update(new DataView(new Uint8Array([97, 98, 99]).buffer)).digest('hex'));
    eq(hex, createHash('sha1').update(new Uint8Array([0, 97, 98, 99, 0]).subarray(1, 4)).digest('hex'));
    eq('de2e331d891ae267a7009cb45b4e8830f170e0c937288ea2731a1941c7a53b0d', createHash('sha256').update('é', 'latin1').digest('hex'));
    if(!deno) eq(createHash('sha256').update('abc').digest('hex'), createHash('sha256').update('abc', 'bogus').digest('hex'));
  },

  'a view is read where it is, not from its buffer start'() {
    const bytes = new Uint8Array(70).map((v, i) => i);

    for(const offset of [1, 2, 3, 5, 7])
      eq(createHash('sha256').update(Buffer.from(bytes.slice(offset, offset + 40))).digest('hex'), createHash('sha256').update(bytes.subarray(offset, offset + 40)).digest('hex'));
  },

  'update takes an ArrayBuffer on qjsm, where readFileSync returns one'() {
    if(typeof scriptArgs == 'undefined') return;

    eq('a9993e364706816aba3e25717850c26c9cd0d89d', createHash('sha1').update(new Uint8Array([97, 98, 99]).buffer).digest('hex'));
  },

  'update copies the data'() {
    const bytes = new Uint8Array([97, 98, 99]);
    const h = createHash('sha1').update(bytes);

    bytes.fill(0);
    eq('a9993e364706816aba3e25717850c26c9cd0d89d', h.digest('hex'));
  },

  'update returns the hash'() {
    const h = createHash('md5');

    assert(h.update('a') === h, 'chain');
    assert(h instanceof Hash, 'instance');
  },

  'copy() forks the state'() {
    const h = createHash('sha256').update('a');
    const c = h.copy();

    c.update('b');
    eq('ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb', h.digest('hex'));
    eq('fb8e20fc2e4c3f248c60c39bd652f3c1347298bb977b8b4d5903b85055620603', c.digest('hex'));
  },

  'a finished hash throws ERR_CRYPTO_HASH_FINALIZED'() {
    for(const use of [h => h.digest(), h => h.update('a'), h => h.copy()]) {
      const h = createHash('md5');

      h.digest();

      const e = fails(() => use(h));

      eq('ERR_CRYPTO_HASH_FINALIZED Digest already called', `${e.code} ${e.message}`);
    }
  },

  'algorithm names are case-insensitive, with the usual aliases'() {
    const want = createHash('sha256').update('a').digest('hex');

    for(const name of deno ? ['SHA256'] : ['SHA256', 'sha-256', 'RSA-SHA256']) eq(want, createHash(name).update('a').digest('hex'));
  },

  'bad arguments throw as Node does'() {
    assert(fails(() => createHash('nope')).message.startsWith('Digest method not supported'), 'unknown algorithm');

    for(const [value, text] of [[undefined, 'undefined'], [1, 'type number (1)']]) {
      const e = fails(() => createHash(value));

      eq(`TypeError ERR_INVALID_ARG_TYPE The "algorithm" argument must be of type string. Received ${text}`, `${e.name} ${e.code} ${e.message}`);
    }

    for(const [value, text] of [[123, 'type number (123)'], [undefined, 'undefined']]) {
      const e = fails(() => createHash('sha1').update(value));

      eq(`TypeError ERR_INVALID_ARG_TYPE The "data" argument must be of type string or an instance of Buffer, TypedArray, or DataView. Received ${text}`, `${e.name} ${e.code} ${e.message}`);
    }
  },

  'hash() is one call, hex by default'() {
    eq('a9993e364706816aba3e25717850c26c9cd0d89d', hash('sha1', 'abc'));
    eq('qZk+NkcGgWq6PiVxeFDCbJzQ2J0=', hash('sha1', 'abc', 'base64'));
    eq('a9993e364706816aba3e25717850c26c9cd0d89d', hash('sha1', 'abc', 'buffer').toString('hex'));
  },

  'createHmac() matches Node, long keys are hashed first'() {
    eq('9c196e32dc0175f86f4b1cb89289d6619de6bee699e4c378e68309ed97a1a6ab', createHmac('sha256', 'key').update('abc').digest('hex'));
    eq('9b4a918f398d74d3e367970aba3cbe54e4d2b5d9', createHmac('sha1', '').update('abc').digest('hex'));
    eq('b5245971beb52a5a986812c4666a05c735bf5bb7aba32eae2192adad605df4112d6c285d1c46cf81ccb7ab8c2c3b7b3c6793216909b5add05223ed21f24cdb1e', createHmac('sha512', 'k'.repeat(200)).update('msg').digest('hex'));
    eq('63530468a04e386459855da0063b6596', createHmac('md5', new TextEncoder().encode('key')).digest('hex'));
    assert(createHmac('sha1', 'k') instanceof Hmac, 'Hmac');
    assert(crypto.createHmac === createHmac, 'default.createHmac');
  },

  'createHmac() without a key throws ERR_INVALID_ARG_TYPE'() {
    const e = fails(() => createHmac('sha256'));

    eq('TypeError ERR_INVALID_ARG_TYPE', `${e.name} ${e.code}`);
  },
});
