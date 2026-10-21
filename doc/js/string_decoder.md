# string_decoder

Source: `lib/string_decoder.js` — import as `string_decoder` (also `node:string_decoder`, and `require()`); qjsm only

Node's [`string_decoder`](https://nodejs.org/api/string_decoder.html): decodes a byte stream chunk by chunk
without splitting a character between two chunks. Built on `textcode`'s `TextDecoder` and `decode()`.

```js
import { StringDecoder } from 'string_decoder';

const d = new StringDecoder('utf8');

d.write(Uint8Array.of(0xe2, 0x82)); // ''  (the bytes wait)
d.end(Uint8Array.of(0xac));         // '€'
```

| Member | Meaning |
| --- | --- |
| `new StringDecoder(encoding = 'utf8')` | `utf8`, `utf16le`, `latin1`, `base64`, `base64url`, `hex`, `ascii`, or an alias (`utf-8`, `ucs2`, `ucs-2`, `utf-16le`, `binary`), any case; else `TypeError` `ERR_UNKNOWN_ENCODING` |
| `encoding` | the normalized name, `'utf16le'` for `'UCS-2'` |
| `write(buf)` | the text of the held bytes plus `buf`, up to the last whole character; a string `buf` is returned as is |
| `end(buf?)` | `write(buf)` plus the held bytes, then the decoder starts over |

`buf` is a TypedArray, DataView or Buffer; anything else throws `TypeError` `ERR_INVALID_ARG_TYPE`.

What waits for the next chunk, and what `end()` makes of it:

| Encoding | Held back | `end()` |
| --- | --- | --- |
| `utf8` | an incomplete character | U+FFFD |
| `utf16le` | an odd byte, a high surrogate ending an even chunk | the lone surrogate, an odd byte is dropped |
| `base64`, `base64url` | the bytes after the last whole 3-byte group | encoded, `=` padded for `base64` |
| `hex`, `latin1`, `ascii` | nothing | — |

A byte order mark is kept, as in Node. The default export is `{ StringDecoder }`.

Not covered: Node's undocumented `lastChar`, `lastNeed`, `lastTotal` and `text()`.
