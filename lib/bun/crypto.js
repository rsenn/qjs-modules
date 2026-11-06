/* bun/crypto.js: Bun.CryptoHasher over node/crypto (Hash and Hmac).
 * depends on: node/crypto, node/buffer.
 * rule: Bun's names, argument shapes and encodings, checked against bun 1.4.2.
 *
 *   const h = new Bun.CryptoHasher('sha256');
 *   h.update('abc').digest('hex');   // digest() resets the hasher
 *
 * limits:
 *   algorithms   md5, sha1, sha224, sha256, sha384, sha512, sha512-224, sha512-256,
 *                Bun's blake2, sha3, md4, ripemd160 and shake are absent
 *   not here     update() of a Blob
 */
import { createHash, createHmac, getHashes } from 'node/crypto';
import { Buffer } from 'node/buffer';

const nodeError = (Type, code, message) => Object.assign(new Type(message), { code });

const SIZES = Object.fromEntries(getHashes().map(name => [name, createHash(name).digest().length]));

/* sha-256 and SHA256 are sha256 */
const canonical = name => (typeof name == 'string' ? name.toLowerCase().replace(/^sha-(\d+)$/, 'sha$1') : name);

const bytesOf = (data, encoding) => {
  if(typeof data == 'string') return Buffer.from(data, Buffer.isEncoding(encoding) ? encoding : 'utf8');
  if(ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength).slice();
  if(data instanceof ArrayBuffer) return new Uint8Array(data).slice();

  throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'expected blob, string or buffer');
};

export class CryptoHasher {
  #algorithm;
  #key;
  #chunks = [];

  constructor(algorithm, key) {
    if(typeof algorithm != 'string') throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'Expected an algorithm name as an argument');

    const name = canonical(algorithm);

    if(!(name in SIZES)) throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', `Unsupported algorithm ${algorithm}`);

    this.#algorithm = name;
    this.#key = key === undefined ? undefined : bytesOf(key);
  }

  static get algorithms() {
    return Object.keys(SIZES);
  }

  static hash(algorithm, data, encoding) {
    return new CryptoHasher(algorithm).update(data).digest(encoding);
  }

  get algorithm() {
    return this.#algorithm;
  }

  get byteLength() {
    return SIZES[this.#algorithm];
  }

  update(data, encoding) {
    this.#chunks.push(bytesOf(data, encoding));

    return this;
  }

  copy() {
    const copy = new CryptoHasher(this.#algorithm, this.#key);

    copy.#chunks = [...this.#chunks];

    return copy;
  }

  /* hex, base64, base64url and the other Buffer encodings give a string, a TypedArray is filled and returned, otherwise a Buffer */
  digest(encoding) {
    const h = this.#key ? createHmac(this.#algorithm, this.#key) : createHash(this.#algorithm);
    const view = ArrayBuffer.isView(encoding);

    if(encoding !== undefined && !view && encoding != 'buffer' && !Buffer.isEncoding(encoding)) throw nodeError(TypeError, 'ERR_INVALID_ARG_VALUE', `Unknown encoding: ${encoding}`);

    for(const chunk of this.#chunks) h.update(chunk);

    this.#chunks = [];

    if(!view) return h.digest(encoding);

    const out = h.digest();

    if(encoding.byteLength < out.length) throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', `TypedArray must be at least ${out.length} bytes`);

    new Uint8Array(encoding.buffer, encoding.byteOffset, encoding.byteLength).set(out);

    return encoding;
  }
}

export default { CryptoHasher };
