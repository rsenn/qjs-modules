/* webcrypto.js: crypto.subtle, CryptoKey and SubtleCrypto of the Web Crypto API.
 * depends on: lws (crypto engines, loaded on first use), node/buffer.
 * rule: shapes, errors and messages as bun 1.4.2, engines in C, API in JS.
 *
 *   const pair = await crypto.subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-256' }, true, ['sign', 'verify']);
 *   await crypto.subtle.sign({ name: 'ECDSA', hash: 'SHA-256' }, pair.privateKey, bytes);   // 64 bytes, r||s
 *
 * | method                              | here                                           |
 * | ----------------------------------- | ---------------------------------------------- |
 * | digest                              | SHA-1, SHA-256, SHA-384, SHA-512               |
 * | generateKey, importKey, exportKey   | ECDSA P-256, P-384, P-521 and HMAC             |
 * | sign, verify                        | the same, ECDSA signatures are IEEE P1363      |
 *
 * limits:
 *   formats      raw and jwk, no pkcs8 or spki
 *   algorithms   no RSA, ECDH, AES, PBKDF2, HKDF: those reject with NotSupportedError
 *   needs        the lws module built with OpenSSL 3
 */
import { Buffer } from 'node/buffer';

const lws = () =>
  import('lws').then(m => {
    if(typeof m.crypto?.digest != 'function') throw new DOMException('The algorithm is not supported', 'NotSupportedError');

    return m.crypto;
  });

const fail = (name, message) => new DOMException(message, name);
const notSupported = () => fail('NotSupportedError', 'The algorithm is not supported');
const missing = () => fail('SyntaxError', 'A required parameter was missing or out-of-range');
const nodeError = (Type, code, message) => Object.assign(new Type(message), { code });

const HASHES = ['SHA-1', 'SHA-256', 'SHA-384', 'SHA-512'];
const BLOCK_BITS = { 'SHA-1': 512, 'SHA-256': 512, 'SHA-384': 1024, 'SHA-512': 1024 };
const CURVES = { 'P-256': 32, 'P-384': 48, 'P-521': 66 };

const base64url = bytes => Buffer.from(bytes).toString('base64url');
const unbase64url = text => new Uint8Array(Buffer.from(String(text), 'base64url'));

const isBytes = v => ArrayBuffer.isView(v) || v instanceof ArrayBuffer;

