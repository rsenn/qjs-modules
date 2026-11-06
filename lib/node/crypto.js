/* node/crypto.js: Node's crypto (https://nodejs.org/api/crypto.html) for node:crypto.
 * depends on: digest (md5, sha1, sha2 over third_party/Hash-Algorithms), node/buffer.
 * rule: Node's names, argument shapes, error codes and messages, checked against node 23.
 *
 *   import { createHash } from 'node:crypto';
 *   createHash('sha256').update('abc').digest('hex')
 *
 * | node:crypto                        | here                               |
 * | ---------------------------------- | ---------------------------------- |
 * | createHash(alg), Hash              | the chunks are kept, hashed at     |
 * |                                    | digest(), copy() shares the chunks |
 * | hash(alg, data, outputEncoding)    | createHash in one call             |
 * | createHmac(alg, key), Hmac         | HMAC (RFC 2104) over digest        |
 * | getHashes()                        | the algorithms of digest           |
 *
 * limits:
 *   algorithms   md5, sha1, sha224, sha256, sha384, sha512, sha512-224, sha512-256
 *                and the spellings sha-256 and rsa-sha256
 *   Hash         no Transform stream: write(), pipe() and options are absent
 *   Hmac         no Transform stream, no KeyObject or CryptoKey as key
 *   not here     randomBytes and the rest of node:crypto
 */
import { algorithms, digest } from 'digest';
import { Buffer } from 'node/buffer';

const nodeError = (Type, code, message) => Object.assign(new Type(message), { code });

const received = v => (typeof v == 'string' ? `type string ('${v}')` : v == null ? String(v) : typeof v == 'object' ? `an instance of ${v.constructor?.name ?? 'Object'}` : `type ${typeof v} (${String(v)})`);

const NAMES = new Set(algorithms.map(a => a.name));
const BLOCK = new Map(algorithms.map(a => [a.name, a.blockSize]));

/* node:crypto name -> digest name: sha-256 and rsa-sha256 are sha256 */
function algorithmName(name) {
  if(typeof name != 'string') throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "algorithm" argument must be of type string. Received ${received(name)}`);

  const n = name.toLowerCase().replace(/^rsa-/, '').replace(/^sha-(\d+)$/, 'sha$1');

  if(!NAMES.has(n)) throw new Error('Digest method not supported');

  return n;
}

/* the bytes of a string, Buffer, TypedArray, DataView or ArrayBuffer, as a copy: the caller may change its buffer later.
   ArrayBuffer is no Node input, but qjsm's readFileSync() returns one where Node returns a Buffer */
function bytesOf(data, encoding) {
  if(typeof data == 'string') return Buffer.from(data, Buffer.isEncoding(encoding) ? encoding : 'utf8');
  if(ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength).slice();
  if(data instanceof ArrayBuffer) return new Uint8Array(data).slice();

  throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "data" argument must be of type string or an instance of Buffer, TypedArray, or DataView. Received ${received(data)}`);
}

/* the digest as a Buffer, or as a string for a known encoding */
function output(bytes, encoding) {
  const buffer = Buffer.from(bytes);

  return typeof encoding == 'string' && encoding != 'buffer' && Buffer.isEncoding(encoding) ? buffer.toString(encoding) : buffer;
}

/* the chunks as one Uint8Array */
function concat(chunks) {
  const message = new Uint8Array(chunks.reduce((n, c) => n + c.length, 0));
  let offset = 0;

  for(const chunk of chunks) {
    message.set(chunk, offset);
    offset += chunk.length;
  }

  return message;
}

export class Hash {
  #algorithm;
  #chunks;
  #done = false;

  constructor(algorithm, options, chunks = []) {
    this.#algorithm = algorithmName(algorithm);
    this.#chunks = chunks;
  }

  #check() {
    if(this.#done) throw nodeError(Error, 'ERR_CRYPTO_HASH_FINALIZED', 'Digest already called');
  }

  update(data, inputEncoding) {
    this.#check();
    this.#chunks.push(bytesOf(data, inputEncoding));

    return this;
  }

  digest(outputEncoding) {
    this.#check();
    this.#done = true;

    return output(digest(this.#algorithm, concat(this.#chunks)), outputEncoding);
  }

  copy(options) {
    this.#check();

    return new Hash(this.#algorithm, options, [...this.#chunks]);
  }
}

export const createHash = (algorithm, options) => new Hash(algorithm, options);

/* HMAC: H((K ^ opad) || H((K ^ ipad) || message)), K padded with zeros to the block size, hashed first when longer */
export class Hmac {
  #algorithm;
  #key;
  #chunks = [];
  #done = false;

  constructor(algorithm, key) {
    this.#algorithm = algorithmName(algorithm);

    if(typeof key != 'string' && !ArrayBuffer.isView(key) && !(key instanceof ArrayBuffer))
      throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "key" argument must be of type string or an instance of ArrayBuffer, Buffer, TypedArray, or DataView. Received ${received(key)}`);

    let bytes = bytesOf(key);

    if(bytes.length > BLOCK.get(this.#algorithm)) bytes = new Uint8Array(digest(this.#algorithm, bytes));

    this.#key = new Uint8Array(BLOCK.get(this.#algorithm));
    this.#key.set(bytes);
  }

  update(data, inputEncoding) {
    if(this.#done) throw nodeError(Error, 'ERR_CRYPTO_HASH_FINALIZED', 'Digest already called');

    this.#chunks.push(bytesOf(data, inputEncoding));

    return this;
  }

  digest(outputEncoding) {
    if(this.#done) return output(new Uint8Array(0), outputEncoding);

    this.#done = true;

    const pad = byte => concat([this.#key.map(b => b ^ byte)]);
    const inner = new Uint8Array(digest(this.#algorithm, concat([pad(0x36), ...this.#chunks])));

    return output(digest(this.#algorithm, concat([pad(0x5c), inner])), outputEncoding);
  }
}

export const createHmac = (algorithm, key) => new Hmac(algorithm, key);

export const getHashes = () => algorithms.map(a => a.name);

/* Node 21.7: createHash(...).update(data).digest(outputEncoding), hex by default */
export const hash = (algorithm, data, outputEncoding = 'hex') => new Hash(algorithm).update(data).digest(outputEncoding);

export default { createHash, createHmac, getHashes, hash, Hash, Hmac };
