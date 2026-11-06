/* Bun.password of lib/password.js: argon2id (lws crypto engine, skipped without it) and bcrypt; expected values come from bun 1.4.2.
 * runs under bun as well: bun tests/unittests/test-bun-password.js */
import { assert, eq, tests } from '../../lib/tinytest.js';

if(!globalThis.Bun) await import('../../lib/globals.js');

const Bun = globalThis.Bun ?? (await import('../../lib/bun.js')).default;
const native =
  !!globalThis.process?.versions?.bun ||
  (() => {
    try {
      Bun.password.hashSync('pw', 'argon2id');

      return true;
    } catch(e) {
      return false;
    }
  })();
const P = Bun.password;

/* made by bun */
const ARGON = '$argon2id$v=19$m=1024,t=1,p=1$3ZFPuCZY8o1Os16sgIZfDMpJDSmbRxcYzOEBwVz3z1o$T1u4GuIRFzdKfxHirM1Qwxaj7gRBEkbGxtDn1MjOLSc';
const ARGON_UTF8 = '$argon2id$v=19$m=512,t=2,p=1$1KyNekXwqffhc9GZ0qvOuXA1W2cJ7dKqkBr/UONrliI$+lRYEsDGRDmFvxStApqunZ/0CSLLy8POOl1d5Z3cTog';
const BCRYPT = '$2b$04$3VLj8VcrblUpxE0Y7JUci.KYormMpfJ7aIG8O5S.CCTIpfi72gZP6';

/* the error of a call, thrown at once or as a rejection */
const fails = async call => {
  try {
    await call();
  } catch(e) {
    return `${e.name} ${e.code} ${e.message}`;
  }

  return null;
};

tests({
  async 'bcrypt: $2b$ hash, verify of a hash made by bun'() {
    const h = await P.hash('hunter2', { algorithm: 'bcrypt', cost: 4 });

    eq('$2b$04$', h.slice(0, 7));
    eq(60, h.length);
    eq(true, await P.verify('hunter2', h));
    eq(true, await P.verify('hunter2', BCRYPT));
    eq(false, await P.verify('nope', BCRYPT));
    eq('$2b$10$', (await P.hash('pw', 'bcrypt')).slice(0, 7));
  },

  async 'bad arguments throw as Bun does'() {
    eq('TypeError ERR_INVALID_ARG_TYPE Rounds must be an integer between 4 and 31', await fails(() => P.hash('pw', { algorithm: 'bcrypt', cost: 3 })));
    eq('TypeError ERR_INVALID_ARG_TYPE password must not be empty', await fails(() => P.hash('')));
    eq('TypeError ERR_INVALID_ARG_TYPE Memory cost must be at least 8', await fails(() => P.hash('pw', { algorithm: 'argon2id', memoryCost: 0 })));
    eq('TypeError ERR_INVALID_ARG_TYPE Time cost must be greater than 0', await fails(() => P.hash('pw', { algorithm: 'argon2id', timeCost: 0 })));
    assert((await fails(() => P.hash('pw', 'md5')))?.startsWith('TypeError ERR_INVALID_ARG_TYPE Expected algorithm'), 'unknown algorithm');
  },

  async 'verify: empty password or hash is false'() {
    eq(false, await P.verify('', BCRYPT));
    eq(false, await P.verify('pw', ''));
  },

  ...(native
    ? {
        async 'argon2id is the default, the string has Bun\'s shape'() {
          for(const h of [await P.hash('pw'), await P.hash('pw', 'argon2id')]) {
            const [, alg, version, params, salt, hash] = h.split('$');

            eq('argon2id v=19 m=65536,t=2,p=1 43 43', `${alg} ${version} ${params} ${salt.length} ${hash.length}`);
          }
        },

        async 'options set the cost and the string says so'() {
          const h = await P.hash('pw', { algorithm: 'argon2id', memoryCost: 4096, timeCost: 3 });

          eq('m=4096,t=3,p=1', h.split('$')[3]);
          eq(true, await P.verify('pw', h));
        },

        async 'verify of hashes made by bun, also with non-ASCII and as bytes'() {
          eq(true, await P.verify('hunter2', ARGON));
          eq(false, await P.verify('hunter3', ARGON));
          eq(true, await P.verify('pässwörd', ARGON_UTF8));
          eq(true, await P.verify('hunter2', new TextEncoder().encode(ARGON)));
          eq(true, await P.verify(new TextEncoder().encode('hunter2'), ARGON));
        },

        'sync variants'() {
          const h = P.hashSync('pw', { algorithm: 'argon2id', memoryCost: 1024, timeCost: 1 });

          eq(true, P.verifySync('pw', h));
          eq(false, P.verifySync('pw2', h));
          eq('$argon2id$v=19$m=1024,t=1,p=1', h.split('$').slice(0, 4).join('$'));
        },

        async 'two hashes of one password differ (random salt)'() {
          const o = { algorithm: 'argon2id', memoryCost: 1024, timeCost: 1 };

          assert((await P.hash('pw', o)) !== (await P.hash('pw', o)), 'salt');
        },

        async 'a hash of no known algorithm throws'() {
          eq('Error PASSWORD_UNSUPPORTED_ALGORITHM Password verification failed with error "UnsupportedAlgorithm"', await fails(() => P.verify('pw', 'garbage')));
        },
      }
    : {
        'skipped: the lws module has no argon2id'() {},
      }),
});
