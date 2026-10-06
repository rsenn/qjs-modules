# webassembly

The `WebAssembly` global (W3C JS API) over the native [`wasm`](../native/wasm.md) module.
Opt-in: importing the module installs the global when none exists.

```js
import 'webassembly';

const { instance } = await WebAssembly.instantiate(bytes, { env: { f: x => x * 2 } });
instance.exports.g(21); // 42
```

## Exports

| name | meaning |
| --- | --- |
| `WebAssembly` (default and named) | the namespace object, also `globalThis.WebAssembly` |

## Namespace

| member | enumerable | notes |
| --- | --- | --- |
| `compile(bytes)` | yes | resolves a `Module` |
| `instantiate(bytes, imports?)` | yes | resolves `{ module, instance }` |
| `instantiate(module, imports?)` | yes | resolves an `Instance` |
| `validate(bytes)` | yes | synchronous; throws `TypeError` for a non-BufferSource |
| `compileStreaming(source)` | yes | `source` is a `Response` or a promise of one |
| `instantiateStreaming(source, imports?)` | yes | as `instantiate(bytes, ...)` |
| `Module`, `Instance`, `Memory`, `Table`, `Global` | no | from `wasm` |
| `CompileError`, `LinkError`, `RuntimeError` | no | from `wasm` |
| `Symbol.toStringTag` | no | `"WebAssembly"` |

The async members never throw synchronously; they reject:

| cause | rejection |
| --- | --- |
| bad argument | `TypeError` |
| invalid bytes | `WebAssembly.CompileError` |
| unresolved or mistyped import | `WebAssembly.LinkError` |

The streaming members take any object with `arrayBuffer()` and `headers.get()`; they reject
with `TypeError` unless the content type is `application/wasm` and the status is ok.

## Not supported

`Tag`, `Exception`, `JSTag`, shared memories and `externref`; see
[wasm](../native/wasm.md#not-yet-supported).
