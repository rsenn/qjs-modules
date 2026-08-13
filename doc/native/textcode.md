# textcode

Source: `quickjs-textcode.c` — module exports **`TextDecoder`** and **`TextEncoder`**

WHATWG-style text encoding/decoding, extended with endianness control and
incremental (buffered) operation.

## TextDecoder

```js
new TextDecoder([encoding])   // length 1
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `decode(input)` | 1 | method | Decodes bytes (ArrayBuffer/typed array) into a string. |
| `encoding` | — | getter | The label of the active encoding (enumerable). |
| `endian` | — | getter | Byte order used for multi-byte encodings. |
| `buffered` | — | getter | Bytes held back awaiting more input (incremental decode). |

## TextEncoder

```js
new TextEncoder([encoding])   // length 1
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `encode(input)` | 1 | method | Encodes a string into a `Uint8Array`. |
| `encodeInto(source, dest)` | 2 | method | Encodes `source` into the existing `dest` typed array; returns read/written counts. |
| `encoding` | — | getter | The label of the active encoding (enumerable). |
| `endian` | — | getter | Byte order used for multi-byte encodings. |
| `buffered` | — | getter | Bytes pending across incremental encode calls. |
