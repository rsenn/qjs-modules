# wasm

`import * as wasm from 'wasm'` — a subset of the WebAssembly JS API over a
pluggable engine (`include/wasm-backend.h`). The shipped backend is wasm3
(`src/wasm3-backend.c`, vendored in `third_party/wasm3`); WAMR
(`src/wamr-backend.c`, `third_party/wamr`) is the second implementation.

## Exports

| Name | Meaning |
| --- | --- |
| `validate(bytes)` | `true` when `bytes` is a loadable module |
| `Module(bytes)` | compiles; throws `CompileError` |
| `Module.imports(m)` / `Module.exports(m)` | `[{module?, name, kind}]` |
| `Instance(module, imports)` | `.exports` holds functions and `Memory` objects |
| `Memory` (exported only) | `.buffer`, `.grow(delta)` (returns the old page count) |
| `Table` (exported only) | `.length`, `.get(index)` returns a callable function or `null` |
| `CompileError`, `LinkError` | `Error` subclasses; wasm traps surface as `RuntimeError` |
| `backend` | name of the linked engine, e.g. `"wasm3"` |

`i64` maps to `BigInt`. Memories are shared between instances that import
the same `Memory`; the old `ArrayBuffer` is detached when the memory grows.

## Not yet supported

- `new Memory()`, `Table`, `Global` constructors; `Table.set`/`grow`; global exports; table and global imports
- `instantiate()`/`compile()` promises, `.wasm` imports
- funcref/externref values, multi-value host returns

```js
const { add } = new wasm.Instance(new wasm.Module(bytes)).exports;
add(2, 3); // 5
```
