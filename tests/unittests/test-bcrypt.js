import * as bcrypt from 'bcrypt';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* keep the cost factor low - bcrypt is deliberately slow, and default (12)
 * rounds would make this test noticeably slow for no benefit. */
const ROUNDS = 4;

tests({
  'HASHSIZE and SALTSIZE constants'() {
    eq(bcrypt.HASHSIZE, 64);
    eq(bcrypt.SALTSIZE, 29);
  },
  'genSalt() returns a salt string of SALTSIZE length'() {
    const salt = bcrypt.genSalt(ROUNDS);
    eq(typeof salt, 'string');
    eq(salt.length, bcrypt.SALTSIZE);
    assert(salt.startsWith('$2a$04$'));
  },
  'genSalt() produces a different salt each call'() {
    const a = bcrypt.genSalt(ROUNDS);
    const b = bcrypt.genSalt(ROUNDS);
    assert(a !== b);
  },
  'hash() with an explicit salt is deterministic'() {
    const salt = bcrypt.genSalt(ROUNDS);
    const a = bcrypt.hash('correct horse', salt);
    const b = bcrypt.hash('correct horse', salt);
    eq(a, b);
    eq(typeof a, 'string');
    assert(a.startsWith(salt));
  },
  'hash() without a salt generates one internally'() {
    const h = bcrypt.hash('correct horse');
    eq(typeof h, 'string');
    assert(h.startsWith('$2a$'));
  },
  'compare() returns true for a matching password'() {
    const salt = bcrypt.genSalt(ROUNDS);
    const h = bcrypt.hash('correct horse', salt);
    eq(bcrypt.compare('correct horse', h), true);
  },
  'compare() returns false for a non-matching password'() {
    const salt = bcrypt.genSalt(ROUNDS);
    const h = bcrypt.hash('correct horse', salt);
    eq(bcrypt.compare('wrong password', h), false);
  },
  'different salts produce different hashes for the same password'() {
    const h1 = bcrypt.hash('correct horse', bcrypt.genSalt(ROUNDS));
    const h2 = bcrypt.hash('correct horse', bcrypt.genSalt(ROUNDS));
    assert(h1 !== h2);
    eq(bcrypt.compare('correct horse', h1), true);
    eq(bcrypt.compare('correct horse', h2), true);
  },
});
