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
- `src/compat/glob.c:glob3()` *(pre-existing TODO-style comment)* — `/* TODO: don't call for ENOENT or
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
- `lib/readline.js` and `lib/buffer.js` were removed in commit `958cffc9` as stubs; both are
  back as real implementations (`Buffer` and `readline`, 2026-10-07). `lib/perf_hooks.js` now has User
  Timing (`mark`, `measure`, entries, `PerformanceObserver`, 2026-10-08); missing: `timerify`,
  histograms, `monitorEventLoopDelay`, `eventLoopUtilization`.
- `lib/module.js` (Node's `node:module`) covers the whole export list; `register()`,
  `.node` addons and the compile cache are refused, see `doc/js/module.md`.

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
`src/wasm3-backend.c` wired into CMake (`cmake/deps/BuildWasm3.cmake`) and
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
- `.mjs` forces module-eval mode (`src/qjsm.c:jsm_hook_load()`) but `.cjs` has no special handling
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
- `node:readline`: done 2026-10-07/08 (`lib/readline.js`, `lib/readlinePromises.js`): line mode and
  `terminal: true` (editing, history, tab completion, `emitKeypressEvents`; expected bytes recorded from node). Still
  missing: kill-ring rotation, undo/redo, `readline/promises`' `Readline` class.

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

### Summary

**Priority order:**
1. **LOW:** Inline BitSet (10.1) - simplifies dependencies (minor)
2. **LOW:** Inline async-closure (10.2) - clarifies MySQL-specific code

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
  that mixes unrelated concerns: module bookkeeping (`ADD_MODULE`/`FIND_MODULE`),
  path resolution (`NORMALIZE_MODULE`/`LOCATE_MODULE`/`LOAD_MODULE`), and
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

Node.js API compatibility gaps (process, timers, assert, buffer, console, globals, perf_hooks, tty, url) are tracked in `doc/api-compatibility-nodejs.md`.

## Tier 13 — make `child-process.[ch]` free of JSValue/JSContext

Keep
`include/child-process.h` + `src/child-process.c` as a **pure-C** process
layer (no `quickjs.h`, no `JS_*`, no `js_malloc`), and move everything that
needs the engine into `quickjs-child-process.c`.

### Where the engine leaks in today (verified, `src/child-process.c`)

| Symbol | Engine dependency | Goes to |
| --- | --- | --- |
| `ChildProcess.onexit` | `JSValue` member | binding wrapper struct |
| `child_process_signal()` | `js_module_namespace_sync("os")`, `JS_Call` of `os.signal` | binding |
| `child_process_sigchld()` | `JSCFunction`, `JSContext` | binding (calls core reaper) |
| `child_process_exitcode/signalcode()` | return `JSValue` | binding (read plain fields) |
| `child_process_notify()` | `JS_Call(onexit)` | binding |
| `child_process_new()` | `js_mallocz`, installs SIGCHLD handler | core `calloc`; handler install in binding |
| `child_process_remove()` | takes `JSContext*` to uninstall handler | core `list_del` only; binding uninstalls |
| `child_process_environment()` | `PropertyEnumeration`, `Vector`, `js_malloc`, `JS_FreeCString` | binding (`js_child_process_options`) |
| `child_process_free_rt()` | `js_free_rt`, `js_strv_free_rt`, `JS_FreeValueRT` | core `free`/`strv_free`; binding frees `onexit` |
| header | `#include <quickjs.h>` | removed |

Already pure: `child_process_get/spawn/status/wait/kill`, the `W*` macros,
the Windows `SIG*` block, `child_process_signals[]`.

### Target shape

```c
/* include/child-process.h: no quickjs.h */
typedef struct ChildProcess { /* same fields minus onexit */ } ChildProcess;

ChildProcess* child_process_new(void);          /* NULL with errno set */
void          child_process_free(ChildProcess*);/* closes pipes, frees strings */
int           child_process_reap(void (*cb)(ChildProcess*, void*), void*);
                                                /* waitpid(-1, WNOHANG) + status + list_del; calls cb on exit */
int           child_process_count(void);        /* list length, drives handler install/uninstall */
```

Binding side (`quickjs-child-process.c`):

```c
typedef struct { ChildProcess cp; JSValue onexit; } JSChildProcess; /* cp first: cast is valid */
```

- opaque pointer stays `ChildProcess*`-compatible via first-member embedding, so
  `js_child_process_data2()` and all existing `cp->field` reads keep working.
- `onexit` get/set (`quickjs-child-process.c:394,411`) switch to `((JSChildProcess*)cp)->onexit`.
- finalizer: `JS_FreeValueRT(rt, jcp->onexit); child_process_free(&jcp->cp); js_free_rt(rt, jcp);`
- SIGCHLD handler (`os.signal(SIGCHLD, fn)`) installed when `child_process_count()` goes 0→1
  after `spawn`, removed on 1→0 after `remove`; the callback calls `child_process_reap()`
  with a `cb` that does `JS_Call(onexit, exitcode, signalcode)`.
- `child_process_environment()` becomes a static in the binding, building `char**` with
  `malloc`; the core takes **malloc'd** `file/cwd/args/env` and `free()`s them
  (binding uses `strdup`/`malloc`, never `js_malloc`).

### Steps

1. **Baseline**: `ctest -R child` + `qjsm tests/unittests/test-child-process.js`; record results.
2. **Allocator (decided)**: the core owns its strings via libc `malloc/free`; the binding
   hands over `strdup`-style copies of `JS_ToCString` results (freed with `JS_FreeCString`
   on its side). `js_strv_free_rt` is replaced by a local `strv_free()` in the core (check
   `src/utils.c` for a JS-free equivalent first).
3. **Split struct**: drop `onexit` from `ChildProcess`; add `JSChildProcess` in the binding;
   update constructor, `js_child_process_wrap`, get/set (`:176,394,411`) and the finalizer.
4. **Move handler + JS result helpers** (`child_process_signal`, `_sigchld`, `_exitcode`,
   `_signalcode`, `_notify`) into the binding as statics; make `remove` JS-free.
5. **Move `child_process_environment`** into the binding; delete `#include "utils.h"`,
   `property-enumeration.h`, `char-utils.h` from `src/child-process.c` where no longer needed
   (keep `path.h` for `path_isname` on Windows).
6. **Strip header**: remove `#include <quickjs.h>`; verify with
   `cc -fsyntax-only -Iinclude -x c include/child-process.h` (no `-I` for quickjs).
7. **Check no other users**: `grep -rn "child-process.h\|child_process_" ~/Sources/quickjs/qjs-*`
   (before this plan only `quickjs-child-process.c` includes it).