function bytesArg(v, name = 'data') {
  if(isBytes(v)) return v;

  const got = typeof v == 'string' ? `type string ('${v}')` : v == null ? String(v) : `type ${typeof v}`;

  throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "${name}" argument must be of type ArrayBuffer, Buffer, TypedArray, or DataView. Received ${got}`);
}

const view = v => (v instanceof ArrayBuffer ? new Uint8Array(v) : new Uint8Array(v.buffer, v.byteOffset, v.byteLength));

/* the name of a hash given as 'sha256', 'SHA-256' or { name: 'SHA-256' } */
function hashName(h) {
  const n = String(typeof h == 'object' && h ? h.name : h)
    .toUpperCase()
    .replace(/^SHA-?/, 'SHA-');

  if(!HASHES.includes(n)) throw fail('NotSupportedError', 'Unrecognized algorithm name');

  return n;
}

/* an algorithm argument as an object with the name in upper case */
function algorithmOf(a) {
  const o = typeof a == 'string' ? { name: a } : a;

  if(!o || typeof o != 'object' || typeof o.name != 'string') throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "algorithm" argument must be of type string or an instance of Object');

  return { ...o, name: o.name.toUpperCase() };
}

const keys = new WeakMap();
const TOKEN = Symbol('CryptoKey');

export class CryptoKey {
  #type;
  #extractable;
  #algorithm;
  #usages;

  constructor(token, type, extractable, algorithm, usages, data) {
    if(token !== TOKEN) throw new TypeError('Illegal constructor');

    this.#type = type;
    this.#extractable = extractable;
    this.#algorithm = algorithm;
    this.#usages = usages;
    keys.set(this, data);
  }

  get type() {
    return this.#type;
  }

  get extractable() {
    return this.#extractable;
  }

  get algorithm() {
    return this.#algorithm;
  }

  get usages() {
    return [...this.#usages];
  }

  get [Symbol.toStringTag]() {
    return 'CryptoKey';
  }
}

const makeKey = (type, extractable, algorithm, usages, data) => new CryptoKey(TOKEN, type, extractable, algorithm, usages, data);

/* the usages of the order sign, verify, and an error when one is not in allowed */
function checkUsages(usages, allowed) {
  if(!Array.isArray(usages) || usages.some(u => !allowed.includes(u))) throw missing();

  return allowed.filter(u => usages.includes(u));
}

const curveOf = a => {
  if(!(a.namedCurve in CURVES)) throw notSupported();

  return a.namedCurve;
};

/* the uncompressed point 04 || x || y */
function rawPoint({ x, y }) {
  const out = new Uint8Array(1 + x.length + y.length);

  out[0] = 4;
  out.set(x, 1);
  out.set(y, 1 + x.length);

  return out;
}

const hmacParams = a => {
  if(a.hash === undefined) throw nodeError(TypeError, 'ERR_MISSING_OPTION', 'Member HmacImportParams.hash is required and must be an instance of (object or DOMString)');

  return hashName(a.hash);
};

const hmacAlgorithm = (hash, bits) => ({ name: 'HMAC', hash: { name: hash }, length: bits });

async function generateKey(algorithm, extractable, usages) {
  const a = algorithmOf(algorithm);
  const native = await lws();

  if(a.name == 'ECDSA') {
    const curve = curveOf(a);
    const allowed = checkUsages(usages, ['sign', 'verify']);

    if(!allowed.length) throw missing();

    const pair = native.ecGenerateKey(curve);
    const d = new Uint8Array(pair.d);
    const point = { x: new Uint8Array(pair.x), y: new Uint8Array(pair.y) };
    const algorithm2 = { name: 'ECDSA', namedCurve: curve };

    return {
      privateKey: makeKey('private', !!extractable, algorithm2, allowed.filter(u => u == 'sign'), { curve, d, ...point }),
      publicKey: makeKey('public', true, algorithm2, allowed.filter(u => u == 'verify'), { curve, ...point }),
    };
  }

  if(a.name == 'HMAC') {
    const hash = hmacParams(a);
    const allowed = checkUsages(usages, ['sign', 'verify']);
    const bits = a.length ?? BLOCK_BITS[hash];

    if(!allowed.length || !(bits > 0)) throw missing();

    const secret = globalThis.crypto.getRandomValues(new Uint8Array(Math.ceil(bits / 8)));

    return makeKey('secret', !!extractable, hmacAlgorithm(hash, bits), allowed, { secret, hash });
  }

  throw notSupported();
}

async function importKey(format, keyData, algorithm, extractable, usages) {
  const a = algorithmOf(algorithm);
  const native = await lws();

  if(!['raw', 'jwk'].includes(format)) throw notSupported();

  if(a.name == 'ECDSA') {
    const curve = curveOf(a);
    const size = CURVES[curve];
    const algorithm2 = { name: 'ECDSA', namedCurve: curve };
    let x, y, d;

    if(format == 'raw') {
      const raw = view(bytesArg(keyData, 'keyData'));

      if(raw.length != 1 + 2 * size || raw[0] != 4) throw fail('DataError', 'Invalid keyData');

      x = raw.slice(1, 1 + size);
      y = raw.slice(1 + size);
    } else {
      if(keyData?.kty != 'EC' || keyData.crv != curve) throw fail('DataError', 'Invalid keyData');

      if(keyData.ext === false && extractable) throw fail('DataError', 'Invalid keyData');

      x = unbase64url(keyData.x);
      y = unbase64url(keyData.y);
      d = keyData.d === undefined ? undefined : unbase64url(keyData.d);
    }

    const type = d ? 'private' : 'public';
    const allowed = checkUsages(usages, [type == 'private' ? 'sign' : 'verify']);

    if(type == 'private' && !allowed.length) throw missing();
    if(!native.ecValidatePoint(curve, x, y)) throw fail('DataError', 'Invalid keyData');

    if(d) {
      let pub;

      try {
        pub = native.ecPublicKey(curve, d);
      } catch(e) {
        throw fail('DataError', 'Invalid keyData');
      }

      if(Buffer.compare(Buffer.from(pub.x), Buffer.from(x)) || Buffer.compare(Buffer.from(pub.y), Buffer.from(y))) throw fail('DataError', 'Invalid keyData');
    }

    return makeKey(type, !!extractable, algorithm2, allowed, { curve, d, x, y });
  }

  if(a.name == 'HMAC') {
    const hash = hmacParams(a);
    const allowed = checkUsages(usages, ['sign', 'verify']);
    let secret;

    if(format == 'raw') secret = view(bytesArg(keyData, 'keyData')).slice();
    else {
      if(keyData?.kty != 'oct' || typeof keyData.k != 'string') throw fail('DataError', 'Invalid keyData');

      secret = unbase64url(keyData.k);
    }

    if(!allowed.length) throw missing();

    return makeKey('secret', !!extractable, hmacAlgorithm(hash, a.length ?? secret.length * 8), allowed, { secret, hash });
  }

  throw notSupported();
}

async function exportKey(format, key) {
  if(!(key instanceof CryptoKey)) throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "key" argument must be an instance of CryptoKey');
  if(!['raw', 'jwk'].includes(format)) throw notSupported();
  if(!key.extractable) throw fail('InvalidAccessError', 'key is not extractable');

  const data = keys.get(key);
  const ops = key.usages;

  if(key.algorithm.name == 'ECDSA') {
    if(format == 'raw') {
      if(key.type != 'public') throw fail('InvalidAccessError', 'Unable to export a raw EC private key');

      return rawPoint(data).buffer;
    }

    return { crv: data.curve, ...(data.d && { d: base64url(data.d) }), ext: true, key_ops: ops, kty: 'EC', x: base64url(data.x), y: base64url(data.y) };
  }

  if(format == 'raw') return data.secret.slice().buffer;

  return { alg: `HS${data.hash.replace('SHA-', '')}`, ext: true, k: base64url(data.secret), key_ops: ops, kty: 'oct' };
}

/* a key that may do op, else InvalidAccessError, for the algorithm a */
function usable(key, a, op) {
  if(!(key instanceof CryptoKey)) throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "key" argument must be an instance of CryptoKey');
  if(key.algorithm.name != a.name) throw fail('InvalidAccessError', 'Key algorithm mismatch');
  if(!key.usages.includes(op) || key.type == (op == 'sign' ? 'public' : 'private')) throw fail('InvalidAccessError', `Unable to use this key to ${op}`);

  return keys.get(key);
}

const ecdsaHash = a => {
  if(a.hash === undefined) throw nodeError(TypeError, 'ERR_MISSING_OPTION', 'Member EcdsaParams.hash is required and must be an instance of (object or DOMString)');

  return hashName(a.hash);
};

async function sign(algorithm, key, data) {
  const a = algorithmOf(algorithm);
  const native = await lws();

  bytesArg(data);

  if(a.name == 'ECDSA') {
    const hash = ecdsaHash(a);
    const k = usable(key, a, 'sign');

    return native.ecSign(k.curve, k.d, hash, data);
  }

  if(a.name == 'HMAC') {
    const k = usable(key, a, 'sign');

    return native.hmac(k.hash, k.secret, data);
  }

  throw notSupported();
}

async function verify(algorithm, key, signature, data) {
  const a = algorithmOf(algorithm);
  const native = await lws();

  bytesArg(signature, 'signature');
  bytesArg(data);

  if(a.name == 'ECDSA') {
    const hash = ecdsaHash(a);
    const k = usable(key, a, 'verify');

    return native.ecVerify(k.curve, k.x, k.y, hash, signature, data);
  }

  if(a.name == 'HMAC') {
    const k = usable(key, a, 'verify');
    const want = view(new Uint8Array(native.hmac(k.hash, k.secret, data)));
    const got = view(signature);
    let diff = want.length ^ got.length;

    for(let i = 0; i < want.length; i++) diff |= want[i] ^ (got[i] ?? 0);

    return diff === 0;
  }

  throw notSupported();
}

export class SubtleCrypto {
  async digest(algorithm, data) {
    const name = hashName(algorithmOf(algorithm).name);
    const native = await lws();

    return native.digest(name, bytesArg(data));
  }

  generateKey(algorithm, extractable, keyUsages) {
    return generateKey(algorithm, extractable, keyUsages);
  }

  importKey(format, keyData, algorithm, extractable, keyUsages) {
    return importKey(format, keyData, algorithm, extractable, keyUsages);
  }

  exportKey(format, key) {
    return exportKey(format, key);
  }

  sign(algorithm, key, data) {
    return sign(algorithm, key, data);
  }

  verify(algorithm, key, signature, data) {
    return verify(algorithm, key, signature, data);
  }

  get [Symbol.toStringTag]() {
    return 'SubtleCrypto';
  }
}

for(const name of ['encrypt', 'decrypt', 'deriveBits', 'deriveKey', 'wrapKey', 'unwrapKey'])
  Object.defineProperty(SubtleCrypto.prototype, name, {
    value: async function() {
      throw notSupported();
    },
    writable: true,
    configurable: true,
  });

export const subtle = new SubtleCrypto();

export default { subtle, SubtleCrypto, CryptoKey };
