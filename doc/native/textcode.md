# textcode

Source: `quickjs-textcode.c` — module exports **`TextDecoder`** and **`TextEncoder`**

WHATWG-style text encoding/decoding, extended with endianness control and
incremental (buffered) operation.

## TextDecoder

WHATWG [TextDecoder](https://encoding.spec.whatwg.org/#interface-textdecoder), the same as the global one in
browsers, node and bun, plus utf-32 and two extra getters.

```js
new TextDecoder(label = 'utf-8', { fatal = false, ignoreBOM = false })   // length 1
dec.decode(input, { stream = false })
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `decode(input, options)` | 1 | method | Decodes an ArrayBuffer, SharedArrayBuffer, typed array or DataView (none for `undefined`). Each maximal invalid subpart becomes U+FFFD. With `stream: true` an incomplete sequence at the end is held for the next call. |
| `encoding` | — | getter | The WHATWG name: `'utf-8'`, `'utf-16le'`, `'windows-1252'`, ... |
| `fatal` | — | getter | Whether invalid input throws instead of giving U+FFFD. |
| `ignoreBOM` | — | getter | Whether a leading byte order mark is kept. |
| `endian` | — | getter | Extension: `true` for a big-endian encoding. |
| `buffered` | — | getter | Extension: bytes held back by `stream: true`. |

Encodings and labels follow the [WHATWG list](https://encoding.spec.whatwg.org/#names-and-labels), labels match
case-insensitively with surrounding whitespace ignored:

| Encoding | Example labels |
| --- | --- |
| `utf-8` | `utf8`, `unicode-1-1-utf-8` |
| `utf-16le`, `utf-16be` | `utf-16`, `ucs-2`, `unicode` / `utf-16be` |
| `windows-1252` | `latin1`, `iso-8859-1`, `ascii`, `us-ascii` |
| `windows-1250` ... `windows-1258`, `windows-874` | `cp1250`, `iso-8859-9` (windows-1254), `tis-620` (windows-874) |
| `iso-8859-2` ... `iso-8859-16`, `iso-8859-8-i` | `latin2`, `cyrillic`, `greek`, `hebrew`, `l9` |
| `utf-32le`, `utf-32be` | extension: `utf-32`, `utf-32le`, `utf-32be` |

Not supported: `ibm866`, `koi8-r`, `koi8-u`, `macintosh`, `x-mac-cyrillic`, the CJK encodings, `replacement` and
`x-user-defined`.

Errors carry Node's `code`:

| Error | code | When |
| --- | --- | --- |
| RangeError | `ERR_ENCODING_NOT_SUPPORTED` | unknown label |
| TypeError | `ERR_ENCODING_INVALID_ENCODED_DATA` | invalid input with `fatal: true` |
| TypeError | `ERR_INVALID_ARG_TYPE` | input is not bytes, options is not an object |

## TextEncoder

```js
new TextEncoder([encoding])   // length 1
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `encode(input)` | 1 | method | Encodes a string into a `Uint8Array`. |
| `encodeInto(source, dest)` | 2 | method | Encodes `source` into the existing `dest` typed array; returns read/written counts. |
| `encoding` | — | getter | `'utf-8'`, or `'utf-16le'`/`'utf-16be'`/`'utf-32le'`/`'utf-32be'` for the extension encodings (enumerable). |
| `endian` | — | getter | Byte order used for multi-byte encodings. |
| `buffered` | — | getter | Bytes pending across incremental encode calls. |