8. **Verify**: `grep -n "JS_\|JSValue\|JSContext\|js_" src/child-process.c include/child-process.h`
   returns nothing; build static + shared module targets and wasm3/WAMR builds
   (`child-process` is in the module list for both, `CMakeLists.txt:598-601`); full `ctest`;
   run under ASan once to check the finalizer frees `JSChildProcess` exactly once and the
   SIGCHLD handler is uninstalled when the last child is reaped.
9. **Docs/tracking**: note layering in `doc/native/child-process.md`; no new public JS API, so no `api-compatibility.md` change.

### Risks / things to check

- `child_process_list` and `child_process_handler` are process-global statics; with the
  handler install/uninstall moved into the binding, a second `JSContext` (e.g. worker) could
  race the count — keep it as today (single-context assumption), document it.
- `ChildProcess` embedded first in `JSChildProcess`: `child_process_get(pid)` returns a core
  pointer; cast back is only valid for objects created through the binding (all of them are).
- `child_process_spawn` still prints to `stderr` on failure (`posix_spawnp error`,
  `CreateProcessA error`); consider returning `-1` with `errno` only, so the binding can throw
  — separate follow-up, not part of this split.
- Windows branch has `DynBuf db;` unused and `child_process_kill` casts `pid` to `HANDLE`;
  both untested here, leave as is (surgical).

## Tier 14 — JSON pipeline: parsers, writers, builders, walkers and their adapters

Survey of every JSON entity (read 2026-10-10 from `include/{jread,jwrite,jbuild,walk,json,sj,stream-utils}.h`
and `quickjs-json.c`) with its push/pull/evented shape, the API boundaries between them,
and where an adapter would let one source feed one sink. Goal: JSON streamed from any
source (`Reader`, chunks, fd, JS value) pumped into an event/callback sink that is **pure C
(no JSValue)**, with the JS classes as thin adapters on top.

### Inventory

| Entity | File | Direction | Model | Input | Output | Engine-free? |
| --- | --- | --- | --- | --- | --- | --- |
| `sj_*` | `include/sj.h` | bytes → tokens | pull, whole buffer | `char*`, len | `sj_Value`, `sj_iter_array/object` | yes (third-party, header-only) |
| `jr_*` (`jread`) | `src/jread.c` | bytes → events | **push**, chunked, resumable mid-token, resyncs after errors | `jr_read(cb, chunk, len, ud, state)`, `jr_finish` | `jr_callback(jr_type_t, jr_str_t*, ud)` | code yes; `jread.h` includes `vector.h` → `quickjs.h` |
| `JsonParser` (`json.h`) | `src/json.c` | bytes → events | **pull**, `Reader`, `JSON_NEED_DATA` to resume, comments, `Location` | `Reader` (4 KiB-block buffered) | one `JsonValueType` per `json_parse()` | no: `json_init(…, JSContext*)`, `json_free(…, JSRuntime*)`, `Location` |
| `jwrite_*` (`JsonWriter`) | `src/jwrite.c` | events → bytes | **push** (caller calls `jwrite_object_start/key/int64/…`), `Writer` sink, backpressure via `Writer` returning 0 | event calls | bytes through `Writer`; `-JWRITE_E_*` | code yes; header includes `stream-utils.h`/`vector.h` → `quickjs.h` |
| `jbuild_*` (`JsonBuilder`) | `src/jbuild.c` | events → JS value | push (`push/pop/key/value`), consumes `jr_type_t` | events | `JSValue` root | **no** (JSContext, JSValue) |
| `WalkIterator` / `WalkInterface` | `src/walk.c` | JS value → events | **pull** (`walk_next()` = one event), or iface callbacks | `JSValue` | 6 callbacks (`key/object_start/array_start/object_end/array_end/value`) taking `JSContext*`, `JSValueConst` | **no** |
| `JSON.read` | `quickjs-json.c` | bytes → JSValue | whole-buffer, `sj` + `parse_val` frame stack | string/buffer | value | no |
| `JSON.write` | `quickjs-json.c` | JSValue → bytes | whole-value, own `PropertyEnumeration` stack + `Writer` | value | bytes/`Writer` | no |
| `JSONL.parse/parseChunk` | `quickjs-json.c` | bytes → values | chunk-resumable, own boundary scanner (`jsonl_value_end`) then `sj` | string/Uint8Array | values + `read/done/error` | no |
| `JsonPushParser` | `quickjs-json.c` | bytes → events/JSValue | push: `write(chunk)`, `close()`; `jr_read` → either `jbuild` (`.root`) or JS callbacks per `jr_type_t` | chunks | callbacks or `root` | no (wrapper of `jr_*` + `jbuild_*`) |
| `JsonParser` (JS class) | `quickjs-json.c` | bytes → events | pull, `Reader` from fd/buffer/function/`read()` method; `[Symbol.iterator]` | any `reader_from_js` source | `JsonValueType` ints (+`NEED_DATA`) | no (wrapper of `json.c`) |
| `JsonWriter` (JS class) | `quickjs-json.c` | JSValue/events → bytes | push; implements `WalkInterface` (`js_walk_register`) so a `WalkIterator` can drive it | `Writer` (`writer_from_js`) | bytes | no (wrapper of `jwrite_*` + walk) |
| `JsonSerializer` | `quickjs-json.c` | JSValue → bytes | **pull** `read(n)`, produces only n bytes, `skip`/`CappedBuf`/`blocked` | JSValue | n-byte chunks | no — own recursion + own `sw_*` writers; does **not** use `walk`/`jwrite` yet; to be migrated, see "JsonSerializer → walk migration" |
| `jsonStreams.js` | `lib/jsonStreams.js` | JS-level | WHATWG `TransformStream`s (Deno `@std/json` shapes) | strings/values | values/strings | JS only, no native code |

Transport layer under all of it (`include/stream-utils.h`): `Reader` = pull
`ssize_t read(fd, buf, n, rd)`; `Writer` = push `ssize_t write(fd, buf, n, wr)` (return 0 =
blocked); constructors from dynbuf/buf/bytes/fd (pure C) and `*_from_js*` (engine-bound);
combinators `reader_buffered/counted/location`, `writer_tee/buffered/counted/escaped`.

### Event vocabularies (the real API boundaries)

There is no single event type; four exist side by side:

