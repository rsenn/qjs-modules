import { genSalt, hash as bcryptHash, compare } from 'bcrypt';

/**
 * `Bun.password`-compatible password hashing (https://bun.com/docs/api/hashing).
 * Bun supports `"bcrypt"` and three Argon2 variants (`"argon2id"` - its default -
 * `"argon2i"`, `"argon2d"`); this engine's `bcrypt` native module only implements
 * bcrypt (libbcrypt), so `algorithm` here only ever accepts `"bcrypt"` - anything
 * else throws rather than silently hashing with the wrong (unavailable) algorithm.
 * `verify()`/`verifySync()` don't need an `algorithm` option for the same reason
 * Bun's don't: the hash string is self-describing (bcrypt's `$2b$<cost>$<salt+hash>`
 * Modular Crypt Format).
 */

const DEFAULT_COST = 12;

function checkOptions(options) {
  const { algorithm = 'bcrypt', cost = DEFAULT_COST } = options;

  if(algorithm !== 'bcrypt') throw new Error(`Bun.password compat: algorithm '${algorithm}' isn't supported here - this engine only has a bcrypt binding, no argon2`);
  if(!(cost >= 4 && cost <= 31)) throw new RangeError('cost must be between 4 and 31');

  return cost;
}

export function hashSync(password, options = {}) {
  const cost = checkOptions(options);
  return bcryptHash(password, genSalt(cost));
}

export async function hash(password, options = {}) {
  return hashSync(password, options);
}

export function verifySync(password, hash) {
  return compare(password, hash);
}

export async function verify(password, hash) {
  return verifySync(password, hash);
}

const password = { hash, hashSync, verify, verifySync };

export default password;
