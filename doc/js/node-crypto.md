# node-crypto

Source: `lib/node/crypto.js`, imported as `node:crypto` (or `crypto` from a file below `node_modules`)

The hashing part of Node's [crypto](https://nodejs.org/api/crypto.html), on top of the native [digest](../native/digest.md) module. Names, argument shapes, error codes and messages follow Node (checked against node 23).

```js
import { createHash } from 'node:crypto';

createHash('sha256').update('abc').digest('hex'); // 'ba7816bf...'
```

| `node:crypto` | Description |
| --- | --- |
| `createHash(algorithm)`, `Hash` | `update(data, inputEncoding)` chains, `digest(outputEncoding)` gives a `Buffer` or a string, `copy()` forks the state. The chunks are kept and hashed at `digest()`. |
| `hash(algorithm, data, outputEncoding = 'hex')` | `createHash` in one call (Node 21.7). |
| `getHashes()` | The names of [digest](../native/digest.md): md5, sha1, sha224, sha256, sha384, sha512, sha512-224, sha512-256. |

Algorithm names are case-insensitive, `sha-256` and `rsa-sha256` also work. After `digest()` every call on the hash throws `ERR_CRYPTO_HASH_FINALIZED`.

## Differences from Node

- Fewer algorithms: no ripemd160, sha3, blake2, shake.
- `Hash` is no `Transform` stream (this project does not implement Node streams): no `write()`, `pipe()`, `end()`, and the `options` argument is ignored.
- Not implemented: `createHmac`, `randomBytes`, `randomUUID`, `createCipheriv` and the rest of `node:crypto`. `crypto.getRandomValues()` and `crypto.randomUUID()` stay on the `crypto` global.
- The input is kept in memory until `digest()`.
