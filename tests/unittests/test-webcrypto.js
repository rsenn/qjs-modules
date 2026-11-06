/* crypto.subtle of lib/webcrypto.js, engines from the lws module (skipped without it); expected values come from bun 1.4.2.
 * runs under bun as well: bun tests/unittests/test-webcrypto.js */
import { assert, eq, tests } from '../../lib/tinytest.js';

if(!globalThis.Bun) await import('../../lib/globals.js');

const native = !!globalThis.process?.versions?.bun || (await globalThis.crypto.subtle.digest('SHA-256', new Uint8Array(1)).then(() => true, () => false));

const subtle = globalThis.crypto.subtle;
const data = new TextEncoder().encode('hello');
const hex = buffer => [...new Uint8Array(buffer)].map(b => b.toString(16).padStart(2, '0')).join('');
const b64url = text => Uint8Array.from(atob(text.replace(/-/g, '+').replace(/_/g, '/')), c => c.charCodeAt(0));

/* made by bun: private jwk and the signature of hello */
const FIXED = [
  {
    curve: 'P-256',
    hash: 'SHA-256',
    jwk: { crv: 'P-256', d: 'TelJ7yBSKCz9hDBeuYBlVDn9ZQnf7yyscR19HA6sHKc', ext: true, key_ops: ['sign'], kty: 'EC', x: '-9BIB3HHi6ncN3B9_pFYVlSq4Q-28WIjZPpZC9X8n7E', y: 'tN4Z8FVrr9l-vyECp_I99n8Y-UlIFbmme9FQmx9b4is' },
    sig: 'mbMznl3jYCp5yE4eSxiP1WE-UG642pYUvF_KFm2uDgf4nLikXb9dX6FZnNDnUbjyfnVeIOzjmYsi8-zx0abh5A',
  },
  {
    curve: 'P-384',
    hash: 'SHA-384',
    jwk: { crv: 'P-384', d: 'POuRGUn-fZmfZxPjqBgvAXzhz9mQZlvD5FnDPHqdN0SSyw0TLR_8RL5ehRNJw0bX', ext: true, key_ops: ['sign'], kty: 'EC', x: 'aY2SDqc70FzJ2QIadLjtvrxp2NCOI1QsQS0J19WhBDPqBhqRZJ0HBf4cHg2Y-ke_', y: 'BVI-FJ76Vnlg1FjcV73gKk87taCt0pgb4Kn0dMYUKqGHtVt-dxe1ZFNdOf4VCPCa' },
    sig: 'WiVi7TjnfUzPz9sjU6ZvmDpr-bS-OcJGsgpr1Q3BcWqBIP-vdsnLsqC-U-hwkLjQ5MasIwqSTJ2U7_zT4b2D5zDrD8wxvRzwb-vKOrIkJk23P86A0DXV6VIzjnvPO86R',
  },
];

async function fails(promise) {
  try {
    await promise;
  } catch(e) {
    return `${e.name}: ${e.message}`;
  }

  return null;
}

const ec = { name: 'ECDSA', namedCurve: 'P-256' };