| Vocabulary | Defined in | Events | Carries |
| --- | --- | --- | --- |
| `jr_type_t` + `jr_str_t` | `jread.h` | error, null, true, false, number, string, array_start/end, object_start/end, key | decoded text (escapes resolved), number as raw text; no position, no comments |
| `JsonValueType` | `json.h` | OBJECT, OBJECT_END, ARRAY, ARRAY_END, KEY, STRING, TRUE, FALSE, NULL, NUMBER, COMMENT, + NEED_DATA/ERROR/RESYNC | token in `json->token`, position in `Location` |
| `WalkEvent` / `WalkInterface` | `walk.h` | key, object_start, array_start, object_end, array_end, value | `JSValue` (key and value), engine-bound |
| `jwrite_*` calls / `jbuild_*` calls | `jwrite.h`, `jbuild.h` | the same six + null/bool/int64/double/string/raw | C scalars (jwrite) / `jr_type_t`+text (jbuild) |

Boundaries, where one layer hands over to the next:

```
 bytes ──Reader (pull)──► JsonParser/json_parse ──JsonValueType──┐
 bytes ──chunks (push)──► jr_read ──────────────jr_callback──────┼─► (sink)
 bytes ──whole buffer───► sj / JSONL / JSON.read ─► JSValue (no event stage)
 JSValue ──walk_next/WalkInterface──────────────────────────────┤
                                                                 ▼
 sinks: jbuild (→JSValue) · jwrite (→Writer→bytes) · JS callbacks (JsonPushParser)
```

### Where interoperability / adapting is possible (gaps)

Already connected: `jr_read → jbuild` (JsonPushParser), `walk → jwrite` (JsonWriter as
`WalkInterface`), `Reader → json_parse` (JsonParser).

| # | Adapter (source → sink) | Why / what is missing | Engine-free? |
| --- | --- | --- | --- |
| A1 | `jr_callback` → `jwrite_*` ("reformat"/minify/pretty/JSON5-ify while streaming) | no direct pump today; strings are already decoded by jr so `jwrite_string` re-escapes; numbers via `jwrite_number` | **yes** |
| A2 | `json_parse` (pull) → `jr_callback` (`json_pump(JsonParser*, jr_callback, ud)`) | map `JsonValueType`→`jr_type_t`; lets the `Reader`-based parser feed the same sinks as the chunk parser; comments need a new event | needs `json.c` decoupled from `JSContext` first |
| A3 | `Reader` → `jr_read` (`jr_pump(Reader*, cb, ud, state)`: read loop + `jr_finish`) | `jr_read` is chunk-only; every caller hand-writes the loop | **yes** |
| A4 | `WalkIterator` → `jr_callback` (JS value → pure-C events) | one engine-bound adapter in `quickjs-json.c` emitting `jr_*` events; after it, jwrite/jbuild/JS-callbacks all consume value trees through the same sink instead of `WalkInterface` | adapter engine-bound, sink pure |
| A5 | `JsonSerializer` rebuilt on `WalkIterator` + `JsonWriter` (**decided**, plan below) | removes the second serializer (own recursion, own writers, `skip`/`blocked` retry machinery); `walk_next()` is one event per call and resumable | no (walk is engine-bound) |
| A6 | `JSON.write` rebuilt on `walk` + `jwrite` | third copy of the same traversal; also gives `JSON.write` the JSON5 options | no |
| A7 | `JSONL.parse/parseChunk` on `jr_read` | own boundary scanner + `sj`; `jr_read` already resumes mid-token and resyncs per value | engine-bound result, pure scan |
| A8 | `jbuild` taking an allocator-agnostic tree (e.g. an arena of nodes) so `jr → tree → JSValue` | would let the whole read side stay pure C and convert once at the end | design question, not needed yet |
| A9 | JS: `JsonPushParser`/`JsonWriter` ↔ WHATWG streams (`TransformStream` of bytes→events, values→bytes) | `lib/jsonStreams.js` has Deno's shapes but is pure JS (`JSON.parse`/`stringify` per chunk), so it neither streams within a value nor uses the native stages | JS |

### Proposed direction

1. **Make `jr_callback(jr_type_t, jr_str_t*, void*)` the one pure-C event sink.** It already
   has the full six-event vocabulary plus scalars, and two consumers. Gaps to close in it:
   a comment event, a byte offset (for errors/`Location`), and a number form that is not
   forced to text. Do not add a second vtable type unless the above cannot be fitted.
2. **Make the pure-C layer actually pure:** `jread.h`/`jwrite.h` pull `quickjs.h` through
   `vector.h` and `stream-utils.h`; split so `jread.[ch]`/`jwrite.[ch]` compile with no
   engine header (check with `cc -fsyntax-only -Iinclude` and no QuickJS include path).
3. **Add the engine-free adapters A1 and A3** first (small, testable from C or from the
   existing `JsonPushParser`/`JsonWriter` tests), then A2 after `json.c` stops needing
   `JSContext` (`json_init`/`json_free`/`Location`).
4. **Then collapse duplicates** onto the shared stages, one at a time, each with its
   existing tests as the safety net (`tests/unittests/test-json.js`): A5 first (decided,
   plan below), then A6, then A7.
5. **Standard targets** for any new JS-visible surface: Bun `JSONL.parse`/`parseChunk`
   (already present), Deno `@std/json` streams (`lib/jsonStreams.js`), WHATWG
   `TransformStream`; no new custom public API (see project principles).

### Open questions

- Is `JSON_RESYNC`/error-skip behaviour in `json.c` meant to match `jr_read`'s resync? The two
  recover differently (`json.c` tracks `skip_depth`; `jr` resyncs at next `,`/closer) — decide
  before A2 so both parsers report the same event stream on bad input.
- `jr_str_t` is `int32_t len`: strings/keys over 2 GiB truncate; fine for now, note for A3.
- (answered) `JsonSerializer.read(n)` keeps its pull-n-bytes contract by buffering, see the
  migration plan; `blocked` retry semantics are dropped.

### JsonSerializer → walk migration (A5)

`JsonSerializer` (`quickjs-json.c`, "JsonSerializer" block, ~l.1060-1500) is the pull
serializer: `read(n | buffer)` yields only as much text as asked. It re-implements the
value traversal that `include/walk.h` already provides (`json_serializer_step_inner` ≈
`walk_next` + `JsonWriter`), plus a retry mechanism (`skip`, `delivered`, `blocked`,
`CappedBuf`, `write_skip`) for a `Writer` that refuses bytes mid-event.

#### Target shape

