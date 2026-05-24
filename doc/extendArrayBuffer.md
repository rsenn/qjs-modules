# extendArrayBuffer

Source: `lib/extendArrayBuffer.js` (pure JS) — default export: `extendArrayBuffer`

Installs extra (non-enumerable) methods onto `ArrayBuffer.prototype`.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `extendArrayBuffer(proto=ArrayBuffer.prototype, ctor)` | 0–2 | function | Defines the extension methods. **(default export)** |
| `ArrayBufferExtensions` | — | const | The bag of methods that get installed. |

## Added `ArrayBuffer.prototype` methods

| Method | Description |
| --- | --- |
| `search(needle)` | Finds a byte pattern within the buffer. |
| `view(...)` | Returns a typed-array/DataView view over the buffer. |
