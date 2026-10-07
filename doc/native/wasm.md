# wasm

Source: `quickjs-wasm.c` (the JS API), `include/wasm-backend.h` (engine interface),
`src/wasm3-backend.c`, `src/wamr-backend.c`, `src/wasm-sections.c` (shared section parser)

`import * as wasm from 'wasm'` is the WebAssembly JS API (W3C JS API, the same classes as the
`WebAssembly` global of browsers, Node, Bun and Deno) over a pluggable C engine. The
`WebAssembly` global itself, the `.wasm` ES-module loader and WASI are JavaScript layers on top
of this module:

| Layer | File | Doc |
| --- | --- | --- |
| native API | `quickjs-wasm.c` | this page |
| `WebAssembly` global, `instantiate()`, `*Streaming()` | `lib/webassembly.js` | [webassembly](../js/webassembly.md) |
| `import './x.wasm'` | `lib/wasm-loader.js` | [wasm-loader](../js/wasm-loader.md) |
| WASI preview1 (`node:wasi`) | `lib/wasi.js` | [wasi](../js/wasi.md) |

```js
import * as wasm from 'wasm';

const { add } = new wasm.Instance(new wasm.Module(bytes)).exports;
add(2, 3); // 5
```

## Engines

Exactly one engine is linked per build, chosen at configure time; `wasm.backend` names it.

```sh
cmake -B build/wasm3 -S .                          # WASM_BACKEND=wasm3 (default)
cmake -B build/wamr  -S . -DWASM_BACKEND=wamr      # WebAssembly Micro Runtime
```

Any other value is a configure error. The engines are vendored (`third_party/wasm3`,
`third_party/wamr`); changes to either belong in isolated, tolerant patches in `patches/`.
wasm3 builds as a static library through an ExternalProject, once per linkage (shared module,
static `qjsm`); a changed wasm3 source needs its ExternalProject `-build` stamp removed.
WAMR is built as an interpreter only (no AOT, no JIT, no WASI libc; bulk memory and reference
types on).

| Feature | wasm3 | wamr |
| --- | --- | --- |
| function imports and exports, `Memory` exports | yes | yes |
| `new Memory()`, shared by several instances | yes | no (`TypeError`) |
| memory imports | yes | no |
| `new Table()`, `Table.set`/`grow`, table imports | yes | no; an exported `Table` is read-only (`length`, `get`) |
| `new Global()`, global imports and exports | yes | no; global exports are omitted from `.exports` |
| bulk memory (`memory.copy`, `memory.fill`) | yes | yes |
| multi-memory | yes (vendored fork) | no |
| legacy exception handling (`try`/`catch`, `Tag`) | no | no |

Programs built with Emscripten or clang's `-fwasm-exceptions` use the legacy exception-handling
opcodes; both engines reject them (wasm3 with `unknown memory`, WAMR silently exits). Build such
programs without `-fwasm-exceptions` (Emscripten's JS-based setjmp/longjmp emulation works, see
`examples/wasm-shish.js`), or run them where the host engine supports exceptions.

## Architecture

`wasm-backend.h` is one C interface, shaped like `quickjs.h`, implemented by both engines:

| quickjs.h | wasm-backend.h |
| --- | --- |
| `JSRuntime`, `JSContext` | `WBRuntime`, `WBContext` (one runtime per thread) |
| `JSValue`, `JS_DupValue`, `JS_FreeValue` | `WBValue`/`WBExtern`, `WB_Dup*`, `WB_Free*` |
| `JS_EXCEPTION`, `JS_GetException` | `-1` or `NULL`, `WB_GetException` |

A failing call returns `-1` or `NULL` with the reason left in the context; nothing aborts or
longjmps. `WB_GetException` yields a kind that `quickjs-wasm.c` maps to a JS error:

| Kind | JS error |
| --- | --- |
| `WB_ERR_COMPILE` | `CompileError` |
| `WB_ERR_LINK` | `LinkError` |
| `WB_ERR_TRAP` | `RuntimeError` |
| `WB_ERR_HOST` | none; the JS exception thrown by the host function propagates |
| `WB_ERR_RANGE`, `WB_ERR_TYPE` | `RangeError`, `TypeError` |
| `WB_ERR_UNSUPPORTED` | the caller picks the error (`TypeError` for the missing features above) |