```c
typedef struct {
  JSContext* ctx;
  WalkIterator it;      /* the traversal; replaces `stack`, `is_primitive`, step_inner */
  JsonWriter wr;        /* the formatting; replaces sw_*, write_indent/primitive/string */
  DynBuf out;           /* wr.writer = writer_from_dynbuf(&out); never blocks */
  size_t out_pos;       /* bytes of `out` already handed to read() */
  JSValue root;         /* getter only; walk_close() clears it->root */
  Location* loc;
  unsigned error : 1;
} JsonSerializer;
```

Core loop (both `read(n)` and `read(buffer)`): `while(avail < want && (r = walk_next(&it, ctx)) > 0) {}`
with `avail = out.size - out_pos`; `r < 0` → `error`, `walk_close()`, return the exception as
today; then take `n` characters (existing `utf8_strlen`/`utf8_byteoffset` code) or copy up to
`cap` bytes into the buffer and keep the remainder in `out`.

Why this works without a blocking writer: `walk_next()` does not retry an event (state is
advanced before the callback runs), and `jwrite` mutates its frame state before writing, so
a half-written event cannot be replayed. Writing into an unbounded `DynBuf` and slicing
afterwards is what the string path of `read(n)` already does; the buffer path now does the
same instead of `CappedBuf`.

#### Mapping of today's members and getters

| Today | After | Notes |
| --- | --- | --- |
| `stack` (`PropertyEnumeration` vector) | `it.stack` | same element type → `property_recursion_path(&it.stack, ctx)` keeps working for `.path` |
| `started` | `it.state != WALK_START` | |
| `finished` | `it.state == WALK_DONE` | still true while text remains buffered, as now |
| `error` | `error` | set when `walk_next()` or `jwrite_*` fails |
| `blocked` | removed / constant `false` | no blocking stage left; check usage before deleting (only `quickjs-json.c` found; tests do not read it) |
| `indent` get/set | `wr.opts.indent` | the setter may run mid-stream; `jwrite` reads it per event |
| `is_primitive` | gone | `walk_next()` reports a primitive root as one `value` event |
| `write_skip`, `write_capped`, `CappedBuf`, `skip`, `delivered`, `skip_writer`, `dest_writer` | deleted | retry machinery |
| `sw_putc/sw_indent/sw_string/sw_primitive/sw_puts` | deleted | `jwrite_*` via `js_jsonwriter_walk` |
| `json_serializer_step[_inner]` | deleted | `walk_next()` |
| `location` | unchanged | `location_count(loc, bytes, n)` on the bytes handed out |

`js_jsonwriter_walk()` is defined after the serializer; add a forward declaration (or move
the serializer block below the `JsonWriter` block).

#### Behaviour differences to decide before coding

| Case | Serializer today | Walk + JsonWriter |
| --- | --- | --- |
| circular reference | falls through to the primitive writer: the object's `toString` string | `null` (`walk_next` substitutes `JS_NULL`) — matches the doc comment in `walk.c`; pick `null` |
| empty container with `indent` | writes `[` + newline + indent… (checked in code: `sw_indent` after the opening bracket unconditionally) | `jwrite` decides; compare against `JSON.stringify(v, null, n)` (the commented-out test at `test-json.js:572` was meant to assert this) |
| `: ` after key | `:` plus a space only when `indent` is set | `jwrite` opts (`minify`, `indent`) |
| undefined/function/symbol values | `null` | `null` (`js_jsonwriter_value`) — same |
| numbers | `JS_ToCString` text, NaN/Infinity → `null` | same text, plus the JSON5 `hex_numbers` option (unused here) |

#### Steps

1. **Pin the current output**: add a differential test that serializes a corpus (nested
   arrays/objects, empty containers, unicode and escapes, numbers incl. `-0`/`1e21`/BigInt,
   sparse arrays, undefined/function members, circular object) with `JsonSerializer` at
   indent 0 and 2, at read sizes 1, 7 and "all", and as `read(buffer)`; record outputs
   from the *old* implementation as expected strings. Also compare with `JSON.stringify`
   where the semantics are meant to agree.
2. **Restructure the struct** as above (delete the retry members), `vector_init(&js->stack)`
   → `walk_init(&js->it, ctx, root, js_jsonwriter_walk(&js->wr))`; `jwrite_init(&js->wr, …)`
   with `wr.writer = writer_from_dynbuf(&js->out)`, `wr.opts.indent = indent`.
3. **Rewrite `js_jsonserializer_read`** around the loop above; unify the `n` and `buffer`
   branches on the shared `out` buffer; keep the return values (`JS_NewInt64(count)` for a
   buffer, string or `undefined` at EOF for a count — see existing tests 543-650).
4. **Rewrite getters/setter/constructor/finalizer** per the mapping table; the finalizer
   calls `walk_close(&js->it, ctx)` (needs a context: use `js->ctx`, or free through
   `property_recursion_free` + `JS_FreeValueRT` with the runtime as `walk_close` does) and
   `jwrite_free(&js->wr)`.
5. **Delete what became unused** in `quickjs-json.c`: `write_capped`, `write_skip`,
   `sw_*`, `json_serializer_step*`, `CappedBuf`. Keep `write_json_primitive`,
   `write_json_string`, `write_indent`, `write_push` until `JSON.write` (A6) moves too —
   `grep` shows they are still used by `js_json_write` (l.369-460).
6. **Verify**: `qjsm tests/unittests/test-json.js`, the new differential cases, and
   `utilities/jsonpp.js` against a few files; ASan run once (frames are freed in
   `walk_close` on every path: done, error, finalizer mid-walk).
7. **Docs/tracking**: `doc/native/json.md` (JsonSerializer section: `blocked` gone, circular
   → `null`, indent output as chosen), drop the Open question above, mark A5 done.

#### Risks

- **Memory**: the whole requested chunk is built before slicing; `read(buffer)` with a huge
  `cap` produces `cap` bytes at once (same as the string path today). Acceptable; an
  incremental cap is possible by stopping the loop once `avail >= want`.
- **One-event granularity**: a single huge string value is written as one event, so `read(1)`
  on it buffers the whole string once. The old serializer had the same granularity
  (`sw_string` per value).
- **Indent changes mid-stream** (setter) now apply from the next event only, as before.

## Tier 15 — PoC: streaming JSON transformer, pure-C call chain, no tree

> API now specified by Tier 17 (`JsonRewriter`, `jrewrite_*`): this tier's rules become hooks/options of it and its
> tests become the rewriter's test suite.

Proves Tier 14's adapter A1 (`jr_callback` → `jwrite_*`) with real filtering logic, as a
test: bytes in → `jr_read` → **filter callback** → `jwrite_*` → `Writer` → bytes out. No
tree is built and nothing crosses C → JS → C: the whole chain is plain function calls
inside one `jr_read()` call.