tests(
  native
    ? {
        async 'digest'() {
          eq('ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad', hex(await subtle.digest('SHA-256', new TextEncoder().encode('abc'))));
          eq(20, (await subtle.digest({ name: 'sha-1' }, data)).byteLength);
          eq('NotSupportedError: Unrecognized algorithm name', await fails(subtle.digest('MD5', data)));
        },

        async 'ECDSA keys: shape of a generated pair'() {
          const pair = await subtle.generateKey(ec, true, ['sign', 'verify']);

          eq('privateKey,publicKey', Object.keys(pair).join());
          eq('public true verify', `${pair.publicKey.type} ${pair.publicKey.extractable} ${pair.publicKey.usages}`);
          eq('private true sign', `${pair.privateKey.type} ${pair.privateKey.extractable} ${pair.privateKey.usages}`);
          eq('ECDSA P-256', `${pair.publicKey.algorithm.name} ${pair.publicKey.algorithm.namedCurve}`);
          eq('[object CryptoKey]', Object.prototype.toString.call(pair.publicKey));
          assert(pair.publicKey instanceof CryptoKey, 'CryptoKey');
        },

        async 'export raw and jwk'() {
          const pair = await subtle.generateKey(ec, true, ['sign', 'verify']);
          const raw = new Uint8Array(await subtle.exportKey('raw', pair.publicKey));
          const jwk = await subtle.exportKey('jwk', pair.privateKey);

          eq('65 4', `${raw.length} ${raw[0]}`);
          eq('crv,d,ext,key_ops,kty,x,y', Object.keys(jwk).join());
          eq('EC P-256 true sign', `${jwk.kty} ${jwk.crv} ${jwk.ext} ${jwk.key_ops}`);
          eq('crv,ext,key_ops,kty,x,y', Object.keys(await subtle.exportKey('jwk', pair.publicKey)).join());
        },

        async 'a non-extractable key does not export'() {
          const pair = await subtle.generateKey(ec, false, ['sign', 'verify']);

          eq('InvalidAccessError: key is not extractable', await fails(subtle.exportKey('jwk', pair.privateKey)));
          assert(pair.publicKey.extractable, 'the public key is always extractable');
        },

        async 'sign gives 64 bytes and verify accepts them'() {
          const pair = await subtle.generateKey(ec, true, ['sign', 'verify']);
          const alg = { name: 'ECDSA', hash: 'SHA-256' };
          const sig = await subtle.sign(alg, pair.privateKey, data);

          eq(64, sig.byteLength);
          eq(true, await subtle.verify(alg, pair.publicKey, sig, data));
          eq(false, await subtle.verify(alg, pair.publicKey, sig, new Uint8Array(3)));
        },

        async 'a signature and key made by bun verify, and the key imports from jwk'() {
          for(const { curve, hash, jwk, sig } of FIXED) {
            const alg = { name: 'ECDSA', hash };
            const key = await subtle.importKey('jwk', jwk, { name: 'ECDSA', namedCurve: curve }, false, ['sign']);
            const { d, key_ops, ext, ...pub } = jwk;
            const pubKey = await subtle.importKey('jwk', { ...pub, key_ops: ['verify'], ext: true }, { name: 'ECDSA', namedCurve: curve }, true, ['verify']);

            eq('private false sign', `${key.type} ${key.extractable} ${key.usages}`);
            eq(true, await subtle.verify(alg, pubKey, b64url(sig), data));
            eq(true, await subtle.verify(alg, pubKey, await subtle.sign(alg, key, data), data));
            eq(1 + 2 * b64url(jwk.x).length, (await subtle.exportKey('raw', pubKey)).byteLength);
          }
        },

        async 'import raw public key, bad points are DataError'() {
          const pair = await subtle.generateKey(ec, true, ['sign', 'verify']);
          const raw = await subtle.exportKey('raw', pair.publicKey);
          const key = await subtle.importKey('raw', raw, ec, true, ['verify']);

          eq('public verify', `${key.type} ${key.usages}`);
          eq('DataError: Invalid keyData', await fails(subtle.importKey('raw', new Uint8Array(65), ec, true, ['verify'])));

          const { d, ...pub } = await subtle.exportKey('jwk', pair.privateKey);

          eq('DataError: Invalid keyData', await fails(subtle.importKey('jwk', { ...pub, y: pub.x }, ec, true, ['verify'])));
        },

        async 'wrong key or algorithm is InvalidAccessError'() {
          const pair = await subtle.generateKey(ec, true, ['sign', 'verify']);
          const alg = { name: 'ECDSA', hash: 'SHA-256' };

          eq('InvalidAccessError: Unable to use this key to sign', await fails(subtle.sign(alg, pair.publicKey, data)));
          eq('InvalidAccessError: Key algorithm mismatch', await fails(subtle.sign({ name: 'HMAC' }, pair.privateKey, data)));
        },

        async 'bad parameters'() {
          eq('NotSupportedError: The algorithm is not supported', await fails(subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-999' }, true, ['sign'])));
          eq('SyntaxError: A required parameter was missing or out-of-range', await fails(subtle.generateKey(ec, true, ['encrypt'])));

          const pair = await subtle.generateKey(ec, true, ['sign']);

          eq('ERR_MISSING_OPTION', await subtle.sign({ name: 'ECDSA' }, pair.privateKey, data).catch(e => e.code));
        },

        async 'P-384 and P-521 pairs sign and verify'() {
          for(const [namedCurve, hash, size] of [['P-384', 'SHA-384', 96], ['P-521', 'SHA-512', 132]]) {
            const pair = await subtle.generateKey({ name: 'ECDSA', namedCurve }, true, ['sign', 'verify']);
            const sig = await subtle.sign({ name: 'ECDSA', hash }, pair.privateKey, data);

            eq(size, sig.byteLength);
            eq(true, await subtle.verify({ name: 'ECDSA', hash }, pair.publicKey, sig, data));
          }
        },

        async 'HMAC: import raw, sign, verify, export'() {
          const key = await subtle.importKey('raw', new TextEncoder().encode('key'), { name: 'HMAC', hash: 'SHA-256' }, true, ['sign', 'verify']);
          const sig = await subtle.sign('HMAC', key, new TextEncoder().encode('abc'));

          eq('secret', key.type);
          eq('HMAC SHA-256 24', `${key.algorithm.name} ${key.algorithm.hash.name} ${key.algorithm.length}`);
          eq('9c196e32dc0175f86f4b1cb89289d6619de6bee699e4c378e68309ed97a1a6ab', hex(sig));
          eq(true, await subtle.verify('HMAC', key, sig, new TextEncoder().encode('abc')));
          eq(false, await subtle.verify('HMAC', key, sig, data));
          eq('{"alg":"HS256","ext":true,"k":"a2V5","key_ops":["sign","verify"],"kty":"oct"}', JSON.stringify(await subtle.exportKey('jwk', key)));
        },

        async 'HMAC: generateKey takes the block size as length'() {
          const key = await subtle.generateKey({ name: 'HMAC', hash: 'SHA-256' }, true, ['sign']);

          eq(512, key.algorithm.length);
          eq(64, (await subtle.exportKey('raw', key)).byteLength);
        },
      }
    : {
        'skipped: the lws module has no crypto'() {},
      },
);