`src/wasm-sections.c` parses a module's import, export and custom sections itself, so
`Module.imports()`, `Module.exports()` and `Module.customSections()` behave the same on both
engines. How each engine links:

- **wasm3** shares `Memory`/`Table`/`Global` objects between instances through synthesized
  modules (`wbm<N>`) and a rewritten import section per instance; functions are linked after
  `m3_LoadModule` and before `m3_RunStart`.
- **WAMR** registers host functions per (module, name, signature) as raw natives, one 64-bit
  slot per parameter; the trampoline finds the host function through the instance's custom data.

## Exports

| Name | Meaning |
| --- | --- |
| `validate(bytes)` | `true` when `bytes` is a loadable module; throws `TypeError` for a non-BufferSource |
| `Module(bytes)` | compiles; throws `CompileError` |
| `Module.imports(m)` / `Module.exports(m)` | `[{module?, name, kind}]`, `kind` is `function`, `memory`, `table` or `global` |
| `Module.customSections(m, name)` | payloads of the custom sections called `name`, as `ArrayBuffer` copies |
| `Instance(module, imports)` | `.exports` is a frozen, null-prototype object of functions, `Memory`, `Table`, `Global`; `.module` is the `Module` |
| `Memory({initial, maximum})` | `.buffer`, `.grow(delta)` (returns the old page count) |
| `Table({element, initial, maximum}, value?)` | `.length`, `.get(i)`, `.set(i, fn)`, `.grow(delta, fn?)`; holds `anyfunc` |
| `Global({value, mutable}, v?)` | `.value`, `.valueOf()`; `value` is `i32`, `i64` (BigInt), `f32` or `f64` |
| `CompileError`, `LinkError`, `RuntimeError` | `Error` subclasses; wasm traps surface as `RuntimeError` |
| `backend` | name of the linked engine, `"wasm3"` or `"wamr"` |

## Semantics

- **Values:** `i32`, `f32`, `f64` are JS numbers; `i64` is a `BigInt` in both directions.
- **Imports:** `imports` is `{ moduleName: { name: value } }`; a function import is any JS
  function (its return value converted to the declared result type, a thrown exception propagates
  through the guest as-is), a memory/table/global import the matching object. An immutable
  `Global` import may be a plain number (or BigInt for `i64`).
- **Errors:** descriptors are validated (`TypeError` for the wrong shape, `RangeError` for
  out-of-range sizes); a missing import is a `LinkError`, a type mismatch too; a guest trap is a
  `RuntimeError`.
- **Exports:** exported functions are named by function index (`add.name === '0'`).
  `Table.set` accepts only exported wasm functions or `null`, as in the spec. `Table.get(i)` and an
  export of the same function are different objects.
- **Memory:** instances importing the same `Memory` share it; when it grows, the old
  `ArrayBuffer` is detached and `.buffer` returns a new one. `shared` memories are not supported.
- **Ownership:** `Memory`, `Table`, `Global` and function wrappers hold their instance and the
  instance's `Module` alive; host functions created for an instantiation are released with it.

## Not yet supported

- `import source` and `with { type: 'wasm' }` for `.wasm` ([`lib/wasm-loader.js`](../js/wasm-loader.md) covers plain imports)
- `externref` tables and globals, shared memories, `Tag`/`Exception`/`JSTag`
- one function object per wasm function
- multi-value host returns
- the engine-specific gaps in the table above

## Tests and examples

| File | Covers |
| --- | --- |
| `tests/unittests/test-wasm.js` | the native API, on both engines |
| `tests/unittests/test-webassembly.js` | the `WebAssembly` global |
| `tests/unittests/test-wasm-loader.js` | `.wasm` imports |
| `tests/unittests/test-wasi.js` | WASI (also runs on node, bun, deno) |
| `examples/wasm-shish.js` | the Emscripten builds of shutil and shish (portable: qjsm, node, bun, deno) |
| `examples/wasi-shish.js` | the WASI build of shish (needs an engine with wasm exceptions if built with `-fwasm-exceptions`) |
