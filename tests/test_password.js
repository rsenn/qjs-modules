import password, { hash, hashSync, verify, verifySync } from 'password';
import { assert, eq, tests } from './tinytest.js';

await tests({
  async 'hashSync() then verifySync() round-trips'() {
    const h = hashSync('hunter2', { cost: 4 });
    assert(verifySync('hunter2', h), 'expected the same password to verify');
    assert(!verifySync('wrong', h), 'expected a different password to fail verification');
  },

  async 'hash()/verify() (async) round-trip'() {
    const h = await hash('hunter2', { cost: 4 });
    assert(await verify('hunter2', h), 'expected the same password to verify');
    assert(!(await verify('wrong', h)), 'expected a different password to fail verification');
  },

  'default export bundles the same four functions'() {
    eq(password.hash, hash);
    eq(password.hashSync, hashSync);
    eq(password.verify, verify);
    eq(password.verifySync, verifySync);
  },

  'defaults to bcrypt when no options given'() {
    const h = hashSync('hunter2');
    assert(h.startsWith('$2a$') || h.startsWith('$2b$'), `expected a bcrypt hash, got: ${h}`);
  },

  'rejects algorithms other than bcrypt'() {
    let threw = false;
    try {
      hashSync('x', { algorithm: 'argon2id' });
    } catch(e) {
      threw = true;
    }
    assert(threw, 'expected hashSync() to throw for algorithm: "argon2id"');
  },

  'rejects an out-of-range cost'() {
    let threw = false;
    try {
      hashSync('x', { cost: 2 });
    } catch(e) {
      threw = true;
    }
    assert(threw, 'expected hashSync() to throw for cost: 2 (below the 4-31 range)');
  },
});
