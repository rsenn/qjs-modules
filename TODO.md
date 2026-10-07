# TODO

Unfinished work found by scanning the whole codebase (native `quickjs-*.c` bindings, `src/`,
`include/`, `lib/*.js`, `doc/*.md`, `tests/test_*.js`), ordered by leverage: highest-impact,
cheapest-to-fix items first, general cleanup last. Every item below was verified by reading
the code (and, where noted, by actually running it) — not just grepped.

This supersedes the sparse root-level `TODO` file; its four items are folded in below (marked
*(pre-existing)*).

## Reassessment (2026-10-04)

Done/stale items are removed rather than struck through (history is in `git log`), and the
Tier 5/6 line numbers were refreshed against the current tree. Open work keeps its prior
relative order: Tier 9 DOM gaps, Tier 11 `qjsm.c` refactors, Tier 10 C API consolidation,
Tier 12/13 yaml/cyaml. Line numbers elsewhere may still have drifted.

## Roadmap

Four standing goals for this project, in priority order. Every tier below should be read
against these — they're the "why" behind what gets picked up next.

1. **Be the standard library QuickJS deserves** — WHATWG-spec'd web APIs (streams, URL,
   events, encoding, DOM) and Deno/Bun-like runtime APIs (fs, process, timers, readline,
   child_process, ...), with a coherent, documented JS surface over the native bindings.
2. **Be a toolbox for working with QuickJS itself** — inspection/reflection/deep-object/pointer
   utilities for debugging and metaprogramming.
3. **Be a lexer/parser toolkit** — a general, reusable grammar/lexer framework, not just
   glue code for one format.
4. **Support archives, filesystem, sockets, serial, and databases** as first-class, ergonomic
   JS APIs, not just raw native bindings.

## Tier 3 — known perf/architecture debt (already flagged) and spec-compliance gaps

- **`js_is_*` type-check helpers (`js_is_arraybuffer`, `js_is_date`, `js_is_map`, etc. in
  `src/utils.c`) are slow** *(pre-existing TODO item)* — each does
  `instanceof || Object.prototype.toString comparison` via a string compare instead of a tag
  check. These sit on hot paths (serialization, `deep`, `inspect`), so worth profiling before
  extending the fast path to the remaining helpers.
  **Partially addressed**: `js_is_arraybuffer`/`js_is_sharedarraybuffer`/`js_is_date`/
  `js_is_map`/`js_is_weakmap`/`js_is_set` now have a `JS_GetClassID()`-tag fast path
  (probed once per type, gated on `HAVE_JS_GETCLASSID` since not every linked quickjs
  exposes `JS_GetClassID`), falling back to the old string-compare path otherwise.
  `js_is_generator`/`js_is_regexp`/`js_is_promise`/`js_is_dataview`/`js_is_error` still use
  the slow path.

- **NDJSON/JSON-Lines support in the `json` module** *(investigated 2026-10-06)* — what
  exists today, and what the other runtimes have that we lack.

  *Current state* (each verified by running it):
  - `json.read(text)` handles one value only: `read('{"a":1}\n{"a":2}\n')` throws
    `2:1: unexpected trailing data`.
  - `JsonParser` (pull) tokenizes consecutive top-level values separated only by
    whitespace; `.depth` returns to 0 after each, but it builds no values.
  - `JsonPushParser.write()` accepts several documents per chunk, blank lines and `\r\n`,
    and resyncs after a bad line. But `.root` is overwritten by each new document, and
    becomes the *in-progress* object as soon as the next line starts (`write('{"a":1}\n{"a')`
    leaves `.root` as `{}`), so documents can't be collected unless every `write()` is
    exactly one line. The per-event options object only fires when all seven callbacks
    are given, otherwise it falls back to builder mode.
  - Writing: `json.write(v)` and `JsonSerializer`/`JsonWriter` each emit one value; a line
    is `write(v) + '\n'`. No NDJSON writer.
  - Commas are now enforced (`[1 2]` throws in both parsers), so the old comma-laxness
    caveat no longer applies.

  *Equivalents elsewhere* — Node has none built in (the idiom is `readline` + `JSON.parse`
  per line, which already works here), so nothing is proposed from Node:
  - **Bun `Bun.JSONL`** (priority 3 in the API order):
    `JSONL.parse(input: string|Uint8Array): any[]` — returns the values parsed so far;
    throws `SyntaxError` only if none parsed; an incomplete trailing value is ignored.
    `JSONL.parseChunk(input, start?, end?)` → `{ values, read, done, error }` — never
    throws; `read` counts chars for a string, bytes for a `Uint8Array` (`start`/`end` are
    byte offsets, `Uint8Array` only); `done` is true when all input was consumed.
    Proposal: export `JSONL` from `json` with those two functions and Bun's semantics.
    Bun has no `JSONL.stringify`, so none is proposed.
  - **Deno `@std/json`** (JSR, lowest priority): `JsonParseStream` (NDJSON lines →
    objects), `ConcatenatedJsonParseStream` (back-to-back values, any delimiter), and
    `JsonStringifyStream` with `prefix`/`suffix` options (objects → `JSON + '\n'`).
    Proposal, only after `JSONL.parseChunk` exists: thin `TransformStream` wrappers in
    `lib/` (WHATWG streams, `lib/streams.js`), no new native API.

  Not proposed: a `document` callback on `JsonPushParser`/`JsonParser` (the earlier idea
  here) — no Bun/Deno/Node counterpart, and `JSONL.parseChunk` covers the use case.

  *Decision on `read()`*: keep it strict. It mirrors `JSON.parse`, where trailing data is an
  error, and loosening it (or adding a `{multiple: true}` option, which no other runtime
  has) would hide truncated input from callers that expect one value. Multi-document input
  goes through `JSONL.parse` instead.

  *Done* (2026-10-07): `JSONL.parse`/`JSONL.parseChunk` exported by `json`, Bun's results
  verified case by case against `bun`, tests in `test-json.js`, documented in
  `doc/native/json.md`. Implemented as a value-boundary scanner plus `JS_ParseJSON`, not on the
  `JsonParser` pull engine. The Deno-style stream wrappers are done too (`lib/jsonStreams.js`, results diffed against
  `jsr:@std/json`). `TextLineStream` (`lib/textLineStream.js`) feeds `JsonParseStream`.

