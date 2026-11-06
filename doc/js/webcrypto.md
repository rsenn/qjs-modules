# webcrypto

Source: `lib/webcrypto.js`, reached as `crypto.subtle`, `CryptoKey` and `SubtleCrypto` after `import 'globals'` (loaded on first use)

`crypto.subtle` of the Web Crypto API. The engines (digest, HMAC, ECDSA) come from the `crypto` export of the `lws` module of [qjs-lws](https://github.com/rsenn/qjs-lws), which is loaded with `import('lws')` on the first call, so every method is asynchronous and needs qjs-lws built with OpenSSL 3. Names, shapes and error messages follow Bun (checked against bun 1.4.2).

```js
const pair = await crypto.subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-256' }, true, ['sign', 'verify']);
const sig = await crypto.subtle.sign({ name: 'ECDSA', hash: 'SHA-256' }, pair.privateKey, bytes); // 64 bytes, r||s
await crypto.subtle.verify({ name: 'ECDSA', hash: 'SHA-256' }, pair.publicKey, sig, bytes); // true
```

| Method | Supported |
| --- | --- |
| `digest(alg, data)` | `SHA-1`, `SHA-256`, `SHA-384`, `SHA-512` |
| `generateKey()`, `importKey()`, `exportKey()` | ECDSA `P-256`, `P-384`, `P-521` and HMAC; formats `raw` and `jwk` |
| `sign()`, `verify()` | the same; ECDSA signatures are IEEE P1363 (`r \|\| s`), HMAC verify compares in constant time |
| `encrypt()`, `decrypt()`, `deriveBits()`, `deriveKey()`, `wrapKey()`, `unwrapKey()` | reject with `NotSupportedError` |

## Differences from Bun

- no `pkcs8` or `spki` formats, no RSA, ECDH, AES, PBKDF2, HKDF
- `DOMException.code` is not set
- the nominal key objects are `CryptoKey` instances whose key material is hidden, as in the browsers
