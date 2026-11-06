/* password.js: Bun.password, argon2id and bcrypt.
 * depends on: bcrypt (bcrypt), lws (crypto.argon2id, loaded on first use), node/buffer.
 * rule: Bun's defaults, hash strings and error messages, checked against bun 1.4.2.
 *
 *   await Bun.password.hash('pw');                         // $argon2id$v=19$m=65536,t=2,p=1$<salt>$<hash>
 *   await Bun.password.hash('pw', { algorithm: 'bcrypt' }); // $2b$10$...
 *   await Bun.password.verify('pw', hash);                 // the hash says which algorithm
 *
 * limits:
 *   default              argon2id as in Bun, bcrypt when the lws crypto module is missing
 *   argon2i and argon2d  hash() and verify() throw PASSWORD_UNSUPPORTED_ALGORITHM, only argon2id exists
 *   async                hash() and verify() compute on the calling thread, argon2id takes ~140 ms
 *   bcrypt               passwords over 72 bytes are cut, Bun hashes them with SHA-512 first
 */
import { genSalt, hash as bcryptHash, compare } from 'bcrypt';
import { Buffer } from 'node/buffer';

const ALGORITHMS = ['bcrypt', 'argon2id', 'argon2d', 'argon2i'];

const typeError = message => Object.assign(new TypeError(message), { code: 'ERR_INVALID_ARG_TYPE' });
const passwordError = (code, message) => Object.assign(new Error(message), { code });

/* true when the lws crypto module with argon2id loads */
const available = () => {
  try {
    return typeof requireModule('lws').crypto?.argon2id == 'function';
  } catch(e) {
    return false;
  }
};

const engine = () => {
  const crypto = requireModule('lws').crypto;

  if(typeof crypto?.argon2id != 'function') throw passwordError('PASSWORD_UNSUPPORTED_ALGORITHM', 'argon2id needs the lws module built with OpenSSL');

  return crypto;
};

const b64 = bytes => Buffer.from(bytes).toString('base64').replace(/=+$/, '');
const unb64 = text => new Uint8Array(Buffer.from(text, 'base64'));

/* a password or hash as bytes: a string, a number, or a buffer */
function bytesOf(v) {
  if(ArrayBuffer.isView(v)) return new Uint8Array(v.buffer, v.byteOffset, v.byteLength);
  if(v instanceof ArrayBuffer) return new Uint8Array(v);

  return new TextEncoder().encode(String(v));
}

/* the options as { algorithm, cost, memoryCost, timeCost }, with Bun's defaults and checks */
function optionsOf(options = {}) {
  const o = typeof options == 'string' ? { algorithm: options } : options;
  const { algorithm = available() ? 'argon2id' : 'bcrypt', cost = 10, memoryCost = 65536, timeCost = 2 } = o;

  if(!ALGORITHMS.includes(algorithm)) throw typeError(`Expected algorithm to be a unknown algorithm, expected one of: "bcrypt", "argon2id", "argon2d", "argon2i" (default is "argon2id") for 'hash'.`);
  if(algorithm == 'bcrypt' && !(Number.isInteger(cost) && cost >= 4 && cost <= 31)) throw typeError('Rounds must be an integer between 4 and 31');
  if(!(memoryCost >= 8)) throw typeError('Memory cost must be at least 8');
  if(!(timeCost > 0)) throw typeError('Time cost must be greater than 0');

  return { algorithm, cost, memoryCost, timeCost };
}

const same = (a, b) => {
  let diff = a.length ^ b.length;

  for(let i = 0; i < a.length; i++) diff |= a[i] ^ (b[i] ?? 0);

  return diff === 0;
};

const enough = (name, got, want) => {
  if(got < want) throw typeError(`Not enough arguments to '${name}'. Expected ${want}, got ${got}.`);
};

export function hashSync(password, options) {
  enough('hashSync', arguments.length, 1);

  const { algorithm, cost, memoryCost, timeCost } = optionsOf(options);

  if(password === undefined || (typeof password == 'string' && !password) || (ArrayBuffer.isView(password) && !password.byteLength)) throw typeError('password must not be empty');

  if(algorithm == 'bcrypt') return bcryptHash(String(password), genSalt(cost).replace(/^\$2a\$/, '$2b$'));
  if(algorithm != 'argon2id') throw passwordError('PASSWORD_UNSUPPORTED_ALGORITHM', `${algorithm} is not supported here, only argon2id and bcrypt`);

  const salt = globalThis.crypto.getRandomValues(new Uint8Array(32));
  const raw = engine().argon2id(bytesOf(password), salt, { memoryCost, timeCost, parallelism: 1, hashLength: 32 });

  return `$argon2id$v=19$m=${memoryCost},t=${timeCost},p=1$${b64(salt)}$${b64(new Uint8Array(raw))}`;
}

/* a bad argument throws at once, as Bun's does */
export function hash(password, options) {
  enough('hash', arguments.length, 1);

  return Promise.resolve(hashSync(password, options));
}

export function verifySync(password, hash) {
  enough('verifySync', arguments.length, 2);

  const text = typeof hash == 'string' ? hash : new TextDecoder().decode(bytesOf(hash));

  if(!text || (typeof password == 'string' && !password)) return false;

  if(/^\$2[abxy]\$/.test(text)) return compare(String(password), text);

  const m = /^\$(argon2(?:id|i|d))\$v=(\d+)\$m=(\d+),t=(\d+),p=(\d+)\$([^$]*)\$([^$]*)$/.exec(text);

  if(!m) throw passwordError(/^\$argon2/.test(text) ? 'PASSWORD_INVALID_ENCODING' : 'PASSWORD_UNSUPPORTED_ALGORITHM', `Password verification failed with error "${/^\$argon2/.test(text) ? 'InvalidEncoding' : 'UnsupportedAlgorithm'}"`);
  if(m[1] != 'argon2id') throw passwordError('PASSWORD_UNSUPPORTED_ALGORITHM', `${m[1]} is not supported here, only argon2id and bcrypt`);
  if(m[2] != '19') throw passwordError('PASSWORD_INVALID_ENCODING', 'Password verification failed with error "InvalidEncoding"');

  const want = unb64(m[7]);
  const raw = engine().argon2id(bytesOf(password), unb64(m[6]), { memoryCost: +m[3], timeCost: +m[4], parallelism: +m[5], hashLength: want.length });

  return same(new Uint8Array(raw), want);
}

/* a missing argument throws at once, a bad hash rejects */
export function verify(password, hash) {
  enough('verify', arguments.length, 2);

  try {
    return Promise.resolve(verifySync(password, hash));
  } catch(e) {
    return Promise.reject(e);
  }
}

const password = { hash, hashSync, verify, verifySync };

export default password;