## Tier 5 — lower-value cleanup (dead alternate code, disabled diagnostics, unfinished scaffolding)

Not urgent individually, but worth a pass since dead/disabled code in the same functions as
live logic has been the source of earlier bugs — cleaning it up now prevents the next one.

- Disabled alternate implementation with no remaining purpose: `src/utils.c:js_values_free()`
  (old `js_values_free(JSContext*, ...)` overload, commented out above the live
  `JSRuntime*` one).
- Duplicated disabled `FROM_UNIXTIME(...)` date-formatting block in both
  `quickjs-mysql.c:js_mysql_print_value()` and `quickjs-pgsql.c:js_pgconn_print_value()`.
- `quickjs-sockets.c`: a whole abandoned `PROP_SYSCALL/PROP_ERRNO/PROP_ERROR/PROP_RET/PROP_AF`
  property block (the property enum, the cases in `js_socket_get()`, and both
  `js_socket_proto_funcs`/`js_asyncsocket_proto_funcs` registrations, all consistently
  disabled together) plus a commented-out `js_sockopt()` helper above `js_sockets_funcs`.
- `quickjs-pointer.c:js_pointer_init()` — disabled forwarding of `Array.prototype.map/reduce/
  forEach/keys/values` onto `Pointer.prototype`; currently not exposed at all.
  `quickjs-pointer.c:js_pointer_funcs()` — an abandoned `STATIC_COMMON` draft that was never
  wired into any function table.
- `quickjs-predicate.c:js_predicate_call()` (commented-out result `switch`),
  `quickjs-inspect.c:inspect_number()` (disabled exponent-stripping number formatter) —
  superseded alternates, safe to delete.
- `quickjs-lexer.c:js_lexer_method()` (`LEXER_BACK`) — `Lexer.prototype.back()` only accepts a token/location object;
  a disabled branch would have let callers pass a raw string instead (currently throws
  `TypeError` for that case).
- `quickjs-path.c:js_path_method()` — a disabled, superseded duplicate of `PATH_REALPATH`
  handling (the live implementation is `js_path_method_dbuf` + `path_realpath3`, registered
  in `js_path_funcs` and working — **not** a missing
  feature, just dead leftover code confusingly shaped like one).
- `src/glob.c:glob3()` *(pre-existing TODO-style comment)* — `/* TODO: don't call for ENOENT or
  ENOTDIR? */`, minor optimization.
- `wasm` module: see the WebAssembly item in Tier 7. The dead `if(MODULE_WASM)` block in
  `CMakeLists.txt` (its `option(MODULE_WASM ...)` is commented out, no `quickjs-wasm.c`
  exists) is either replaced by that work or removed.

## Tier 6 — quickjs-2026 forward-compatibility (found during 2026-07-23 assessment)

- Additional disabled-code items found by the same pass, same shape as Tier 5 (commented-out
  case/branch, feature silently missing rather than erroring):
  - `quickjs-lexer.c:js_lexer_proto_funcs` — iterator `next` and the `values` alias are
    commented out (`[Symbol.iterator]` itself is live).
  - `quickjs-misc.c:js_misc_funcs` — `realpath` (`js_misc_realpath()` exists but is
    unregistered), `resizeArrayBuffer`, `isHTMLDDA` (+ the matching `// case IS_HTMLDDA` in
    `js_misc_is()`) and the `search` alias are commented out; `searchArrayBuffer` itself is
    live.
  - `quickjs-pgsql.c:js_pgresult_funcs` — iterator `next` disabled. `escapeString` has a live
    registration in `js_pgconn_funcs` alongside a disabled duplicate, so it is not missing.
  - `quickjs-tree-walker.c:js_tree_walker_constructor()` /
    `js_tree_iterator_constructor()` — `tree_walker_setroot()`'s return value is discarded
    (minor, probably harmless but worth a look).

## Tier 7 — roadmap gaps: JS standard-library surface (goal 1) vs. what exists

Native bindings that currently have **no `lib/*.js` wrapper at all**, so they're usable only as
raw native modules rather than as part of a documented "standard library" surface: `blob`,
`child-process`, `gpio`, `serial`, `mmap`, `directory`,
`queue`, `repeater`, `virtual`, `magic`, `syscallerror`, `location`.
`sockets` has only
a low-level `lib/socklen_t.js` helper, not a `net`/`dgram`-style ergonomic wrapper - **and
should not get one** (see below).

