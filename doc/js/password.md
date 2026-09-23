# password

Source: `lib/password.js` (pure JS) — default export: `{ hash, hashSync, verify, verifySync }`

`Bun.password`-compatible password hashing/verification
(https://bun.com/docs/api/hashing, https://bun.com/guides/util/hash-a-password),
backed by the native `bcrypt` module (libbcrypt).

Bun supports both bcrypt and Argon2 (its default). This engine only has a bcrypt
binding - no Argon2 - so `options.algorithm` here only ever accepts `"bcrypt"`
(the default); anything else throws instead of silently hashing with an
unavailable algorithm. `verify()`/`verifySync()` don't take an `algorithm` option,
same as Bun's: bcrypt's `$2b$<cost>$...` hash format is self-describing.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `hash(password, options?)` | 1-2 | async function | Hashes `password`. `options`: `{ algorithm: "bcrypt", cost }` (`cost` 4-31, default 12). |
| `hashSync(password, options?)` | 1-2 | function | Synchronous `hash()`. |
| `verify(password, hash)` | 2 | async function | Whether `password` matches `hash`. |
| `verifySync(password, hash)` | 2 | function | Synchronous `verify()`. |
| *(default)* | — | object | `{ hash, hashSync, verify, verifySync }` |

## Example

```js
import { hash, verify } from 'password';

const h = await hash('super-secure-pa$$word');
await verify('super-secure-pa$$word', h); // true
```
