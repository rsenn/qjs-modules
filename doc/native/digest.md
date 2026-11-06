# digest

Source: `quickjs-digest.c` — module exports **`digest`**, **`algorithms`**

One-shot message digests: md5, sha1 and the SHA-2 family. The engine under [node-crypto](../js/node-crypto.md). It wraps [th-blitz/Hash-Algorithms](https://github.com/th-blitz/Hash-Algorithms) (MIT, a git submodule in `third_party/Hash-Algorithms`, pinned to 953a0b6), which hashes a whole message at once, so chunking is the caller's job.

```js
import { digest, algorithms } from 'digest';

digest('sha256', new TextEncoder().encode('abc')); // ArrayBuffer, 32 bytes
```

| Export | Description |
| --- | --- |
| `digest(algorithm, data)` | `data` is an `ArrayBuffer` or any view, the result an `ArrayBuffer` of the digest size. `algorithm` is a lowercase name from `algorithms`. Throws `TypeError` for other data, `RangeError` for an unknown algorithm. |
| `algorithms` | Array of `{ name, size, blockSize }`: md5 (16, 64), sha1 (20, 64), sha224 (28, 64), sha256 (32, 64), sha384 (48, 128), sha512 (64, 128), sha512-224 (28, 128), sha512-256 (32, 128). |

## Notes

- The library reads the message as 32 or 64 bit words. `digest()` copies a message that is not 8-byte aligned (a view at an odd offset) before hashing.
- Digest words are written big-endian explicitly, but the library itself reads message words as little-endian: no big-endian hosts.
- Built when `MODULE_DIGEST=ON` (the default) and the submodule is checked out; its sources are compiled into `libmodules.a`.
