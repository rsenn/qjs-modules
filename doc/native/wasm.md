# wasm

`import * as wasm from 'wasm'` — a subset of the WebAssembly JS API over a
pluggable engine (`include/wasm-backend.h`). The shipped backend is wasm3
(`src/wasm3-backend.c`, vendored in `third_party/wasm3`); WAMR
(`src/wamr-backend.c`, `third_party/wamr`) is the second implementation.

## Backend

`cmake -DWASM_BACKEND=wasm3` (default) or `-DWASM_BACKEND=wamr`; exactly one is
linked, so `wasm.backend` names the engine.

| Feature | wasm3 | wamr |
| --- | --- | --- |
| `new Memory()`, shared by several instances | yes | no (`TypeError`) |
| `new Table()`, `Table.set`/`grow`, table imports | yes | no; exported `Table` is read-only (`length`, `get`) |
| `new Global()`, global imports and exports | yes | no (global exports are omitted) |
| function imports and exports, `Memory` exports | yes | yes |

## Exports

| Name | Meaning |
| --- | --- |
| `validate(bytes)` | `true` when `bytes` is a loadable module |
| `Module(bytes)` | compiles; throws `CompileError` |
| `Module.imports(m)` / `Module.exports(m)` | `[{module?, name, kind}]` |
| `Module.customSections(m, name)` | payloads of the custom sections called `name`, as `ArrayBuffer` copies |
| `Instance(module, imports)` | `.exports` is a frozen, null-prototype object of functions, `Memory`, `Table`, `Global` |
| `Memory({initial, maximum})` | `.buffer`, `.grow(delta)` (returns the old page count) |
| `Table({element, initial, maximum}, value?)` | `.length`, `.get(i)`, `.set(i, fn)`, `.grow(delta, fn?)`; holds `anyfunc` |
| `Global({value, mutable}, v?)` | `.value`, `.valueOf()`; `value` is `i32`, `i64` (BigInt), `f32` or `f64` |
| `CompileError`, `LinkError` | `Error` subclasses; wasm traps surface as `RuntimeError` |
| `backend` | name of the linked engine, e.g. `"wasm3"` |

Exported functions are named by their function index (`add.name === '0'`)
and `Table.set` accepts only exported wasm functions or `null`, as in the
spec. A `Global` import may also be a plain number (or BigInt for `i64`)
when it is immutable. `i64` maps to `BigInt`. Memories are shared between
instances that import the same `Memory`; the old `ArrayBuffer` is detached
when the memory grows.

## WASI

`WASI` (Node's `node:wasi`) is `lib/wasi.js`, see `doc/js/wasi.md`.

## Not yet supported

- `instantiate()`/`compile()` promises, the `WebAssembly` global, `.wasm` imports
- `externref` tables and globals, shared memories, `Tag`/`Exception`
- one function object per wasm function: `Table.get(i)` and an export of the same function are different objects
- multi-value host returns

```js
const { add } = new wasm.Instance(new wasm.Module(bytes)).exports;
add(2, 3); // 5
```
