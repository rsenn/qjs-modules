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
| `encodeInto(source, dest)` | 2 | method | Encodes `source` into the existing `dest` typed array; returns `{ read, written }`, `read` in UTF-16 units as WHATWG. |
| `encoding` | — | getter | `'utf-8'`, or `'utf-16le'`/`'utf-16be'`/`'utf-32le'`/`'utf-32be'` for the extension encodings (enumerable). |
| `endian` | — | getter | Byte order used for multi-byte encodings. |
| `buffered` | — | getter | Bytes pending across incremental encode calls. |

## Internal helpers

Module exports that `lib/` builds Node APIs on; not standard APIs themselves.

| Function | Args | Description |
| --- | --- | --- |
| `isAscii(view)` / `isUtf8(view)` | 1 | Whether the bytes are 7-bit ASCII / valid UTF-8 (no overlong form, no surrogate); `buffer.isAscii()`/`isUtf8()`. |
| `utf8Offsets(bytes, indices)` | 2 | Converts a `Uint32Array` of UTF-16 indices into the text `bytes` (UTF-8) encodes to byte offsets, in place, one pass when ascending. |
| `encode(string, encoding)` | 2 | `'utf-16le'`, `'latin1'` or `'ascii'` bytes of a string as an `ArrayBuffer`, lone surrogates kept (Node's `Buffer.from`). |
| `decode(view, encoding)` | 2 | `'latin1'` (byte = code point) or `'ascii'` (`byte & 0x7f`) bytes as a string (Node's `toString`). |
| `stringWidth(str)` | 1 | Terminal cells of a string: controls and combining marks 0, East Asian wide and emoji 2, the rest 1, escape sequences 0. |
| `stripAnsi(str)` | 1 | The string without CSI, OSC, DCS/SOS/PM/APC and two byte escape sequences. |
| `truncateLines(lines, width, ellipsis = '…')` | 3 | `truncate()` of each element of the array `lines`, in one call; a new array. Throws `TypeError` when `lines` is not an array. |
| `truncate(str, width, ellipsis = '…')` | 3 | The string cut to `width` cells with the ellipsis, escapes kept and a reset appended when it had any; the string itself when it fits. |
| `fitTable(width, make, flex = 0, drop = [])` | 4 | A table of the rows `make(bar)` returns (header first) cut to `width` cells: meters of `bar` = 6, 3, 0 cells, then the `flex` columns cut with an ellipsis as `truncate()` does, then the `drop` columns removed one by one (`make` runs once per `bar`, its rows are reused after a drop); columns two spaces apart, number columns on the right. Throws what `make` throws. |
| `wrap(str, width)` | 2 | Lines of at most `width` cells broken after spaces, newlines kept, over-long words split, a color closed at a break and opened again; an array of strings. |
| `prevCluster(str, pos)`, `nextCluster(str, pos)` | 2 | The UTF-16 index of the character boundary before / after `pos` (clamped to the string): `nextCluster('e\u0301x', 0)` is 2. A character is a surrogate pair, a letter with its combining marks, variation selector or emoji modifier, a ZWJ emoji sequence, a pair of regional indicators (a flag), a Hangul syllable or CR LF: a compact UAX 29. A `pos` inside a character gives its start or end. |

The last seven are the native half of `lib/terminal.js`, which exports them under the same names and keeps pure JS versions for runtimes without `textcode`. A cell wider than the width (a wide character at width 1) goes onto a line of its own in `wrap`.

## Export list

Generated by `utilities/doc-api.js` from the `textcode` dump of `utilities/api-surface.sh`: 30 members. Edit the text above this block, not the block.

**Functions:** `decode()`, `encode()`, `fitTable()`, `isAscii()`, `isUtf8()`, `nextCluster()`, `prevCluster()`, `stringWidth()`, `stripAnsi()`, `truncate()`, `truncateLines()`, `utf8Offsets()`, `wrap()`

### `TextDecoder`

class, constructible

- **methods:** `decode()`
- **properties:** `buffered (get)`, `encoding (get)`, `endian (get)`, `fatal (get)`, `ignoreBOM (get)`, `[Symbol.toStringTag]`

### `TextEncoder`

class, constructible

- **methods:** `encode()`, `encodeInto()`
- **properties:** `buffered (get)`, `encoding (get)`, `endian (get)`, `[Symbol.toStringTag]`

