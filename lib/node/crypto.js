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
 * | getHashes()                        | the algorithms of digest           |
 *
 * limits:
 *   algorithms   md5, sha1, sha224, sha256, sha384, sha512, sha512-224, sha512-256
 *                and the spellings sha-256 and rsa-sha256
 *   Hash         no Transform stream: write(), pipe() and options are absent
 *   not here     createHmac, randomBytes and the rest of node:crypto
 */
import { algorithms, digest } from 'digest';
import { Buffer } from 'node/buffer';

const nodeError = (Type, code, message) => Object.assign(new Type(message), { code });

const received = v => (typeof v == 'string' ? `type string ('${v}')` : v == null ? String(v) : typeof v == 'object' ? `an instance of ${v.constructor?.name ?? 'Object'}` : `type ${typeof v} (${String(v)})`);

const NAMES = new Set(algorithms.map(a => a.name));

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

    const message = new Uint8Array(this.#chunks.reduce((n, c) => n + c.length, 0));
    let offset = 0;

    for(const chunk of this.#chunks) {
      message.set(chunk, offset);
      offset += chunk.length;
    }

    return output(digest(this.#algorithm, message), outputEncoding);
  }

  copy(options) {
    this.#check();

    return new Hash(this.#algorithm, options, [...this.#chunks]);
  }
}

export const createHash = (algorithm, options) => new Hash(algorithm, options);

export const getHashes = () => algorithms.map(a => a.name);

/* Node 21.7: createHash(...).update(data).digest(outputEncoding), hex by default */
export const hash = (algorithm, data, outputEncoding = 'hex') => new Hash(algorithm).update(data).digest(outputEncoding);

export default { createHash, getHashes, hash, Hash };