```
 chunk ──► jr_read(cb = jrewrite_event, ud = JsonRewriter*, &jr_state)
              │ per token: (jr_type_t, jr_str_t*)
              ▼
          jrewrite_event ── decides: forward / drop / rewrite / hold one event
              │ jwrite_object_start · array_start · key · string · jwrite_number · …
              ▼
          JsonWriter ──► Writer (DynBuf in the test) ──► expected-output comparison
```

### Scope and placement

- **Placement:** `tests/c/jstream.c` first (PoC lives next to its test, no new public
  header, nothing exported to JS). Promote to `src/jrewrite.[ch]` only if it holds up, as the
  engine-free `jr → jwrite` pump of Tier 14 (A1/A3).
- **Not in scope:** any JS-visible class/function (no new public API per project
  principles), `JsonBuilder`/`WalkIterator` (both engine-bound), schema validation.

### The filter (`JsonRewriter`)

State, all plain C:

```c
typedef struct {
  JsonWriter* wr;               /* the sink; NULL-safe checks not needed */
  Vector frames;                /* of Frame { is_object, index; } = the path, no values stored */
  int skip;                     /* >0: inside a dropped subtree; counts nested starts */
  char* held; size_t held_len;  /* one buffered key, copied (jr_str_t is borrowed) */
  unsigned error : 1, blocked : 1;
  const JsonRewriterRule* rules; size_t nrules;   /* what to do, see below */
} JsonRewriter;
```

Rules, chosen so each one needs a different streaming technique (that is the point of the
PoC); each is a test fixture, not a feature of a library:

| Rule | Example | Technique it exercises |
| --- | --- | --- |
| passthrough / reformat | `{"a":[1,2]}` → pretty (`indent=2`) or minified | forward every event; `jwrite` owns commas/indent |
| drop key by name (any depth) | drop `secret` | `key` event: do not forward, set `skip` for the *next value*; a container value raises `skip` until its matching end |
| depth limit | max depth 2, deeper containers become `null` (or are dropped) | `frames` size on each `*_start`; emit one `jwrite_null` and skip the rest |
| pick path | keep only `$.a.b[2]` (stream out just that value) | path match on `frames`+held key; nothing is written outside the match; stops early (caller ignores later events) |
| redact by key | `"password": "x"` → `"password": "***"` | hold the key, decide on the value event, then forward key + replacement |
| number fidelity | `12345678901234567890`, `1e400`, `-0`, `0.10` pass through unchanged | `jwrite_number` with the token text — impossible via a JS number tree |

### Event handling (`jrewrite_event`)

