/* Bun.CryptoHasher of lib/bun/crypto.js; expected values come from bun 1.4.2.
 * runs under bun as well: bun tests/unittests/test-bun-crypto.js */
import { assert, eq, tests } from '../../lib/tinytest.js';

const Bun = globalThis.Bun ?? (await import('../../lib/bun.js')).default;
const { CryptoHasher } = Bun;
const ABC = 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad';

const fails = fn => {
  try {
    fn();
  } catch(e) {
    return e;
  }

  return null;
};

tests({
  'update() chains and digest() gives hex, base64, base64url or a Buffer'() {
    const h = new CryptoHasher('sha256');

    assert(h.update('abc') === h, 'update returns this');
    eq(ABC, h.digest('hex'));
    eq('ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=', new CryptoHasher('sha256').update('abc').digest('base64'));
    eq('ungWv48Bz-pBQUDeXa4iI7ADYaOWF3qctBD_YfIAFa0', new CryptoHasher('sha256').update('abc').digest('base64url'));

    const raw = new CryptoHasher('sha256').update('abc').digest();

    eq(32, raw.length);
    eq('ba', raw[0].toString(16));
    eq('Buffer', new CryptoHasher('sha1').update('abc').digest('buffer').constructor.name);
  },

  'digest() resets the hasher'() {
    const h = new CryptoHasher('sha256').update('abc');

    eq(ABC, h.digest('hex'));
    eq('2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a4881', h.update('x').digest('hex'));
  },

  'algorithm, byteLength and the name spellings'() {
    const h = new CryptoHasher('sha256');

    eq('sha256 32', `${h.algorithm} ${h.byteLength}`);
    eq(20, new CryptoHasher('sha1').byteLength);

    for(const name of ['SHA256', 'sha-256']) eq('e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855', new CryptoHasher(name).digest('hex'));

    eq('d41d8cd98f00b204e9800998ecf8427e', new CryptoHasher('md5').digest('hex'));
    eq('c672b8d1ef56ed28ab87c3622c5114069bdd3ad7b8f9737498d0c01ecef0967a', new CryptoHasher('sha512-256').digest('hex'));
    assert(CryptoHasher.algorithms.includes('sha256'), 'algorithms');
  },

  'update() takes strings with an encoding, buffers and views'() {
    eq(ABC, new CryptoHasher('sha256').update('616263', 'hex').digest('hex'));
    eq(ABC, new CryptoHasher('sha256').update(new Uint8Array([97, 98, 99]).buffer).digest('hex'));
    eq(ABC, new CryptoHasher('sha256').update(new Uint8Array([97, 98, 99])).digest('hex'));
    eq(ABC, new CryptoHasher('sha256').update('a').update('bc').digest('hex'));
  },

  'copy() forks the state'() {
    const a = new CryptoHasher('sha256').update('a');
    const b = a.copy();

    b.update('b');
    eq('ca978112', a.digest('hex').slice(0, 8));
    eq('fb8e20fc', b.digest('hex').slice(0, 8));
  },

  'a key makes it an HMAC'() {
    eq('9c196e32dc0175f86f4b1cb89289d6619de6bee699e4c378e68309ed97a1a6ab', new CryptoHasher('sha256', 'key').update('abc').digest('hex'));
  },

  'hash() is one call'() {
    eq(ABC, CryptoHasher.hash('sha256', 'abc', 'hex'));
  },

  'bad arguments throw TypeError'() {
    eq('TypeError ERR_INVALID_ARG_TYPE Unsupported algorithm nope', ((e = fails(() => new CryptoHasher('nope'))) => `${e.name} ${e.code} ${e.message}`)());
    eq('TypeError ERR_INVALID_ARG_TYPE Expected an algorithm name as an argument', ((e = fails(() => new CryptoHasher())) => `${e.name} ${e.code} ${e.message}`)());
    eq('TypeError ERR_INVALID_ARG_TYPE expected blob, string or buffer', ((e = fails(() => new CryptoHasher('sha256').update(5))) => `${e.name} ${e.code} ${e.message}`)());
    eq('TypeError ERR_INVALID_ARG_VALUE Unknown encoding: wat', ((e = fails(() => new CryptoHasher('sha256').update('a').digest('wat'))) => `${e.name} ${e.code} ${e.message}`)());
  },
});