**No `net`/`dgram`-style wrapper on top of `quickjs-sockets.c` - use `../qjs-lws/` instead**
(decided 2026-09-23). `quickjs-sockets.c` mirrors BSD sockets in a JS-classed way with
unclear, under-tested usage semantics: a non-async `Socket` set non-blocking can surface
as `ENOENT` or a `SyscallError` depending on path, the non-async API can be run blocking
and the `AsyncSocket` API can be run non-blocking, and none of this - especially error
handling - has real test coverage. Building a `net`-shaped wrapper on it would inherit all
of that. `../qjs-lws/` already has a better-tested TCP/UDP story on a real event loop
(libwebsockets' own): `lib/tcpsocket.js`/`lib/tcpsocketstream.js`,
`lib/udpsocket.js`/`lib/udpsocketstream.js` - and `TCPSocketStream` is already
`ReadableStream`/`WritableStream`-shaped, matching this project's WHATWG-first stance
(see CLAUDE.md's "never implement Node.js Streams" rule) with no extra work needed. Point
any future `net`-API-compat ask at qjs-lws rather than building on `quickjs-sockets.c`.

**Exception: RAW sockets** (`SOCK_RAW`/`AF_PACKET` - ICMP, packet capture, custom L3/L4
protocols) are the one thing neither `quickjs-sockets.c`-as-is nor qjs-lws (an
application-protocol library, not a packet-level tool) currently gets you cleanly. If a
real use case shows up, a narrow `lib/rawsocket.js` over `quickjs-sockets.c`'s raw-socket
path could be worth it - but only after actually pinning down (and likely fixing) the
blocking-semantics/error-handling issues above, since raw sockets are exactly where a
silent wrong-mode bug bites hardest (dropped/malformed packets, not just a slow read).

WHATWG/Deno/Bun API gaps in `lib/`:
- `fetch` — missing (out of scope here, see `../qjs-lws/`).
- `structuredClone` — done in `lib/globals.js` (opt-in global, `transfer` included); checked against node on 28 cases, differences: errors are plain `Error` not `DOMException`, a SharedArrayBuffer is returned as the same object.
- `Worker` — no global; only QuickJS's own `os.Worker` exists (also see Tier 9.9).
- `lib/readline.js` and `lib/buffer.js` were deliberately removed in commit `958cffc9` (they
  were 9/12-line stubs); their docs are gone too. A real `node:readline`/`Buffer` would be new
  work, see the `node:readline` item below. `lib/perf_hooks.js` (13 lines:
  `now`/`timeOrigin` only, no marks or measures) is still thin as described, no change there.
- `lib/module.js` (Node's `node:module`) only implements `builtinModules`, `isBuiltin()`,
  `createRequire()`, `registerHooks()`. Missing: `Module` class, async `register()` hooks,
  `syncBuiltinESMExports()`, `SourceMap`.

**WebAssembly: `.wasm` imports** *(investigated 2026-10-06)* — the `WebAssembly` global is
done (`lib/webassembly.js`, opt-in, with `*Streaming`); `.wasm` ESM import is done (`lib/wasm-loader.js`, opt-in, instance phase only); wasm3 is the leading runtime candidate (see the findings below).

*What other runtimes provide:*
- **Browser / Node / Deno / Bun:** a global `WebAssembly` (W3C JS API): `Module`, `Instance`,
  `instantiate`, `compile`, `validate`, `Memory`, `Table`, `Global`, `CompileError`,
  `LinkError`, `RuntimeError`.
- **ESM integration** (WebAssembly esm-integration proposal): a wasm import's module name is
  resolved like a JS specifier; each export becomes a named export (a global export resolves
  to the value it holds); instantiation happens at evaluation time.
  - Node (no flag since v22.19/v24.5) and Deno (since 2.1): instance phase,
    `import * as M from './x.wasm'`.
  - Node: source phase, `import source m from './x.wasm'` / `import.source()`.
  - Bun: no instance import found; `.wasm` is an asset path (`with { type: "file" }`) fed to
    `WebAssembly.instantiate()` by hand.
- **WASI:** `node:wasi` (Node, Bun): `new WASI(opts)`, `wasi.wasiImport`, `wasi.start(instance)`.

*Fit in qjsm:*
- `src/qjsm.c` already has a `.json` synthetic-module loader and the import-attributes loader
  (`with { type: 'json' }`); a `.wasm` branch follows the same shape.
- QuickJS C modules can't declare dependencies, so the loader should return generated JS glue
  (`import * as m0 from 'env'; const i = instantiate(bytes, {env: m0}); export const add =
  i.exports.add;`), which makes the wasm import section link through normal specifiers.
- `import source` is not supported by the engine (no occurrence in `quickjs.c`); only the
  instance phase is realistic.

*Plan (in order):*
1. `WebAssembly` global as a native `wasm` module plus a thin `lib/` wrapper: `Module`,
   `Instance`, `instantiate`, `compile`, `validate`, the three error classes, `Memory`,
   `Global`; then `Table` and `Module.imports()/exports()`.
2. `.wasm` in the qjsm loader (instance phase, glue approach above), also via
   `with { type: 'wasm' }`.
3. `node:wasi`-shaped `WASI` class. wasm3's own `m3_api_wasi` links straight into a runtime
   rather than through an import object, so the binding must detect the WASI object at
   instantiate time and link it natively.

*Does the runtime choice affect Browser/Bun/Deno/Node compatibility?* The JS-visible API
(`WebAssembly` global, `.wasm` ESM import, `node:wasi`) is the same whichever runtime sits
underneath; the runtime only sets the ceiling on which modules and API features work.

*Runtime comparison* (2026-10-06; "?" = not confirmed; wasm3 and WAMR rows verified against
their source, the rest from READMEs):

| | wasm3 | WAMR | toywasm | wasmi | wasmtime | wasmer | WasmEdge |
|---|---|---|---|---|---|---|---|
| Language / build | C, cmake | C, cmake | C, cmake | Rust, cargo | Rust, cargo or prebuilt | Rust, cargo | C++, cmake (+LLVM for AOT) |
| License | MIT | Apache-2.0 + LLVM exc. | BSD-2 | Apache-2.0/MIT | Apache-2.0 | MIT | Apache-2.0 |
| Execution | interpreter | interp, AOT, JIT | interpreter (slow) | interpreter | JIT/AOT | JIT/AOT | interp or AOT |
| Footprint | ~550 KB `libm3.a`, 1.5 MB source | ~57 KB interp (README), 9.8 MB source | small | `no_std` capable | large | large | large |
| Standard `wasm.h` | no | yes | no | yes | yes | yes | yes |
| Reference types | yes | yes | yes | yes | ? | ? | ? |
| Multi-memory | yes | not stated | yes | yes | ? | ? | ? |
| SIMD | no | yes | yes | yes | ? | ? | ? |
| Exceptions | yes | yes | yes | in dev. | ? | ? | ? |
| GC | no | yes | not stated | in dev. | ? | ? | ? |
| Threads | no (README: N/A) | shared memory | yes | in dev. | ? | ? | ? |
| WASI | `m3_api_wasi` | yes | preview1 | `wasmi_wasi` | `wasi.h` | yes | yes |
| Status | minimal maintenance, still committing (HEAD 2026-09-29, v0.9.2) | active, steering committee | active | active, audited | active | active | CNCF sandbox |

*Findings from reading the sources* (clones of both repos, 2026-10-06):
- **wasm3, imports are satisfied only by other wasm modules in the same runtime.**
  `m3_env.c` resolves an imported memory/table/global with `m3_FindModule(runtime,
  import.moduleUtf8)` plus `Module_FindExportedMemory/Table/Global`. There is no API to hand
  it a host-created memory or table.
- **wasm3, workaround spiked and working** (memory case): load a tiny synthesized module
  `(module (memory (export "mem") 1))` under the import's module name, then load the real
  module in the same runtime. The importer reads and writes the same bytes, and the host
  sees them through `m3_GetMemory(exporting_module)`. This gives `WebAssembly.Memory` as an
  import. The same path exists in `m3_env.c` for tables and globals but was not spiked.
  Consequence: instances that share a `Memory`/`Table`/`Global` must live in one wasm3
  runtime.
- **wasm3, `m3_LinkGlobal`** supplies a value only (no shared mutable cell); a shared
  `WebAssembly.Global` needs the synthesized-module route above.
- **wasm3, no public import/export enumeration** (`Module.imports()/exports()`,
  `Object.keys(instance.exports)`): `M3Module.exports` (`M3Export*`, `numExports`) and the
  function/global/memory/table arrays are only in the internal `m3_env.h`.
- **wasm3, public API otherwise covers** `m3_GetMemory`, `m3_FindGlobal/GetGlobal/SetGlobal`,
  `m3_GetTableFunction` (read a table slot), `m3_FindFunctionIn`, `m3_LinkRawFunctionEx`
  (host function with userdata), `m3_Call*`, and `m3_GetErrorInfo`.
- **WAMR through `wasm.h` is no better, and worse in one respect:**
  `wasm_instance_new()` handles `WASM_EXTERN_MEMORY`/`WASM_EXTERN_TABLE` imports with
  `LOG_WARNING("doesn't support import memories and tables for now, ignore them")`, so a
  host-created `Memory`/`Table` import is silently dropped. Imported globals link their
  initial value only (`global_data_linked`), not a shared cell. Host-side
  `wasm_memory_grow()` returns false ("only allow growing a memory via the opcode").
- **WAMR through its native API, spiked** (classic interpreter, `WAMR_BUILD_MULTI_MODULE=1`,
  linked via `build-scripts/runtime_lib.cmake`; a module reader callback hands it the bytes
  of the synthesized memory module when the import section asks for "A"):
  - Imported memory works: a byte the host wrote through `wasm_runtime_get_default_memory()`
    of the importing instance was read back by wasm code (`load() = 42`).
  - Host-side grow works through the native API (`wasm_runtime_enlarge_memory()`, 2 -> 3
    pages).
  - **Not shareable:** the imported module is instantiated as a sub-instance of each parent
    instance. A second instance of the same importer got its own memory (`load() = 0`, not
    42). WAMR registers modules, not instances, so two instances cannot share one memory,
    table or global.
  - **No standalone memory:** a memory exists only inside an instance. A
    `WebAssembly.Memory` that is created, read or written before any instance exists, or
    passed to several instances, has nothing to live in.
- **wasm3, same test** (shared-memory case): a second importer module loaded into the same
  runtime saw the host's write (`7`), so one `Memory` can back many instances, and exists
  before any importer is instantiated.
- **Host functions with an environment pointer exist in both** (`m3_LinkRawFunctionEx`,
  `wasm_func_new_with_env`), enough to bridge a JS closure per import.

*Revised recommendation (after both spikes):* wasm3, despite the weaker maintenance story.
The deciding factor is the JS API's object model, not the runtime features: a
`WebAssembly.Memory` (and `Table`/`Global`) is a standalone object that can exist before
instantiation and be shared by several instances. wasm3 can do that (a synthesized exporter
module in one shared runtime); WAMR cannot (memory lives inside one instance's sub-instance
tree), and its `wasm.h` drops such imports outright.
- WAMR is the better runtime for *running* modules (SIMD, GC, threads, AOT/JIT, maintained
  by a steering committee). Revisit it if real modules hit wasm3's missing SIMD/threads; it
  would then need a backend that gives up shared `Memory` objects or emulates them.
- Keep the runtime behind a small private backend interface (load, instantiate, call,
  memory, global, table, link host function, trap info) so that swap stays cheap; do not bind
  `wasm.h` for portability (WAMR's has the import gap above).
- wasm3 is not dead: HEAD is 2026-09-29 and v0.9.2 carries exceptions, memory64, multi-memory
  and snapshots, though the maintainer calls it minimal maintenance.

*Status (2026-10-07):* `quickjs-wasm.c` is scaffolded over `include/wasm-backend.h`, with
`src/wasm3-backend.c` wired into CMake (`cmake/BuildWasm3.cmake`) and
`tests/unittests/test-wasm.js` passing. `src/wamr-backend.c` is selectable with `-DWASM_BACKEND=wamr`
(function imports only; exported Table is read-only). Memory/Table/Global constructors,
imports and `Module.customSections` work on wasm3; `lib/wasi.js` (`WASI`) works on both.
Docs: `doc/native/wasm.md`, `doc/js/wasi.md`.

*Next steps:*
1. `lib/webassembly.js`: the `WebAssembly` global, `instantiate()`/`compile()` and the
   `*Streaming` variants (Node, Bun and Deno all have them).
2. `.wasm` ESM loader on `registerHooks` (Node and Deno: named exports, imports resolved as
   modules by name; no default export).
3. Missing vs Node/Bun/Deno: `Tag`/`Exception`/`JSTag`, shared memory, externref, one function
   object per wasm function.

`src/qjsm.c` runtime-compat gaps vs Node/Bun/Deno (found during 2026-09-19
node:-prefix audit):
- `import.meta.resolve()` is missing, and `is_main`/main-module detection is always
  `FALSE` for normal loads (`src/qjsm.c:jsm_module_loader()`,
  `js_module_set_import_meta(ctx, module, FALSE, FALSE)`; `import.meta` has only `url` and
  `main`, no `filename`/`dirname`) — Node/Deno/Bun all support these.
- The default filesystem module loader never consults `package.json`'s `"type"`,
  `"exports"`, or `"main"` fields (`src/qjsm.c:jsm_module_package()` only handles an internal
  `"_moduleAliases"` map) — real Node package layouts resolve wrong without opting into
  the `lib/nodeModulesLoader.js` demo loader.
- `.mjs` forces module-eval mode (`src/qjsm.c:jsm_module_load_hooked()`) but `.cjs` has no special handling
  (no forced CJS `module`/`exports`/`require` wrapper regardless of `package.json`
  `"type"`).
- No global `require()` by default (only via explicit `import` of `lib/require.js`) —
  Node/Bun provide it ambiently in CJS contexts.
- No `Buffer` global and no Node-style `crypto`/`zlib`/`dns`/`net`/`worker_threads`
  builtins at all (checked `quickjs-builtins.h`'s full native+compiled list) — common
  Node imports fail outright, not even reachable via a `node:` alias.
- `TextEncoder`/`TextDecoder`/`queueMicrotask` exist only as named exports of
  `textcode`/`util` (imported by `lib/streams.js` and `lib/dom.js`), not ambient globals like every
  other runtime provides.

`node:<name>` export-surface diff vs Bun (found 2026-09-19, via
`utilities/runtime-diff.js -r qjsm -r bun` against assert/child_process/console/events/
fs/module/path/perf_hooks/process/stream/timers/tty/url/util/yaml - `node:repl` excluded,
Bun itself doesn't implement it; `node:tty` failed to load on qjsm, see `BUGS`'s
`lib-tty-imports-nonexistent-fdopensync`). Two kinds of divergence:

**Missing in qjsm** (real Node/Bun exports our `node:x` doesn't have) - by module. *Since 2026-10-07 done: `events` (Node-conformant `EventEmitter`, `on`, `getEventListeners`, `*MaxListeners`), `fs` callback API + `promises` + `fs/promises`, `process` (`on`/`emit`, `nextTick`, `exitCode`, `memoryUsage`, `uptime`), `timers` `setImmediate` + `timers/promises`, `url` `fileURLToPath`/`pathToFileURL`, `util` (`format`, `promisify`, `callbackify`, `parseArgs`, `isDeepStrictEqual`, `deprecate`, `debuglog`, `TextEncoder`/`TextDecoder`), `path.posix`. Still open: `child_process.execFile*` (spawnSync cannot capture stdout), `path.win32`, `path.join` normalization, `process.version(s)`, `Buffer`, `fs.ReadStream`/`WriteStream`, legacy `url.parse`, `module.Module`, `console`, `readline`.*
- `assert`: `CallTracker`, `strict`
- `child_process`: `execFile`, `execFileSync`, `fork`
- `console`: nearly everything (`log`, `error`, `warn`, `info`, `debug`, `table`,
  `group*`, `time*`, `count*`, `assert`, `dir`, `trace`, ...) - `console` isn't wired to
  `node:console` at all yet
- `events`: `EventEmitterAsyncResource`, `getEventListeners`, `getMaxListeners`,
  `setMaxListeners`, `addAbortListener`, `captureRejection*`, `listenerCount`, `on`
- `fs`: the whole `*Sync`-free async-callback API (`readFile`, `writeFile`, `access`,
  `stat`, `mkdir`, `rm`, `cp`, `glob`, ...), `promises`, `ReadStream`/`WriteStream` -
  qjsm's `fs` is sync-only plus a hand-rolled `fsPromises`, no callback style at all
- `module`: `Module` class, async `register()`, `syncBuiltinESMExports()`, `SourceMap` (already
  tracked above)
- `path`: `posix`, `win32`, `matchesGlob`, `toNamespacedPath`
- `perf_hooks`: `Performance*` classes, `createHistogram`, `monitorEventLoopDelay`
- `process`: has `cwd`, `chdir`, `kill`, `stdin`/`stdout`/`stderr`, `exit`, `hrtime`,
  `platform`, `uid`/`gid`/`euid`/`egid`; missing `argv`, `env`, `pid`, `nextTick`, `on`/`emit`
  (EventEmitter surface), `memoryUsage`, `versions`/`version`, `arch`, `uptime`, `exitCode`,
  `umask`, `title`, `execPath`, ... (`lib/process.js` covers only a slice of this)
- `stream`: `Readable`/`Writable`/`Duplex`/`Transform`/`PassThrough` (Node stream
  classes - qjsm only has WHATWG `ReadableStream`/`WritableStream`/`TransformStream`).
  **Not a gap to close** - CLAUDE.md now states Node Streams are deliberately never
  implemented here (incompatible model from WHATWG streams, which wins per the
  standards-priority order). `pipeline`/`finished` (WHATWG-stream-compatible
  equivalents of these two specifically) are still worth having, though.
- `timers`: `setImmediate`/`clearImmediate`, `active`/`enroll`/`unenroll` (legacy timer
  API), `promises`
- `url`: legacy `url.parse`/`format`/`resolve` API, `fileURLToPath`/`pathToFileURL`
- `util`: `promisify`, `callbackify`, `deprecate`, `format`, `debuglog`, `parseArgs`,
  `TextEncoder`/`TextDecoder` (re-exported from `util` in Node), `isDeepStrictEqual`

**WARN - qjsm exports under `node:x` that Bun's real `node:x` doesn't have**: `assert`,
`console`, `fs`, `path`, `perf_hooks`, and especially `util` (100+ names - `util` is this
project's internal POSIX/reflection grab-bag, not just the Node `util` surface) all carry
qjs-modules-specific extras on the *same* module name Node scripts expect. Since
`jsm_builtin_find()` (`src/qjsm.c`) strips a leading `node:` unconditionally for *any*
registered builtin - not just ones that are actually part of Node's API - `node:yaml`
and any other qjsm-only module name also resolves under the `node:` prefix even though
Node/Bun have no such module (confirmed: Bun errors `No such built-in module: node:yaml`,
qjsm resolves it fine). A Node/Bun script that imports a typo'd or wrong `node:`-prefixed
name would fail loudly there but silently succeed here if the name happens to collide
with one of qjsm's own module names - worth deciding whether `node:` stripping should be
restricted to an actual Node-builtins allowlist rather than every registered module.

Extended 2026-09-22 to `node:os`/`node:readline` (not in the original list) and
cross-checked against Deno too (`-r qjsm -r bun -r deno`, all three loaded every module
in the original list cleanly except the two already-known `node:tty`/`node:yaml` cases -
Deno also errors `No such built-in module: node:yaml`, matching Bun):
- `node:readline`: missing `Interface`/`createInterface`/`emitKeypressEvents`/
  `moveCursor`/`clearScreenDown`/`promises` (matches the already-tracked "9-line stub"
  note above) - no extra qjsm-only names here, so no WARN case for this one.

## Tier 8 — architecture cleanup (goal 3 dogfooding, code duplication)

- **Three independent CSS-selector implementations**: `lib/parsel.js` (ported `parsel-js`),
  `lib/css-selectors.js` (compiler built on `parsel.js`), and `lib/css3-selectors.js` (a
  *second*, independent compiler with its own hand-rolled tokenizer, duplicating helper
  functions nearly verbatim from `css-selectors.js`, e.g. `escapeRegExp`/`getAttribute`/
  `hasAttribute`/`isElement`/`childElements`). `lib/css-selectors.js` appears dead: nothing
  in `lib/`, `tests/`, `utilities/`, `examples/`, the sibling `qjs-*` projects or `plot-cv`
  imports it (`lib/dom.js` and `tests/unittests/test-css3-selectors.js` import
  `css3-selectors.js` instead; `tools/site/build.js` only lists its doc page). Worth either deleting `css-selectors.js` or
  consolidating `css3-selectors.js` to build on the shared `lib/lexer`/`lib/parser/grammar.js`
  toolkit (goal 3) instead of duplicating a tokenizer.
- **Lexer/parser toolkit (goal 3) isn't dogfooded by the project's own hardest parsing
  problems.** `lib/parser/grammar.js` + the native `lexer` module are genuinely reused across 9
  independent grammars (`lib/lexer/{bnf,c,cmake,csv,ecmascript,ini,make,shell,xml}.js` — `lib/xml/read.js` was
  rewritten to drive `XMLLexer`/`lib/lexer/xml.js` as a JS port of `js_xml_parse()`, verified
  against the native `xml.read()`/`xml.write()` for tree shape, option surface, and formatting
  quirks; `lib/xml/write.js` is a matching port of `js_xml_write()`), which is good evidence of
  real generality — but `css3-selectors.js` still hand-rolls its own tokenizer instead of
  building on the toolkit.
- **Inconsistent non-enumerable-property idiom across `lib/extend*.js`.** Most files
  (`extendArray.js`, `extendArrayBuffer.js`, `extendAsyncFunction.js`, `extendFunction.js`,
  `extendMap.js`, `extendSet.js`) wrap their extension object in the shared `nonenumerable()`
  helper from `lib/util.js`. `extendGenerator.js`/`extendAsyncGenerator.js` instead
  re-implement the same marking logic inline. Worth converging on one convention.
  (`extendMath.js`/`extendObject.js`, which used yet other idioms, were removed 2026-09 as
  zero-usage with no standard target.)

## Tier 9 — DOM API implementation priorities (browser sandbox, goal 1)

The `lib/dom.js` DOM implementation has the core browser APIs in place. The following
classes are **done**: `EventTarget`, `Event`, `CustomEvent`, `UIEvent`, `MouseEvent`,
`KeyboardEvent`, `FocusEvent`, `InputEvent`, `WheelEvent`, `Touch`, `TouchList`,
`TouchEvent`, `PointerEvent`, `PopStateEvent`, `HashChangeEvent`, `History`, `DOMRect`,
`DOMRectReadOnly`, `Range`, `Selection`, `MutationObserver`, `HTMLElement` (with `dataset`,
`style`, `hidden`, `tabIndex`, `offsetWidth`/`offsetHeight`/`offsetTop`/`offsetLeft`/`offsetParent`,
`clientWidth`/`clientHeight`/`clientTop`/`clientLeft`,
`scrollWidth`/`scrollHeight`/`scrollTop`/`scrollLeft`, etc.), 50+ `HTMLElement` subclasses
(Input, Button, Form, Anchor, Image, TextArea, Select, Option, Script, Style, Link, Media,
Video, Audio, Table, etc.), `DocumentFragment`, `Navigator`, `Location`, `Storage`, `Window`
(with `setTimeout`/`setInterval`/`requestAnimationFrame`/`cancelAnimationFrame`/`history`/`getSelection`),
`File` (in `lib/file.js`), `DOMStringMap`, `CSSStyleDeclaration`, `NodeList`, `HTMLCollection`.

Element geometry: `getBoundingClientRect()` and `getClientRects()` implemented on Element.

Test suites: `tests/unittests/test-dom.js`, `test-dom-event-subclasses.js`,
`test-dom-history.js`, `test-dom-geometry.js` and `test-dom-range-selection.js`.

Remaining items ordered by leverage:

### 9.1 Fetch API (OUT OF SCOPE for this repo)
**Status:** Implemented in the separate `../qjs-lws/` project (`lib/fetch.js`), which wraps libwebsockets for HTTP transport. Not to be duplicated here.

### 9.2 FormData (OUT OF SCOPE for this repo)
**Status:** Implemented in the separate `../qjs-lws/` project (`lib/lws/formdata.js`). Not to be duplicated here.

### 9.3 CSSOM - CSS Object Model (LOWER - computed styles and media queries)
**Why:** Reading computed styles and responsive design.

**Implementation:**
- `window.getComputedStyle(element)` → full `CSSStyleDeclaration` (currently a stub)
- `window.matchMedia(query)` → `MediaQueryList` (currently a stub)
- `MediaQueryList`: `matches`, `media`, `addListener()`, `removeEventListener()`
- `StyleSheet`, `CSSStyleSheet`, `CSSRule` classes (lower priority)

**Files:** `lib/dom.js`

**Status:** `CSSStyleDeclaration` class exists. `getComputedStyle()` and `matchMedia()` are stubs returning empty values.

### 9.4 IntersectionObserver (LOWER - viewport visibility detection)
**Why:** Lazy loading, infinite scroll, analytics.

**Implementation:**
- `IntersectionObserver` class: constructor with callback and options
- `observe(element)`, `unobserve(element)`, `disconnect()`
- `IntersectionObserverEntry`: `target`, `isIntersecting`, `intersectionRatio`

**Files:** `lib/dom.js`

**Status:** Not implemented.

### 9.5 ResizeObserver (LOWER - element size change detection)
**Why:** Responsive components, layout adjustments.

**Implementation:**
- `ResizeObserver` class: constructor with callback
- `observe(element)`, `unobserve(element)`, `disconnect()`
- `ResizeObserverEntry`: `target`, `contentRect`

**Files:** `lib/dom.js`

**Status:** Not implemented.

### 9.6 File + Blob remaining APIs (LOWER - see also Tier 7)
**Why:** File uploads, downloads, binary data.

**Implementation:**
- `FileList`: array-like collection of Files
- `FileReader`: `readAsText()`, `readAsDataURL()`, `readAsArrayBuffer()`, `onload`, `onerror`
- `URL.createObjectURL(blob)`, `URL.revokeObjectURL(url)`

**Files:** `lib/dom.js`, `lib/file.js`

**Status:** `File` class (in `lib/file.js`), `Blob` (native binding) and `Blob.prototype.stream()` exist. `FileList`, `FileReader`, and object URL methods are missing.

### 9.7 WebSocket (OUT OF SCOPE for this repo)
**Status:** Implemented in the separate `../qjs-lws/` project (`lib/websocket.js`, `lib/websocketstream.js`), which wraps libwebsockets. Not to be duplicated here.

### 9.8 Canvas API (OUT OF SCOPE for this repo)
**Status:** Implemented in the separate `../qjs-nanovg/` project (`lib/canvas2d.js`), on top of nanovg + `../qjs-glfw/` for window/GL context. Not to be duplicated here. `HTMLCanvasElement` in `lib/dom.js` keeps its minimal `width`/`height` stub only for structural DOM compatibility (e.g. `<canvas>` element presence); a real `getContext()`/`CanvasRenderingContext2D` belongs in qjs-nanovg.

### 9.9 Web Workers (LOWER - background threads, see also Tier 7)
**Why:** Heavy computation without blocking main thread.

**Implementation:**
- `Worker` class: constructor with script URL
- `postMessage(data)`, `terminate()`
- Events: `onmessage`, `onerror`
- Requires separate execution context

**Files:** `lib/worker.js`

**Status:** No global `Worker` (QuickJS's own `os.Worker` exists; also tracked in Tier 7).

## Tier 10 — C API consolidation and cleanup

Low-usage or redundant C APIs in `include/` and `src/` that should be consolidated,
inlined, or removed to reduce maintenance burden and code duplication.

### 10.1 BitSet only used by one module (LOW - inline)
**Problem:** `bitset.h/bitset.c` has only 2 uses:
- Used only by `src/bitset.c` itself
- Used only by `include/json.h` (which is used by `quickjs-json.c`)

This is a small utility that's only needed by one consumer.

**Recommendation:** Inline `bitset.h/bitset.c` into `json.c` or keep it as a simple
dependency since it's small and well-isolated.

**Files:** `include/bitset.h`, `src/bitset.c`

**Impact:** Small; inline it or keep as-is (minor cleanup opportunity).

### 10.2 async-closure.h only used by MySQL (LOW - inline)
**Problem:** `async-closure.h/async-closure.c` has only 2 uses:
- Used only by `quickjs-mysql.c`
- Used only by itself (`src/async-closure.c`)

This is a MySQL-specific async handler pattern.

**Recommendation:** Inline `async-closure.h/async-closure.c` into `quickjs-mysql.c` or
move to `quickjs-mysql.h`. This is MySQL-specific functionality that doesn't need to be
a general-purpose API.

**Files:** `include/async-closure.h`, `src/async-closure.c`

**Impact:** Clarifies that this is MySQL-specific code.

### 10.3 child-process.h only used by one module (LOW - inline)
**Problem:** `child-process.h/child-process.c` has only 2 uses:
- Used only by `quickjs-child-process.c`
- Used only by itself (`src/child-process.c`)

This is a large module that's only consumed by one binding.

**Recommendation:** Inline `child-process.h/child-process.c` into `quickjs-child-process.c`.
The header can remain as `quickjs-child-process.h` if needed for external use, but the
internal implementation doesn't need to be a separate module.

**Files:** `include/child-process.h`, `src/child-process.c`

**Impact:** Simplifies module structure, though no line count reduction (just consolidation).

### Summary

**Priority order:**
1. **LOW:** Inline BitSet (10.1) - simplifies dependencies (minor)
2. **LOW:** Inline async-closure (10.2) - clarifies MySQL-specific code
3. **LOW:** Inline child-process (10.3) - simplifies structure

## Tier 11 — `src/qjsm.c` refactoring opportunities (found during 2026-08-16 read-through)

A full read-through of `src/qjsm.c` (the `qjsm` entry point / module loader) turned up no
correctness bugs, but several structural spots worth revisiting. Dead/commented-out code and
one obscure reversed-subscript idiom (`(dsl = path_dirlen1(path))[path]`) were already cleaned
up in place during this pass; what's left below is genuine restructuring, deliberately not
done inline since each is either large or a judgment call on API shape.

- **`main()` in `src/qjsm.c` is a ~430-line monolith** doing CLI parsing, runtime/context setup, script and
  `-I`/`-m` loading, REPL bootstrap, and (behind `--dump --quit`) an unrelated instantiation-time
  microbenchmark, all in one function. Splitting into `jsm_parse_args()`,
  `jsm_setup_runtime()`, `jsm_run_scripts()`, and `jsm_bench_instantiation()` would make each
  piece testable/readable in isolation. Nontrivial: the pieces share a lot of local state
  (`had_error`, `sargs`, `include_list`, ...) that would need to move into a small context
  struct or be threaded through as parameters.
- **`jsm_module_func()` is a single function** dispatching on a `magic` enum
  that mixes unrelated concerns: module bookkeeping (`ADD_MODULE`/`FIND_MODULE`/
  `FIND_MODULE_INDEX`), path resolution (`NORMALIZE_MODULE`/`LOCATE_MODULE`/`LOAD_MODULE`), and
  the `MODULE_LOADER` hook registry.
- **`jsm_module_loader()` (the `JSModuleLoaderFunc` implementation) does six distinct things
  in one function** (~140 lines) with several `goto end;`/`goto again;` jumps: `data:` URL
  handling, dispatch through the external loader chain (`jsm_call_loaders`), circular-import
  detection, `package.json` alias resolution, builtin-module lookup, and filesystem
  resolution + the "could not load module" error formatting. Worth splitting along those
  seams (e.g. `jsm_resolve_data_url`, `jsm_run_external_loaders`, `jsm_warn_circular`) so each
  piece can be reasoned about independently — risky to do without first having a test that
  exercises each branch.
- **CLI flag variables in `main()` are declared `char`** (`dump_memory`, `trace_memory`,
  `empty_run`, `module`, `load_std`, `list_modules` — e.g. `-d -d -d ...` increments
  `dump_memory` via `dump_memory++`) rather than `int`/`BOOL`. Not currently reachable (argc
  is bounded), but the type doesn't communicate "counter" and invites a subtle bug if the
  parsing loop is ever restructured.
- **The hand-rolled long-option parser in `main()`** (`/* cannot use getopt because we want to
  pass the command line to the script */`) is a legitimate constraint, but the resulting loop
  is a ~150-line sequence of `if(opt == 'x' || !strcmp(longopt, "xxx")) { ...; break; }`
  blocks that's easy to get subtly wrong when adding a new flag. Could become a small
  `{shortopt, longopt, handler}` table walked by one loop, without pulling in libc `getopt`.
- **Global mutable state is spread across ~10 independent `thread_local`/`static` variables**
  (`jsm_stack`, `jsm_builtin_modules`, `module_list`, `debug_list`, `module_loaders`,
  `loaded_modules`, `package_json`, `exename`/`exelen`, `jsm_rt`/`jsm_ctx`, `interactive`,
  `DEBUG_MODULE`) instead of one grouped `struct`. Not urgent, but would make the module's
  state easier to audit and would be a prerequisite for ever running more than one `qjsm`
  runtime per process.
- **`jsm_search_suffix()`'s debug trace stringifies a function pointer by identity comparison**
  (`fn == &is_module ? "is_module" : fn == &jsm_search_path ? "jsm_search_path" :
  "<unknown>"`) to name the `ModuleLoader*` passed in. Silently prints `"<unknown>"` if a third
  implementation is ever added. A small named-enum-plus-lookup (or just naming the parameter
  at each call site in the trace) would stay correct automatically.

## Tier 12 — deferred: `yaml` module `read()` (YAML → JS)

`quickjs-yaml.c` currently only implements `write()` (JS value → block-YAML text), added for
the eagle-agent EDA parts catalog export (used by `~/Sources/plot-cv/eagle-*.js`). Parsing was explicitly
out of scope for that work and is deferred:

- A `read(text)` function, symmetric with `write()`, decoding the same block-YAML subset
  back into a JS value.
- If/when that's built, model it as a computed-goto push/SAX parser in
  `src/yread.c`/`include/yread.h`, the same pattern `src/jread.c`/`include/jread.h` (JSON) and
  `src/xread.c`/`include/xread.h` (XML) already use, rather than a recursive-descent parser
  inline in `quickjs-yaml.c`.
- Scope stays pinned to the writer's own subset (block style, plain/quoted scalars, no
  anchors/aliases/multi-doc/flow/tags) — no need to handle full YAML 1.1/1.2.

  **Superseded by Tier 13 below** if cyaml adoption goes ahead: cyaml's `cyaml_parse()` +
  `cyaml_emit()` would cover both `read()` and `write()` (full YAML 1.2, not just this
  writer's subset), making a hand-rolled `src/yread.c` push parser unnecessary. Leaving this
  entry in place until that decision is made.

## Tier 13 — adopt cyaml as the `yaml` module's backing library (plan only, not started)

`quickjs-yaml.c` currently hand-rolls its own block-YAML writer (`quickjs-yaml.c:js_yaml_write()`,
~200 lines of DynBuf-based emission logic covering only a
restricted subset — see Tier 12 above) and has no reader at all. Replace both with
[cyaml](https://github.com/andrewmd5/cyaml) (MIT, C11, zero dependencies beyond libc,
passes the full `yaml-test-suite`), vendored as a git submodule.

**Submodule placement:** put it at `3rdparty/cyaml`. All current submodules (`libarchive`,
`pigpio`, `libutf`, `tutf8e`, `libserialport`, `libbcrypt`) live under `third_party/<name>`,
not `3rdparty/`. So the name the plan wants for the new `cyaml` submodule differs from the
existing directory; decide whether to follow `third_party/` or rename it to `3rdparty/` as a
separate follow-up, not bundled into the cyaml change itself.

**API shape — full-tree only, no evented/pull parser:** confirmed by reading `src/cyaml.h`
(and the README's "Event stream output" feature) that cyaml has no SAX-style push parser
(no read-callback/handler registration) and no incremental pull parser (no "get next
event" call that holds cursor state across calls) comparable to this project's own
`src/jread.c`/`src/xread.c` computed-goto push parsers or the pull-style primitives
`XMLPushParser`/JSON's streaming reader expose. `cyaml_parse()`/`cyaml_parse_stream()`
take the entire source buffer up front and return a fully-built `cyaml_doc_t*` node tree;
there is no way to feed it chunks incrementally or stop after N events. `cyaml_events()`/
`cyaml_stream_events()` sound like a streaming API from the name, but they are the
opposite: they take an *already fully-parsed* `cyaml_doc_t*` and serialize it back out as
a textual canonical event-log string (the same event-log format `yaml-test-suite` uses for
its conformance fixtures) — a dump format, not a live callback/cursor API. Implication for
this module: `read()` will have to be a whole-buffer-in, whole-JS-value-out conversion
(`cyaml_parse()` → walk the `cyaml_node_t` tree → build the JS value), same shape as the
existing JSON module's non-streaming `parse()`, not the incremental push/pull style used
for XML. If an evented/pull YAML reader is ever wanted, cyaml won't provide it — would
still need the hand-rolled `src/yread.c` design sketched in Tier 12, on top of or instead
of cyaml.

**Plan:**
1. Add `3rdparty/cyaml` submodule (`https://github.com/andrewmd5/cyaml`), wire into
   `CMakeLists.txt` (cyaml ships its own `CMakeLists.txt`; likely `add_subdirectory()` with
   `CYAML_BUILD_TESTS`/`CYAML_BUILD_SHARED` off, static-link into `quickjs-yaml.c`'s module).
2. Replace `js_yaml_write()`'s hand-rolled emission with `cyaml_doc_new()` +
   `cyaml_new_*()`/`cyaml_map_set()`/`cyaml_seq_push()` tree construction from the JS value,
   then `cyaml_emit()`. Drops the current subset restrictions (flow style, anchors/aliases,
   multi-doc all become available "for free").
3. Implement `read()` via `cyaml_parse()` + a `cyaml_node_t` → `JSValue` walk (using
   `cyaml_scalar_kind()`/`cyaml_as_int()`/`cyaml_as_float()`/`cyaml_as_bool()`/
   `cyaml_scalar_str()` for scalars, iterate seq/map nodes for containers).
4. Decide whether to also expose YPATH (`cyaml_path()`/`cyaml_path_query()`) as a
   `yaml`-module convenience, or leave that as a `deep`/`pointer`-module-style concern —
   default to *not* exposing it initially (avoid growing custom API surface per the
   "internal vs public APIs" project principle), revisit only if a concrete use case shows up.
5. Update `doc/native/yaml.md` for the new `read()` export and the widened `write()`
   compliance (full YAML 1.2 vs. today's documented subset).
6. Remove/resolve the Tier 12 entry above once this lands (either fold its `read()` request
   into this work, or explicitly drop the yread.c push-parser design if cyaml covers the
   need without it).