| `jr_type_t` | Normal | While `skip > 0` | Error |
| --- | --- | --- | --- |
| `object_start` / `array_start` | `jwrite_*_start`, push frame | `skip++` | |
| `object_end` / `array_end` | `jwrite_*_end`, pop frame | `skip--`; at 0 the dropped value is complete | |
| `key` | apply drop/redact/pick rule; else hold or `jwrite_key` | ignored | |
| `string` | `jwrite_string` (jr decodes escapes, jwrite re-escapes) | ignored | |
| `number` | `jwrite_number(text)` | ignored | |
| `true` / `false` / `null` | `jwrite_true` / `jwrite_false` / `jwrite_null` | ignored | |
| `error` | set `error`, drop all later events (jr resyncs, but the writer's frame state would not match) | same | |

- `borrowed:` `jr_str_t.cstr` is valid only until the callback returns (accumulator
  buffer); anything kept across events (the held key) is copied.
- Each `jwrite_*` return is checked: `< 0` → `error`; `jr_callback` returns `void`, so
  failures are sticky flags the caller reads after `jr_read()` returns.
- Output strings are re-escaped by `jwrite`, so `"é"` and `"\/"` come out as `é`
  and `/`; expected files are canonical, not byte-identical to the input (numbers are).

### Tests (the PoC's acceptance criteria)

1. **Chunking invariance**: feed each input in 1-byte, 2-byte, 7-byte, and whole chunks;
   the output must be identical (`jr_read` resumes mid-token, mid-escape, mid-keyword).
2. **Every rule above** against small fixtures (inputs and expected outputs inline in
   the `.c`, generated once with `node -e 'JSON.stringify(...)'` and checked in).
3. **Bounded memory**: a generated input of ≥ 50 MB (one big array of small objects,
   produced chunk by chunk, never held whole) runs through the chain; assert peak
   `frames` depth and that RSS stays flat (`getrusage` before/after < a few MiB). This is
   what "no tree" is for.
4. **Errors**: truncated input (`{"a":[1,`) → `jr_finish` error, `error` flag set, no
   crash, no writes after the error; garbage mid-stream → same.
5. **Idempotence**: filter(passthrough, indent 0) of its own output is unchanged.
6. **No engine**: the test never creates a `JSRuntime`/`JSContext` and passes none
   anywhere; a stray `JS_*` call on a missing context would crash it.

### Build / wiring

- `tests/c/jstream.c` is built only with `DO_TESTS` as an executable linked against the
  `modules` static library (for now) and registered with `add_test(NAME jstream COMMAND …)`
  next to the JS tests loop in `CMakeLists.txt` (~l.895-925); the glob there only matches
  `test-*.js`, so the C test needs its own `add_executable`/`add_test`.
- **Purity gate (phase 2):** link the same test from only `jread.c jwrite.c jrewrite.c` +
  `cutils.c` + a pure `vector` + a pure `writer`, with no `libquickjs`; an undefined `JS_*`
  symbol then fails the link. Verified blockers today (`nm -u` of the objects):
  `jread.o` needs `vector_free/put/realloc`, `unicode_to_utf8`; `jwrite.o` needs
  `vector_free/put`, `dbuf_init2`, `writer_free`, `writer_write`. Those live in `vector.c`
  and `stream-utils.c`, which also hold `JS_*` code → split out a pure `vector` core and a
  pure `writer` core (this is Tier 14 step 2, now with a test that enforces it).

### Steps

1. Skeleton: `tests/c/jstream.c` with a `DynBuf` `Writer`, `JsonWriter`, `jr_state_t`, a
   pass-through `jrewrite_event`, and the chunking-invariance test (rule 1 + test 1).
2. Add `drop key` (needs `skip`), then `depth limit`, then `redact` (needs the held key),
   then `pick path` (needs `frames`); each with its fixtures before the next.
3. Number fidelity + error tests; the 50 MB bounded-memory test.
4. CMake: `add_executable`/`add_test`, run under ASan once.
5. Phase 2: purity gate (pure `vector`/`writer` split), then decide promotion to
   `src/jrewrite.[ch]` and the Tier 14 adapters A1/A3 built from this code.

### Findings this PoC is expected to surface (decide when hit)

- **`jr_callback` has no return value**: a filter that finds its answer early (pick path)
  or a blocked `Writer` cannot tell `jr_read` to stop; sticky flags + the caller ceasing to
  feed chunks is the workaround. A `int`-returning `jr_callback` (nonzero = stop) would be a
  small change to `jread.h`; weigh against the other callers (`JsonPushParser`).
- **No position in events**: errors cannot report offset/line; Tier 14 lists this as a gap
  in the sink (comment event, byte offset).
- **Comments** (`//`, `/* */`) are skipped by `jr` silently; a transformer cannot preserve
  them. Fine for the PoC, relevant for a JSON5 round-tripper.

## Tier 16 — thin C connectors: pull parser (`json.h`) and `JsonPushParser` → `JsonWriter`

Folded into Tier 17. The pull connector is `jrewrite_pull()`, the push connector is
`jrewrite_callback()` (a `jr_callback`), the `JsonPushParser(jsonWriter)` constructor form and the
`writer_buffered` check are Tier 17's "JS wiring", and this tier's facts, type map and tests
(equivalence at 1/2/7-byte chunks, number fidelity, NEED_DATA classification, resume, comments,
writer failure, the "no JS call per token or byte" counting test) are Tier 17's tests 1, 6, 9, 11-13.
Nothing is implemented separately under the `jconnect_*` names.

## Tier 17 — `JsonRewriter` (`jrewrite`): the filtering dispatcher between `jread` and `jwrite`

One engine-free C object that takes jread's events in, keeps track of where in the document
it is, filters and transforms, and gives jwrite's events out. It replaces the loose pieces of
Tiers 15/16 (`jconnect_feed`/`push`/`pull` and the Tier 15 PoC's filter callback) by one type whose
default options are the plain pass-through. Name: `jrewrite` pairs with `jread`/`jwrite`/`jbuild`.

```
 jr_read ─────► jrewrite_callback ─┐
 json_parse ──► jrewrite_event ────┼─► [ state · depth gate · key hold · drop · hook ] ─► JsonWriteInterface ─► JsonWriter ─► Writer
 another JsonWriteInterface ───────┘            (JsonRewriter)                       └─► another JsonRewriter (chain)
```

Possible now because `jwrite` mirrors `jr_type_t` one to one (error, null, true, false, number,
string, array_start/end, object_start/end, key), so input and output speak the same eleven events
and the dispatcher is a plain `switch`.

### API (`include/jrewrite.h`, `src/jrewrite.c`; pure C, libc allocation)

```c
typedef struct JsonRewriter JsonRewriter;

typedef enum { JREWRITE_PASS = 0, JREWRITE_DROP, JREWRITE_STOP } JsonRewriteAction;
typedef enum { JREWRITE_DEEP_EMPTY, JREWRITE_DEEP_NULL, JREWRITE_DEEP_DROP } JsonRewriteDeep;

/* what a hook sees: one *member* (key + value, or a bare array element / root value). */
typedef struct {
  jr_type_t type;               /* null true false number string array_start object_start; never key/end/error */
  const char* str; size_t len;  /* scalar text (decoded UTF-8 for strings, raw text for numbers) */
  const char* key; size_t key_len;  /* NULL outside objects; the held key */
  unsigned depth;               /* open containers around this event; root value = 0 */
  uint32_t index;               /* position in the parent in the *input*, 0-based */
} JsonRewriteEvent;

typedef JsonRewriteAction JsonRewriteFn(JsonRewriter*, JsonRewriteEvent*, void* ud);

typedef struct {
  unsigned min_depth;                 /* 0 = none; see "depth" below */
  unsigned max_depth;                 /* UINT_MAX = none */
  JsonRewriteDeep deep;                /* what a container at max_depth turns into */
  const char* const* drop_keys; size_t ndrop_keys;  /* exact name, any depth */
  JsonRewriteFn* filter; void* filter_ud;            /* optional, runs last */
  unsigned track_path : 1;            /* keep ancestor keys so jrewrite_path() works */
} JsonRewriteOptions;

void jrewrite_init(JsonRewriter*, JsonWriteInterface sink, const JsonRewriteOptions*);
void jrewrite_free(JsonRewriter*);

void jrewrite_event(JsonRewriter*, jr_type_t, const char* str, size_t len);   /* the dispatcher */
void jrewrite_callback(jr_type_t, const jr_str_t*, void* ud);                  /* a jr_callback, ud = rewriter */
JsonWriteInterface jrewrite_interface(JsonRewriter*);                         /* as a sink: chainable */

/* pull: runs json_parse() of include/json.h until it stops, feeding jrewrite_event().
 * returns JREWRITE_DONE (clean end at depth 0) | JREWRITE_NEED_DATA (reader dry inside a
 * value; resumable: call again) | JREWRITE_PARSE (first parse error, message in json->error)
 * | JREWRITE_WRITE (first sink error, code in jrewrite_error()). Comments are dropped. */
int jrewrite_pull(JsonParser*, JsonRewriter*);

/* state, never throws */
int      jrewrite_error(const JsonRewriter*);          /* 0, or -JWRITE_E_* / -JREWRITE_E_*; sticky, first failure */
unsigned jrewrite_depth(const JsonRewriter*);          /* open containers in the input */
size_t   jrewrite_path(const JsonRewriter*, char* buf, size_t cap);  /* "$.a[2].b"; needs track_path */
BOOL     jrewrite_done(const JsonRewriter*);           /* a root value just completed */
const JsonRewriteStats* jrewrite_stats(const JsonRewriter*);  /* events_in/out, dropped, root_values, max_depth_seen */
```

Errors: writer codes pass through (`-JWRITE_E_*`); the connector adds `JREWRITE_E_STRUCTURE` (a hook
changed a container type, or the input nests wrongly), and the input's `error` event becomes
`-JWRITE_E_SOURCE` (`jwrite_error` does the same on the writer side).

### Semantics

**Depth.** The depth of an event is the number of containers open around it; a root value has
depth 0, its children 1. A container start has the depth of the container itself.

| Option | Effect on `{"a":{"b":[1,2]}}` |
| --- | --- |
| none | `{"a":{"b":[1,2]}}` |
| `max_depth = 1`, `JREWRITE_DEEP_EMPTY` | `{"a":{}}`: the container at depth 1 is emitted empty, its contents skipped |
| `max_depth = 1`, `JREWRITE_DEEP_NULL` | `{"a":null}` |
| `max_depth = 1`, `JREWRITE_DEEP_DROP` | `{}`: member `a` dropped, key included |
| `min_depth = 1` | `{"b":[1,2]}`: events at depth < 1 (the root braces, key `a`) are hidden; the value at depth 1 becomes a root value (like jq `.[]`); several give a sequence |
| `min_depth = 2` | `[1,2]` |

- Hidden containers still push a frame (state tracking continues); their keys are dropped
  because a key needs an enclosing object in the output.
- Several root values need a separator: `JsonWriteOptions` gets `jsonl : 1` (each root value
  followed by `\n`). This is a prerequisite change in `jwrite.c` (small: the root check in
  `jwrite_after_value`) and also answers Tier 16's "concatenated documents" question.
- `max_depth` and `min_depth` together select a band of levels.

**Members, not events, are filtered.** Keys are held (copied; `jr_str_t` is borrowed) until the
next event, so a decision can use key and value together and a dropped member removes its key
too. The hook is called once per member with the key attached; it never sees `key`, `*_end` or
`error` events on their own.

**Hook rights.** It returns `JREWRITE_PASS`, `JREWRITE_DROP` (member and, for a container, its subtree) or
`JREWRITE_STOP` (finish: later events are ignored, `jrewrite_done()` stays true; the caller stops feeding).
It may change `ev->key/key_len` (rename), `ev->str/len` (new value text), and `ev->type` among
the scalar types (`number`→`string`, anything→`null`). Changing to or from a container type is
`JREWRITE_E_STRUCTURE`. Storage for new text comes from `jrewrite_scratch(c, len)`, valid until the
next event (the hook must not return pointers into its own stack).

**Event flow** (per input event):

| Input | State | Action |
| --- | --- | --- |
| `error` | any | store `-JWRITE_E_SOURCE`, stop forwarding |
| any | error or stopped | ignored |
| `key` | skipping a subtree | ignored |
| `key` | in an object | copy into the held-key buffer |
| scalar / start | skipping a subtree | ignored; a start raises the skip count |
| scalar / start | normal | depth gate → `drop_keys` on the held key → hook → forward with key |
| end | skipping | lower the skip count |
| end | normal | pop the frame; forward the end if the container was emitted; if the stack is empty, a root value is complete (`jrewrite_done`, `root_values++`) |

**State tracked**: frame stack (`is_object`, input index,
`emit`, key offset/len), held key, skip count, depth, `max_depth_seen`, counters
(`events_in`, `events_out`, `dropped`, `root_values`), sticky error, `done`/`stopped`, and with
`track_path` an append-only buffer of ancestor keys (frames store offsets, popping truncates:
no per-frame allocation) from which `jrewrite_path()` renders `$.a[2].b`.

### Uses (each a test, none a library feature)

| Use | Configuration |
| --- | --- |
| reformat / minify / JSON5-ify | defaults; writer options choose the format |
| strip secrets | `drop_keys = {"password","token"}` |
| outline of a big document | `max_depth = 2`, `JREWRITE_DEEP_NULL` |
| stream the items of a huge array | `min_depth = 1` + `jsonl` |
| pick `$.a.b[2]` | hook compares `jrewrite_path()`, returns `JREWRITE_STOP` after the match |
| redact | hook returns `JREWRITE_PASS` with `ev->str` replaced |
| rename keys (`snake_case`→`camelCase`) | hook rewrites `ev->key` via `jrewrite_scratch` |
| count / validate only | sink = a no-op interface; read `jrewrite_stats()` |
| two stages | `jrewrite_interface(second)` as the sink of the first |

### Tests (`tests/c/jrewrite.c`, the Tier 15 harness)

1. Defaults equal the plain `jr` → `jwrite` chain for the whole corpus, at chunk sizes 1, 2, 7 and whole.
2. The depth table above, each row, with arrays and nested mixes; `min`+`max` band.
3. `drop_keys`: at root, nested, in an array of objects, last member of an object (comma state),
   only member (`{}` remains), key whose value is a container.
4. Hook: rename, redact, change type, `JREWRITE_DROP` on a container, `JREWRITE_STOP`, illegal container
   type change → `JREWRITE_E_STRUCTURE`.
5. State: `jrewrite_path` at every event of a sample, `root_values`/`jrewrite_done` over `1 2 [3]`,
   stats counts.
6. Errors: `error` event, truncated input (`jr_finish`), writer failing after N bytes, nothing
   written after the first error.
7. Chain of two connectors equals one connector with both rules.
8. Bounded memory: ≥ 50 MB generated input, flat RSS, `max_depth_seen` as expected.
9. Pull: `jrewrite_pull` over a `Reader` equals the push result; `[1,2` → `NEED_DATA`, `[1,2]`, ``
   and whitespace → `DONE`, `[1,x]` → `PARSE` with nothing written after; resume after the reader
   runs dry (append, call again); comments skipped with `json->comments` set; a number text
   such as `12345678901234567890`, `1e400`, `-0`, `0.10` passes unchanged.
10. Purity gate (after Tier 14 step 2): linked without `libquickjs`.

JS (`tests/unittests/test-json.js`):
11. `new JsonPushParser(new JsonWriter(sink, {indent: 2}))` fed a corpus in chunks equals
    `JSON.stringify(v, null, 2)`.
12. **No crossing (the point of the tier):** `sink` counts its calls; for ≥ 100k values the count
    stays orders of magnitude below the token or byte count (bounded by the buffer size). A
    `JsonPushParser` with a callbacks object is the contrast case (calls scale with tokens).
13. Lifetime: drop the only JS reference to the `JsonWriter`, keep writing through the parser,
    `gc()`: no crash, output intact.

### Facts the pull and push sides rest on (read from the code)

| Fact | Where | Consequence |
| --- | --- | --- |
| `json_parse()` returns one `JsonValueType` per call; string/key/number/comment text is in `json->token` | `json.h`, `json.c` | the pull side maps the type and passes `(json->token.buf, .size)` to `jrewrite_event()` |
| strings and keys are **decoded** in the token, numbers are raw text | `json.c` ~l.273/315 | same data as `jr_str_t`; `jwrite_string` re-escapes, `jwrite_number` keeps numbers verbatim |
| clean EOF and "ran out mid-value" both return `JSON_NEED_DATA` (reader `0` → `NEED_DATA`, `<0` → `JSON_ERROR`) | `json.c` l.222-226 | `jrewrite_pull` classifies it: `json->stack.len == 0 && tok_kind == JSON_TOK_NONE` is a clean end, else truncated |
| `JSON_ERROR` sets `json->error`; the parser resyncs by itself on the next call | `json.c` | the pump stops at the first error; continuing would feed events the writer's frame stack cannot match |
| `JSON_TYPE_COMMENT` only appears with `json->comments` set | `json.h` | `jwrite` has no comment event: dropped |
| `jr_callback` returns `void`; `jr_read()` cannot be stopped from a callback | `jread.h` | the rewriter keeps a sticky error and a `stopped` flag; the caller stops feeding |
| `jwrite_put()` writes **one byte at a time** through the `Writer` | `jwrite.c` | a JS-backed `Writer` (`writer_from_jsfunction`/`jsstream`) would run one JS call per output byte: wrap it with `writer_buffered()` |
| `js_jsonwriter_constructor` calls `writer_from_js()` and does not wrap the result | `quickjs-json.c` ~l.1696 | verify whether `writer_from_js` buffers; if not, add `writer_buffered` there |
| `json_init()` needs a `JSContext` (`dbuf_init_ctx`, `location_new(ctx)`); `json_getc()` calls `location_nextchar(json->loc, c)` unguarded | `json.c` l.109-141, l.205 | `jrewrite_pull` takes an initialised parser and uses no ctx, but building a parser still needs one until a ctx-free init exists (step 8) |

Type map (the only table, in `jrewrite_event`; the pull side translates first):

| `jr_type_t` | `JsonValueType` (pull) | `jwrite` call |
| --- | --- | --- |
| `object_start` / `object_end` | `OBJECT` / `OBJECT_END` | `object_start` / `object_end` |
| `array_start` / `array_end` | `ARRAY` / `ARRAY_END` | `array_start` / `array_end` |
| `key` | `KEY` | `key(text, len)` |
| `string` | `STRING` | `string(text, len)` |
| `number` | `NUMBER` | `number(text, len)` |
| `true` / `false` / `null` | `TRUE` / `FALSE` / `NULL` | `true_` / `false_` / `null` |
| `error` | `JSON_ERROR` | `error(msg, len)`, returns `-JWRITE_E_SOURCE`, writes nothing |
| none | `COMMENT` | none: skipped |
| none | `NEED_DATA` | ends the pump |

`borrowed:` text pointers are valid only until the next `json_parse()` / the callback returns;
the rewriter copies what it keeps (the held key) and `jwrite_*` copies into the `Writer`.

### JS wiring (no new public API at first)

- **Push:** `new JsonPushParser(jsonWriter)`: the constructor takes a function or a callbacks
  object; add "an object of class `JsonWriter`" as a third form (detect with
  `JS_GetOpaque(arg, js_jsonwriter_class_id)`). The parser owns a `JsonRewriter` whose sink is
  `jwrite_interface(writer)`; `refcount:` it keeps a `JS_DupValue` of the writer object,
  released in the finalizer, so the writer cannot be collected under it. `write(chunk)` runs
  `jr_read(jrewrite_callback, …)` entirely in C; `close()` = `jr_finish` plus a check of
  `jrewrite_error()`, throwing `SyntaxError` for parse errors and the `jwrite` message for
  writer errors. The only JS involvement is the user's `Writer` endpoint.
- **Pull:** C only. `JsonParser` (JS class) is token-at-a-time; pulling a whole stream into a
  writer has no standard JS shape and no user, so no JS entry point.
- **Options from JS**, if a use case shows up: declarative only, `new JsonPushParser(writer,
  { minDepth, maxDepth, dropKeys })`. A JS `filter` function would put a JS call back into
  every member (C → JS → C) and is deliberately left out.
- The Deno-style streams in `lib/jsonStreams.js` (Tier 14 A9) are where a standard JS surface
  could later sit on top of this.

### Steps

1. `JsonWriteOptions.jsonl` in `jwrite.c` + a test (also settles "concatenated documents").
2. `jrewrite.[ch]`: struct, `jrewrite_init/free`, `jrewrite_event` pass-through,
   `jrewrite_callback`, `jrewrite_interface`; tests 1 and 6.
3. Frames, depth, `max_depth` with the three policies; test 2.
4. Held key, `drop_keys`, skip counting; test 3.
5. Hook (`JsonRewriteEvent`, actions, scratch); test 4.
6. `track_path`, stats, `done`; test 5.
7. `jrewrite_pull` with the NEED_DATA classification; test 9.
8. Wire `JsonPushParser(jsonWriter)`; tests 11 and 13; check/add `writer_buffered` in the
   `JsonWriter` constructor; test 12.
9. Chain + memory tests (7, 8); register `jrewrite.c` in the `modules` library and CTest
   (`add_executable`/`add_test` next to the JS test loop, `CMakeLists.txt` ~l.895-925; the glob
   only matches `test-*.js`); ASan once.
10. Phase 2, engine-free: `json_init` without `JSContext` (libc allocator, optional `Location`:
    guard `location_nextchar` and the `json->loc` uses); Tier 14 step 2 (pure `vector`/`writer`
    split); link the C tests without `libquickjs` as the purity gate (test 10).
11. Rebuild the Tier 15 PoC rules on `JsonRewriter`; mark Tier 14 A1/A2/A3 and Tiers 15/16 done.
12. Docs: `doc/native/json.md` (rewriter section, `JsonPushParser(writer)` form, comments
    dropped, `jsonl`).

### Open questions

- **`min_depth` meaning:** the "sequence of children" reading above (jq `.[]`) vs. keeping the
  children's own keys. The first needs `jsonl`; the second is impossible with a JSON writer
  (keys at root). Chosen: sequence.
- **Hook sees only members:** a rule that needs the container's end (e.g. "drop empty objects")
  cannot be written; it would need a lookahead of one event. Add only when a use case appears.
- **Early stop:** `JREWRITE_STOP` and `jrewrite_done` only mark the state; `jr_read` cannot be interrupted
  from a callback, so the caller must stop feeding. An `int`-returning `jr_callback` would fix
  that at the source (Tier 15 finding).
- **Duplicate keys, NaN-like number text:** passed through unchanged; `jwrite_number` already
  rejects text that cannot be a number.
- **Blocked writer:** `jwrite` mutates its frame stack before writing, so an event refused by a
  blocked `Writer` (return 0) cannot be replayed. Until `jwrite` is retry-safe the rewriter
  requires a non-blocking `Writer` (buffer / dynbuf / buffered fd) and reports a blocked write as
  a sink error. Check whether `jwrite_*` can return 0 at all.
- **Position in errors:** the pull side has `json->loc`/`json->pos`, the push side has nothing
  until the event sink gains a byte offset (Tier 14).
