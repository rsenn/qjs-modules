# blob

Source: `quickjs-blob.c` — module export: **`Blob`**

A WHATWG-style `Blob`: an immutable chunk of binary data with a MIME type.

## Constructor

```js
new Blob(parts[, options])   // length 1
```

`parts` is an array of strings / ArrayBuffers / typed arrays / other Blobs that are concatenated; `options` may carry a `type` (MIME) string.

## Methods

| Method | Args | Description |
| --- | --- | --- |
| `arrayBuffer()` | 0 | Returns a promise resolving to the blob's contents as an `ArrayBuffer`. |
| `bytes()` | 0 | Returns the blob's contents as a `Uint8Array`. |
| `text()` | 0 | Returns a promise resolving to the contents decoded as a UTF-8 string. |
| `stream()` | 0 | Returns a `ReadableStream` over the blob's bytes. |
| `slice(start, end, contentType)` | 0 | Returns a new `Blob` covering the byte range `[start, end)`, optionally with a new MIME type. |

## Properties (read-only)

| Property | Description |
| --- | --- |
| `size` | Length of the blob in bytes (enumerable). |
| `type` | MIME type string. |
| `[Symbol.toStringTag]` | `"Blob"`. |
