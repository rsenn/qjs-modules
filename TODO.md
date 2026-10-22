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

## ctest state (2026-10-21)

184 of 186 pass. Fixed since the 2026-10-18 list below: `test-extract-c` (`lex()` folds the `pp_*` run of a directive into one `preprocessor` token), `test-directory` (`new Directory()` is an empty
iteration), `test-mmap` (`filename()` reads `/proc/self/maps` until EOF), `test-lexer-cpp` (a backslash-newline
is skipped, no token), `test-typescript` (strip-only syntax), `test-globals`. Still failing:

| Test | Cause | Fix |
| --- | --- | --- |
| `test-misc` | `valuePointer`, `gettid` | not investigated |
| `test-sql` | needs a reachable MySQL/PostgreSQL | environment |

Verified, small, open (`BUGS`): `new Directory(path, { recursive })` zeroes the default `flags`/`mask`
(directory-options-object-zeroes-defaults); `test-typescript.js` is gated on `swc` although the loader
runs node (typescript-test-gated-on-swc-cli).

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
- `Intl` — English-only `lib/intl.js` (2026-10-21), the global from `lib/globals.js` and `lib/bun.js`; matches node's en-US output (`test-intl.js`). Missing: `Segmenter`, `DisplayNames`, `DurationFormat`, NumberFormat `style: 'unit'`, `formatRange`, IANA zones other than UTC and the local one, and `Number`/`Date.prototype.toLocaleString` and `String.prototype.localeCompare` going through it.
- `Worker` — no global; only QuickJS's own `os.Worker` exists (also see Tier 9.9).
- `lib/readline.js` and `lib/buffer.js` were removed in commit `958cffc9` as stubs; both are
  back as real implementations (`Buffer` and `readline`, 2026-10-07). `lib/perf_hooks.js` now has User
  Timing (`mark`, `measure`, entries, `PerformanceObserver`, 2026-10-08); missing: `timerify`,
  histograms, `monitorEventLoopDelay`, `eventLoopUtilization`.
- `lib/module.js` (Node's `node:module`) covers the whole export list; `register()`,
  `.node` addons and the compile cache are refused, see `doc/js/module.md`.

**WebAssembly: `.wasm` imports** *(investigated 2026-10-06)* — the `WebAssembly` global is
done (`lib/webassembly.js`, opt-in, with `*Streaming`); `.wasm` ESM import is done (`lib/loaders/wasm.js`, opt-in, instance phase only); wasm3 is the leading runtime candidate (see the findings below).

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
- Done 2026-10-21: `package.json` `"type"` and syntax detection pick CJS or ESM for a `.js` file
  (`lib/loaders/commonjs.js`), `require()` searches `node_modules` and gives Node's module objects
  for core names, and a package below `node_modules` gets Node's `util`/`events`/`buffer` by bare name.
- Open from the sweep (`BUGS`): `require()` ignores `"exports"`, no `.json` extension, no
  `require.resolve`, one shared `require()`, no file and line in CommonJS stack frames.
- No Node-style `crypto`/`zlib`/`dns`/`net`/`worker_threads`/`vm` builtins (`string_decoder` done 2026-10-21)
  — common Node imports fail outright (`tests/node-modules-sweep.sh` counts them).
- Node globals still missing under `lib/node.js` (2026-10-21, `doc/js/node.md`): `MessageChannel`, `BroadcastChannel`, the compression streams,
  `navigator`, `localStorage`, `crypto.subtle`, `URLPattern`.

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
qjs-modules-specific extras on the *same* module name Node scripts expect. (A `node:` name
Node does not have, such as `node:yaml` or `node:sqlite`, throws `ERR_UNKNOWN_BUILTIN_MODULE`
since 2026-10-21, as on Node and Bun.)

Extended 2026-09-22 to `node:os`/`node:readline` (not in the original list) and
cross-checked against Deno too (`-r qjsm -r bun -r deno`, all three loaded every module
in the original list cleanly except the two already-known `node:tty`/`node:yaml` cases -
Deno also errors `No such built-in module: node:yaml`, matching Bun):
- `node:readline`: done 2026-10-07/08 (`lib/readline.js`, `lib/readlinePromises.js`): line mode and
  `terminal: true` (editing, history, tab completion, `emitKeypressEvents`; expected bytes recorded from node). Still
  missing: kill-ring rotation, undo/redo, `readline/promises`' `Readline` class.

## Tier 8 — architecture cleanup (goal 3 dogfooding, code duplication)

- **`lib/css3-selectors.js` hand-rolls its own tokenizer**; rebuild it on the shared
  `lib/lexer`/`lib/parser/grammar.js` toolkit (goal 3). (`lib/css-selectors.js` and
  `lib/parsel.js` were removed 2026-10-21, no users.)
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
- Used only by `include/json/json.h` (which is used by `quickjs-json.c`)

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
  detection, builtin-module lookup (`package.json` aliasing left the loader on 2026-10-14), and filesystem
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
  `loaded_modules`, `jsm_disabled_names`, `jsm_builtin_depth`, `exename`/`exelen`, `jsm_rt`/`jsm_ctx`, `interactive`,
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
  `src/yread.c`/`include/yread.h`, the same pattern `src/json/jread.c`/`include/json/jread.h` (JSON) and
  `src/xml/xread.c`/`include/xml/xread.h` (XML) already use, rather than a recursive-descent parser
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
`src/json/jread.c`/`src/xml/xread.c` computed-goto push parsers or the pull-style primitives
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
| `jr_*` (`jread`) | `src/json/jread.c` | bytes → events | **push**, chunked, resumable mid-token, resyncs after errors | `jr_read(cb, chunk, len, ud, state)`, `jr_finish` | `jr_callback(JsonEventType, jr_str_t*, ud)` | code yes; `jread.h` includes `vector.h` → `quickjs.h` |
| `JsonParser` (`json.h`) | `src/json/json.c` | bytes → events | **pull**, `Reader`, `JSON_NEED_DATA` to resume, comments, `Location` | `Reader` (4 KiB-block buffered) | one `JsonEventType` per `json_parse()` | no: `json_init(…, JSContext*)`, `json_free(…, JSRuntime*)`, `Location` |
| `jwrite_*` (`JsonWriter`) | `src/json/jwrite.c` | events → bytes | **push** (caller calls `jwrite_object_start/key/int64/…`), `Writer` sink, backpressure via `Writer` returning 0 | event calls | bytes through `Writer`; `-JWRITE_E_*` | code yes; header includes `stream-utils.h`/`vector.h` → `quickjs.h` |
| `jbuild_*` (`JsonBuilder`) | `src/json/jbuilder.c` | events → JS value | push (`push/pop/key/value`), consumes `JsonEventType` | events | `JSValue` root | **no** (JSContext, JSValue) |
| `JwalkIterator` / `WalkInterface` | `src/json/jwalk.c` | JS value → events | **pull** (`jwalk_next()` = one event), or iface callbacks | `JSValue` | 6 callbacks (`key/object_start/array_start/object_end/array_end/value`) taking `JSContext*`, `JSValueConst` | **no** |
| `JSON.read` | `quickjs-json.c` | bytes → JSValue | whole-buffer, `sj` + `parse_val` frame stack | string/buffer | value | no |
| `JSON.write` | `quickjs-json.c` | JSValue → bytes | whole-value, own `PropertyEnumeration` stack + `Writer` | value | bytes/`Writer` | no |
| `JSONL.parse/parseChunk` | `quickjs-json.c` | bytes → values | chunk-resumable, own boundary scanner (`jsonl_value_end`) then `sj` | string/Uint8Array | values + `read/done/error` | no |
| `JsonPushParser` | `quickjs-json.c` | bytes → events/JSValue | push: `write(chunk)`, `close()`; `jr_read` → either `jbuild` (`.root`) or JS callbacks per `JsonEventType` | chunks | callbacks or `root` | no (wrapper of `jr_*` + `jbuild_*`) |
| `JsonParser` (JS class) | `quickjs-json.c` | bytes → events | pull, `Reader` from fd/buffer/function/`read()` method; `[Symbol.iterator]` | any `reader_from_js` source | `JsonEventType` ints (+`NEED_DATA`) | no (wrapper of `json.c`) |
| `JsonWriter` (JS class) | `quickjs-json.c` | JSValue/events → bytes | push; implements `WalkInterface` (`js_walk_register`) so a `JwalkIterator` can drive it | `Writer` (`writer_from_js`) | bytes | no (wrapper of `jwrite_*` + walk) |
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
| `JsonEventType` + `jr_str_t` | `jread.h` | error, null, true, false, number, string, array_start/end, object_start/end, key | decoded text (escapes resolved), number as raw text; no position, no comments |
| `JsonEventType` | `json.h` | OBJECT, OBJECT_END, ARRAY, ARRAY_END, KEY, STRING, TRUE, FALSE, NULL, NUMBER, COMMENT, + NEED_DATA/ERROR/RESYNC | token in `json->token`, position in `Location` |
| `WalkEvent` / `WalkInterface` | `jwalk.h` | key, object_start, array_start, object_end, array_end, value | `JSValue` (key and value), engine-bound |
| `jwrite_*` calls / `jbuild_*` calls | `jwrite.h`, `jbuilder.h` | the same six + null/bool/int64/double/string/raw | C scalars (jwrite) / `JsonEventType`+text (jbuild) |

Boundaries, where one layer hands over to the next:

```
 bytes ──Reader (pull)──► JsonParser/json_parse ──JsonEventType──┐
 bytes ──chunks (push)──► jr_read ──────────────jr_callback──────┼─► (sink)
 bytes ──whole buffer───► sj / JSONL / JSON.read ─► JSValue (no event stage)
 JSValue ──jwalk_next/WalkInterface──────────────────────────────┤
                                                                 ▼
 sinks: jbuild (→JSValue) · jwrite (→Writer→bytes) · JS callbacks (JsonPushParser)
```

### Where interoperability / adapting is possible (gaps)

Already connected: `jr_read → jbuild` (JsonPushParser), `walk → jwrite` (JsonWriter as
`WalkInterface`), `Reader → json_parse` (JsonParser).

| # | Adapter (source → sink) | Why / what is missing | Engine-free? |
| --- | --- | --- | --- |
| A1 | `jr_callback` → `jwrite_*` ("reformat"/minify/pretty/JSON5-ify while streaming) | no direct pump today; strings are already decoded by jr so `jwrite_string` re-escapes; numbers via `jwrite_number` | **yes** |
| A2 | `json_parse` (pull) → `jr_callback` (`json_pump(JsonParser*, jr_callback, ud)`) | map `JsonEventType`→`JsonEventType`; lets the `Reader`-based parser feed the same sinks as the chunk parser; comments need a new event | needs `json.c` decoupled from `JSContext` first |
| A3 | `Reader` → `jr_read` (`jr_pump(Reader*, cb, ud, state)`: read loop + `jr_finish`) | `jr_read` is chunk-only; every caller hand-writes the loop | **yes** |
| A4 | `JwalkIterator` → `jr_callback` (JS value → pure-C events) | one engine-bound adapter in `quickjs-json.c` emitting `jr_*` events; after it, jwrite/jbuild/JS-callbacks all consume value trees through the same sink instead of `WalkInterface` | adapter engine-bound, sink pure |
| A5 | `JsonSerializer` rebuilt on `JwalkIterator` + `JsonWriter` (**decided**, plan below) | removes the second serializer (own recursion, own writers, `skip`/`blocked` retry machinery); `jwalk_next()` is one event per call and resumable | no (walk is engine-bound) |
| A6 | `JSON.write` rebuilt on `walk` + `jwrite` | third copy of the same traversal; also gives `JSON.write` the JSON5 options | no |
| A7 | `JSONL.parse/parseChunk` on `jr_read` | own boundary scanner + `sj`; `jr_read` already resumes mid-token and resyncs per value | engine-bound result, pure scan |
| A8 | `jbuild` taking an allocator-agnostic tree (e.g. an arena of nodes) so `jr → tree → JSValue` | would let the whole read side stay pure C and convert once at the end | design question, not needed yet |
| A9 | JS: `JsonPushParser`/`JsonWriter` ↔ WHATWG streams (`TransformStream` of bytes→events, values→bytes) | `lib/jsonStreams.js` has Deno's shapes but is pure JS (`JSON.parse`/`stringify` per chunk), so it neither streams within a value nor uses the native stages | JS |

### Proposed direction

1. **Make `jr_callback(JsonEventType, jr_str_t*, void*)` the one pure-C event sink.** It already
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
value traversal that `include/json/jwalk.h` already provides (`json_serializer_step_inner` ≈
`jwalk_next` + `JsonWriter`), plus a retry mechanism (`skip`, `delivered`, `blocked`,
`CappedBuf`, `write_skip`) for a `Writer` that refuses bytes mid-event.

#### Target shape

```c
typedef struct {
  JSContext* ctx;
  JwalkIterator it;      /* the traversal; replaces `stack`, `is_primitive`, step_inner */
  JsonWriter wr;        /* the formatting; replaces sw_*, write_indent/primitive/string */
  DynBuf out;           /* wr.writer = writer_from_dynbuf(&out); never blocks */
  size_t out_pos;       /* bytes of `out` already handed to read() */
  JSValue root;         /* getter only; jwalk_close() clears it->root */
  Location* loc;
  unsigned error : 1;
} JsonSerializer;
```

Core loop (both `read(n)` and `read(buffer)`): `while(avail < want && (r = jwalk_next(&it, ctx)) > 0) {}`
with `avail = out.size - out_pos`; `r < 0` → `error`, `jwalk_close()`, return the exception as
today; then take `n` characters (existing `utf8_strlen`/`utf8_byteoffset` code) or copy up to
`cap` bytes into the buffer and keep the remainder in `out`.

Why this works without a blocking writer: `jwalk_next()` does not retry an event (state is
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
| `error` | `error` | set when `jwalk_next()` or `jwrite_*` fails |
| `blocked` | removed / constant `false` | no blocking stage left; check usage before deleting (only `quickjs-json.c` found; tests do not read it) |
| `indent` get/set | `wr.opts.indent` | the setter may run mid-stream; `jwrite` reads it per event |
| `is_primitive` | gone | `jwalk_next()` reports a primitive root as one `value` event |
| `write_skip`, `write_capped`, `CappedBuf`, `skip`, `delivered`, `skip_writer`, `dest_writer` | deleted | retry machinery |
| `sw_putc/sw_indent/sw_string/sw_primitive/sw_puts` | deleted | `jwrite_*` via `js_jsonwriter_walk` |
| `json_serializer_step[_inner]` | deleted | `jwalk_next()` |
| `location` | unchanged | `location_count(loc, bytes, n)` on the bytes handed out |

`js_jsonwriter_walk()` is defined after the serializer; add a forward declaration (or move
the serializer block below the `JsonWriter` block).

#### Behaviour differences to decide before coding

| Case | Serializer today | Walk + JsonWriter |
| --- | --- | --- |
| circular reference | falls through to the primitive writer: the object's `toString` string | `null` (`jwalk_next` substitutes `JS_NULL`) — matches the doc comment in `jwalk.c`; pick `null` |
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
   → `jwalk_init(&js->it, ctx, root, js_jsonwriter_walk(&js->wr))`; `jwrite_init(&js->wr, …)`
   with `wr.writer = writer_from_dynbuf(&js->out)`, `wr.opts.indent = indent`.
3. **Rewrite `js_jsonserializer_read`** around the loop above; unify the `n` and `buffer`
   branches on the shared `out` buffer; keep the return values (`JS_NewInt64(count)` for a
   buffer, string or `undefined` at EOF for a count — see existing tests 543-650).
4. **Rewrite getters/setter/constructor/finalizer** per the mapping table; the finalizer
   calls `jwalk_close(&js->it, ctx)` (needs a context: use `js->ctx`, or free through
   `property_recursion_free` + `JS_FreeValueRT` with the runtime as `jwalk_close` does) and
   `jwrite_free(&js->wr)`.
5. **Delete what became unused** in `quickjs-json.c`: `write_capped`, `write_skip`,
   `sw_*`, `json_serializer_step*`, `CappedBuf`. Keep `write_json_primitive`,
   `write_json_string`, `write_indent`, `write_push` until `JSON.write` (A6) moves too —
   `grep` shows they are still used by `js_json_write` (l.369-460).
6. **Verify**: `qjsm tests/unittests/test-json.js`, the new differential cases, and
   `utilities/jsonpp.js` against a few files; ASan run once (frames are freed in
   `jwalk_close` on every path: done, error, finalizer mid-walk).
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

### Tier 14 follow-ups (2026-10-14)

- **`JsonWriter` raw-number write.** `utilities/jsonpp.js` normalizes floats and
  exponents (`1e3` becomes `1000`, `1.50` becomes `1.5`) because JS cannot hand
  `jwrite_number()` the token text; only plain integers pass through (via `BigInt`).
  Exact pass-through is the recommendation in `doc/native/json.md`, but no
  Bun/Deno/Node counterpart exists, so only if `jsonpp` needs it.
- **`jsonpp --write` is not streaming.** It buffers the whole output so a file that
  fails to parse is left alone; streaming to the temp file and renaming at the end
  gives the same safety with bounded memory.
- **`jsonpp` writes a value together with its following comma**, so output lags one
  element behind the input (matters for `tail -f` use).
- **Decide `JsonSerializer.read()` EOF value and `JsonWriter` default spacing:** see
  `BUGS` (`jsonserializer-read-eof-returns-undefined`, `jsonwriter-default-output-vs-test`).

## Tier 15 — PoC: streaming JSON transformer, pure-C call chain, no tree

> API now specified by Tier 17 (`JsonRewriter`, `jrewrite_*`): this tier's rules become hooks/options of it and its
> tests become the rewriter's test suite.

Proves Tier 14's adapter A1 (`jr_callback` → `jwrite_*`) with real filtering logic, as a
test: bytes in → `jr_read` → **filter callback** → `jwrite_*` → `Writer` → bytes out. No
tree is built and nothing crosses C → JS → C: the whole chain is plain function calls
inside one `jr_read()` call.

```
 chunk ──► jr_read(cb = jrewrite_event, ud = JsonRewriter*, &jr_state)
              │ per token: (JsonEventType, jr_str_t*)
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
  principles), `JsonBuilder`/`JwalkIterator` (both engine-bound), schema validation.

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

| `JsonEventType` | Normal | While `skip > 0` | Error |
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

Possible now because `jwrite` mirrors `JsonEventType` one to one (error, null, true, false, number,
string, array_start/end, object_start/end, key), so input and output speak the same eleven events
and the dispatcher is a plain `switch`.

### API (`include/jrewrite.h`, `src/jrewrite.c`; pure C, libc allocation)

```c
typedef struct JsonRewriter JsonRewriter;

typedef enum { JREWRITE_PASS = 0, JREWRITE_DROP, JREWRITE_STOP } JsonRewriteAction;
typedef enum { JREWRITE_DEEP_EMPTY, JREWRITE_DEEP_NULL, JREWRITE_DEEP_DROP } JsonRewriteDeep;

/* what a hook sees: one *member* (key + value, or a bare array element / root value). */
typedef struct {
  JsonEventType type;               /* null true false number string array_start object_start; never key/end/error */
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

void jrewrite_event(JsonRewriter*, JsonEventType, const char* str, size_t len);   /* the dispatcher */
void jrewrite_callback(JsonEventType, const jr_str_t*, void* ud);                  /* a jr_callback, ud = rewriter */
JsonWriteInterface jrewrite_interface(JsonRewriter*);                         /* as a sink: chainable */

/* pull: runs json_parse() of include/json/json.h until it stops, feeding jrewrite_event().
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
| `json_parse()` returns one `JsonEventType` per call; string/key/number/comment text is in `json->token` | `json.h`, `json.c` | the pull side maps the type and passes `(json->token.buf, .size)` to `jrewrite_event()` |
| strings and keys are **decoded** in the token, numbers are raw text | `json.c` ~l.273/315 | same data as `jr_str_t`; `jwrite_string` re-escapes, `jwrite_number` keeps numbers verbatim |
| clean EOF and "ran out mid-value" both return `JSON_NEED_DATA` (reader `0` → `NEED_DATA`, `<0` → `JSON_ERROR`) | `json.c` l.222-226 | `jrewrite_pull` classifies it: `json->stack.len == 0 && tok_kind == JSON_TOK_NONE` is a clean end, else truncated |
| `JSON_ERROR` sets `json->error`; the parser resyncs by itself on the next call | `json.c` | the pump stops at the first error; continuing would feed events the writer's frame stack cannot match |
| `JSON_EVENT_COMMENT` only appears with `json->comments` set | `json.h` | `jwrite` has no comment event: dropped |
| `jr_callback` returns `void`; `jr_read()` cannot be stopped from a callback | `jread.h` | the rewriter keeps a sticky error and a `stopped` flag; the caller stops feeding |
| `jwrite_put()` writes **one byte at a time** through the `Writer` | `jwrite.c` | a JS-backed `Writer` (`writer_from_jsfunction`/`jsstream`) would run one JS call per output byte: wrap it with `writer_buffered()` |
| `js_jsonwriter_constructor` calls `writer_from_js()` and does not wrap the result | `quickjs-json.c` ~l.1696 | verify whether `writer_from_js` buffers; if not, add `writer_buffered` there |
| `json_init()` needs a `JSContext` (`dbuf_init_ctx`, `location_new(ctx)`); `json_getc()` calls `location_nextchar(json->loc, c)` unguarded | `json.c` l.109-141, l.205 | `jrewrite_pull` takes an initialised parser and uses no ctx, but building a parser still needs one until a ctx-free init exists (step 8) |

Type map (the only table, in `jrewrite_event`; the pull side translates first):

| `JsonEventType` | `JsonEventType` (pull) | `jwrite` call |
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

## Tier 14 — qjsm closed builtins: follow-ups (2026-10-14)

Builtins are closed (`doc/qjsm-layout.md` 4.1). Done 2026-10-14: `disableBuiltin()`,
`lib/loaders/trace.js`, computed-`import()` report at configure time, `vfs` loads `archive`
lazily, `QJSM_INSTALL_BUILTIN_NATIVE`, `manifest.json`, build-info blob (`--version`,
`--build-info`, `buildInfo`, `Module.layout/builtins/bytecode/installed/manifest/verify`).
What is left:

- `cmake --install --prefix X` relocates only `bin/` and `share/`; native and JS modules go to
  the absolute `QUICKJS_C_MODULE_DIR`/`QUICKJS_JS_MODULE_DIR` (`qjsm-layout.md` 3.2).
- Generated `package.json` / import maps for Node, Bun, Deno (`qjsm-layout.md` 7).
- Configure snapshot baseline not regenerated (no stored baseline found in the tree).
- ctest: 13 of 138 fail (arraybuffer-sink, buffer, directory, extract-c, globals, json,
  lexer-cpp, misc, mmap, node-child-process, node-util, sql, util-node); unrelated to the
  builtin work, each needs its own look.

## Tier 15 — PoC: streaming JSON transformer, pure-C call chain, no tree

> API now specified by Tier 17 (`JsonRewriter`, `jrewrite_*`): this tier's rules become hooks/options of it and its
> tests become the rewriter's test suite.

Proves Tier 14's adapter A1 (`jr_callback` → `jwrite_*`) with real filtering logic, as a
test: bytes in → `jr_read` → **filter callback** → `jwrite_*` → `Writer` → bytes out. No
tree is built and nothing crosses C → JS → C: the whole chain is plain function calls
inside one `jr_read()` call.

```
 chunk ──► jr_read(cb = jrewrite_event, ud = JsonRewriter*, &jr_state)
              │ per token: (JsonEventType, jr_str_t*)
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
  principles), `JsonBuilder`/`JwalkIterator` (both engine-bound), schema validation.

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

| `JsonEventType` | Normal | While `skip > 0` | Error |
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

Possible now because `jwrite` mirrors `JsonEventType` one to one (error, null, true, false, number,
string, array_start/end, object_start/end, key), so input and output speak the same eleven events
and the dispatcher is a plain `switch`.

### API (`include/jrewrite.h`, `src/jrewrite.c`; pure C, libc allocation)

```c
typedef struct JsonRewriter JsonRewriter;

typedef enum { JREWRITE_PASS = 0, JREWRITE_DROP, JREWRITE_STOP } JsonRewriteAction;
typedef enum { JREWRITE_DEEP_EMPTY, JREWRITE_DEEP_NULL, JREWRITE_DEEP_DROP } JsonRewriteDeep;

/* what a hook sees: one *member* (key + value, or a bare array element / root value). */
typedef struct {
  JsonEventType type;               /* null true false number string array_start object_start; never key/end/error */
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

void jrewrite_event(JsonRewriter*, JsonEventType, const char* str, size_t len);   /* the dispatcher */
void jrewrite_callback(JsonEventType, const jr_str_t*, void* ud);                  /* a jr_callback, ud = rewriter */
JsonWriteInterface jrewrite_interface(JsonRewriter*);                         /* as a sink: chainable */

/* pull: runs json_parse() of include/json/json.h until it stops, feeding jrewrite_event().
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
| `json_parse()` returns one `JsonEventType` per call; string/key/number/comment text is in `json->token` | `json.h`, `json.c` | the pull side maps the type and passes `(json->token.buf, .size)` to `jrewrite_event()` |
| strings and keys are **decoded** in the token, numbers are raw text | `json.c` ~l.273/315 | same data as `jr_str_t`; `jwrite_string` re-escapes, `jwrite_number` keeps numbers verbatim |
| clean EOF and "ran out mid-value" both return `JSON_NEED_DATA` (reader `0` → `NEED_DATA`, `<0` → `JSON_ERROR`) | `json.c` l.222-226 | `jrewrite_pull` classifies it: `json->stack.len == 0 && tok_kind == JSON_TOK_NONE` is a clean end, else truncated |
| `JSON_ERROR` sets `json->error`; the parser resyncs by itself on the next call | `json.c` | the pump stops at the first error; continuing would feed events the writer's frame stack cannot match |
| `JSON_EVENT_COMMENT` only appears with `json->comments` set | `json.h` | `jwrite` has no comment event: dropped |
| `jr_callback` returns `void`; `jr_read()` cannot be stopped from a callback | `jread.h` | the rewriter keeps a sticky error and a `stopped` flag; the caller stops feeding |
| `jwrite_put()` writes **one byte at a time** through the `Writer` | `jwrite.c` | a JS-backed `Writer` (`writer_from_jsfunction`/`jsstream`) would run one JS call per output byte: wrap it with `writer_buffered()` |
| `js_jsonwriter_constructor` calls `writer_from_js()` and does not wrap the result | `quickjs-json.c` ~l.1696 | verify whether `writer_from_js` buffers; if not, add `writer_buffered` there |
| `json_init()` needs a `JSContext` (`dbuf_init_ctx`, `location_new(ctx)`); `json_getc()` calls `location_nextchar(json->loc, c)` unguarded | `json.c` l.109-141, l.205 | `jrewrite_pull` takes an initialised parser and uses no ctx, but building a parser still needs one until a ctx-free init exists (step 8) |

Type map (the only table, in `jrewrite_event`; the pull side translates first):

| `JsonEventType` | `JsonEventType` (pull) | `jwrite` call |
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

## Tier 14 — qjsm closed builtins: follow-ups (2026-10-14)

Builtins are closed (`doc/qjsm-layout.md` 4.1); what is left:

- Run the full `ctest` suite and regenerate the configure snapshot baseline (disk was full).
- JS API to disable a builtin at run time (today: `--disable-builtin`, `QJSM_DISABLE_BUILTINS`).
- A ready-made JS tracing hook for `registerHooks()` (the C `DEBUG=modules` trace stays).
- `qjsm_scan_imports` only sees literal specifiers; a computed `import(x)` inside a builtin
  is not followed (build-time) and not restricted (run time).
- `QJSM_INSTALL_BUILTIN_NATIVE` / manifest / build-info blob: see `doc/qjsm-layout.md` 3-6.
- Default build now links libarchive (`vfs` -> `archive`); `vfs` depends on it softly
  (`ArchiveProvider` loads it on first use).

## Tier 15 — Bun compatibility layer: `Bun.*` in `lib/bun.js` (planned 2026-10-14)

`lib/bun.js` is a compiled builtin (`bun`, default on) and `qjsm` installs its default
export as `globalThis.Bun`. Tests live in `tests/unittests/test-bun*.js`; they use the
`Bun` global when the builtin is compiled in and import `../../lib/bun.js` otherwise
(check both: `QJSM_DISABLE_BUILTINS=bun qjsm tests/unittests/test-bun-file.js`).
Rules: copy Bun's names, argument shapes, return values and error fields (probe `bun -e`
first, per "Survey the other runtimes"); every phase adds a `test-bun-<area>.js`, a section
in `doc/js/bun.md`, and a line in the `compatibility: Bun` table in `cmake/qjsm.cmake`.
`Bun` is not a runtime-detection signal (`typeof Bun` is `'object'` here).

Phases are sorted by leverage (CLAUDE.md: lift an own API to a standard one, then
cheap wrappers over code that already exists, then new engine work).

### Phase 1 — `Bun.file`, `Bun.write`, stdio (DONE 2026-10-14)

`Bun.file(path | URL | fd, {type?})` → lazy `Blob` subclass: `name`, `type` (by extension),
`size`, `lastModified`, `text/json/arrayBuffer/bytes/stream/slice/exists/delete/unlink/writer`.
`Bun.write(path | URL | fd | BunFile, string | binary | Blob | BunFile)`, `Bun.stdin/stdout/stderr`,
`FileSink` (`write/flush/end/ref/unref`). Known gaps: `formData()`, `Bun.file(...).write()`-style
S3 methods, `Bun.write(dest, Response)` only via a duck-typed `arrayBuffer()`, `size` of a
slice is clamped to the file (Bun reports `end - start`), `FileSink.start()`, `fs.watch`-based
`Bun.file(...).watch`, and `Bun.write` of a sliced BunFile writes the whole file path.

### Phase 2 — lift own modules to their Bun names (highest leverage, code already exists)

| API | Backed by | Note |
| --- | --- | --- |
| `Bun.password.hash/verify(+Sync)` | `lib/password.js` (bcrypt) | the module already follows Bun's shape; re-export, add the `argon2` error |
| `Bun.JSONL.parse/parseChunk` | `json` native | done in the module; only the alias is missing |
| `Bun.ArrayBufferSink` | `arraybuffer-sink` native | same shape as Bun's |
| `Bun.deepEquals(a, b, strict?)`, `Bun.deepMatch` | `deep` | `deep` is internal; this gives it a standard name |
| `Bun.inspect(v, opts)` | `inspect` | `Bun.inspect.custom`, `Bun.inspect.table` later |
| `Bun.mmap(path)` | `mmap` | returns `Uint8Array`; Bun's `shared`/`sync` options |
| `Bun.Glob` (`scan`, `scanSync`, `match`) | `fs` glob / `path.fnmatch` | |
| `Bun.XML` / `Bun.YAML` / `Bun.TOML` / `Bun.JSON5` / `Bun.JSONC` `.parse` | `xml`, `yaml` (Tier 12/13), new parsers | XML and YAML first, the other three only if a user shows up |

### Phase 3 — process and environment (cheap, pure wrappers)

`Bun.env`, `Bun.argv`, `Bun.main`, `Bun.version`, `Bun.revision`, `Bun.sleep(ms | Date)`,
`Bun.sleepSync`, `Bun.nanoseconds`, `Bun.which(cmd, {PATH, cwd})`, `Bun.pathToFileURL`,
`Bun.fileURLToPath`, `Bun.resolveSync/resolve`, `Bun.isMainThread`, `Bun.gc`, `Bun.peek`,
`Bun.escapeHTML`, `Bun.stripANSI`, `Bun.stringWidth`, `Bun.concatArrayBuffers`,
`Bun.randomUUIDv7`, `Bun.indexOfLine`, `Bun.semver`, `Bun.color`, `Bun.enableANSIColors`,
`Bun.readableStreamTo{Text,JSON,Bytes,ArrayBuffer,Blob,Array}`.

### Phase 4 — `Bun.spawn` / `Bun.spawnSync` and `Bun.$`

On `child_process`: `{ cmd, cwd, env, stdin/stdout/stderr: 'pipe' | 'inherit' | 'ignore' | BunFile | fd,
onExit }` → `Subprocess` (`pid`, `exited`, `exitCode`, `signalCode`, `kill()`, `stdin` as
`FileSink`, `stdout`/`stderr` as `ReadableStream`); `spawnSync` → `{ stdout, stderr, exitCode, success }`.
`Bun.$` (shell tagged template) only after this works. Needs the stream plumbing of
`BunFile.stream()` and `FileSink` from phase 1.

### Phase 5 — hashing and compression

`Bun.hash` (wyhash, crc32, adler32, cityHash32/64, murmur...), `Bun.CryptoHasher`,
`Bun.SHA1/SHA256/SHA512...`, `Bun.sha`, `Bun.gzipSync/gunzipSync/deflateSync/inflateSync`
(+ zstd). Check first what `archive`/`bcrypt` already link (zlib?) before adding a dependency.

### Phase 6 — databases

`Bun.sql` / `Bun.SQL` (tagged template) over `sqlite`, `pgsql`, `mysql`; `Bun.SQL` option
`adapter`. Needs a design pass: the three native modules have different result shapes.

### Out of scope here (sibling projects or no standard target)

| API | Where |
| --- | --- |
| `Bun.serve`, `Bun.fetch`, `Bun.connect`, `Bun.listen`, `Bun.udpSocket`, `Bun.WebSocket` | `../qjs-lws/` (`lib/serve.js`, `lib/tcpsocket.js`, `lib/udpsocket.js` already target these names; wire `Bun.*` there, not here) |
| `Bun.FFI` / `bun:ffi` | `../qjs-ffi/` |
| `Bun.Transpiler`, `Bun.build`, `Bun.Image` | needs a transpiler/bundler/renderer the engine does not have (`Bun.markdown`: Tier 25) |
| `Bun.S3Client`, `Bun.s3`, `Bun.redis`, `Bun.RedisClient`, `Bun.secrets`, `Bun.Cookie/CookieMap`, `Bun.CSRF`, `Bun.cron`, `Bun.WebView`, `Bun.Terminal`, `Bun.FileSystemRouter`, `Bun.embeddedFiles`, `Bun.openInEditor`, `Bun.generateHeapSnapshot`, `Bun.shrink`, `Bun.unsafe` | no user, no standard target; decide only when a script asks |

## Event model: `json` and `xml` alike (plan 2026-10-17)

Rule: every JSON concept has an XML twin with the same shape, and the code of one is written so the other can be read off it.
`include/json/jevent.h` mirrors `include/xml/xevent.h`; `JsonWriteInterface` becomes **`JsonEventSink`**, `XmlEventSink` is its twin.
Both are tables of function pointers with `opaque` first; a producer drives any sink without knowing what it is.

| concept | JSON (`include/json/`) | XML (`include/xml/`) | state |
| --- | --- | --- | --- |
| event kinds | `JsonEventType` (`error, null, true, false, number, string, array_start, array_end, object_start, object_end, key`) | `XmlEventType` (`error, element_start, attribute, text, element_end`, plus `comment, declaration, instruction, cdata`); `xml_event_t` ids are a prefix of it | J: exists; X: Phase 1 |
| one event | `JsonEvent { type, str, len, SourcePos at }` (`jr` fills `err_*` already) | `XmlEvent { type, name, value, has_value, SourcePos at }` | new, both |
| sink | `JsonEventSink` (rename of `JsonWriteInterface`; members unchanged: `error, null, true_, false_, number, string, array_start, array_end, object_start, object_end, key`) | `XmlEventSink` (`error, element_start, attribute, text, element_end, comment, declaration, instruction, cdata`) | J: rename + `typedef` alias for one release; X: new |
| sink constructors | `jwrite_sink(&JsonWriter)`, `jbuild_sink(&JsonBuilder)`, `jrewrite_sink(&JsonRewriter)` (were `jwrite_interface`, planned `jrewrite_interface`) | `xwrite_sink`, `xbuild_sink`, `xrewrite_sink` | rename J; new X |
| pump (source drives sink) | `jread_pump` (push `jr` state to a sink), `json_pump(JsonParser*, sink)` (pull) | `xread_pump`, `xml_pump(XMLParser*, sink)`; `xwalk_pump` | new, both; `jwalk` already drives a sink |
| writer | `JsonWriter` (`jwrite_*`) | `XmlWriter` (`xwrite_*`) | exists |
| builder | `JsonBuilder` (`jbuild_*`) | `XmlBuilder` (`xbuild_*`), layered on `jbuilder` | J exists; X Phase 2 |
| tree walker | `walk` + `jwalk` hooks | `walk` + `xwalk` hooks | exists |
| rewriter (filter) | `JsonRewriter` (Tier 17) | `XmlRewriter` (Phase 8): both own a downstream sink and are sinks | planned |
| error report | `err_code`, `err_pos`, `err_len` in `jr_state_t`; `JsonParser.err_*` | same names in `xr_state_t` (`xread`), `XMLParser.err_*` | done (Tier 23 E1c) |
| position | `SourcePos` / `Location` | same | done |
| strings | valid until the next event | same | rule, both |
| failure value of a sink call | bytes written (>= 0) or `-J*_E_*` | bytes written (>= 0) or `-X*_E_*` | J exists; X exists in `xwrite` |

JS classes follow the same table (names kept: `Json*` has no web twin, `XML*` follows `XMLSerializer`/`XMLHttpRequest`):

| role | JSON | XML | alike already? |
| --- | --- | --- | --- |
| one-shot read | `json.read` / `JSON.parse` | `xml.read` | yes (errors: Tier 23) |
| pull parser | `JsonParser` | `XMLParser`, `XMLNodeParser` | members `location`, `depth`, `error`; token ids differ (to unify with `JsonEvent`/`XmlEvent` types) |
| push parser | `JsonPushParser` (`onError`) | `XMLPushParser` (`onParseError`) | callback names differ: decide one (recommended `onError` for both, `onParseError` an alias on XML since parse5 and the DOM use it) |
| writer | `JsonWriter` | `XMLWriter` | methods differ in kind only (`objectStart`/`elementStart`) |
| serializer | `JsonSerializer` | `XMLTreeSerializer` | both `read(n)`; renamed so `XMLSerializer` stays free for the DOM class |
| builder result | `.root`, `.path` | `.root`, `.path` | yes |

Steps (each keeps ctest as it is; J = json, X = xml; both after Phase M):

| # | step | gate |
| --- | --- | --- |
| EV1 (done) | `include/json/jevent.h`: `JsonEvent`, `JsonEventSink`; `JsonWriteInterface` kept as `typedef JsonEventSink`; `jwrite_sink()` next to `jwrite_interface()` | done 2026-10-17: `jevent.h` (`JsonEvent`, `JsonEventSink`, `jevent_send` in `src/json/jevent.c`); `jwrite_sink()`, `JsonWriteInterface` and `jwrite_interface()` kept as aliases; `tests/c/events-sinks.c` (ctest `c-events-sinks`) drives both writers through `jevent_send`/`xevent_send` |
| EV2 (done) | `include/xml/xevent.h`: `XmlEvent`, `XmlEventSink`, `xwrite_sink()` | done 2026-10-17: `xevent.h` (`XmlEventType` whose ids equal `xml_event_t`'s, checked by `_Static_assert` in `src/xml/xevent.c`; `XmlEvent`, `XmlEventSink`, `xevent_send`), `xwrite_sink()` (`error` member writes nothing); `XE_COMMENT/DECLARATION/INSTRUCTION/CDATA` have no parser producing them yet |
| EV3 (done) | `jbuild_sink()` / `xbuild_sink()` (Phase 2 `xbuilder`); the push parsers feed the sink instead of calling `jbuild_*` / `xml_builder_*` | done 2026-10-17: `jbuild_sink()` in `jbuilder`; `include/xml/xbuilder.h` + `src/xml/xbuilder.c` (`XmlBuilder` on `JsonBuilder`: `xbuild_*`, `xbuild_sink()`, no frame stack of its own); `JsonPushParser`, `XMLPushParser` and `XMLParser` feed their builder through `jevent_send`/`xevent_send`; the old `XMLBuilder` (140 lines, `flat` mode was never set) is gone; `.root` and `.path` shapes unchanged, no test changed. Then (same day) `read()` moved onto it too: `js_xml_parse` is 70 lines of event loop over `xbuild_*` (`XB_PLAIN` objects with `Object.prototype`, `XB_FLAT` lists, `xbuild_last()` for the location WeakMap, `xbuild_finish()` for input that ends inside elements); the element shape is `read()`'s for every user, so `XMLParser.root` and `XMLPushParser.root` now give no `children` on `<a/>` and none or `attributes` on comments/declarations (no test changed); checked against the previous `read()` with a temporary copy: 28070 runs (4010 documents, 7 option sets incl. flat, tolerant, location, selfClosingTags) with 0 differences. Still open from Phase 2: `XMLNodeParser` assembly is not on `xbuilder` yet; typed markup events are dropped by `xbuild_sink` until the parsers emit them |
| EV4 (done) | `jread_pump` / `json_pump`, `xread_pump` / `xml_pump`: each parser can drive any sink; `quickjs-json.c` and `quickjs-xml.c` choose source, sink, `Reader`, `Writer` only | done 2026-10-17: `jread_pump`, `jread_pump_finish`, `json_pump` (`src/json/jpump.c`, declared in `jevent.h`) and `xread_pump`, `xread_pump_finish`, `xml_pump` (`src/xml/xpump.c`, in `xevent.h`); `tests/c/pump-roundtrip.c` (ctest `c-pump-roundtrip`): all four parsers into the writers at chunk sizes 1, 4, 7, 1000 and whole, a JSON error event, a failing sink stopping `xml_pump`. Limits found: sink members carry no position (`JsonEvent.at` only exists for `jevent_send` callers; the position stays in `jr_state_t.err_*` / `JsonParser.err_at` / `XMLParser.tag_line`), and the pull `JsonParser` cannot tell the end of its input from a pause (`json_pump` returns `JSON_PUMP_AGAIN` at both). Also fixed: `json_clear()` leaked the container stack (`BitSet`); the bindings are not on the pumps yet (next: `JsonPushParser`/`XMLPushParser` call `*_pump` instead of their own callbacks) |
| EV5 (superseded by P4 below) | `xwalk` and `jwalk` drive an `XmlEventSink` / `JsonEventSink` (`WalkInterface` becomes the engine-side adapter of it) | `write()`, `XMLTreeSerializer` byte-identical |
| EV6 | rename `jwrite_interface` and delete the alias after one release; align the push-parser callback names; `JsonRewriter` / `XmlRewriter` on sinks | per Tier 17 / Phase 8 |
| EV7 (done) | JS wrappers of the sinks, twins of `js_walk_wrap/define/register`: `js_xmlsink_*` (`include/xml/xsink.h`) and `js_jsonsink_*` (`include/json/jsink.h`); `XMLWriter` is decorated with the `XmlEventSink` methods over `xwrite` (new: `comment`, `declaration`, `instruction`, `cdata`, `error`) | done 2026-10-17: `test-xml.js` 161; the JSON wrapper compiles, nothing uses it yet |
| EV8 (superseded by P6 below) | JS objects as sinks for the C pumps: the handshake `Symbol.for('qjs.jsonsink')` / `Symbol.for('qjs.xmlsink')` (an object answering with its sink), a tee sink; `JsonWriter` on `js_jsonsink_*` (its methods take JS values, so decide first whether it keeps its own set) | `XMLPushParser` / `JsonPushParser` can be plugged into any writer, builder or tee |

Notes: `JsonEvent.at` is filled only when the source tracks a position (`jr`, `xr` with `SourcePos`); a sink must not require it.
`WalkInterface` stays engine-bound (`JSContext`, `JSValue`); the engine-free `*EventSink` is the one the C-only couplings use.
Tier 18 and Tier 20 refer here for everything about events; their own "event protocol" paragraphs are superseded where they differ.

## Event model v2: pull and push events, engines in C, APIs in `lib/` (plan 2026-10-18)

Design and reasons: [doc/api-event-model.md](doc/api-event-model.md); interfaces: `doc/api-interface-{json,xml}.md` (D9 to D11 / D10 to D13).
State 2026-10-18 (end of day): P1 to P8, P11, P12, the lift, P16 and the JS side of P13 are done and pushed; `quickjs-json.c` is 1412 and `quickjs-xml.c` 1339 lines (was 1962 and 2156), what is left in them is classes, constructors, getters, finalizers and the push parsers' error rules. Running state: `doc/event-model-state.md`. Open: P9 (`lib/` glue except `lib/jsonl.js`), P10 (`api-surface/` regeneration), the C side of P13, P14, P15, and the list under "Open" below.

Rules (the whole plan in six lines):

1. every class of `quickjs-json.c` / `quickjs-xml.c` is a source or a sink, pull or push, over `JsonEvent` / `XmlEvent`;
2. a pull parser fills a caller's event (`json_next(p, &ev)`, `xml_next(p, &ev)`), a push parser calls a handler with the same event, a walker is a pull source;
3. a sink is `JsonEventSink` / `XmlEventSink` (writer, builder, JS object, tee, rewriter); the serializer is the pull sink: source + writer + `DynBuf`, stepped by `read(n)`;
4. a source reaches a sink in C: `source.pump(sink)`, `pushParser.connect(sink)`, found through `Symbol.for('qjs.jsonsink')` / `qjs.xmlsink` / `qjs.jsonsource` / `qjs.xmlsource`;
5. the native modules export engines only; `JSONL`, `JSON5`, `JSONC`, `read`, `write`, `DOMParser`, `XMLSerializer`, `XML.*`, `Blob.json()`, the `@std/json` streams are compositions in `lib/`;
6. a dialect is a name given to the parser, not a parallel entry point.

| # | step | size | depends on | gate |
| --- | --- | --- | --- | --- |
| P1 (done) | events: `JsonEventSink.comment` and `.raw`; `XmlEvent` `str` / `len` aliases of `name` / `nlen`; `JsonPull` / `XmlPull` enums; `JsonEventHandler` / `XmlEventHandler`; sink wrappers (`jsink.c`, `xsink.c`) and writers get the new members | small | none | `tests/c/events-sinks.c` |
| P2 (done) | `json_next(p, &ev)`, `xml_next(p, &ev)` with `at` always set; errors are events; typed markup events from the XML parsers (B4); `jevent_pump` / `xml_pump` are the loop; `JsonParser`, `XMLParser`, `XMLNodeParser` classes read events, private token ids go; `Q4` measured against `jr_read` throughput | medium | P1 | `pump-roundtrip.c`, `test-json.js`, `test-xml.js` unchanged |
| P3 (done) | push parsers call a handler with the event (`jr_read`, `xr_read`); `jr_callback`, `jr_str_t`, `xr_callback`, `xr_str_t`, `jread_pump`, `xread_pump` removed; decide Q5 (`xread` kept or deleted) | medium | P1 | chunk sizes 1, 2, 7, 1000 byte-identical |
| P4 (done) | `jwalk_event`, `xwalk_event`: walkers as pull sources; the tag / attributes / children interpretation of `xtreewrite.c` moves into `xwalk`; `xtreewrite` becomes a pump into `xwrite`; replaces EV5 / B1 | medium | P1 | `write()` golden and `JsonSerializer` byte-identical |
| P5 (done) | demand-driven `JsonSerializer` / `XMLTreeSerializer` over any pull source (a value or a parser); replay (`skip`, `delivered`, `capped`) deleted; `readText(n)` on character boundaries; replaces X2 | medium | P4 | equality with `write()` at read sizes 1, 7, 4096; a `JsonParser` source reformats |
| P6 (done) | handshake symbols, `source.pump(sink)`, `pushParser.connect(sink)`, JS object to C sink adapter, `tee`; replaces EV8, B3, X4, J4, J5 | medium | P1 | a push parser plugged into a writer, a builder and a tee; a JS-object sink gets the same events |
| P7 (done) | builders: `proto` option, queue of completed roots with end positions (`take()`), `jbuild_value_js()`, flat `XmlBuilder` for `XMLNodeParser` (X3, B6) | medium | P1 | differential 0, `test-xml.js` |
| P8 (done) | parser options: dialect names in the `JsonParser` constructor (`'json'`, `'jsonc'`, `'json5'`, `'jsonl'` over the `JR_*` flags), strict mode (J1 step 2), one `xml_options_from_js()`, `strict`, `entities` | medium | P2 | `test-json-dialects`, `tests/unittests/test-xml-errors.js` |
| P9 (partly: `lib/jsonl.js`, `lib/json5.js`, `lib/jsonc.js`, `Blob.json()` on the strict reader, C `read`/`write` and `sj.h` removed 2026-10-22) | the glue: `lib/jsonl.js`, `lib/json5.js`, `lib/jsonc.js`, `lib/bun.js` (`Bun.JSONL`, `JSON5`, `JSONC`, `XML`), `lib/jsonStreams.js` on the engines, `lib/xml.js` (`read`, `write`), `lib/dom.js` (`DOMParser`, `XMLSerializer`), `Blob.json()`; then C `JSONL`, `read`, `write` deprecated and deleted (J1, J2, J7, X7, X8 phase 9) | large | P2 to P8 | Bun / Browser / Deno differential batteries, `portable-check.sh` |
| P10 | clean-up: duplicated token ids, `property-enumeration.h` and `char-utils.h` out of the bindings, `api-surface/` regenerated, `doc/native/{json,xml}.md` rewritten to the engine classes | small | P9 | line counts in `doc/api-implement-{json,xml}.md` |

Steps after the bindings are lifted (what moves into `include/` and `src/`, what becomes private, what merges: `doc/api-event-model.md` 12;
the file-by-file state of P1 to P8 is `doc/event-model-state.md`). P11 and P12 come before the lift because they make it small.

| # | step | size | depends on | gate |
| --- | --- | --- | --- | --- |
| P11 (done 2026-10-18) | `JsonEvent` / `XmlEvent` get `code` (stable error name) and `span` (bytes of the offending token); `json_next`, `xml_next`, `jr_feed`, `xr_feed` fill them, so a binding stops reading `err_code`, `err_len`, `tag_line` from parser internals | small | P1 to P3 | `tests/c/event-model.c`: code and span of an error at chunk sizes 1 and whole; done, plus `json_next_end()` for the end of a pull parse |
| P12 (done) | engine-bound helpers out of the bindings, into `src/`: `js_syntax_error_from_event` (`errinfo.h`, `src/js-syntax-error.c`), `json_options_from_js` / `jwrite_options_from_js` / `xml_options_from_js` / `xwrite_options_from_js` (`jopts.c`, `xopts.c`; absorbs J1 step 1 `jwrite_options_indent*`), `js_jsonevent_new` / `js_jsonevent_callback` and the XML twins (`jevent-js.c`, `xevent-js.c`), the handshake and `js_json_pump` / `js_json_connect` (`jconnect.c`, `xconnect.c`; the C half of P6), `js_jserial_read` / `js_xserial_read`, `location_from_srcpos`, `reader_from_js_input` (absorbs X3 part) | medium | P11 | `tests/c/event-model.c`, `test-json-errors.js`, `test-xml-errors.js`; `quickjs-json.c` about 900 and `quickjs-xml.c` about 1000 lines after the lift |
| (lift) (done) | P2 to P6 and P8 on the binding side (classes read events, serializers on `JsonSerial` / `XmlSerial`, handshake, `pump()` / `connect()`, constructor options, `next()` results `{type, value, loc}`); folds X2 to X5, J4 to J6 | large | P11, P12 | `test-json.js`, `test-xml.js`, `test-xml-write-golden.js` unchanged |
| P13 (JS side done, C entry points open) | privatize and delete the old entry points: `json_parse` + `JSON_NEED_DATA` / `JSON_NONE` / `JSON_ERROR` / `JSON_RESYNC`, `json_end`, `json_getc` / `json_ungetc`, `jr_read` / `jr_finish` / `jr_callback` / `jr_str_t`, `xml_parse` + `XML_PARSE_*` + `event_name` / `event_value` / `event_has_value`, `xr_read` / `xr_finish` / `xr_callback` / `xr_str_t`, the pump wrappers (`jread_pump*`, `xread_pump*`, `json_pump`, `xml_pump`), `jbuild_push` / `jbuild_pop` / `jbuild_key` / `jbuild_value`, `xbuild_start` / `xbuild_attribute` / `xbuild_text` / `xbuild_end`; `JsonParser` and `XMLParser` private (`json_new` / `xml_new`) | medium | lift | `grep` of each symbol outside its own file is empty; ctest unchanged |
| P14 | tokenizers: make `c-json-dialects` green (`JR_LEGACY` `[1,,2,]`), then measure `jread` against `json.c` and `xread` against `xml.c` (MB/s at chunk sizes 1, 64, 4096) and merge: the loser becomes a thin adapter or is deleted (about 1200 lines JSON, 570 XML) | medium | lift | `c-json-dialects` green, `test-json.js`, `test-xml.js`, chunk sizes 1, 2, 7 identical |
| P15 | `walk_next()` leaves the event in the iterator (`it->event`); `WalkInterface`, the capture layer of `jwalkev.c` / `xwalkev.c` and `jwalk_init(..., iface)` lose their argument; `js_walk_register` / `js_walk_define` / `js_walk_wrap` deleted (B1, EV5, B2 alias) | medium | lift | `write()`, `JsonSerial`, `XwalkSource` byte-identical |
| P16 (done) | X1 decided: `write()` = `xwalk_source` pumped into an `XmlWriter` (`XW_PRETTY`); `xtreewrite.[ch]`, `xlayout.[ch]` deleted (about 560 lines) | medium | lift, X1 | `test-xml-write-golden.js` regenerated and reviewed |

Open questions (taken as decided, `doc/api-event-model.md` 9): Q1 `comment` member, Q2 status enums, Q3 `raw` member, Q4 `at` always set, Q5 `xread`
fate, Q6 `Bun.XML` shape in the glue first, Q7 `JsonParser.next()` result shape.

Order: P11, P12, (lift), P13; P14, P15, P16 are independent of each other after the lift; P9 (the `lib/` glue) can start once the lift has the handshake.

Effect on the lists below: J1 step 1 (`jwrite_options_indent*`, `walk_path()`, offset helpers) and X1 (the `write()` layout decision) stay as the first
independent steps; J2, J3, J7 and X6 to X8 wait for P8 / P9; EV5, EV8, B1, B3, X2, X4, X5, J4 to J6 are folded into P4 to P6.

## Open: json and xml (state 2026-10-18)

C side (the order is free, none blocks the others):

- P13 rest: `json_parse` / `json_end` / `json_getc` / `json_ungetc` and `xml_parse` / `event_name` / `event_value` / `event_has_value` are still public
  engine entry points under `json_next` / `xml_next`; the JS surface no longer uses them. `tests/c/json-dialects.c` calls `json_parse` directly.
- P14: merge the tokenizers. **JSON done 2026-10-18**: `json.c` is deleted, `JsonParser` / `json_next` reads a block, feeds it to `jr_feed()` and
  queues the events (`src/json/jnext.c`, strings copied into the queue, `jr_flush()` delivers a line comment at the end of the data,
  `jr_state_t.comment_events` makes `jread` deliver comments, structural events now carry their own `at`). `tests/c/json-pull-dump.c`
  (`c-json-pull-dump`, golden `tests/fixtures/json-pull-dump.txt`) is the regression for events, positions and errors of the pull parser; `c-json-dialects`
  is gone (it compared `json.c` with `jread`, `c-jread-dialects` stays). Differences from the old pull parser, all deliberate: columns count characters,
  not bytes; a comment event is at its start, not its end; error messages come from the error code (`jnext_message`); an unclosed container in
  `legacy` is `unexpected-end` (it was silent); `legacy` rejects a raw newline in a string.
  **XML done 2026-10-18**: `xml.c` is deleted, `XMLParser` / `xml_next` is the same queue over `xr_feed` in `XR_COMPAT` mode (`src/xml/xnext.c`, the Reader
  answering 0 ends the input, less than 0 is `XML_PULL_AGAIN`). `xr_state_init_flags(st, XR_COMPAT | XR_TOLERANT | XR_FLAT | XR_NO_IMPLIED_END)` does everything
  `xml.c` did (markup events, element stack, void tags, raw text, implied ends, flat, positions); measured on 8 MB, MB/s at chunk 1 / 64 / 4096: `xread` strict ~140,
  `XR_COMPAT` 22 / 46 / 48, `xml.c` was ~13-17. The oracle is frozen as `tests/fixtures/xml-pull-dump.txt` (`tests/c/xml-pull-dump.c`, `c-xml-pull-dump`: a corpus
  and 400 random inputs, five flag sets, read sizes 1, 3, 16384); `c-xread-compat` went with `xml.c`. Differences from the old pull parser, all deliberate: an instruction
  not ended by `?>` is not left open as an element, an instruction's `at` is its own `<` (it was the next tag's), flat mode does not swallow the end after an
  instruction, `self_closing` is only set for `<a/>` and void tags, the mismatched-end-tag event carries the message `mismatch </name>` in `str`, `XMLParser.depth` is
  kept as it was (an element counts as open after its attributes).
- `quickjs-json.c` size (2026-10-18): 1412 to 933 lines. Done: `js_class_define` / `js_object_from_target` (`utils.h`) replace the per-class registration and
  prototype boilerplate, `jpush.[ch]` (push parser modes, errors, connect), `jserializer.[ch]`, `js_jsonpull_pump` (`jconnect.c`), `json_new_from_js`
  (`jevent-js.c`), `jwrite_js_stack` (`jopts.c`). About 190 of the remaining lines are the required header comments, 550 are function bodies. Going below
  ~700 needs API decisions, not more lifting: merge `JsonParser` and `JsonPushParser` into one class, move `JsonWriter` / `JsonBuilder` / `tee` to the `lib/` glue
  (P9), drop `JsonSerializer.read(n)` in favour of the iterator. Decided 2026-10-18: `JsonPushParser` stays a separate class (no merge with `JsonParser`), the C core is shared instead.
  `quickjs-xml.c` got the same treatment: 1340 to 967 lines (`js_class_define`, `js_object_from_target`, the push parser in `xpush.[ch]`). The `XmlParser` wrapper (reader, location, selfClosingTags, pump, next) is `xparser.[ch]`; `XMLParser.loc` is a borrowed `Location*` the parser updates, no copy.
  Unrelated ctest failure seen 2026-10-19: `test-typescript.js` (the type stripper rejects parameter properties in strip-only mode).
- P15: `WalkInterface` and the capture layer of `jwalkev.c` / `xwalkev.c`.
- B5: DOCTYPE internal subset in `xml.c` / `xread.c` (needed by strict mode and entities), `BUGS` `xml-doctype-internal-subset-mangled`.
- B9: `walk.c` test for a 100000-deep array, a shared object and a cycle at depth.

JS side:

- P9: `lib/bun.js` (`XML`), `lib/jsonStreams.js`, `lib/xml.js` (`read`, `write`), `lib/dom.js`
  (`DOMParser`, `XMLSerializer`, `<parsererror>`, X7). Done 2026-10-22: `lib/json5.js`, `lib/jsonc.js` (`Bun.JSON5`, `Bun.JSONC`),
  `Blob.json()` on `json_read_value()`, json `read` / `write` and the vendored `sj.h` removed.
- J3 / X8 (`JsonRewriter`, `XmlRewriter`, Tier 17), J7 and X6 (errors: `Blob.prototype.json()`
  and `JSONL` carry `loc`, lenient `xml.read` repairs report through `onParseError`).
- P10: `api-surface/` regenerate (it still lists the old `XMLSerializer` and removed members).

Tests and tools:

- `tests/tools/xml-diff.js` (the `read()` differential, oracle a build of `ecdf0ca7`), `test-xml-errors.js`, `test-json-errors.js`;
- engine-free C tests for `xwrite` `XW_PRETTY` (the golden covers it only through `write()`).

Done since 2026-10-17 (details in `doc/event-model-state.md`): typed markup events, `next()` / iterator / `pump()` / `connect()` / `tee` / `JsonBuilder` / `XmlBuilder`,
handshake symbols, `JsonWriter` as a sink, `xml.write()` on `XW_PRETTY` (golden regenerated, layout changed), `parse()` and `XMLNodeParser` removed, push-parser callback mode,
tee owner and the `read` / `write` cores moved into `src/`.
Unrelated ctest failures (2026-10-18): `test-directory`, `test-extract-c`, `test-globals`, `test-lexer-cpp`, `test-misc`, `test-mmap`, and `test-sql` when no database is up.

## Tier 18 — `json` module: lift to standard shapes (spec 2026-10-17)

Survey (probed node 22 / bun 1.4 / deno 2.9, plus npm `json5`, `jsonc-parser`, `stream-json`,
`@streamparser/json`, `jsonparse`, `lossless-json`, jsr `@std/json`). Engine built-ins
(`JSON.parse/stringify`, `rawJSON`, `isRawJSON`, reviver `context.source`) and JSON modules
(`import x from './x.json' with {type:'json'}`) already work in qjsm. Only Bun has `JSON5`,
`JSONC`, `JSONL` namespaces; only `Blob.prototype.json()` / `Response.json()` / `Body.json()`
exist as JSON readers in the web platform.

### Target surface of `json`

| Export | Standard shape | Source | Status |
| --- | --- | --- | --- |
| `JSONL.parse`, `JSONL.parseChunk` | `Bun.JSONL` | Bun | done |
| `JsonParseStream`, `ConcatenatedJsonParseStream`, `JsonStringifyStream` | `@std/json` | Deno | done (`lib/jsonStreams.js`) |
| `Blob.prototype.json()` | WHATWG Body-style mixin, Bun's Blob | Bun | this change (`quickjs-blob.c`, `JS_ParseJSON`) |
| `JSON5.parse(text)` / `JSON5.stringify(v, null, space)` | `Bun.JSON5` (also npm `json5`) | Bun | planned |
| `JSONC.parse(text)` | `Bun.JSONC` | Bun | planned |
| `JsonPushParser` + `onValue`, `paths`, `end()` alias of `close()` | `@streamparser/json` `JSONParser` | npm | planned |
| `JsonParser`, `JsonSerializer`, `JsonWriter`, `TYPE_*`/token constants | none (every library invents its own) | — | keep, internal engines |
| `read(text)`, `write(v, replacer, space)` | `JSON.parse` / `JSON.stringify` | engine | deprecate after the above; callers: `plot-cv/yt-playlist.js`, `plot-cv/clang-ast.js` |

### Spec decisions

- `JSON5.parse` / `JSONC.parse`: one argument (`length` 1) like Bun, optional reviver is *not*
  added (Bun ignores it). Built on `jread` + `jbuilder`; trailing data is a `SyntaxError`.
- `JSON5.stringify(v, replacer, space)`: `length` 3 like Bun. Differs from `JSON.stringify`
  only in output syntax (bare keys, single quotes, `NaN`/`Infinity`, trailing commas when
  indented); Bun rejects a replacer function and BigInt, so does this; `undefined` for a
  value with no JSON form. Built on `jwalk` + `JsonWriter` options.
- `write(undefined)` returns `undefined` (as `JSON.stringify`); BOM and reviver follow
  `JSON.parse` (BOM is a `SyntaxError`); none of this needs separate decisions once `read`
  and `write` are deprecated.
- No `JSONL.stringify`, no `JSONC.stringify` (Bun has neither).
- Deep nesting: `JS_ParseJSON` recurses (RangeError past the C stack); Bun parses 100000 levels.
  `JSON5.parse`/`JSONC.parse` built on `jread`/`jbuilder` do not recurse.

### `Blob.prototype.json()` — Bun behaviour to match (probed on bun 1.4.2)

| Input | Result |
| --- | --- |
| `new Blob(['{"a":1}']).json()` | `Promise` resolving to `{a: 1}` |
| UTF-8 BOM first | stripped, then parsed |
| empty or whitespace only | rejects `SyntaxError` |
| bad JSON, `1 2`, JSON5 / comments, `NaN`, `undefined` | rejects `SyntaxError` |
| invalid UTF-8 byte | decoded to U+FFFD, then parsed (`"\xff"` → `"�"`) |
| lone surrogate escape `"\ud800"` | kept |
| `type` option | ignored |
| `this` not a Blob | throws `TypeError` synchronously (not a rejection) |
| `length` / name | 0 / `json`; `File` and `Response(blob)` inherit |
| property flags | method of `Blob.prototype`; standard `writable, configurable, non-enumerable` (Bun's `enumerable`, non-configurable is a quirk, not copied) |

Message texts differ from Bun's (engine messages); the error *class* is what matches.

### Order

1. `Blob.prototype.json()` — done (`quickjs-blob.c`, tests in `test-blob.js`, probed against bun). Two known
   differences, both deliberate: nesting past the C stack rejects with `SyntaxError` ("stack overflow")
   because `JS_ParseJSON` recurses (Bun: 100000 levels fine); the method is `writable, non-enumerable,
   configurable` (Bun's is enumerable, non-configurable). Closing the first needs a strict, non-recursive
   `jread` mode that builds ordinary-prototype objects (`jbuilder` makes null-prototype ones and `jread`
   accepts comments); build it with step 2, then point `Blob.json()` at it (blob.so would link json.so).
2. `JSON5.parse`/`JSONC.parse` on `jread`+`jbuilder`; tests diffed against `bun`.
3. `JSON5.stringify` on `jwalk`+`JsonWriter` (`bareKeys`, `singleQuotes`, special numbers).
4. `JsonPushParser` `onValue`/`paths`/`end()`.
5. Deprecate `read`/`write`, migrate the `plot-cv` callers, then remove.

### Findings and status of `quickjs-json.c` (2026-10-17)

Done in this round (all in `tests/unittests/test-json.js`, 71 tests; differential against
`JSON.stringify` over 420 cases through both `write` and `JsonSerializer`, 0 differences;
valgrind clean; about 1.2-1.4x the engine's `JSON.stringify`):

- `jwalk` (on the generic `walk`) + `jwrite` follow `JSON.stringify` rule for rule: `toJSON(key)`, replacer function and
  allow-list, boxed primitives, `undefined`/function/symbol, holes, `NaN`/`Infinity`, BigInt and
  circular `TypeError`, `space`, `rawJSON`, lone surrogates as `\udXXX`. `JsonSerializer` is rebuilt on
  them (one step per `read()`, output kept in a `DynBuf`, so user JS never runs twice).
- `Writer` can write into a JS `ArrayBufferSink` without leaving C
  (`sink[Symbol.for('qjs.dynbuf')]()` hand-off; each `.so` has its own copy of the static core
  library, so a pointer cannot be shared by a global).

Known limits of the write side:

| Limit | Detail |
| --- | --- |
| deep nesting is O(n^2) | the circular check scans the stack; 200000 levels is slow (nothing recurses, so no overflow) |
| string `space` | first 10 bytes only, not 10 characters |
| `JsonWriter` options | the JS constructor has `indent` as a number but no indent *string* |
| `JsonSerializer.blocked` | constant `false` (a step never waits for room) |

Findings on the read side (not yet changed):

| Finding | Detail |
| --- | --- |
| `read()` | duplicates `JSON.parse`; trailing data is an error; no comments |
| `jread` / `jbuilder` | lenient (comments, NaN-like tokens accepted); `jbuild` builds null-prototype objects, so neither is a drop-in for `JSON.parse` results |
| `JsonParser`, `JsonPushParser` | accept `//` and `/* */` comments; `JsonPushParser` resyncs after a bad value |
| engine `JS_ParseJSON2(JS_PARSE_JSON_EXT)` | JSON5-like, but rejects `1.`, `\x` escapes, unicode bare keys; recursive (stack overflow near 100000 levels) |
| `sj.h` | minimal and least conformant; not used for the new work |
| `Blob.json()`, `Response.json()` | Bun and WHATWG name for "parse a body"; `Blob.json()` is done |

Stream wrappers (`lib/jsonStreams.js`: `JsonParseStream`, `ConcatenatedJsonParseStream`,
`JsonStringifyStream`) already exist and are diffed against `jsr:@std/json`.

Open questions: strict `jread` mode (no comments, ordinary-prototype objects, non-recursive) to
serve `JSON5.parse`/`JSONC.parse`/`Blob.json()` deep nesting; whether `JsonPushParser` gains
`onValue`/`paths` (npm `@streamparser/json` shape) or stays callback-by-token.

### Status and remaining work (re-assessed 2026-10-17)

Documents written from this round: [`doc/api-compare-json.md`](doc/api-compare-json.md) (against Browser, Bun, Node,
Deno and the npm/jsr packages, probed), [`doc/api-implement-json.md`](doc/api-implement-json.md) (the split, and the
header-by-header lift candidates), [`doc/api-interface-json.md`](doc/api-interface-json.md) (C and JS interfaces).

Done:

| Item | Where |
| --- | --- |
| `Blob.prototype.json()`, 6 tests, probed against bun | `quickjs-blob.c`, `test-blob.js` |
| `JSONL.parse` / `JSONL.parseChunk` | `quickjs-json.c` |
| `JsonSerializer`, `write()` rebuilt on `jwalk` + `jwrite`; differential 0 on 420 cases | `src/json/jwalk.c`, `src/json/jwrite.c` |
| `walk.[ch]` split into a generic walker plus `jwalk` (JSON rules as `WalkHooks`) | `src/walk.c`, `src/json/jwalk.c` (Tier 20 phase 4a, 4b) |
| `jbuild.[ch]` renamed `jbuilder.[ch]` | `src/`, `include/` |
| `lib/jsonStreams.js` (`@std/json` port) | `lib/` |

Findings of the documentation round (not fixed, in `BUGS`):

- `read()` does not decode string escapes at all and accepts what `JSON.parse` rejects (trailing commas, `01`, `1.`, raw tab);
  `json-read-does-not-decode-escapes`. Reason enough to take `sj.h` out (step 4 of the lift list below).
- `XMLTreeSerializer.read(n)` cuts at n bytes (`xmlserializer-read-n-splits-utf8`); `xml.read()` splits a DOCTYPE internal subset
  (`xml-doctype-internal-subset-mangled`).

Still to do, in order (details and line estimates in `doc/api-implement-json.md` section 4):

1. Pure-C lifts: `jwrite_options_indent*` (the `space` rule), `walk_path()` (so `JsonSerializer.path` stops reaching into the stack),
   UTF-16/UTF-8 offset helpers in `char-utils` (for `JSONL`).
2. `jread` strict mode and JSON5/JSONC dialect flags; `jbuilder` prototype option and `jbuild_value_js()` (shared with Tier 20 `xbuilder`).
3. `JSON5.parse`, `JSONC.parse`, `Blob.json()` on the strict reader (closes the nesting-depth difference with Bun).
4. Deprecate `read()`/`write()`, migrate `plot-cv/yt-playlist.js` and `plot-cv/clang-ast.js`, remove `read()` and the vendored `sj`;
   move the `JSONL` value scanner to `src/`.
5. `JSON5.stringify`; `JsonPushParser` `onValue`, `paths`, `end()`.

Expected `quickjs-json.c` size: 1809 now, about 1620 at the end.

Parse errors and locations (`SyntaxError.loc`, codes, `onError`) are Tier 23; steps E0 and E1 can start before step 1 above, E2 and E3 follow
step 2 and 3.

## Tier 19 — `Bun.*` compatibility gaps in `lib/bun.js` (assessed 2026-10-17)

Reference: `bun 1.4.2` exposes 115 top-level names on `Bun`; `lib/bun.js` has 6 (`plugin`, `file`,
`write`, `stdin`, `stdout`, `stderr`). This tier lists the other 109 in two parts: what can be
written now from code that already exists (part 1), and what needs functionality that does not
exist yet (part 2). It refines Tier 15 (phases 2-6 there are covered here, with the same rules:
probe `bun -e` first, copy names/arguments/errors, one `tests/unittests/test-bun-<area>.js`, a
section in `doc/js/bun.md`, a line in the `compatibility: Bun` table of `cmake/qjsm.cmake`).
Backing names were checked by listing module exports in qjsm, not by running the code against
Bun; each item still needs its own probe-and-diff.

### Part 1 — implementable now (the functionality exists)

| `Bun.*` | Backed by | Work |
| --- | --- | --- |
| `JSONL.parse`, `JSONL.parseChunk` | `json` `JSONL` | alias only |
| `ArrayBufferSink` | `arraybuffer_sink` | alias; same shape |
| `password.hash/verify`, `hashSync/verifySync` | `lib/password.js` (`bcrypt`) | alias; add the argon2 error |
| `deepEquals(a, b, strict?)`, `deepMatch(subset, a)` | `deep.equals` | probe `strict`; `deepMatch` is a subset test, not in `deep` (small) |
| `inspect(v, opts)`, `inspect.custom`, `inspect.table` | `inspect.inspect` | alias; `custom` is a symbol, `table` is new formatting |
| `mmap(path, {shared, sync})` | `mmap` | wrap into a `Uint8Array` |
| `env`, `argv`, `main`, `cwd`, `version`, `revision`, `version_with_sha`, `isMainThread`, `origin` | `process`, `module` build info | plain properties (`origin` is `''`) |
| `fileURLToPath`, `pathToFileURL` | `url` | alias |
| `which(cmd, {PATH, cwd})` | `path.search` (confirm it walks `PATH`) | wrapper |
| `Glob` (`match`, `scan`, `scanSync`) | `misc.glob`, `path.fnmatch`, `fs.walkSync` | class; `scan` options `cwd`, `dot`, `absolute`, `onlyFiles` |
| `nanoseconds()`, `gc()` | `misc.hrtime`, `std.gc` | wrapper |
| `sleep(ms \| Date)`, `sleepSync(ms)` | `timers/promises` `setTimeout`; `os.sleep` | wrapper |
| `concatArrayBuffers(list, max?, asUint8?)` | `misc.concatArrayBuffer` | wrapper |
| `allocUnsafe(n)` | `Uint8Array` | one line |
| `peek(promise)`, `peek.status` | `misc.promiseState` / `promiseResult` | wrapper |
| `stripANSI(s)` | `util.stripAnsi` | alias |
| `stringWidth(s, opts)` | `util` width helper | current helper counts chars; Bun counts East Asian wide (`"日本a"` is 5) and zero-width: add the width table |
| `escapeHTML(v)` | none needed | ~5 lines; `&lt; &amp; &gt; &quot; &#x27;` |
| `readableStreamTo{Text,JSON,Bytes,ArrayBuffer,Blob,Array,FormData}` | `streams`, `textcode`, `Blob`, `JSON` | a reader loop each; `FormData` needs the qjs-lws `FormData`, do it last or skip |
| `SQL`, `sql`, `postgres` | `lib/sql.js` | export; Bun's `postgres` is an alias of `sql` |
| `YAML.stringify` | `yaml.write` | alias only; `YAML.parse` is part 2 |
| `resolveSync(spec, from)`, `resolve` | `module._resolveFilename` | wrapper; probe the error shape |
| `spawn`, `spawnSync` | `child_process` (native, `lib/node/child_process.js`) | adapter: `Subprocess` (`pid`, `exited`, `exitCode`, `signalCode`, `kill`, `stdin` as `FileSink`, `stdout/stderr` as `ReadableStream`), `spawnSync` result `{stdout, stderr, exitCode, success}` |
| `$` (shell template) | on `spawn` | only after `spawn` works; a small parser for pipes, `&&`, redirects, globs |
| `Archive` | `archive` (libarchive: tar, zip, filters) | high-level wrapper: `new Bun.Archive(files)`, `bytes()`, `extract(dir)`; probe Bun's shape first |
| `XML.parse`, `XML.stringify` | `xml.read`, `xml.write` | converter between our element tree and Bun's `{"a":{"@x":"1","b":"t"}}` form; a native implementation was written and shelved in `attic/quickjs-xml-object.inc.c` (see `attic/README.md`) |
| `semver.{satisfies, order}` | none | new pure JS (~100 lines), no dependency |
| `color(input, fmt)` | `terminal.hslToRgb` | new pure JS parsers/formatters (hex, rgb, hsl, css names, `ansi`) |
| `sliceAnsi`, `wrapAnsi` | `util.stripAnsi`, `ansiStyles` | new pure JS; needs the `stringWidth` table |
| `indexOfLine(buf, offset)` | `misc.searchArrayBuffer` | wrapper |
| `randomUUIDv7(enc?, ts?)` | `/dev/urandom` via `fs`, time | new pure JS; (`misc.rand` is not cryptographic, do not use it) |
| `enableANSIColors` | `process.stdout.isTTY` | property |
| `JSON5.parse`, `JSONC.parse`, `JSON5.stringify` | `json` | not yet: depends on Tier 18 steps 2-3 |

Known gaps inside the six already done (Tier 15 phase 1): `formData()`, S3-style `BunFile.write`,
`Bun.write(dest, Response)` only through a duck-typed `arrayBuffer()`, `size` of a slice is clamped
to the file, `FileSink.start()`, `BunFile.watch`, `Bun.write` of a sliced BunFile writes the whole file.

### Part 2 — functionality missing (what has to exist first)

| `Bun.*` | Missing | Needed |
| --- | --- | --- |
| `hash` (wyhash, crc32, adler32, cityHash32/64, murmur32/64) | no hash functions anywhere in `lib/` or `quickjs-*.c` | new native `hash` module, or pure-JS ports (slow) |
| `CryptoHasher`, `MD4`, `MD5`, `SHA1`, `SHA224`, `SHA256`, `SHA384`, `SHA512`, `SHA512_256`, `sha(data, enc?)` | no digest implementation | native binding to a digest library (OpenSSL/mbedtls/libgcrypt, or vendored); also gives `crypto.subtle.digest` and `randomUUIDv5` |
| `randomUUIDv5` | needs SHA-1 | the digest module above |
| `gzipSync`, `gunzipSync`, `deflateSync`, `inflateSync` | `archive` only wraps whole-archive filters, no zlib-framed bytes API | native zlib binding (also `CompressionStream`/`DecompressionStream`) |
| `zstdCompress(+Sync)`, `zstdDecompress(+Sync)` | no zstd outside libarchive's archive filter | native zstd binding |
| `TOML.parse/stringify` | no TOML parser | new parser (C or JS) |
| `YAML.parse` | `yaml` module has `write` only | Tier 12/13 plan (cyaml reader) |
| `dns.lookup`, `dns.prefetch`, `dns.getCacheStats` | no `getaddrinfo` binding in `sockets` | add to `sockets` |
| `serve`, `fetch`, `connect`, `listen`, `udpSocket` | HTTP/TCP/UDP stack | `../qjs-lws/` (`lib/serve.js`, `tcpsocket.js`, `udpsocket.js`); wire `Bun.*` there, not here |
| `FFI` | FFI | `../qjs-ffi/` |
| `Transpiler`, `build`, `Image`, `registerMacro` | no transpiler, bundler or image engine (`markdown`: Tier 25) | out of scope; `plugin()` already throws `TypeError` for loaders that need one |
| `S3Client`, `s3`, `RedisClient`, `redis`, `secrets` | S3/Redis clients, OS keychain | out of scope (network/credential stores) |
| `Cookie`, `CookieMap`, `CSRF` | cookie model, HMAC (needs digests) | pure JS possible later (WHATWG-style cookie parsing); `CSRF` needs the digest module |
| `cron`, `WebView`, `Terminal`, `FileSystemRouter`, `embeddedFiles`, `openInEditor`, `generateHeapSnapshot`, `shrink`, `unsafe`, `jest`, `isStandaloneExecutable` | runtime-specific (scheduler, browser, pty, bundle, heap) | no standard target; skip, `isStandaloneExecutable` could be `false` |

### Order

1. Part 1 as three batches, each with tests and `doc/js/bun.md`: (a) aliases and process/environment
   rows, (b) `Glob`, `which`, `sleep`, `readableStreamTo*`, `escapeHTML`, `stringWidth`, `peek`, (c) the
   adapters (`spawn`/`spawnSync`, `Archive`, `XML`, `$`).
2. Tier 18 steps 2-3, then the `JSON5`/`JSONC` rows.
3. Part 2 in this order, by number of dependants: a digest module (unlocks 11 names plus `randomUUIDv5`, `CSRF`),
   then zlib/zstd, `hash`, `dns`, `YAML.parse`, `TOML`.

## Tier 20 — `quickjs-xml.c` onto engine-free C pieces, mirroring the `quickjs-json.c` refactor (planned 2026-10-17)

Goal: `quickjs-xml.c` becomes what `quickjs-json.c` became, a binding layer (classes, argument parsing,
getters, finalizers) over pure C pieces in `src/` that know nothing of JS values or each other. Today
(2713 lines) the JS binding, three tree builders, three writers and two tokenizers live in one file.

### Mapping, JSON side to XML side

| role | JSON (done) | XML today | XML target |
| --- | --- | --- | --- |
| event protocol | `JsonEventType` (one enum for `jread`, `jbuilder`, `jwrite`), `JsonWriteInterface`, `WalkInterface` | two unrelated enums (`xml_event_t` in `xml.h`, `xr_type_t` in `xread.h`) and no write interface; `XMLWriter` borrows `xml_event_t` as method ids | `include/xevent.h`, phase 1 |
| pull tokenizer | `src/json/json.c` | `src/xml/xml.c` (events; used by `read()`, `XMLParser`, `XMLNodeParser`) | keep |
| push tokenizer | `src/json/jread.c` (`jr_callback`) | `src/xml/xread.c` (`xr_callback`) | keep first; maybe merge into `xml.c` (phase 5) |
| events to JS value | `src/json/jbuilder.c` (`jbuild_*`) | three copies: the `read()` adapter (`XRFrame`), `XMLBuilder` (`XMLParser.root`, `XMLPushParser.root`), `XMLNodeParser` | `src/xbuilder.c` (`xbuild_*`), one engine for all |
| events to text | `src/json/jwrite.c` (`jwrite_*`, `Writer`) | `xml_write_*` over JS values, `XMLWriter` inline, `XMLTreeSerializer` inline | `src/xml/xwrite.c` (`xwrite_*`) |
| JS value to events | `src/json/jwalk.c` (renamed from `walk.c`, on top of a generic `src/walk.c`) | `property_enumeration` loops (`js_xml_write_tree/list`, `xml_enumeration_next`, `XMLTreeSerializer` stacks) | `src/xml/xwalk.c` (`xwalk_*`) |
| filter between the two | `jrewrite` (Tier 17, planned) | none | `src/xrewrite.c` (phase 8) |

### What is wrong today, with the evidence

| Problem | Where | Effect |
| --- | --- | --- |
| three tree builders disagree | `read()` gives `true` for a valueless attribute and no `children` on `<a/>`; `XMLBuilder` gives `""` and always `children: []`; `XMLNodeParser` has its own flat form | the same document has three shapes; `XMLParser.root` is not `read()` |
| two tokenizers disagree | `xread.c` has no tolerant mode, implied end tags, flat mode, raw-text tags or tag locations | `XMLPushParser` parses HTML differently from `read()` and `XMLParser` |
| `XMLWriter` does not escape | `writer_putjs()` copies text and attribute values raw; `write()` escapes `& < > "` | `XMLWriter.text('a<b')` writes invalid XML |
| `XMLTreeSerializer` replays | `skip`/`delivered`/`capped` re-run a step to resume (same design `JsonSerializer` had before its rewrite) | complex, and any user code in a step would run twice |
| writer depends on JS | `xml_write_element` reads `tagName`/`attributes` itself | no way to write from C, from a parser or from a rewriter |
| four writers, four rule sets | `write()` tree, `write()` flat list, `XMLTreeSerializer` and `XMLWriter` each format and escape on their own (table in Phase 3) | the same tree prints differently per entry point; one fix needs four edits |
| `XMLWriter.attribute(n, true)` | `!(js_is_null_or_undefined(v) && JS_IsBool(v))` can never be false, so a value-less attribute is never written; `true` gives `n="true"` (by reading the code) | differs from `write()` and `XMLTreeSerializer`, which write `n` |
| escaping helper twice | `dbuf_put_escaped_xml` (DynBuf) and `xsw_raw_escaped` (resumable) | two copies of one rule |
| `xml_enumeration_next` | hand-rolled pointer juggling around `property_recursion_push` | already caused a use-after-free once (see its comment) |

Size now (lines): `quickjs-xml.c` 2713 (parse adapter 220, write functions 300 + 150, `XMLTreeSerializer`
600, `XMLBuilder` 175, `XMLPushParser` 230, `XMLParser` 360, `XMLNodeParser` 230, `XMLWriter` 210),
`src/xml/xml.c` 910, `src/xml/xread.c` 409. Target for `quickjs-xml.c`: about 1200.

### Status (re-assessed 2026-10-17)

Companion documents: [`doc/api-compare-xml.md`](doc/api-compare-xml.md) (Browser, Bun, Node, Deno and eight packages, probed on one
document), [`doc/api-implement-xml.md`](doc/api-implement-xml.md) (the split, line counts per section, lift candidates per
header), [`doc/api-interface-xml.md`](doc/api-interface-xml.md) (event protocol, `xml_parser_run`, `xread`, `xwrite`, `xwalk`,
planned `xbuilder`, JS contracts).

Built and green (committed; the current state is in "Remaining work: json and xml" above Tier 18):

| piece | where | state |
| --- | --- | --- |
| `read()` on `src/xml/xml.c` | `quickjs-xml.c`, `xml.c` | done (differential 0, 150 tests); `quickjs-xml.c` was cut from 3064 to 2713 lines |
| `jbuild.[ch]` renamed `jbuilder.[ch]`, `walk.[ch]` renamed `jwalk.[ch]` | `src/`, `include/` | done (4a) |
| generic `walk.[ch]` (531 + 118 lines) with `WalkHooks`; `jwalk` on top (368 + 81) | `src/` | done (4b), `test-json.js` 71 green, `write()` equals `JSON.stringify` on the corpus |
| `xwalk.[ch]` (149 + 61): element trees through the two hooks | `src/` | done (4c), checked with a C harness only; no caller, no JS-level test yet |
| golden corpus for `write()`, `XMLTreeSerializer`, `XMLWriter` (98 cases) | `tests/fixtures/`, `tests/tools/`, `test-xml-write-golden.js` | done (3a) |
| `xwrite.[ch]` (277 + 112): compact and pretty layouts, typed markup, `xwrite_put_escaped` | `src/` | done (3b), `tests/tools/xwrite-golden.c`: 51 identical, 10 differ only in the 3c changes, valgrind clean; no caller yet |

Done since this table was written: Phase M, Phase 1, Phase 2, 3c, 3d, 4d (as `xlayout` and `xtreewrite`). Not started: 3e, 5 to 10.

Measure (2026-10-17): `quickjs-xml.c` is 2156 lines (was 3064), `quickjs-json.c` 1962; the engine-free and adapter code in `src/xml/` is 2.9k
lines, in `src/json/` 3.2k. ctest has five failures unrelated to json and xml: `test-globals`, `test-misc`, `test-mmap`, `test-lexer-cpp`, `test-sql`.

Corrections to the plan found on the way:

- Phase 1 is not on the critical path. `xbuilder` needs only `jbuilder`; `xwrite` and `xwalk` already have their own
  calls. The shared event enum and `XMLWriteInterface` unify what exists, so they come after the first consumers work
  (order below), and `xwrite_interface()` lands with them.
- `xwalk` alone is not enough for `write()`: the tree layout needs the children count of an element (read the
  `children` length when the adapter sees it open) and the flat list needs a one-event lookahead (self-closing iff the
  next entry is the element's own `/x` marker). Both belong to the 4d adapter and are listed there.
- `walk.c` enumerates an element's own keys before `xwalk`'s `keys` hook replaces them; a cheap waste kept to leave
  `walk.c` free of tree knowledge. Revisit only if a profile shows it.
- `write()` has odd layout rules the corpus now pins (children of the root at depth 0, `dt` indent in lists, multi-line
  text losing its newline in the tree writer). Step 3d reproduces them with `XW_TREE`; do not "fix" them there.
- New lift candidates from the header-by-header check (`doc/api-implement-xml.md` section 3): one `reader_from_js_input()` for the
  input argument (about 7 call sites with 3 different helpers), one `xml_options_from_js()` for the four copies of option parsing,
  one location-sync helper for `XMLParser`/`XMLNodeParser`; `XMLWriter.written` can come from `xwrite`'s counter; `property-enumeration.h`
  and `char-utils.h` drop out of the binding once 3d and Phase 5 are done. Rejected: `dbuf_put_escaped_*` and `writer_escaped`
  escape with backslashes, so `xwrite_put_escaped` stays.
- Decided 2026-10-17: our class is renamed `XMLTreeSerializer` (done in `quickjs-xml.c`, tests, docs); `XMLSerializer` is left to the DOM class
  `XMLSerializer().serializeToString(node)` (`doc/api-compare-xml.md` section 5), see the next item.
- Open standards lift: export `lib/dom.js` `Parser` / `Serializer` as `DOMParser` / `XMLSerializer` (Browser priority).
- Exceptions and locations for every XML parser (`SyntaxError.loc`, `code`, `onParseError`, `parser.error`, `<parsererror>` for `DOMParser`) are Tier 23; its
  step E0 (right classes) and E1 (position tracking in `xread`/`xml.c`) fit before Phase 2, and E1c shares the decision on `xread` with Phase 6.
- `xml.read()` does not parse a DOCTYPE internal subset (`BUGS`); the strict mode of Phase 9 needs it for entities, so fix it there.
- `XMLTreeSerializer.read(n)` splits multi-byte characters (BUGS); Phase 5 can fix it for free by cutting on character
  boundaries, but that is a behaviour change and goes in its own line of the ChangeLog.

Next, in this order:

| # | step | why now | gate |
| --- | --- | --- | --- |
| 1 (done) | 3c `XMLWriter` onto `xwrite` | first real caller of `xwrite` | done 2026-10-17 |
| 1b | M layout move, then 1 `xevent.h` and 2 `xbuilder` (design priority, see Phase M and Phase 1 below) | every later step adds files; they should be born in `include/xml/` and implement the event vtable | builds, ctest as before; `read()` differential 0 |
| 2 | 3d `XW_TREE` layout, `write()` emits through `xwrite` | removes about 230 lines; needs only `xwrite` | golden byte-identical, fuzz against `lib/xml/write.js` |
| 3 | 4d `xwalk` adapter into `xwrite`, `write()` traversal replaced | first user of `xwalk`; adds the JS-level `test-walk` cases | golden identical, `lib/dom.js` nodes written |
| 4 | 3e + 5 `XMLTreeSerializer` on `xwalk` + `xwrite` | removes the replay machinery, about 450 lines | serializer tests, equality with `write()` at read sizes 1, 7, 4096 |
| 5 | 2 `xbuilder` (then 1 event protocol) | the three tree builders become one | `read()` differential still 0 |
| 6 | 6, 7 push parser and plumbing | needs the measurement of Phase 6 first | differential at 1, 2, 7 byte chunks |
| 7 | 8 `xrewrite`, 9 object tree, 10 clean-up | features on the finished pieces; 8 waits for Tier 17 | per phase |

### Rules for every phase

1. **Oracle first.** Before a phase changes behaviour-bearing code, keep the previous implementation
   reachable: either a `readOld`-style temporary export, or a build of `HEAD` in a scratch worktree.
   The differential harness used for the `read()` move (`xd2.js`: hand corpus plus 6000 token-soup
   inputs, five option sets, location tuples compared) is to be saved as `tests/tools/xml-diff.js`
   in phase 0 and reused; exit criterion of a phase is 0 differences on inputs that do not end inside
   a tag, and a written list of every intended difference.
2. **Which pieces use the QuickJS API** (same split as the JSON side: `jbuilder.h` and `jwalk.h` include
   `quickjs.h`; `jread`, `json`, `jwrite` do not):

   | piece | QuickJS API | why |
   | --- | --- | --- |
   | `xml.c`, `xread.c`, `xwrite.c`, `xrewrite.c`, the event header | none | text in, events out, text out |
   | `xbuilder.c` | yes (`JSContext`, `JSValue`), through `jbuilder.h` | its product is a JS object tree; built on `jbuilder` |
   | `walk.c`, `jwalk.c`, `xwalk.c` | yes (`JSContext`, `JSValue`) | their input is a JS object tree; `xwalk` is an adapter over `walk` |

   The engine-free ones use libc or `DynBufReallocFunc` allocation (runtime-bound where a finalizer frees),
   `Writer` and `Reader` from `stream-utils.h`, and error codes with a `*_error_message()` like `jwrite`.
   `xbuilder` and `xwalk` keep everything else out of the class code: the binding only converts arguments.
3. **One test file per piece** is not needed; `test-xml.js` grows by the cases each phase pins, plus
   C-level behaviour through the existing classes. `ctest -R "dom|xml|html|vfs"` must stay green
   (`lib/dom.js` depends on `read()`/`write()` output; see the compatibility notes in `doc/native/xml.md`).
4. Update `doc/native/xml.md` and the header comments in the same phase; comments follow the CLAUDE.md rules.

### Phase 0: baseline (small)

- Done: the golden corpus (`tests/fixtures/xml-write-golden.json`, regenerated by `tests/tools/xml-write-golden.js`) and
  `tests/unittests/test-xml-write-golden.js`; it covers `write()`, `XMLTreeSerializer` and `XMLWriter` (98 cases), so
  later phases must not change it except where a step lists a visible change.
- Open: save the `read()` differential harness (fuzz against the old parser, currently a scratch script) to
  `tests/tools/xml-diff.js`; it needs the pre-refactor `read()` as an oracle (build it from commit `ecdf0ca7`).
- Open: record the ctest failure set (7 pre-existing non-deno failures, see Status).

### Phase M: layout (`include/json/`, `src/json/`, `include/xml/`, `src/xml/`)

Decision proposed 2026-10-17, to be done before Phase 1 so the new files are born in place (nothing else in `~/Sources/quickjs`
includes these headers; checked by grep, the only other hits are `.claude/worktrees/`).

| now | after |
| --- | --- |
| `include/{jread,jbuilder,jwrite,jwalk,json}.h`, `src/{jread,jbuilder,jwrite,jwalk,json}.c` | `include/json/`, `src/json/` (same file names) |
| `include/{xml,xread,xwrite,xwalk,xml_entities}.h`, `src/{xml,xread,xwrite,xwalk}.c` | `include/xml/`, `src/xml/` (same file names); new: `xevent.h`, `xbuilder.[ch]`, later `xrewrite.[ch]` |
| `include/walk.h`, `src/walk.c` | stay: generic, used by both |

- Includes become `#include "json/jread.h"` / `"xml/xwrite.h"` with `include/` as the one root; no extra include path.
- CMake: `LIBRARY_SOURCES` globs `src/*.c` and `include/*.h`; add `src/json/*.c src/xml/*.c include/json/*.h include/xml/*.h`, and the
  explicit `src/json/json.c`, `src/json/jread.c` entries and the two C tests (`tests/c/jread-positions.c`, `tests/tools/xwrite-golden.c`).
- Do it with `git mv` (history follows) and one `sed` over `#include` lines, in one commit of its own, with the tree otherwise
  committed or stashed: the files have uncommitted edits now, so `git` shows them as renames plus modifications.
- Risk: low (mechanical); gate: build, ctest identical, `git diff -M --stat` shows renames only.

### Phase 1: the XML event protocol (`include/xml/xevent.h`, new; twin of `include/json/jevent.h`, see "Event model" above)

There is none today. Two enums exist side by side and neither is the contract of anything but its own parser:

| | `xml_event_t` (`xml.h`, pull) | `xr_type_t` (`xread.h`, push) |
| --- | --- | --- |
| values | `PARSE_AGAIN -2, PARSE_ERROR -1, PARSE_OK 0, ELEMENT_START, ATTRIBUTE, ELEMENT_END, TEXT` | `attribute, element_start, element_end, error, text` |
| delivery | returned by `xml_parser_run()`, data in `event_name/event_value/event_has_value` | callback `(type, name, value, user_data)` with `xr_str_t` |
| value-less attribute | `event_has_value == 0` | `value == NULL` |

What the protocol must fix, and the proposal:

- (Superseded in part by the design-priority paragraph after this list: the table of function pointers is the core, the enum its index.) One enum `xe_type_t`: `error, element_start, attribute, text, element_end`, in one fixed order that `xwrite`'s methods, `xbuilder`'s
  methods and both parsers share one to one (as `jwrite` mirrors `JsonEventType`). Status codes (`AGAIN`, `OK`) stay a separate
  return type of the pull parser, not event kinds.
- One string type `xe_str_t { const char* data; size_t len; }` and one callback `xe_callback(type, name, value, user_data)`;
  `xread.c` and `xml.c` get a thin adapter each, `xr_type_t`/`xml_event_t` keep working as aliases for one release.
- `XmlEventSink` (named `XMLWriteInterface` before; function pointers, twin of `JsonEventSink`, see "Event model" above): `element_start`, `attribute`, `text`, `element_end`, plus the
  typed markup below, `opaque` first; implemented by `xwrite` and by `xrewrite` so writers and filters chain.
- Markup that is not an element: comments, `<!DOCTYPE ...>`, `<?...?>` and CDATA currently arrive as ELEMENT_START with a magic tag
  name (`"!-- c --"`, `"!DOCTYPE html"`, `"?xml"`) followed by an immediate ELEMENT_END (except `?`). `read()` and `lib/dom.js`
  depend on that. Recommendation: add the typed events `comment`, `declaration`, `instruction`, `cdata` to the protocol and have
  the parsers emit them; `xbuilder` maps them back to the magic-name elements in its legacy mode, so `read()` stays byte-identical,
  and `xwrite` writes them natively instead of recognising the magic names. Phase 0's golden test pins both directions.
- Positions: every event carries `tag_line/tag_column` (already in `XMLParser` since the `read()` work), as `jr` state does.
- Ownership: strings are valid until the next event (`xml.c` already copies into `ev_name/ev_value` for this; `xread.c` points
  into its accumulators, which the adapter must copy or document).

Exit: both parsers deliver the same event sequence for the same input where their behaviours coincide (a test feeds one document
to both and compares the sequences); `XMLWriter` method ids come from `xe_type_t`.

**Design priority (2026-10-17): C-only coupling through event vtables.** `xml` and `json` are to be backed by engine-free pieces that
plug into each other without a `JSValue` in between, as `quickjs-json.c` already does for JSON (`jread` callback to `jbuild_*`,
`jwalk` to `JsonWriteInterface`). The XML side gets the same, built on what exists:

- `xevent.h` defines `XmlEvent` (type, name, value, position; the same ids as `xml_event_t`, so `xml_parser_run()` results cast
  to it) and **`XmlEventSink`**, a table of function pointers with an `opaque` first argument, like `JsonWriteInterface`:
  `element_start`, `attribute`, `text`, `element_end`, `comment`, `declaration`, `instruction`, `cdata`, `error`.
- Sinks: `xwrite_interface(&wr)` (writer), `xbuild_interface(&b)` (tree, on `jbuilder`), later `xrewrite` (filter that owns a
  downstream sink) and a `tee`. Sources: `xml_parser_pump(XMLParser*, const XmlEventSink*)` (pull parser drives a sink until
  `AGAIN`/`OK`/`ERROR`), `xr_read` with an adapter callback (push), `xwalk` (tree to events, through `WalkInterface`).
- Couplings this makes C-only: parser to writer (`xml.read` to `xwrite`, a streaming reformatter), parser to builder, tree to
  writer (`write()`, `XMLTreeSerializer`), and the same for JSON (`JsonWriteInterface` already; `jread` to a `JsonEventSink` that
  `jbuilder` and `jwrite` implement). The JS classes then only pick a source, a sink and a `Reader`/`Writer` from
  `stream-utils.h`, and take positions from `location.h`/`srcpos.h`.
- Rule for every new piece: it takes or returns an `XmlEventSink`/`JsonWriteInterface`, never a callback of its own shape.

### Phase 2: `xbuilder` (events to JS value; a thin layer over `jbuilder`)

`xbuilder.c` does not get its own frame stack, key handling or attach logic: it translates XML events into
`jbuild_*` calls and keeps only a tiny per-element state. `jbuilder` already attaches a container to its parent
when it is pushed (the partial tree is always reachable from the root), owns the frames, and has `jbuild_path()`.

| XML event | calls on the `JsonBuilder` |
| --- | --- |
| element_start `div` | `push(object)`, `key("tagName")`, `value(string,"div")`; position: `key("line")`, `value(number)` |
| first attribute | `key("attributes")`, `push(object)` (remember: attributes open) |
| attribute `id="x"` | `key("id")`, `value(string,"x")`; value-less attribute: `value(true)` |
| first child or text | `pop()` if attributes open; `key("children")`, `push(array)` (remember: children open) |
| text `hi` | `value(string,"hi")` |
| element_end | `pop()` (children or attributes if open), `pop()` (the element); a self-closing element never opens `children` |

The state is two bits per element (attributes open, children open) kept in a small stack next to the builder, or
in an `xbuild_frame` that wraps `jbuild_push`.

What `jbuilder` needs for this (small, backwards compatible, JSON callers unchanged):

- an object prototype option: `jbuild_push` creates `JS_NewObjectProto(ctx, JS_NULL)` objects; `read()` trees are
  plain objects (`lib/dom.js` calls `hasOwnProperty`, `Object.entries`), so add a `JSValue proto` field on
  `JsonBuilder` (default `JS_NULL`; `xbuilder` sets `JS_UNDEFINED` meaning `Object.prototype`).
- `jbuild_value_js(b, JSValue v)`: attach an already-made value (takes ownership) for line/column numbers,
  avoiding the number-to-string-to-`scan_double` round trip of `jbuild_value(JSON_EVENT_NUMBER, ...)`.
- optional later: `jbuild_key` copies the key with `js_malloc` per key; fine for XML (few keys per element), but
  an interned-atom variant would help both formats.

Decide the one shape and document it: keep `read()`'s (it is what `lib/dom.js` consumes): value-less attribute
`true`, no `children` on a self-closing or declaration element, `children` (possibly empty) otherwise.
`XMLParser.root` and `XMLPushParser.root` then change to match (a visible change: list it in the ChangeLog and
`doc/native/xml.md`; both are rarely used, `lib/html.js` uses `XMLParser` events, not `.root`).

- API: `xbuild_init/free`, `xbuild_start(name)`, `xbuild_attribute(name, value|NULL)`, `xbuild_text`, `xbuild_end`,
  `xbuild_root`, `xbuild_path`, plus options `flat`, `location`, `legacy_attrs`; `include/xbuilder.h` includes
  `jbuilder.h`.
- Replaces the `read()` adapter (`XRFrame`), `XMLBuilder`, and `XMLNodeParser`'s flat node assembly.
- Exit: `read()` differential still 0; `test-json.js` unchanged and green (the `jbuilder` changes), `test-xml.js`
  updated only where `.root` shapes were pinned.
- Risk: low. Saves about 400 lines in `quickjs-xml.c` and adds about 100 in `xbuilder.c` (instead of about 250
  for a standalone builder).

### Phase 3: `xwrite` (events to text; mirrors `jwrite`) and the move of `xml_write_*` out of `quickjs-xml.c`

There was no complete plan for this; this is it. Four writers exist today, with different rules:

| rule | `write()` tree | `write()` flat list | `XMLTreeSerializer` | `XMLWriter` |
| --- | --- | --- | --- | --- |
| text and attribute escaping (`& < >`, `"` in attributes) | yes | yes | yes | **no** |
| value-less attribute (`true`) | bare name | bare name | bare name | `n="true"` (bug, see problems) |
| whitespace | newline after every tag, 2 spaces per depth, text inline when it is the only child, trailing whitespace trimmed | newline after every tag, depth moves only on `dt` | none | `indent` option; never next to text |
| empty element | `" />"` when `children` is empty or absent | `" />"` when the next entry is its own `/x` marker, or the last entry | `" />"` when no children | `" />"` when `elementEnd` follows `elementStart`/`attribute` |
| comment, `!DOCTYPE`, `?pi` | `<tag>` verbatim (`?` gets `?>`), never self-closed | same | same | not supported |
| output | `DynBuf` to string | `DynBuf` to string | `Writer`, resumable by replay | `Writer` |

Target: one engine-free writer, `src/xml/xwrite.c` + `include/xml/xwrite.h`, that owns every formatting and escaping rule. The
four entry points keep their JS surface and become event sources (the traversal half moves in Phases 4 and 5).

**API** (mirrors `jwrite`; every call returns the bytes written >= 0, or `-XWRITE_E_*`; no `JSContext`):

| call | writes |
| --- | --- |
| `xwrite_init(wr, realloc_func, opaque)`, `xwrite_free`, `xwrite_error_message(code)` | the frame stack uses the given allocator |
| `xwrite_element_start(wr, name, len)` | `<name`, the tag stays open for attributes |
| `xwrite_attribute(wr, name, len, value, vlen)` | ` name="escaped"`; `value == NULL`: ` name` |
| `xwrite_children(wr, n)` | nothing; says the element has `n` children (-1 unknown), used by the `XW_TREE` layout only |
| `xwrite_text(wr, str, len)` | `>` if the tag is open, then the escaped text |
| `xwrite_element_end(wr, name, len)` | ` />` if no content since the start, else `</name>`; `name == NULL` uses the stacked one, a different name is `XWRITE_E_MISMATCH` |
| `xwrite_markup(wr, name, len)` | `<name>` verbatim; a name starting with `?` ends with `?>`; the form legacy trees use for comments, doctypes and instructions |
| `xwrite_comment`, `xwrite_declaration`, `xwrite_instruction`, `xwrite_cdata` | typed forms of the above (Phase 1 events); no new JS method is added for them |
| `xwrite_put_escaped(Writer*, str, len, is_attr)` | the one escaping rule, also used by `XMLTreeSerializer` |

Options (`XmlWriteOptions`): `indent` (spaces per level) and `layout`:

| layout | rule | used by |
| --- | --- | --- |
| `XW_COMPACT` | no whitespace added | `XMLTreeSerializer`, `XMLWriter` with `indent` 0 |
| `XW_PRETTY` | newline plus indent between tags, never next to text | `XMLWriter` |
| `XW_TREE` | `write()`'s rules: newline after every tag, text inline only when `xwrite_children(wr, 1)`, continuation lines of multi-line text re-indented, trailing whitespace trimmed at `xwrite_finish`; the flat list uses it with n unknown | `write()` |

`xwrite_interface(wr)` (lands with Phase 1) returns the `XMLWriteInterface` with `opaque` = `wr`, as `jwrite_interface()` does,
so a parser or rewriter can drive an `xwrite` without knowing it.

Not added (no consumer today): `void_tags`, `multiline_text`, name validation (names are written as given, because
legacy trees put `/x`, `!--` and `?xml` in `tagName`).

**Resumability**: not a property of `xwrite`. Phase 5 steps one event per `read()` call into an unbounded `DynBuf`, so
a `Writer` that refuses bytes is an error (`XWRITE_E_WRITE`), as in `jwrite` for everything but its byte-wise puts.

**Steps**, each green before the next:

| step | change | gate |
| --- | --- | --- |
| 3a (done) | extend Phase 0's golden corpus (`tests/unittests/test-xml-write-golden.js`, `write()` only) with `XMLTreeSerializer` and `XMLWriter` outputs over escaping, comments, doctype, PI, empty elements, multi-line text, `indent` 0/2/4, depth cap; generated from the current build before touching code; `lib/xml/write.js` (a JS port of `js_xml_write`) is the second oracle | corpus checked in, passes on HEAD |
| 3b (done) | `include/xml/xwrite.h`, `src/xml/xwrite.c` with `XW_COMPACT` and `XW_PRETTY`, error codes, `xwrite_put_escaped`; no caller yet | `tests/tools/xwrite-golden.c` reproduces the `XMLWriter` and `XMLTreeSerializer` corpus entries: 51 identical, 10 differ only in the 3c changes, valgrind clean |
| 3c (done) | `XMLWriter` on `xwrite` (210 to 130 lines in `quickjs-xml.c`, which is 2897 lines because of Tier 23). **Visible changes, as planned:** text and attribute values are escaped; `attribute(n, true)` (also `undefined`, `null`, no argument) writes `n`; `indent < 0` is compact at level 0. **Also:** ordering mistakes throw `TypeError` with `code` (`mismatched-end-tag`, `unmatched-end-tag`, `attribute-outside-tag`), a refused write throws `Error` with `code` `write-failed`; `elementEnd()` without a name closes the open element; `written` is `xwrite`'s counter. `src/stream-utils.c` `write_jsfunction`: a callback return value that is not a number now means "all accepted" (`s => (out += s)` was a failed write) | `test-xml.js` 159, golden regenerated: exactly the 10 predicted cases differ |
| 3d (done) | `XW_TREE` layout; the byte-emitting functions of `write()` (`xml_write_attributes/string/text/element`, `xml_close_element`, `xml_write_indent`, `dbuf_put_escaped_xml`) become calls into `xwrite`, the traversal (`js_xml_write_tree/list`, `xml_enumeration_next`, `xml_num_children`) stays in `quickjs-xml.c` until 4d | golden byte-identical; fuzz `write(read(x))` against `lib/xml/write.js` equal | **Done as `xlayout.[ch]` (`xlay_*`, explicit depths, trailing whitespace held back) instead of an `XW_TREE` mode of `xwrite`: golden 98 and `test-xml.js` 160 identical.**
| 3e | `XMLTreeSerializer`'s `xsw_raw_escaped` and tag/attribute emission call `xwrite` where the replay design allows (the full switch is Phase 5) | `XMLTreeSerializer` tests unchanged |

Moved or deleted by the end of 3e, from `quickjs-xml.c`: `dbuf_put_escaped_xml`, `xml_write_attributes`,
`xml_write_indent`, `xml_write_string`, `xml_write_text`, `xml_write_element`, `xml_close_element` (about 230 lines), the
`XMLWriter` method switch (about 120). Stays until Phase 4d: `xml_num_children`, `xml_enumeration_next`,
`js_xml_write_tree/list`, `js_xml_write`. Stays until Phase 5: `XMLCappedBuf`, `xml_write_capped/skip`, `xsw_*`,
`xmlserializer_*`.

Risk: medium (many small formatting rules; the golden corpus is the safety net). Saves about 350 lines from
`quickjs-xml.c`, adds about 380 to `src/` and 60 to `include/`.

### Phase 4: a generic `walk`, `jwalk` on top of it, `xwalk` as an adapter

Decision: `walk.[ch]` is `JSON.stringify`'s walker (toJSON, replacer, number rules), so it is renamed `jwalk.[ch]`
like `jread`, `jwrite`, `jbuilder`. A new thin `walk.[ch]` is the generic stepper; `jwalk` becomes `walk` plus the
JSON rules; XML trees are another configuration of `walk`. No separate `builder.c`: `jbuilder.c` already is the
general builder (Phase 2 layers `xbuilder` on it).

```
quickjs-json.c --> jwalk.c (toJSON, replacer, boxed, NaN, bigint, rawJSON) --\
                                                                              +--> walk.c --> property-enumeration.h
quickjs-xml.c  --> xwalk.c (events adapter, Bun and default shape config) ---/
```

What `walk.c` owns (moved out of the current `walk.c`, no JSON knowledge): the frame stack
(`PropertyEnumeration` per open container), step granularity (`walk_next` returns one event), `walk_skip`,
`walk_depth`, circular check, close, the `WalkInterface` callbacks and `WalkEvent`.

What `jwalk.c` keeps, as hooks that `walk.c` calls (NULL hook = plain behaviour):

| hook | JSON implementation |
| --- | --- |
| `resolve(holder, key, value)` returns the value to report | `toJSON()`, then the replacer function, then unwrap `Number/String/Boolean` objects |
| `keep(key atom)` | the array replacer's allow list |
| `classify(value)` returns skip / value / container / raw / error | undefined, symbol, function skipped (null in arrays); `NaN`, `Infinity` to null; bigint TypeError; `JSON.rawJSON()` object to raw |

Public JSON surface (`jwalk_init`, `jwalk_set_replacer`, `jwalk_next`, `jwalk_skip`, `jwalk_close`,
`jwalk_root_omitted`, `JwalkIterator`) keeps its current behaviour exactly; `quickjs-json.c` is the only user.
`WalkInterface` and `WalkEvent` stay in `walk.h` (generic); `raw()` is documented as only ever called by `jwalk`.

`walk.c` stays a plain object/array walker (a thin layer over `property-enumeration.h`, `WALK_TOP`/`WALK_FRAME`
macros for the frames, no tree knowledge). Element trees live in `xwalk.[ch]`, which depends on `walk.h` only, through
the same two hooks `jwalk` uses:

| piece | where | what |
| --- | --- | --- |
| `prepare` hook | `xwalk.c` | undefined omitted; an object is a container only where the shape has one (the root, an item of `children`/a root list, the value of `attributes`, an array under `children`); the rest are values |
| `keys` hook | `xwalk.c` | an element's keys become `tagName`, `attributes`, `children` (those it has, read with a plain get so prototype getters work), replacing the enumerated own keys |
| role of a container | `xwalk.c` | derived from the parent frame and its current key, no extra state in `walk.c` |
| Bun tree | adapter | needs no shape: plain `walk`; the adapter classifies keys |

Unlisted keys (`parentNode`, `ownerDocument`, `line`) are never read, so the circular check never fires on back-links.
Values arrive raw; attribute values and text are converted with `JS_ToString` by the adapter.

The adapter (`xwalk_adapter`, about 80 lines, a second part of `xwalk.c`) maps walk events to XML events on `XMLWriteInterface`:

| walk event (depth, last key) | XML event |
| --- | --- |
| `object_start` at depth 1, or inside `children` | `element_start` (name comes with the next `value`) |
| `key("tagName")` + `value(s)` | the element name |
| `object_start` after `key("attributes")` | attributes open; each `key(k)` + `value(v)` is `attribute(k, v)`; `true` is a value-less attribute |
| `array_start` after `key("children")` | children open |
| `value(s)` inside `children` | `text(s)` |
| `object_end` of the element | `element_end` (self-closing form when no `children` was opened) |

For the Bun tree the same adapter classifies by key: a key starting with `'@'` is an attribute, `'#text'` is text, any
other key is a child element (an array value means repeated elements); order inside an object follows insertion order,
which is Bun's own limitation (text and elements of mixed content cannot be interleaved faithfully).

The flat list shape (array of entries with `/x` end markers) is the same walk with the array as root; the adapter
keeps the flat-list rule (self-closing iff the next entry is this element's own end marker) and `maxDepth`.

Steps, each green before the next:

| step | change | gate |
| --- | --- | --- |
| 4a (done) | `git mv` `walk.[ch]` to `jwalk.[ch]`, rename `walk_*`/`Walk*` that are JSON-rule specific to `jwalk_*`/`Jwalk*`; update `quickjs-json.c`, CMake, `doc/native/json.md`, `utilities/quickjs-json.md` | `test-json.js` unchanged and green |
| 4b (done) | extract `walk.[ch]` (stepper, stack, skip, circular, interface) and make `jwalk.c` call it with the three hooks | `test-json.js` green; differential `JsonWriter.stringify` vs `JSON.stringify` on the fuzz corpus unchanged |
| 4c (done) | element-tree walking as `xwalk.[ch]` on `walk.h` hooks (not in `walk.c`) | `walk` has no JS surface, so the cases (getters, back-links, undefined, nested array as value, root array) were checked with a C harness; the JS-level `test-walk.js` cases ride on 4d's XML write tests |
| 4d (done) | `xwalk.c` adapter into `xwrite`; `write(tree|list, maxDepth)` switches over (`XMLTreeSerializer` is Phase 5); delete the traversal half left by 3d: `js_xml_write_tree/list`, `js_xml_write` body, `xml_enumeration_next`, `xml_num_children` | golden test byte-identical, including `lib/dom.js` nodes; fuzz `write(read(x))` vs the old implementation equal | **Done: the tree goes through `xwalk` (`xwrite_tree` in `src/xml/xtreewrite.c`, lazy start tag, `walk_skip` at maxDepth), both emit via `xlay_*` (the flat list walks through `xwalk` too, `xwrite_tree(..., flat, ...)`).**

Risk: medium for 4b (it moves the `JSON.stringify` path; steps 4a and 4b are pure refactors and need no behaviour
change), low for 4c and 4d. Saves about 450 lines in `quickjs-xml.c`; `walk.c` plus `jwalk.c` stay about the size of
today's `walk.c` (834 lines) plus about 100 for the shape; `xwalk.c` adds about 80.

### Phase 5: `XMLTreeSerializer` on `xwalk` + `xwrite` (mirrors the `JsonSerializer` rewrite)

- One step per `read()` call into an unbounded `DynBuf` served in pieces; delete the replay machinery
  (`skip`, `delivered`, `XMLCappedBuf`, the three-writer chain). Same `read(n)` / `read(buffer, offset, length)` /
  `finished` / `root` surface.
- Deletes `XMLCappedBuf`, `xml_write_capped/skip`, `xsw_*`, `xmlserializer_*` (what 3e left in place).
- Test: the existing serializer tests, plus equality with `write()` for every golden input at read sizes 1, 7, 4096.
- Risk: low once phases 3-4 are in. Saves about 450 lines.

### Phase 6: push side and the two tokenizers

- `XMLPushParser` onto `xbuild_*` (done in phase 2) and `xr_read`, `close()` via `xr_finish`.
- Measure first: run the differential harness `read(x)` vs `XMLPushParser` fed in 1, 2 and 7 byte chunks and list the
  differences caused by `xread.c` lacking tolerant mode, implied ends, flat mode, raw text and locations.
- Decision after the measurement: (A) keep both tokenizers like `json.c`/`jread.c` and document the differences, or
  (B) drive `xml.c` from a push `Reader` (it is already resumable through `XML_PARSE_AGAIN`) and delete `xread.c`/`xread.h`
  (409 + 92 lines, one parser fewer to keep equal). Recommendation: B, unless the measurement shows `xread` is faster by
  a margin that matters.

### Phase 7: share the event plumbing of `XMLParser` and `XMLNodeParser`

- `xml_parser_build/callback`, `xml_nodeparser_sync_location`, the location glue and the options parsing are written
  twice; one `XmlParserBase` (reader, `XMLParser`, loc, filename) for both classes.
- Saves about 150 lines; behaviour unchanged.

### Phase 8: `xrewrite` (mirrors `jrewrite`, Tier 17)

- Pure C filter between parse events and `xwrite`: drop elements/attributes/comments, rename, force void-tag
  self-closing, minify or pretty-print, HTML to XML. Chain of rewriters. Constructor form
  `new XMLParser(input, writer)` / `new XMLPushParser(xmlWriter)` with no JS call per token (counting test as in Tier 17).
- Do it after `jrewrite` exists so the two share the design (state, depth gate, hook).

### Phase 9: `XML.parse` / `XML.stringify` (Bun's object tree) as a mode of the same pieces

- The shelved implementation in `attic/quickjs-xml-object.inc.c` (about 2000 lines, its own parser and writer) is the
  spec and the test source (`attic/xml-object-tests.js`, expectations taken from bun 1.4.2). On the new pieces it is
  an `xbuild` object mode (attributes as `'@name'`, `'#text'`, repeated names to arrays, whitespace-only runs next to
  children dropped, strict XML 1.0 plus DOCTYPE entities) and an `xwalk` object mode (arrays repeat the element, node
  form `{name, attributes, children}`), about 300 lines of binding in total.
- The strict XML parser (errors as `SyntaxError: XML Parse error: ...`) is not `xml.c`, which is lenient by design:
  add a `strict` option to `xml.c` (reject unquoted attributes, duplicate attributes, undeclared entities, `--` in
  comments, text after the root, more than one root; declare entities from the DOCTYPE subset) rather than a second parser.

### Phase 10: clean-up

- Remove what the earlier phases leave unused; keep `quickjs-xml.c` to classes and conversions.
- Decide about the JS ports `lib/xml/read.js`, `lib/xml/write.js` and `lib/lexer/xml.js`: they mirror `read()` and
  `write()` (see `cmake/qjsm.cmake`, `lib/xml/read.js`); `lib/xml/write.js` is the second oracle of step 3d. Either keep them as a pure-JS oracle used by the harness, or
  delete them once the harness uses the git oracle.
- ChangeLog, `doc/native/xml.md`, `BUGS` (add what the phases find but do not fix).

### Order and checkpoints

| Phase | Depends on | Lines out of `quickjs-xml.c` (approx.) | Visible behaviour change |
| --- | --- | --- | --- |
| 0 baseline (golden corpus done as 3a; harness script still to save) | none | 0 | none |
| 1 event protocol | 0 | 0 (adds a header and two adapters) | none (`xr_type_t`, `xml_event_t` stay as aliases) |
| 2 `xbuilder` | 1 | 400 | `XMLParser.root`, `XMLPushParser.root` shapes |
| 3 `xwrite` (3a, 3b done) | none (Phase 1 only for `xwrite_interface()`) | 350 | `XMLWriter` escapes, writes value-less attributes bare, `indent < 0` stops breaking at level 0 |
| 4 `xwalk` (4a to 4c done) | 3 | 450 | none (golden) |
| 5 `XMLTreeSerializer` | 3, 4 | 450 | none |
| 6 push side | 2 | 100 (+ 500 if `xread` goes) | `XMLPushParser` HTML handling if option B |
| 7 plumbing | none | 150 | none |
| 8 `xrewrite` | 3, 6, Tier 17 | adds | new feature |
| 9 object tree | 2, 4 | adds about 300 | new feature (`XML.parse`, `XML.stringify`) |
| 10 clean-up | all | rest | none |

Order of work: see Status above (3c, 3d, 4d, 5, then 2 and 1, then 6 to 10). Phases 0, 3a, 3b, 4a to 4c are done.

## Tier 21 — `vfs` follow-ups (lib/vfs.js is now the `node:vfs` provider model, 2026-10-17)

- `vfs.mount()`/`unmount()`/`mountPoint`: redirect `fs` and the module loader for paths
  under a mount point (Node's `node:vfs` does both). Needs a `registerHooks` resolve/load
  hook for imports and a patched `fs` dispatch; not started.
- `createReadStream`/`createWriteStream` must be WHATWG streams (see CLAUDE.md), `watch`,
  `opendir`, `openAsBlob`, `registerProvider`: missing versus `node:vfs`.
- `quickjs-syscallerror.c`: `ELOOP` has no name (see `BUGS`).

## Tier 22 — `path`: shish names in C, Node/Bun/Deno interface (plans: doc/api-implement-path.md, doc/api-interface-path.md, 2026-10-17)

Result: no `path_*` C function ends in a digit and the names common with `~/Sources/shish/lib/path.h`
mean the same; `src/path-lib.c` is shish's `path.h` and nothing else; `src/path.c` is gone; `path` and
`node:path` have the surface and behaviour of Node's `node:path` (posix and win32).
Rules: one step is one commit and ends with `ctest -R path` green (plus `test-path-node.js` under
node/bun/deno once T2 exists); R steps change no behaviour, C/W/G steps change behaviour; a commit is
never both; a step that changes a result quoted in `doc/api-compare-path.md` or `doc/native/path.md`
updates that table; after each phase re-run `./dump-node-path.sh` and read `/tmp/node-path-surface/compare.txt`.

Phase T — tests and tools first
- [x] T1 `./dump-node-path.sh` (dumps node:path of node/bun/deno and `path` of qjsm to `/tmp/node-path-surface/`).
- [x] T2 `utilities/gen-path-golden.mjs` (run with node): prints golden tables (posix; win32 later) as
      JS source, inputs and node's outputs, for the tests below.
- [x] T3 `tests/unittests/test-path-node.js`, first line `// @portable`: the posix valid-input golden
      (basename dirname extname normalize isAbsolute parse join relative resolve format with the 234-case
      inputs of api-compare-path.md, `sep`, `delimiter`, `posix === path`); passes now under qjsm, node, bun, deno.
- [x] T4 `tests/unittests/test-path-interface.js` (qjsm only): `Function.length`, error name/code/message
      (Node's text), exported name list; starts with the cases that pass today, a failing case is
      added in the commit of its fix.
- [x] T5 (done 2026-10-17, table in doc/api-implement-path.md 3.3.1) audit (was Tier 22 task 1): grep `lib/`, `examples/`, `utilities/`, sibling `qjs-*`, `~/Sources/plot-cv`
      for `at components skipSeparator right isSeparator isin equal canonical search getsep skip toArray
      length slice`; write keep/remove per method into section 3.3 of doc/api-implement-path.md.
- [x] T6 (done by hand, not kept as a test: coreutils `realpath` on a symlink tree matched; the old fnmatch looped forever on `[[:class:]]`, the shish one is in `test-path.js`) differential tests before the rename: old `path_fnmatch5` against shish's `path_fnmatch` (patterns,
      `FNM_PATHNAME` `NOESCAPE` `PERIOD`); old `path_realpath3` against `path_resolve(p, n, db, 0, 0, 0)`
      (existing, missing, relative, symlinked).

Phase R — C renames (behaviour-neutral), in this order
- [x] R1 rename the 1-argument macros `path_isabs`, `path_isname`, `path_isrelative` to `path_isabs_s`,
      `path_isname_s`, `path_isrel_s` at all 13 call sites (`src/qjsm.c:1592,1602,1783,1785,1825,1917,1962,2141,2274`,
      `src/child-process.c:266`, `quickjs-path.c` ×3); nothing else.
- [x] R2 `src/path-lib.c`: add `path_isabs(p, n)`, `path_isrel(p, n)`, `path_isname(p, n)` from `shish/lib/path/`;
      define the `_s` macros through them; replace `path_isabsolute2` (`quickjs-path.c` ×2) and delete
      `path_isabsolute1/2`.
- [x] R3 `src/path-lib.c`: port `path_next`, `path_parentlen` (no client yet; used in R10).
- [x] R4 `path_fnmatch`: port shish's into `src/path-lib.c` after T6 passes; the one client
      (`quickjs-path.c` fnmatch) switches; delete `path_fnmatch5` from `src/path.c`/`include/path.h`
      (done; the old one looped forever on `[[:class:]]`).
- [x] R5 new `src/path-sys.c` (banner: non-lexical, `DynBuf` where shish has `stralloc`): move `path_exists(p, n)`,
      `path_isfile(p, n)`, `path_issymlink(p, n)` and the `_s` macros there; switch `src/qjsm.c:343,975,1393,1785`,
      `quickjs-path.c` (exists ×2, issymlink), the `path_search` callback; delete `path_exists1/2`, `path_isfile1`,
      `path_issymlink1`.
- [x] R6 `path_getcwd(DynBuf*)` in `src/path-sys.c`; clients `quickjs-path.c` ×2, `src/qjsm.c:1003`; delete
      `path_getcwd0/1`.
- [x] R7 `path_readlink(p, DynBuf*)`; client `quickjs-misc.c:970`; delete `path_readlink2`.
- [x] R8 `path_gethome(int uid)` in `src/path-sys.c` (reads `/etc/passwd`), `path_homeof(user, n)` in
      `src/path-sys.c`; `src/compat/glob.c` takes `~` from `getenv("HOME")` else `path_gethome(getuid())`,
      `~user` from `path_homeof`; `quickjs-path.c:312`; delete `path_gethome()`/`1`/`2` of `src/path.c`.
- [x] R9 `path_resolve(p, n, DynBuf*, symbolic, cwd, cwdn)` in `src/path-sys.c`; JS `realpath` keeps its
      existence check; delete `path_resolve3`, `path_realpath3`.
- [x] R10 JS-only helpers become `static` in `quickjs-path.c` (or go with their JS method, test and doc row,
      per T5), one commit per family: (a) `length components at slice toArray`, (b) `skip skipSeparator right
      isSeparator getsep`, (c) `isin equal` (`path_isin4`, `path_diff4`/`path_equal4`); `path_dup2/3` go with them.
- [x] R11 `path_compare2`, `path_skipdotslash1/2` become `static` in `src/qjsm.c` (`:531`, `:1775`; try `path_canon`
      for the second).
- [x] R12 delete `src/path.c` and the digit-suffixed declarations and 14 `static inline` helpers of
      `include/path.h`; check the CMake source list; verify `rg 'path_[a-z_]*[0-9]\('` over `src/ include/ quickjs-*.c`
      is empty and `utilities/api-surface.sh` on `path` differs only by methods removed in R10; update `doc/native/path.md`.

Phase C — posix interface of `path` made Node-correct (changes behaviour; needs R1-R2, T3, T4)
- [x] C1 `quickjs-path.c`: helper `js_path_check_string(ctx, v, name)` throwing `TypeError` with `code`
      `ERR_INVALID_ARG_TYPE` and Node's message (`The "path" argument must be of type string. Received type
      number (5)` / `Received null` / `Received an instance of Object`); applied to `isAbsolute` only.
- [x] C2 the same for `dirname`, `extname`, `basename` (`path`, and `suffix` when given).
- [x] C3 the same for `parse` and `join` (every argument, name `path`).
- [x] C4 compare `normalize relative resolve format` error code/message with Node's (`from`/`to`,
      `paths[0]`, `pathObject` of type object); fix the differences. Left: `relative(x)` with one argument is
      qjsm's own form (x against the cwd; `utilities/resolve-imports.js` uses it), Node throws for the missing
      `to`; `format({ base: 5 })` returns `"5"`, Node returns the number 5.
- [x] C5 `Function.length` as Node: `basename` 2, `join` 0, `relative` 2, `resolve` 0, the rest 1.
- [x] C6 `toNamespacedPath` (identity after validation) and `_makeLong` as the same function object.
- [x] C7 subpath table in `src/qjsm.c` (next to `fs/promises`): `path/posix` to `path`; test
      `import('path/posix')` and `import('node:path/posix')`; add the rows to the comment table of `lib/loaders/node.js`.
- [x] C8 `BUGS`: strike `path-functions-coerce-non-string-arguments` and the posix part of `path-missing-node-names`
      (leave `win32` and `matchesGlob` in the entry until W11/G2). Verify with `rg -n 'path-functions-coerce' BUGS`.
- [ ] C9 DECISION (user): `path.exists` follows symlinks (`stat`) like `fs.existsSync`, or keeps `lstat`?
      If it follows: change `quickjs-path.c` only (`PATH_EXISTS` calls `stat`); `path_exists` (lstat) stays for the
      C callers (module search); add a dangling-symlink case to `test-path.js`; strike
      `path-exists-true-for-dangling-symlink` in `BUGS`; fix the `exists` row in `doc/api-compare-path.md`
      section 2 and `doc/native/path.md`. If it keeps lstat: document it as a deliberate difference in both docs
      and strike the `BUGS` entry as "won't fix".
- [ ] C10 DECISION (user): `relative(x)` with one argument (qjsm's own form, x against the cwd). Node throws for
      the missing `to`. If strict: change the four calls in `utilities/resolve-imports.js` (lines ~1971, 2079, 2360,
      2380) to `path.relative(process.cwd(), x)`, drop the `argc == 1` branch in `PATH_RELATIVE`, add the Node
      error case to `test-path-interface.js`. If kept: list it in `doc/api-compare-path.md` section 2 as an extra.

Phase W — `path.win32` (needs T2, C7; all steps are JS, no C until W10)
Port source: node v22.23.3 `lib/path.js` (fetch with `curl -sL https://raw.githubusercontent.com/nodejs/node/v22.23.3/lib/path.js`;
MIT, credit and version in the file header). A JS port of the `win32` object only: no primordials, ES module, no
`internalBinding`; `process` is imported lazily and only read inside `resolve` and `toNamespacedPath`.
Every step ends with its golden slice green in `tests/unittests/test-path-win32.js` under qjsm, node, bun, deno.
- [ ] W1 `utilities/gen-path-golden.mjs --win32`: golden rows for `path.win32`; inputs: drive letters (`C:\a\b`,
      `c:/x`, `C:`, `C:x`, `C:\`), UNC (`\\srv\share\x`, `//srv/share/`, `\\srv`), `\\?\C:\x`, `\\.\COM1`, mixed
      separators, trailing separators, `..` above the root. `resolve` and `toNamespacedPath` rows carry their
      injected state (`{ cwd: 'C:\\work', env: { '=C:': 'C:\\work', '=D:': 'D:\\other' } }`; the generator stubs
      `process.cwd` and `process.env`, the test does the same), never the real environment.
- [ ] W2 `lib/pathWin32.js` scaffold: `sep` `\`, `delimiter` `;`, `isAbsolute`, `toNamespacedPath`, `_makeLong` (the same
      function), shared helpers (`isPathSeparator`, `isWindowsDeviceRoot`, `normalizeString`); new
      `tests/unittests/test-path-win32.js` (`// @portable`, imports `../../lib/pathWin32.js`) with those slices.
- [ ] W3 `normalize`, `join`.
- [ ] W4 `resolve` (per-drive cwd from `process.env['=C:']`, falls back to `process.cwd()`); injected-state rows only.
- [ ] W5 `relative`.
- [ ] W6 `dirname`, `basename` (with suffix), `extname`.
- [ ] W7 `parse`, `format`; the same ERR_INVALID_ARG_TYPE helper as C1 in JS (message text identical to the C one).
- [ ] W8 `posix` / `win32` cross links: `win32.win32`, `win32.posix` (the native module), `posix.win32` (set in W10).
- [ ] W9 register the builtin: `pathWin32` in `BUILTINS_COMPILED`, `set(pathWin32_MODULES process)` in
      `cmake/qjsm.cmake`; `path/win32` in both tables of `src/qjsm.c` (next to `path/posix`); row in the comment
      table of `lib/loaders/node.js`. Check: `qjsm -l` lists it; `import('path/win32')` and `import('node:path/win32')`.
- [ ] W10 `path.win32` lazy getter in `quickjs-path.c` (`globalThis.requireModule('pathWin32')`, as `lib/fs.js` does
      for `fs.promises`), on the module, `default` and `posix`; the portable win32 test switches to
      `import path from 'path'; const { win32 } = path;`. Add `win32` to the shared-names assertion in
      `test-path-interface.js`.
- [ ] W11 docs: `win32` rows in `doc/native/path.md`, `doc/api-compare-path.md` (surface table, findings),
      `doc/api-interface-path.md` (mark done); strike `path-missing-node-names` in `BUGS` except `matchesGlob`.
- [ ] W12 re-run `./dump-node-path.sh`: `compare.txt` must list only `matchesGlob` (until G2) and the qjsm extras.

Phase G — `matchesGlob` (independent, last)
- [ ] G1 glob engine (`*` `**` `?` `[...]` `{a,b}` leading `!`) as a pure function (JS first: `lib/glob.js`, no native
      deps, portable); golden table from node's `path.matchesGlob` via `utilities/gen-path-golden.mjs --glob`
      (patterns x paths, posix and win32 separators); shared with `Bun.Glob`, `fs.glob`, `Bun.Archive` (Tier 19):
      reuse Tier 19's engine if it exists first, else this one is that engine.
- [ ] G2 `path.matchesGlob(path, pattern)` on `path`, `posix` and `win32` on top of G1 (`quickjs-path.c` calls the
      builtin lazily, like `win32`); validation as C1 with names `path` and `pattern`; `Function.length` 2;
      `test-path-node.js` gets the golden rows; strike the rest of `path-missing-node-names` in `BUGS`.

Phase X — the qjsm-only extras (needs T5; removals are breaking and need the user's go per method)
The 38 names no runtime has (api-compare-path.md section 2). Rule of CLAUDE.md "Choosing the Next Task": no user
in `examples/ utilities/ lib/ ~/Sources/plot-cv` and no sibling `qjs-*` plus no standard target means remove.
- [ ] X1 (table written 2026-10-17, doc/api-implement-path.md 3.3.1; waiting for the user's answer) apply the T5 table: list per method keep / remove with the usage found (file:line) in section 3.3 of
      `doc/api-implement-path.md`; show it to the user, nothing is deleted before the answer.
- [ ] X2 per approved family, one commit each: delete the JS method(s) and their `static js_path_*` helper in
      `quickjs-path.c`, the cases in `test-path.js`, the rows in `doc/native/path.md` and
      `doc/api-compare-path.md`; families: (a) component helpers `at components length slice toArray`, (b) offsets
      `baselen basepos dirlen extlen extpos`, (c) scanning `skip skipSeparator right isSeparator getsep`,
      (d) comparison `isin equal`, (e) `absolute canonical isRelative` (covered by `resolve` / `normalize` /
      `!isAbsolute`), (f) filesystem extras that have a standard home `readlink realpath getcwd gethome exists`
      (`fs.readlinkSync`, `fs.realpathSync`, `process.cwd`, `os.homedir`, `fs.existsSync`), (g) the `is*` type
      tests (`fs.Stats`). Check the Tier 22 usage grep again right before each deletion.
- [ ] X3 after each removal: `utilities/api-surface.sh` collect on the build before and after, `diff` is the changelog
      entry (members removed means a major bump).

Phase F — closing Tier 22
- [ ] F1 `rg 'path_[a-z_]*[0-9]\(' src include quickjs-*.c` is empty, `ls src/path*.c` is `path-lib.c path-sys.c`
      (checked 2026-10-17, repeat after phase X).
- [ ] F2 `utilities/api-surface.sh` on the install tree of the commit before Tier 22 and on the last one;
      `diff` goes into the release notes; suggested bump per its summary line.
- [ ] F3 `ctest` full run against `/tmp/ctest-baseline.txt` (24 failures that predate Tier 22: wasm, wasi, mmap,
      lexer-cpp, directory, extract-c, globals, misc, and the `.deno` cases under `ulimit -v`).
- [ ] F4 skill follow-ups (need the user's approval): `path` entry in the `quickjs-scripting` skill's `modules.yaml`
      and `deno-import-map.json`; a note in `quickjs-native-bindings` that a Node-compatible error is built from
      `js_path_invalid_arg`-style code (message, `code` property set on the thrown object).

Runtime notes (no code unless stated)
- Bun: nothing to add, Bun's `path` is Node's; do not copy its `bound X`/`length 1` quirks. Re-run the
  `.bun` ctest after every C/W step.
- Deno: `tests/deno-import-map.json` already maps `path`. Needs the user's approval: add `path` to the
  `quickjs-scripting` skill's `deno-import-map.json` and a `path` entry to its `modules.yaml`.
  Optional, only if something needs it: `lib/stdPath.js`, a port of `jsr:@std/path` extras (`common`,
  `SEPARATOR`, `toFileUrl`, `globToRegExp`), in the style of `lib/jsonStreams.js`.

## Tier 23 — parse errors and locations for `json` and `xml` (planned 2026-10-17)

Requirement: every JSON/XML parser has proper exception handling and location tracking; a parse failure is a `SyntaxError` with an own,
non-enumerable `loc` (a `Location`), plus `length` and a stable `code`. Findings (how Browser, Bun, Node, Deno and the packages report parse errors,
which of them have Location-like classes and custom exception classes): [`doc/api-compare-json.md`](doc/api-compare-json.md) sections 7 to 9 and
[`doc/api-compare-xml.md`](doc/api-compare-xml.md) sections 7 to 9. The contract: [`doc/api-interface-json.md`](doc/api-interface-json.md) section 11 and
[`doc/api-interface-xml.md`](doc/api-interface-xml.md) section 11. The plumbing and tests: `doc/api-implement-json.md` and `doc/api-implement-xml.md`, section 5.

Summary of the findings that shape the design:

- no runtime exposes a correct structured position on a parse `SyntaxError`; Bun's `line`/`column` on it are the throw site of the caller (same value
  for every input), V8 gives a snippet in the message only, SpiderMonkey puts `line N column M` in the message;
- structured positions exist in libraries: `json5` (`lineNumber`, `columnNumber`), `jsonc-parser` (`offset`, `length`, code), `@xmldom/xmldom`
  (`ParseError.locator`), `fast-xml-parser` (`{code, line, col}`), `parse5` (`onParseError`, `startLine`...`endOffset`, spec code names), Bun's
  bundler (`BuildMessage.position`, `AggregateError.errors`);
- custom exception classes appear in tools and libraries (`ParseError`, `TokenizerError`, `BuildMessage`), the platform keeps `SyntaxError`;
- browsers never throw for malformed XML (`DOMParser` returns a `<parsererror>` document) and HTML parsing never fails.

Defects found on the way (in `BUGS` or fixed by step E0): `read()` throws `InternalError`; bad arguments throw `ReferenceError` in four places
(`test-xml.js` line 153 pins this and changes with the step); `JsonParser` reports the position after the token (2:8 for 2:7); `JsonPushParser` and
`XMLPushParser` errors carry no position; `XMLPushParser.write()` is silent on a mismatch.

| Step | Change | Depends on | Gate |
| --- | --- | --- | --- |
| E0 (done) | `read()` throws `SyntaxError`, never `InternalError`; `TypeError` for bad arguments (`json.read`, `xml.read`, `JsonPushParser.write`, `XMLPushParser.write`); `JsonWriter` write failure not `InternalError` | none | done 2026-10-17: arguments are checked by type (`js_is_bytes_or_string`, in `utils.h`) instead of being stringified, so `read(5)`, `read(null)`, `read({})` are `TypeError`; empty text is a `SyntaxError`; the message of `read()` errors is `name:line:col: text`; a write failure is an `Error` with `code: 'write-failed'`; tests added in `test-json.js` and `test-xml.js` |
| E1a (done) | `include/srcpos.h` (`SourcePos`, `srcpos_init(pos, utf16)`, `srcpos_advance`: `\n`, `\r\n` as one, lone `\r`; UTF-8 aware, a split inside a character or between `\r` and `\n` counts the same; `utf16` flag counts a 4-byte character as 2) with `tests/c/test-srcpos.c`, run by ctest as `c-srcpos` (CMake builds every `tests/c/test-*.c`) | none | chunk invariance for every split and piece size; two mutations of the header make it fail |
| E1b | done: `js_syntax_error_new` / `js_throw_syntax_error` in `quickjs-location.[ch]`; `Location` counts through `srcpos_advance` (`\r`, `\r\n` as one break) and `location_from_srcpos()`; first consumer `xml.read` mismatch (`mismatched-end-tag`) | E1a | `test-xml.js` |
| E1c (done) | `jread`: `SourcePos pos/tok`, `err_code` (`jr_error_t`, `jr_error_name()`: unexpected-token, bad-escape, bad-unicode-escape, invalid-utf8, control-character, invalid-literal, unexpected-end), `err_pos` (token start; the offending byte outside a token), `err_len` (bytes); `xread`: `pos`, `err_code` (unexpected-character, unexpected-end), `err_pos` = offending byte, `err_len`; `json.c`: `tok_start`/`err_at`/`err_len`/`err_code` (0-based, `JsonPos`); `xml.c`: `err_code`/`err_len` with `tag_line`/`tag_column`, `parse_getc` counts through `location_count` (`\r\n` one break, columns in characters) | E1a | `tests/c/jread-positions.c` (ctest `c-jread-positions`, 5 chunk sizes per case); `xread`/`json.c` fields have no test until E1d exposes them; `xread` stays token-less (offending byte only) because Tier 20 phase 6 may delete it |
| E1d (done) | `json.read` (`parse_throw` over `sj`: position `r->cur`, codes unexpected-token / unexpected-end / trailing-data), `JsonParser` (`err_at`, token start), `JsonPushParser` write/close (`jrs.err_*`, chunk-invariant), `JSONL` newline error, `xml.read` empty input and mismatch, `XMLPushParser` write/close (`xrs.err_*`); message is `<text> at <file>:<line>:<column>`; bad arguments stay `TypeError`. Not converted: `JSONL` value errors come from `JS_ParseJSON` (engine message, no `loc`); `XMLParser` without input is a `TypeError` | E1b, E1c | `test-json.js`, `test-xml.js` (a separate `test-*-errors.js` comes with E2) |
| E2 (partly done) | done: `JsonPushParser` `onError(err)` (builder and callback mode; `write()`/`close()` then do not throw) and `.error`; `XMLPushParser` `onParseError(err)` and `.error`; `XMLParser.error`, `XMLNodeParser.error` after `PARSE_ERROR`; `JSONL.parseChunk().error` and `JSONL.parse()` carry `loc` (start of the failing value, `length` spans it, code `invalid-json`). Open: lenient `xml.read` repairs to `onParseError` (needs `xml.c` to record repairs); `Blob.json()` rejection with `loc` (needs the strict `jread`, Tier 18 step 2); the old raw `error` key callbacks stay as they are | E1d | `test-json.js`, `test-xml.js` |
| E3 | `AggregateError` for collecting use; strict `XML.parse` / `JSON5.parse` / `JSONC.parse` use the same `code`s | Tier 18 step 3, Tier 20 phase 9 | differential: which inputs are errors against Bun |
| E4 | `DOMParser.parseFromString(s, 'text/xml')` returns a `<parsererror>` document and does not throw (`lib/dom.js`) | Tier 20 DOM export | test against the browser's observable shape |
| E5 | decide the column unit (code points today, UTF-16 recommended) and apply it in `Location`; `SourcePos` already has the switch (`utf16`), so for the parsers it is the argument of `srcpos_init` | E1b | astral-character tests; `JSONL.parseChunk().read` agrees with `loc.charOffset` |

Open decisions: the column unit (E5); whether lenient XML repairs use the HTML spec's code names or their own (recommended: spec names where one
exists); whether `loc` also gets an `end` `Location` (recommended no: `length` suffices, `jsonc-parser` shape).


## Tier 24 — `lib/loaders/` for Bun / Node / Deno compatibility (listed 2026-10-18)

Existing: `commonjs`, `file`, `node` (`node:` prefix), `package-aliases` (`_moduleAliases`, a qjs-modules-ism), `trace`, `typescript`, `wasm`.
Each new loader is a `registerHooks()` resolve/load pair like `file.js`, runs on qjsm only, and needs a test plus `doc/js/` page.
Survey Bun, Node and Deno (`node -e`, `bun -e`, `deno eval`) before writing any of them, as the project rules say.

| Step | Loader | Standard / runtime target | Notes |
| --- | --- | --- | --- |
| L1 | `loaders/text`: `with { type: 'text' }` and `{ type: 'bytes' }` | Bun, Deno, TC39 import-text / import-bytes proposals | copy of `file.js`, reads the file; also `type: 'json'` if the engine does not handle it natively (check first) |
| L2 | `loaders/import-map`: WHATWG import map (`imports`, `scopes`) | browsers, Deno `--import-map`, partly Bun and Node | would replace `package-aliases.js`; `tests/deno-import-map.json` already uses the format |
| L3 | `loaders/package-imports`: `package.json` `"imports"` (`#internal/x`, conditions) and `"exports"` for `node_modules` packages | Node, Bun, Deno | the other way to retire `_moduleAliases`; resolve hook only |
| L4 | `loaders/data-url`: `import('data:text/javascript,...')` | Node, Deno, browsers | small resolve + load hook; handy for inline tests |
| L5 | `loaders/config`: `with { type: 'toml' \| 'yaml' \| 'json5' \| 'jsonc' }` | Bun (toml, yaml) | thin composition over the json / xml engines in `lib/`, no new C code; `lib/jsonl.js` fits here; depends on Tier 12/13 for yaml |
| L6 | `loaders/tsconfig-paths`: `compilerOptions.paths` and `baseUrl` | Bun, Deno | pairs with `typescript.js` |
| L7 | `loaders/cjs-typescript`: `.cts`, format `commonjs-typescript` | Node (type stripping), Bun, Deno | `typescript.js` says it does not handle it |

Not a compatibility item, kept for later: `loaders/mock` (`mock.module(specifier, factory)` like `node:test`) for tinytest.
Out of scope here: `http(s):` imports need fetch, which lives in `../qjs-lws`.

## Tier 25 — CommonMark parser: `lib/lexer/markdown.js` + `lib/parser/markdown.js`, `Bun.markdown` (planned 2026-10-20)

Target: `Bun.markdown.html(src)` and `Bun.markdown.render(src, callbacks)` (Bun 1.4.2, probed); Node and Deno have none.
Pure CommonMark 0.31.2 first; the GFM extensions Bun enables (tables, `~~strike~~`, task lists, extended autolinks) are
a follow-up against the GFM spec. `ansi` and `react` are out of scope.

| File | Holds |
| --- | --- |
| `lib/lexer/markdown.js` | `MarkdownLexer extends Lexer` (inline tokens), `MarkdownBlockRules` (line-start regexes), entity decoding; no structure |
| `lib/parser/markdown.js` | `MarkdownParser`: block phase, inline phase, AST (commonmark.js node types), `{ entering, node }` walker |
| `lib/markdown.js` | `Markdown.html(src)`, `Markdown.render(src, callbacks)`; `lib/bun.js` wires it as `Bun.markdown` |

Split rule, verified against the native `lexer`:

- container prefixes (`>  - `) and tab stops at partial columns (spec 2.2) need column arithmetic: parser, not lexer
- the lexer's regex has no `\p{...}`: `/[\p{P}\p{S}]/u` matched `a.b` as one word, so left/right-flanking checks of
  delimiter runs use JS regexes in the parser
- inline tokens: `escape`, `hardbreak`, `softbreak`, `backticks`, `delim` (`*`/`_` runs), `linkOpen`, `imageOpen`,
  `bracketClose`, `parenOpen/Close`, `autolink`, `htmlInline`, `entity`, `text` (fallback for a lone `<` or `&`)
- block rules: ATX heading, setext underline, thematic break, fence open/close, bullet and ordered list markers, `>`,
  blank, link reference definition start, the 7 HTML block start/end conditions

`render()` callbacks and meta, as Bun passes them (bottom-up, `(children, meta)`):
`heading {level}`, `code {language}`, `list {ordered, start, depth}`, `listItem {index, depth, ordered, start, checked}`,
`link {href, title}`, `image {src, title}`, `paragraph`, `blockquote`, `hr`, `emphasis`, `strong`, `codespan`, `text`.

| Step | Work | Verify |
| --- | --- | --- |
| M1 | lexer + entity table | token dumps; spec section "Entity and numeric character references" passes |
| M2 | block phase (containers, lazy continuation, setext, link ref defs, tight/loose lists) | block sections of the spec pass |
| M3 | inline phase (code spans, delimiter + bracket stack, rule of 3, destinations/titles, label normalization) | whole spec passes |
| M4 | `lib/markdown.js`, `Bun.markdown` in `lib/bun.js` | `render()` callback tests; diff with `bun` HTML on the spec inputs (bun is md4c: a diff is a finding) |
| M5 | `doc/js/markdown.md`, `doc/js/bun.md` section, `cmake/qjsm.cmake` compatibility line | — |

Test: `tests/unittests/test-markdown.js` (tinytest, not `@portable`: needs the native `lexer`), pass counts per spec
section.

### Findings 2026-10-20: reuse commonmark.js instead of writing the parser

commonmark.js (`github.com/commonmark/commonmark.js`, BSD-2, 0.31.2-18-g8988869, the spec's reference implementation) was
cloned and tried on qjsm, node and bun. Everything below was run, not read.

| Question | Result |
| --- | --- |
| size | `lib/` 3233 lines, 9 ES modules (`blocks.js` 1045, `inlines.js` 1084, `node.js`, `common.js`, `render/{renderer,html,xml}.js`) |
| dependencies | `entities` ~7 (only `decodeHTMLStrict`: `decode.js` + generated trie, about 60 KB) and `mdurl` ~1 (only `encode.js`, 2 KB, CommonJS) |
| spec and entities | the repo ships `test/spec.txt` (652 examples), `regression.txt`, `smart_punct.txt`; `entities` has the full HTML5 table. Both questions above are answered by the submodule, nothing to download separately |
| conformance on qjsm | 652/652 spec examples pass (bun-bundled ESM plus an `atob` shim) |
| speed, parse + render | 1108 KB x3: qjsm 2149 + 463 ms (about 1.5 MB/s parse), node 173 + 50 ms, bun about 0.7x node. Roughly half of qjsm's parse time is the block phase, half the inline phase |
| qjsm gap | `entities` unpacks its trie with `atob` or `Buffer`: qjsm has neither global, `ReferenceError: 'Buffer' is not defined` at import. WHATWG `atob`/`btoa` are missing globals in qjsm, worth adding on their own |

**Amalgamating with `resolve-imports.js --recursive --remove-exports`: not usable yet.** Run from the clone with
`-M entities=... -M mdurl/encode.js=...` and an entry file of plain `import`s:

- `"use strict";` as the first statement ends the import scan (`utilities/resolve-imports.js:2161`, the first `;` outside an
  import sets `doneImports`). Every commonmark.js file starts with it, so only the entry's direct imports were inlined. With
  the line stripped all 21 files are found (186 KB output)
- `export { default as X } from './x.js'` re-exports in the entry file are not followed
- `--remove-exports` leaves the `export` keywords of the inlined files (`export default Node;`, `export function`,
  `export { ... } from`): 44 remain, qjsm: `SyntaxError: duplicate exported name 'decodeCodePoint'`
- no scope isolation or renaming: with every `export` stripped, 17 top-level names still collide across files (`tag`,
  `out`, `cr`, `text`, `render` in the two renderers, `fromCodePoint`, `encode`, `C_NEWLINE`, `reHtmlTag`,
  `unescapeString`, ...)
- CommonJS input is copied as is (`module.exports = encode;` from mdurl 1.x)

`bun build lib/index.js --format=esm` does it correctly (114 KB, 75 KB minified). Options, cheapest first:

1. no amalgamation: submodule `third_party/commonmark.js`, install `lib/*.js` as `lib/commonmark/` unchanged, resolve
   `entities` to its `decode.js` and `mdurl/encode.js` to a 10-line ESM copy. qjsm loads 9 files instead of 1
2. a checked-in bundle made by `bun build`, regenerated when the submodule moves
3. teach resolve-imports a bundle mode: directive prologue before imports, follow re-exports, strip `export` in inlined
   files, wrap each module in a function scope (or rename collisions), CJS `module.exports` to a default. Fixes the tool
   for every other vendored lib too, but is the most work

**Using the native `lexer` for speed: no gain.** commonmark.js already scans with regexes on the subject string:

| 1999 KB, commonmark.js's inline split (text run, newline, special char) | 732260 tokens |
| --- | --- |
| `Lexer.next()`, ids only | 226 ms |
| one JS sticky regex `/...|...|.../gy` | 198 ms |
| `for(const tok of lexer)` reading `tok.lexeme` | 674 ms |

Both run libregexp, and the JS-to-C call per token costs as much as the match. Replacing commonmark's `match()`
(`re.exec(subject.slice(pos))`) with a cached sticky/global regex at `lastIndex` also changed nothing on qjsm (2149 vs
2358 ms, noise): the time goes to the per-character JS logic (container matching, delimiter stack, node allocation), which a
tokenizer does not replace. So `lib/lexer/markdown.js` as a speed-up is dropped: it would only pay if the block or inline
phase itself ran in C.

**The fast path is a native engine, and Bun already chose it.** Bun's option names (`tables`, `strikethrough`, `tasklists`,
`wikiLinks`, `latexMath`, `underline`, `permissiveAtxHeaders`, `noIndentedCodeBlocks`, `noHtmlBlocks`, `noHtmlSpans`,
`collapseWhitespace`, `hardSoftBreaks`) are md4c's `MD_FLAG_*` set, and md4c's SAX-style callbacks (`enter_block`,
`leave_block`, `enter_span`, `leave_span`, `text`) are exactly a push source in `doc/api-event-model.md` terms and
exactly what `Bun.markdown.render()` feeds its callbacks from. md4c is MIT, one C file of about 6k lines, CommonMark 0.31
from 0.5.x (Debian here has `libmd4c0` 0.4.8, no headers). Fits "engines in C, APIs in `lib/`": `quickjs-markdown.c`
exports the push source, `lib/markdown.js` builds `html`/`render` on it, and the output matches Bun's byte for byte.

**Revised recommendation** (replaces steps M1-M3 above if accepted):

| Path | Speed on qjsm | Work | Matches Bun |
| --- | --- | --- | --- |
| A: commonmark.js as `lib/commonmark/` (option 1) + `lib/markdown.js` glue | about 1.5 MB/s | small: submodule, two shims, glue, spec test from `test/spec.txt` | CommonMark yes, GFM no (no tables, strikethrough, task lists) |
| B: md4c as a native engine + `lib/markdown.js` glue | C speed | binding + event adapter | yes, including the GFM flags |
| own parser (the plan above) | at best A's speed | largest | CommonMark only, until GFM is written too |

Start with A (it works today, 652/652). Take B when speed or the GFM flags matter. Either way the own lexer/parser pair
is not needed. The `lib/lexer/markdown.js` / `lib/parser/markdown.js` split stays an option only as a goal-3 exercise
(lexer/parser toolkit), not for speed or conformance.

## Tier 26 — `lib/module-lexer.js`: one scanner for ES module and CommonJS imports/exports (plan 2026-10-20)

`utilities/resolve-imports.js` (2437 lines) is a bundler, an import lister/checker and an import merger in one file,
with its own import/export parser inlined. That parser is the reusable part. Factor it out into `lib/` as **one core
scanner** in JS that reports ES module syntax and CommonJS `require`/`exports` in one record shape. CommonJS export
detection follows the rules of **merve**, the lexer Node 26 uses for it (the rules, not the code: merve is C++, this
project is C only). **Thin adapters** give the core the API surfaces existing code calls: merve's, es-module-lexer's,
cjs-module-lexer's and Bun's `Transpiler.scan`/`scanImports`. The same records feed listing, checking, merging,
pruning and CommonJS → ES module conversion.

### Status (2026-10-20)

| Step | State | Where |
| --- | --- | --- |
| 1. core, ES module part | done | `lib/module-lexer.js` `parse()`, `tests/unittests/test-module-lexer.js` (22 cases); commits `4a635e35`, `efa530c2` |
| lexer fixes found on the way | done | `lib/lexer/ecmascript.js`: regex vs division, `{ }` in `${ }`, `$` in template text, any string escape, no exponential backtracking; `test-lexer-ecmascript.js` (15 cases) |
| 2. core, CommonJS part | done | `lib/module-lexer.js` `commonjs()`, `test-module-lexer.js` (40 cases) |
| 3. fixtures and the generator | open | |
| 4. adapters | open | |
| 5. `print()`, `merge()`, port of the tools | `resolve-imports.js` done, `list-exports.js` next | `lib/module-lexer.js`, `doc/js/module-lexer.md`, `test-module-lexer.js` (45 cases), `test-resolve-imports.js` (23 cases) |
| 6. CommonJS → ES module | open | |
| 7. resolution | package resolution done, import maps open | `lib/loaders/packages.js`, `import.meta.resolve` in `src/qjsm.c`, `test-loaders-packages.js` |
| 8. tool cleanup | open | |

`utilities/resolve-imports.js` runs on `parse()`, `print()` and `merge()` since 2026-10-20, ahead of Tier 27:
`FileMap` stays and gets byte ranges, converted from the UTF-16 offsets per file (`ByteOffsets`). The tool still
lexes a file itself, only for `-u` (identifiers outside import statements) and `-C` (comment ranges).

Checked on `lib/`, `utilities/`, `tests/unittests/` (229 files): no lexing errors, and the `(kind, path)` pairs of
the 211 files Bun accepts equal Bun's `scanImports` (743 imports). `parse()` costs about 35% on top of lexing.

### What the current parser gets wrong (run 2026-10-20, `qtool resolve-imports --list-imports -q`)

- **It stops at the first statement that is not an import/export** (`doneImports` in `ProcessFile`,
  `resolve-imports.js:2161`): `import a from './x.js'; const z = 1; import b from './y.js';` lists only `x.js`.
- **No CommonJS at all**: `require('./cjs.js')`, `require.resolve(...)` are not seen.
- **No dynamic import**: `import('./dyn.js')` is skipped on purpose (`imp[1].lexeme != '('`, line 2185).
- **Declarations are not exports**: only `export { ... }`, `export * ...` and `export name` are collected
  (line 2149), so `export const g`, `export function`, `export class` are missing.
- **Duplicated**: `utilities/list-exports.js:124-160` carries its own copies of `ImportFile`, `ExportName`,
  `ImportType`. `~/Sources/plot-cv/resolve-imports.js` is an older copy of the whole tool.

### Survey (probed 2026-10-20)

| Runtime / library | API | Returns | Lacks |
| --- | --- | --- | --- |
| Bun 1.4.2 | `new Bun.Transpiler({ loader: 'js' }).scanImports(src)` | `[{ kind, path }]`, kind is `import-statement`, `require-call`, `dynamic-import`; `require.resolve` only in `scan` (probed 2026-10-20) | bindings, ranges, exports; `export … from` counts as `import-statement` |
| Bun 1.4.2 | `.scan(src)` | `{ exports: ['default', 'f', 'g', 'w'], imports }` | in a file with module syntax the `require-call` entries are dropped; `export *` adds no name |
| merve 1.2.2 (github.com/nodejs/merve, C++20; vendored in Node 26 as `deps/merve`, `process.versions.merve`) | C++ `lexer::parse_commonjs(std::string_view)`; C `merve_parse_commonjs(input, length, &err_loc)` with `merve_get_export_name/_line`, `merve_get_reexport_name/_line`, `merve_get_last_error`, `merve_get_version`; Rust crate `merve::parse_commonjs(&str) -> Result<Analysis, LocatedLexerError>` | `{ exports: [{ name, line }], re_exports: [{ name, line }] }`, lines 1-based; on failure a `lexer_error` (`UNEXPECTED_ESM_IMPORT`, `UNTERMINATED_STRING_LITERAL`, ...) and an `error_location { line, column }` (column in bytes) | imports, offsets; ES module syntax is an error by design |
| cjs-module-lexer 2.3.0 (npm; Node 23's internal CJS lexer; 1.2.3 in Debian's `/usr/share/nodejs`) | `init(): Promise<void>`, `initSync()`, `parse(source, name?)` (1.2.3 has no `initSync`) | `{ exports: ['k', 'm', 'n'], reexports: ['./re.js'] }` | imports |
| es-module-lexer 3.0.4 (npm, Vite) | `init` (a Promise, not a function), `parse(source, name?)` | `[imports, exports, facade, hasModuleSyntax]`; import `{ type, specifier, start, end, importStart, importEnd, dynamicStart?, attributes, attributesStart, phase, glob?, typeOnly }`; export `{ type, name, localName?, start, end, localStart?, localEnd?, importName?, importNameStart?, importNameEnd?, from?, importIndex?, exportStart, typeOnly }` | import bindings, CommonJS |
| Node 26.11 | no public scanner. Internally `internalBinding('cjs_lexer').parse(src)` (merve, reachable with `--expose-internals`) returns `[Set(names), reexports[]]`, and `[Set(), []]` for a syntax error or an ES module instead of throwing | — | public API |
| Deno 2 | `deno info --json` (CLI only) | module graph: `specifier`, resolved `code.specifier`, `span`, `isDynamic` | bindings, exports |

Probed facts:

- Neither `es-module-lexer` nor `cjs-module-lexer` is a Node builtin (23.11 and 26.11): `import()` of either fails
  with `ERR_MODULE_NOT_FOUND` without a `node_modules`, and the `node:` forms fail with `ERR_UNKNOWN_BUILTIN_MODULE`.
  Bun resolves them by auto-installing them into `~/.bun/install/cache`, which is where 3.0.4 and 2.3.0 come from.
- On Node 26, `import * as m from './c.cjs'` gives `['default', 'k', 'm', 'module.exports', 'n', 'x']`: merve's
  names, a `'module.exports'` key, and the names of the re-exported `./re.js`, resolved recursively
  (`lib/internal/modules/esm/translators.js`).
- merve unescapes names (`exports['ab']` is `ab`) and finds `__esModule` from `Object.defineProperty`. In
  `module.exports = { … }` a value must be a name or `require('y')`: `{ 'w-x': 1 }` ends the list at `1`, `{ 'w-x': a }`
  exports `w-x`.
- es-module-lexer 3.0.4 offsets:
  - a static specifier's `start`/`end` exclude the quotes, a dynamic one's include them (the whole argument)
  - `dynamicStart` is the `(`, and an `import.meta` record spans `import.meta`
  - `export default function h` has `start`/`end` on `default` and `localStart`/`localEnd` on `h`
  - a template literal with substitutions gives `specifier: './a*.js', glob: true`

Conclusions:

- **CommonJS exports: merve's rules** (the patterns Node 26 detects, and its names for errors). The core implements
  them in JS, with Node 26's internal binding as the test oracle.
- **Imports: Bun's `kind` vocabulary**, the only standard one.
- **No runtime reports bindings** (which imported name is bound to which local). Merging, pruning unused names and
  CJS ↔ ESM conversion need them: that is the one custom part.
- **Compatible API surfaces, one core**: merve's shape for whoever ports code from Node internals or the Rust crate,
  es-module-lexer 3 and cjs-module-lexer 2 under their npm names so node scripts that install them run on qjsm
  without them, and `Bun.Transpiler`. Each adapter is a field mapping and a filter.
- `lib/lexer/ecmascript.js` sits on the native `lexer` module, so the core and its adapters are qjsm-only. They are
  checked against Node 26, the two npm packages and Bun with recorded expected output (below).

### Files

| File | Holds | Size goal |
| --- | --- | --- |
| `lib/module-lexer.js` | the core: `parse()`, `print()`; ES module syntax (done), `require` calls and merve's CommonJS export rules | the only file that touches tokens |
| `lib/merve.js` | adapter: `parseCommonJS`, `version` in merve's shape | ~30 lines, no tokens |
| `lib/es-module-lexer.js` | adapter: `init`, `parse` with es-module-lexer 3's tuple and field names | ~60 lines, no tokens |
| `lib/cjs-module-lexer.js` | adapter: `init`, `initSync`, `parse` with cjs-module-lexer 2's `{ exports, reexports }` | ~30 lines, no tokens |
| `lib/bun.js` | adapter: `Bun.Transpiler` with `scan`, `scanImports`, in the existing Bun layer | ~40 lines added |
| `lib/cjs-to-esm.js` | `toModule(source)`: CommonJS → ES module, built on `parse` and `print` | step 6 |
| `utilities/resolve-imports.js`, `utilities/list-exports.js` | the tools, ported onto `parse`/`print`, without their own parsers | |
| `tests/unittests/test-module-lexer.js` | the core, by record | |
| `tests/unittests/test-merve.js`, `test-es-module-lexer.js`, `test-cjs-module-lexer.js`, `test-bun.js` | adapters vs recorded output | |
| `tests/fixtures/module-lexer/*.js`, `*.expected.json` | inputs, and what each reference returns for them | |
| `utilities/gen-module-lexer-expected.mjs` | records the references (see step 3), like `gen-path-golden.mjs` | |
| `doc/js/module-lexer.md` | the core API, the record tables below, one section per adapter | |
| `doc/js/bun.md` | the `Bun.Transpiler` entry | |

The bare names `es-module-lexer` and `cjs-module-lexer` are the npm package names, so `import { parse } from
'cjs-module-lexer'` in a script written for node (with the package installed there) runs unchanged on qjsm without
one. `merve` has no JS package anywhere; the name only marks the API shape. Register the four the way the other
`lib/*.js` modules are installed and found (check how `lib/jsonl.js` is wired before adding them).

### The core (`lib/module-lexer.js`)

Rule: **the core reports everything any adapter needs**, so an adapter never scans again. The core always runs both
detections, ES module and CommonJS (cjs-module-lexer and merve report CommonJS exports of any source, Bun's
`scanImports` reports `require` in a module). It keeps ranges for names and specifiers, and lines for merve.

```js
import { parse, print } from 'module-lexer';

const { format, facade, imports, exports, error } = parse(source);
print(imports[0], 'module');     // "import { a, b as c } from './x.js';"
print(imports[0], 'commonjs');   // "const { a, b: c } = require('./x.js');"
```

Result:

| key | holds |
| --- | --- |
| `format` | `'module'` when the source has import/export statements or `import.meta`, else `'commonjs'` |
| `facade` | `true` when the source is nothing but import/export statements (es-module-lexer's facade flag) |
| `imports` | import records, in source order |
| `exports` | export records, in source order, ES and CommonJS alike |
| `error?` | the first lexing error: `{ code, line, column }`, code named as merve's `lexer_error` (`UNTERMINATED_STRING_LITERAL`, ...) |

Import record:

| key | holds |
| --- | --- |
| `kind` | Bun's `import-statement`, `dynamic-import`, `require-call`, `require-resolve`, plus `import-meta` |
| `path` | specifier value, escapes decoded (`'./x.js'`), `null` when not a literal (`import(p + '.js')`) |
| `glob?` | for a template literal with substitutions, the pattern with `*` per substitution: `./a*.js` |
| `bindings` | `[{ imported, local }]`, imported is a name, `'default'` or `'*'`; empty for `import './side.js'` and dynamic ones |
| `reexport?` | `true` for `export … from`, which also yields export records |
| `range` | `[start, end]` of the statement, call or `import.meta` |
| `pathRange` | `[start, end]` of the specifier without quotes, or of the argument expression when it is not a literal |
| `callStart?` | the `(` of a dynamic import |
| `attributes?` | `[start, end]` of `with { … }`, or of the second `import()` argument |
| `line` | 1-based line of `range[0]` |

```js
import a, { b as c } from './x.js'    // bindings: [{ imported: 'default', local: 'a' }, { imported: 'b', local: 'c' }]
const { p, q: r } = require('path')   // kind: 'require-call', bindings: [{ imported: 'p', local: 'p' }, { imported: 'q', local: 'r' }]
const m = require('./m.js')           // bindings: [{ imported: 'default', local: 'm' }]  (module.exports is the default under Node's interop)
```

Export record:

| key | holds |
| --- | --- |
| `kind` | `declaration`, `list`, `default`, `reexport`, `commonjs` |
| `name` | exported name, `'default'`, or `'*'` for `export * from` and a CommonJS re-export |
| `local` | local binding, `null` for `export default <expr>` and re-exports |
| `imported?` | for a re-export, the name in the other module (`e` in `export { e as f } from`), `null` for `export * as w` |
| `path?` | specifier of a re-export |
| `importIndex?` | for a re-export, the index of its record in `imports` |
| `range`, `nameRange`, `localRange?`, `importedRange?` | `[start, end]` of the statement, the exported name (the `default` keyword for a default export), the local name, the imported name |
| `line` | 1-based line of `nameRange[0]` |

Offsets are UTF-16 indices into the source string, as `source.slice(s, e)` uses them on node and bun. An ArrayBuffer
source is decoded first. The lexer's `Location.charOffset` counts characters: check it on a non-BMP character
before relying on it, and convert if it counts code points. merve's error `column` counts bytes: the core reports
it in UTF-16 units like everything else, and the `merve` adapter converts back.

CommonJS exports follow merve 1.2.2's rules (`include/merve/parser.h`, `src/parser.cpp`):

- `exports.x =`, `exports['x'] =`, `module.exports.x =`, `Object.defineProperty(exports, 'x', …)`
- the identifier keys of `module.exports = { a, b: c }`
- re-exports: `module.exports = require('y')`, `{ ...require('y') }`, `__export(require('y'))`,
  `__exportStar(require('y'), exports)`, `Object.keys(require('y')).forEach(…)`
- names unescaped (`\u{XXXX}`, surrogate pairs), `__esModule` found like any other name

Like merve, no scope analysis: a local variable named `require` still counts.

### Adapters

| Adapter | Imports | Exports | Other |
| --- | --- | --- | --- |
| `merve` `parseCommonJS(source)` | — | `commonjs` records → `exports: [{ name, line }]`, `'*'` ones → `reexports: [{ name: path, line }]` | `format == 'module'` or `error` throws SyntaxError with `code` (`UNEXPECTED_ESM_IMPORT`, ...), `line`, `column` (bytes); `version` is the merve version whose rules are followed, `'1.2.2'` |
| cjs-module-lexer `parse` | — | `commonjs` records: unique names → `exports`, `path` of `'*'` → `reexports` | `init` resolves at once, `initSync` does nothing; error behaviour pinned from the recorded output |
| es-module-lexer `parse` | not `require-*`; `type` = `static`, `dynamic`, `import-meta`, or `reexport-star` for `export * from`; `specifier` = `glob ?? path`, `glob` = `!!glob`; `start`/`end` = `pathRange`; `importStart`/`importEnd` = `range`; `dynamicStart` = `callStart`; `attributesStart` = `attributes?.[0] ?? -1` | not `commonjs`; `type` = `direct`, `reexport` or `reexport-all`; `name`, `localName` = `local`, `importName` = `imported`, `from` = `path`, `importIndex`, `exportStart` = `range[0]`, the ranges | `facade`, `hasModuleSyntax` = `format == 'module'`; `init` is a resolved Promise; `typeOnly` is false (no TypeScript); `phase` null until the lexer knows `import source`/`import defer` |
| `Bun.Transpiler.scanImports` | all but `import-meta`: `{ kind, path }` | — | |
| `Bun.Transpiler.scan` | as `scanImports`, minus `require-*` when `format == 'module'` | names of non-`commonjs` records except `'*'` | `transform`/`transformSync` throw TypeError (no transpiler, as for the Bun loaders) |

### Steps

1. **Core, ES module part** (`lib/module-lexer.js`): whole file, dynamic import, `import.meta`, declarations,
   ranges, `facade`. Verify: `test-module-lexer.js` with the cases above plus the late import.
   **Done 2026-10-20** (`test-module-lexer.js`, 22 cases). On the 211 files of `lib/`, `utilities/` and
   `tests/unittests/` that Bun can scan, the `(kind, path)` pairs equal Bun's `scanImports` (743 imports). Bun's
   `scanImports` rejects a `#!` line (`Unexpected #!/usr/bin/env qjsm`), so the adapter will accept more input than
   Bun. Decided while writing it:
   - `range` ends before a terminating `;`, like es-module-lexer's `importEnd`
   - the import record of an `export … from` has empty `bindings`, its names are in the export records
   - error codes are merve's plus `UNEXPECTED_CHARACTER` (a character no token starts with). The first error is
     kept and scanning goes on, unless the lexer itself stops
   - `facade` is true for import, `export … from`, `export *` and `export { … }` statements only, until the
     es-module-lexer fixtures pin it
   - `lib/lexer/ecmascript.js` was fixed on the way: a regex after an operand, `{ }` inside `${ }`, `$` in
     template text, any escape in a string, and exponential backtracking in string, template and comment patterns
     (`lib/node/util.js` hung on its 4 KB template)
2. **Core, CommonJS part**: `require(<literal>)` (not `.require(`), `require.resolve`, merve's export rules.
   Verify: `test-module-lexer.js` CommonJS cases.
   **Done 2026-10-20** (`test-module-lexer.js`, 40 cases). merve 1.2.2's `src/parser.cpp` was read for the rules; the
   CommonJS exports of 29 hand cases and of the 234 `.js` files in `/usr/share/nodejs` (65 with exports) equal Node
   26's `internalBinding('cjs_lexer').parse`, and the `(kind, path)` imports of 444 files equal Bun's `scanImports`
   (`require-resolve` aside, see the survey). The CommonJS pass costs under 2%. Decided while writing it:
   - a `require`/`require.resolve` record needs exactly one argument (Bun skips `require('e', 1)`), a non-literal
     one gets `path: null` like `import(p)`
   - `bindings` only for a top-level `var`/`let`/`const` whose initializer is nothing but the call: `m = require()`
     gives `default`, `{ a, b: c, d = 1 }` gives `a`, `b as c`, `d`; a nested pattern, computed key or rest gives none
   - CommonJS export records: `local` is the value name in `{ a, b: c }`, else `null`; a re-export has `name: '*'`,
     `path`, `importIndex`, and `nameRange` on the `require(…)` call or the name in `Object.keys(name)`; `range` is
     the tokens the pattern matched (`exports.x =`, the whole `module.exports = { … }`, the `defineProperty` call)
   - merve's quirks are kept: `exports.a == 1` exports `a`; `module.exports =` (also `==`) drops earlier
     re-exports; names are not deduplicated in the core (the adapters do)
   - fixed on the way: a `require()` or `import()` inside an `export` statement (`export default function f() {
     require('x') }`, `export const m = await import('./x.js')`) was not seen
   - escaped identifiers (`exports.\u0063d`) are a lexer error, logged as `ecmascript-lexer-escaped-identifier`
     in `BUGS`; merve skips them without an error
3. **Fixtures and the generator** (`gen-module-lexer-expected.mjs`). It records, per fixture:
   - Node 26: `internalBinding('cjs_lexer').parse` (`--expose-internals`), and the namespace keys of
     `import * as m from` the fixture
   - es-module-lexer 3.0.4 and cjs-module-lexer 2.3.0, run under bun, which auto-installs them
   - Bun's `scan` and `scanImports`

   Corpus: hand-written cases, plus the CommonJS files in `/usr/share/nodejs` (239 `.js` files).
4. **Adapters**: `lib/merve.js`, `lib/cjs-module-lexer.js`, `lib/es-module-lexer.js`, `Bun.Transpiler` in
   `lib/bun.js` (the Bun compatibility tier). Verify: their tests equal the recorded output on every fixture. A
   mismatch is fixed in the core, not patched in the adapter (the adapters stay mappings). Where merve and
   cjs-module-lexer disagree, merve (Node 26) decides the core, and the cjs-module-lexer adapter carries the
   documented difference.
5. **`print(record, format)`** and merging (one record per `path`, replaces `Import.merge`/`MergeImports`). Port both
   tools: `resolve-imports.js` and `list-exports.js` use `parse`/`print`; delete `Import`, `Export`, `ImportIdMap`,
   `ImportIds`, `ImportFile`, `ImportType`, `ExportName`, `ExportPairs`, `ImpExpType`, `IsImportExportFrom`,
   `PrintES6Import`, `PrintCJSImport`, `OutputImports` and the copies in `list-exports.js`. Verify:
   `test-resolve-imports.js` (19 cases) passes; `--list-imports` now also lists late, dynamic and `require` imports
   (an intended change: update that test case).
   **`resolve-imports.js` done 2026-10-20**: `print()` and `merge()` in `lib/module-lexer.js`; the tool's own parser
   is gone (`Import`, `Export`, `MergeImports`, `OutputImports` and the token helpers). Bundles, `-C`, `-G` and
   `--list-imports` of `utilities/*.js` are byte-identical to the old tool's. Changes, each with a test case:
   - imports after other statements are bundled, merged and listed (the old parser stopped at the first statement)
   - `--list-imports` also lists dynamic imports and `require` calls with a literal specifier
   - `-m` and the hoisted header keep side-effect imports (`import 'globals'`), which the old code dropped, and a
     namespace import next to named ones from the same module (`-m` kept only `* as os`)
   - a shebang is followed by a newline in a bundle (it ran into the first import)
   - `-m -u` no longer rewrites the file in `ProcessFile()` first, `main()` rewrote it from the original anyway

   Left: `list-exports.js` (its copies of `ImportFile`, `ExportName`, `ImportType`, `TokIs`).
6. **CommonJS → ES module** (`lib/cjs-to-esm.js`): `toModule(source)`, with Node 26's namespace rule from
   `translators.js`: the default export is `module.exports`, plus one named export per CommonJS export name, plus
   the `'module.exports'` key, plus the names of re-exported modules, resolved recursively. Apply the same rule to
   `lib/loaders/commonjs.js`'s `shim()`, which today exports only the runtime keys, without `'module.exports'`.
   Top-level `require` declarations become imports at their range. A `require` inside a function or condition keeps
   working through `const require = createRequire(import.meta.url)` (Node's `module.createRequire`). Verify:
   converted fixtures run as ESM under node and print what the CJS original prints, and their namespace keys equal
   node 26's for the original.
7. **Resolution**: replace `ModuleLoader`, `PackageEntry`, `PathAdjust`, `ResolveAlias`, `RemapModule` with
   `lib/module.js`'s resolver (check first what it covers: `exports` conditions, ESM vs CJS rules); `-M NAME=PATH`
   becomes a shorthand for an entry of a WHATWG import map, and `--import-map FILE` (Deno's flag) reads a whole one.
   Verify: the `-M` cases of `test-resolve-imports.js` unchanged.
   **Package resolution done 2026-10-21**, on qjsm itself rather than `lib/module.js` (its resolver is CommonJS,
   `main` only): `lib/loaders/packages.js` (Node's ESM algorithm: `node_modules`, `exports`, `imports`,
   conditions, patterns, self-reference, legacy `main`) is preloaded as a resolve hook, `-C`/`--conditions` adds
   conditions, and qjsm has `import.meta.resolve(spec, parent?)` (WHATWG, plus Bun's second argument). The 32
   fixture cases equal Node 23.11 (`test-loaders-packages.js`, 12 cases). `resolve-imports.js` resolves through
   `import.meta.resolve`; `ModuleLoader`, `PackageEntry` and `PathAdjust` are gone, a bare name from
   `node_modules` is bundled, one on qjsm's search path stays an import (it was an error).
   Left: `-M` as an import-map entry and `--import-map FILE`; `ResolveAlias` (`-p`) and `RemapModule` stay.
8. **Tool cleanup**: `--help` gets a description per option (it prints names only); `-l` shows twice (print-files and
   the positional `@` row); `-s`, `-c`, `-f`, `-e` appear to have no effect (destructured or assigned, never read):
   verify, then remove. `-i` (`onlyImports`) has none since step 5: the old parser stopped lexing after the imports
   with it. Dead code the port left alone: `UntilEOL`, `Until`, `WholeLine`, `Unquote`, `LiteralSequence`,
   `LeadingWS`, `TokIs`, `IsOneOf`, the `Lexer`/`Token` import, `ModuleExports`. Update the `resolve-imports` entry in `tool-registry/tools.yaml`.

### What stays in `utilities/resolve-imports.js`

`-G` global exports, `-U` userscript wrapping, the dependency tree print. Bundling, in-place rewriting and the writers
move to magic-string (Tier 27).

### Modes by program name (`scriptArgs[0]`, symlinked in `CMakeLists.txt:1099`)

| Name | Equals | Does | State |
| --- | --- | --- | --- |
| `list-imports.js` | `--list-imports` | `file:module names` per import, builtins included | exists |
| `check-imports.js` | `--check-imports` | resolves every import, exit 1 on the first unresolved, no output | exists |
| `merge-imports.js` | `-m` | one import statement per module, file rewritten in place | proposed |
| `prune-imports.js` | `-m -u` | drops imported names the file never uses, in place | proposed |
| `import-tree.js` | `-d` | dependency tree | proposed |
| `cjs-to-esm.js` | new `--to-module` (step 6) | CommonJS file → ES module | proposed |
| `bundle-imports.js` | no mode option | inlines file imports recursively into one script (today's default) | proposed |

`list-exports.js` stays a separate tool (it loads modules to read their exports) and only shares `parse()`.

### Open questions

- Name of the core: `module-lexer`, or something else?
- `merve` as the module name of a JS adapter: merve has no JS package, so the name could also be
  `module-lexer/merve`.
- ES module → CommonJS (`print(…, 'commonjs')` makes it cheap): wanted as a mode (`esm-to-cjs.js`), or not?
- Which of the proposed program names to install.

## Tier 27 — replace `FileMap` with magic-string (plan 2026-10-20)

`FileMap` (`utilities/resolve-imports.js:1090-1500`, plus `NumericRange`, `Range`, `InRange`, `BufferFile`, `FdWriter`/`FileWriter`/`FileReplacer`/`StringWriter`/`ArrayWriter`/`DummyWriter`,
`FileBannerComment`: about 700 lines) rewrites byte ranges of a file. It addresses every edit in the original file's
coordinates and splices other files' `FileMap`s in, so the tool can bundle recursively. Only `resolve-imports.js` uses
it (and its old copy in `~/Sources/plot-cv`).

### Survey (probed 2026-10-20)

| API | Model | Fits FileMap's job |
| --- | --- | --- |
| **magic-string 1.4.3** (npm, Rich Harris; MIT; what Rollup, Vite and Svelte edit code with) | `new MagicString(src)`, edits in original coordinates: `overwrite`/`update(start, end, s)`, `remove`, `appendLeft`/`appendRight`/`prependLeft`/`prependRight(index, s)`, `move`, `slice`, `snip`, `toString()`, `generateMap({ hires })`; `new Bundle({ separator, intro })` with `addSource({ filename, content })` concatenates several | all of it, plus source maps |
| WHATWG `Blob` | immutable composition of byte slices: `new Blob([b.slice(0, 10), 'x', b.slice(20)])`, lazy for file-backed blobs (`fs.openAsBlob`, `Bun.file`) | byte-level only, no edits in original coordinates, no nesting bookkeeping |
| piece table (VS Code's text buffer) | the data structure FileMap is | no shared JS API |

**magic-string runs unmodified on qjsm.** The npm `dist/index.mjs` (58.5 KB) and `@jridgewell/sourcemap-codec` 1.6.0
gave output identical to bun's, the source map `mappings` included, on overwrite + remove + appendLeft + a two-file
`Bundle`. Two details:
- the one bare import `'@jridgewell/sourcemap-codec'` needs to resolve
- it reads the global `TextDecoder` at load time, which qjsm only has after `import 'globals'` (globals are opt-in,
  `lib/globals.js`)

Recommendation: **use magic-string itself, do not lift FileMap into a lib of its own.** It is the de-facto API for
this job. A second, own range-editing API would be exactly the qjs-modules-ism the project avoids. FileMap's byte
offsets become magic-string's UTF-16 indices, the same unit Tier 26 picks for `module-lexer`.

### Files

| File | Holds |
| --- | --- |
| `lib/magic-string.js` | npm `magic-string@1.4.3` `dist/index.mjs`, unchanged apart from a version banner, the codec import pointed at the file below, and `import { TextDecoder } from 'textcode';` so it does not need the global |
| `lib/sourcemap-codec.js` | npm `@jridgewell/sourcemap-codec@1.6.0` `dist/sourcemap-codec.mjs`, unchanged apart from a version banner |
| `utilities/vendor-magic-string.sh` | fetches both tarballs at the pinned versions and applies the two edits, so an update is one run plus a diff |
| `tests/unittests/test-magic-string.js` | `@portable`: the edits resolve-imports needs, the same expected output on qjsm, node and bun |

Keep it as a vendored copy, not a submodule: the git repository holds the sources, the npm tarball holds the built
ESM file that runs here. Bare `import MagicString from 'magic-string'` then works the same on qjsm (lib/) as on node
and bun (`node_modules`).

### Steps

1. **Vendor**: the two files, the script, the test. Verify: the test passes on qjsm, node and bun (ctest `.node`,
   `.bun`).
2. **Port the bundler**: each file becomes a `MagicString`, import ranges from `module-lexer` (Tier 26) are
   `overwrite`/`remove` edits, an inlined file goes in as a `Bundle` source, with the `FileBannerComment` start/end
   lines as `intro`/appended text. `-C` (remove comments) becomes `remove` per comment token, `--merge` becomes
   `overwrite` of the first import plus `remove` of the rest, and in-place output becomes `toString()` + one
   `writeFileSync` to `file.new` + `renameSync` (keeping `FileReplacer`'s mode handling). Delete `FileMap`,
   `NumericRange`, `Range`, `InRange`, `BufferFile`, the six writers and
   `FileBannerComment`. Verify: `test-resolve-imports.js` passes, and bundles of `utilities/*.js` are byte-identical
   to the ones the old code writes (record them before the port).
3. Source maps come for free (`Bundle.generateMap`). Add a `--sourcemap` option only if asked.

### Option B: an own tiny implementation, `lib/magic-string.js` (size estimate 2026-10-20)

The API, dumped with `utilities/describe.sh -r bun --class --dts magic-string` (1.4.3; option types from its
`dist/index.d.mts`):

| Export | Public members |
| --- | --- |
| `MagicString` (default export) | `constructor(string, { filename, indentExclusionRanges, ignoreList })`, `original`, `intro`, `outro`; edit: `append`, `prepend`, `appendLeft`, `appendRight`, `prependLeft`, `prependRight`, `insertLeft`/`insertRight` (deprecated), `overwrite`, `update`, `remove`, `reset`, `move`, `replace`, `replaceAll`, `indent`, `trim`, `trimStart`, `trimEnd`, `trimLines`; read: `toString`, `slice`, `snip`, `clone`, `length`, `isEmpty`, `hasChanged`, `lastChar`, `lastLine`, `getIndentString`; maps: `generateMap`, `generateDecodedMap`, `addSourcemapLocation` |
| `Bundle` | `constructor({ intro, separator })`, `addSource({ filename, content, ignoreList })`, `append`, `prepend`, `clone`, `indent`, `getIndentString`, `trim`, `trimStart`, `trimEnd`, `trimLines`, `toString`, `toMagicString`, `isEmpty`, `length`, `generateMap`, `generateDecodedMap` |
| `SourceMap` | `version`, `file`, `sources`, `sourcesContent`, `names`, `mappings`, `x_google_ignoreList`, `debugId`, `toString()`, `toUrl()` |
| `MagicStringError` | `Error` subclass |

Upstream size, code lines without comments and blank lines (`dist/index.mjs` is 1769 lines in all):

| Area | Upstream | Tiny estimate | What makes it smaller |
| --- | --- | --- | --- |
| `Chunk` linked list (split, edit, each*) | ≈ 150 | 60 | one chunk shape, no deprecation paths |
| `MagicString` edits and reads | ≈ 800 | 300 | no option validation messages; `replace`/`replaceAll` expand `$1 $& $<name>` by letting `String.prototype.replace` do it on a sticky one-match regex (≈ 10 lines instead of `expandReplacement`'s 48) |
| `Bundle` | ≈ 300 | 120 | `toMagicString` and maps over one shared helper |
| source maps: `Mappings`, VLQ writer, `SourceMap`, base64 | ≈ 290 + `@jridgewell/sourcemap-codec` 467 | 120 | `hires: false` and `true` only (no `'boundary'`, `'experimental-range'`); VLQ encode next to the decoder in `lib/module.js:734` (`decodeMappings`) as one shared helper; base64 from `misc.btoa` |
| helpers: `getLocator`, `getRelativePath`, `guessIndent` | ≈ 45 | 30 | `path.relative` from `path` |
| **total** | **≈ 1600 + 467** | **≈ 600** | |

Two smaller cuts:

| Cut | Lines | Covers |
| --- | --- | --- |
| what `resolve-imports.js` needs | ≈ 170 | constructor, `append`/`prepend`, the four `*Left`/`*Right`, `overwrite`/`update`, `remove`, `toString`, `length`, `isEmpty`, `hasChanged`, `clone`, `Bundle` with `addSource`/`append`/`prepend`/`toString`, `separator` and `intro` |
| the edits without source maps | ≈ 450 | the above plus `move`, `reset`, `slice`, `snip`, `trim*`, `lastChar`/`lastLine`, `replace`/`replaceAll`, `indent` |

What qjs-modules contributes is small: `misc.btoa`, the VLQ decoder in `lib/module.js`, `path.relative`. The rest is
string bookkeeping no native module helps with. The saving comes from dropping validation, deprecated aliases and
the two exotic `hires` modes, not from reuse.

Tests for option B:
- a randomized differential test: a seeded generator applies the same edit sequence to the real magic-string (bun
  auto-installs it) and to `lib/magic-string.js`, then compares `toString()`, `slice()` and `generateMap()` output.
  The expected output is recorded once under bun (`utilities/gen-magic-string-expected.mjs`) and checked into
  `tests/fixtures/magic-string/`, so the qjsm test needs no bun.
- `tests/unittests/test-magic-string.js` `@portable` for the hand-written cases: under node and bun the same file
  imports the real package (`magic-string` resolves to `node_modules` there, to `lib/` on qjsm).
- upstream's own suite (vitest, `tests/**/*.test.ts`, 30 files, 162 KB) is the further conformance check, but it
  needs TypeScript stripping and a `describe`/`it`/`expect` shim: run it only if the differential test leaves gaps.

Trade-off:

| | A: vendor the npm dist | B: own implementation |
| --- | --- | --- |
| own lines to maintain | 0 (+ a 30-line vendoring script) | ≈ 170 / 450 / 600 by cut |
| behaviour | exactly upstream | upstream where tested |
| third-party code in `lib/` | 2236 lines | none |
| updates | rerun the script | follow upstream changes by hand |

### Open questions

- A (vendor the npm dist, recommended) or B (own implementation, and which cut)?
- With A: vendor into `lib/`, or depend on an installed `magic-string` and keep `lib/` free of third-party code?

## Tier 28 — native building blocks for zero-copy text editing (plan 2026-10-20)

Small native pieces that make a magic-string implementation (Tier 27 option B) and `resolve-imports.js` output
mostly zero-copy. Three of them are standard APIs qjs-modules lacks or implements slowly, so they pay off on their
own. **Done 2026-10-21** (28.1-28.6), except the optional `json` value-end report (28.6, last row).

Timing on 1 MiB, qjsm, before -> after: latin1 decode 488 -> 13 ms, base64 encode 606 -> 2 ms, base64 parse
1027 -> 346 ms, hex encode 931 -> 3 ms, hex parse 2661 -> 189 ms, `lastIndexOf` near the end 228 -> 0 ms (all-`a`
input with no match 3.3 -> 9.3 ms, memrchr hits every byte), swap32 285 -> 0.7 ms, isAscii 147 -> 0.2 ms, isUtf8
853 -> 2.5 ms.

Measured on qjsm 2026-10-20, the reason the zero-copy path runs on bytes:
- concatenation is cheap: the engine has rope strings (`JS_TAG_STRING_ROPE`), 100k pieces join in 19 ms (node 14 ms)
- `String.prototype.slice` copies: 200 kept 4 MiB slices of one string cost 820 MB RSS (node shares them)
- so unedited chunks stay views into the source's UTF-8 bytes, and only inserted text is encoded

| # | Building block | Kind | Order |
| --- | --- | --- | --- |
| 28.1 | ~~`TextEncoder.encodeInto()` `read` in UTF-16 code units, plus a UTF-16 index → UTF-8 offset helper~~ done: `textcode.utf8Offsets` | WHATWG bug fix + internal helper | 1st |
| 28.2 | ~~`buffer.isAscii()` / `buffer.isUtf8()` in C~~ done: `textcode.isAscii`/`isUtf8` | Node API, faster | 2nd |
| 28.3 | ~~`lineStarts(input)`~~ done: `misc.lineStarts` | internal helper | 3rd |
| 28.4 | ~~`fs.writevSync()` / `fs.writev()`~~ done, with `readvSync`/`readv`, `FileHandle.readv`/`writev` | Node API, missing | 4th |
| 28.5 | ~~Blob as an immutable segment list: zero-copy `slice()` and Blob parts, `fs.openAsBlob()`~~ done, plus `openAsBlobSync` | WHATWG semantics + Node API | 6th, largest |
| 28.6 | ~~lift byte loops in `lib/` into C~~ done: `misc.byteSwap`, `searchArrayBufferLast`, needle views, `textcode.encode`/`decode`, TC39 base64/hex; `json` value end left open | WHATWG/TC39 + internal helpers | 5th |

### 28.1 UTF-16 ↔ UTF-8 offsets

- **Bug**: `textencoder-encodeinto-read-counts-code-points` (`BUGS`). `encodeInto('aé😀', u8)` gives
  `{ read: 3, written: 7 }`, WHATWG and node give `read: 4`. Fix in `quickjs-textcode.c` (`ENCODER_ENCODE_INTO`):
  count a code point above U+FFFF as 2.
- **Helper** `utf8Offsets(bytes, indices)` in `textcode` (internal, not a public standard): `indices` is a sorted
  `Uint32Array` of UTF-16 indices into the text that `bytes` (UTF-8) encodes, converted in place to byte offsets in
  one pass. One call maps all chunk boundaries of an edit list.
- Verify: an `encodeInto` test against node's output for ASCII, Latin-1, BMP and astral text; `utf8Offsets` equal
  to `new TextEncoder().encode(text.slice(0, i)).length` for every `i` of a mixed fixture.

### 28.2 `buffer.isAscii()` / `buffer.isUtf8()` natively

- Today `lib/node/buffer.js:928` loops in JS (`bytesOf(input).every(b => b < 128)`, a byte-by-byte UTF-8 state
  machine).
- Add `isAscii(view)` / `isUtf8(view)` C functions to `textcode` (word-at-a-time ASCII check, a plain UTF-8
  validator, no new dependency). `lib/node/buffer.js` calls them, keeping its argument checks
  (`ERR_INVALID_ARG_TYPE` for a string, as in node).
- An ASCII source needs no offset mapping at all: a UTF-16 index is its byte offset.
- Verify: `test-node-buffer` cases equal node's results on valid, overlong, surrogate and truncated sequences.
  Speed: 1 MiB in well under 1 ms.

### 28.3 `lineStarts(input)`

- `misc.lineStarts(stringOrView)` → `Uint32Array` of the offsets where lines start, first entry 0. Offsets are UTF-16
  indices for a string and byte offsets for a buffer. Lines end at `\n` only, as source maps and magic-string's
  `getLocator` count them. A `memchr` loop for buffers.
- Users: line/column lookup by binary search (source maps, `lastLine()`), `lib/module-lexer.js` error positions.
- Verify: equal to a JS reference on fixtures with `\n`, `\r\n`, a trailing newline and none.

### 28.4 `fs.writevSync()` / `fs.writev()`

- Node: `writevSync(fd, buffers[, position])` returns the bytes written, `writev(fd, buffers[, position], cb)`
  calls back `(err, bytesWritten, buffers)`. `buffers` must be ArrayBufferViews, a string throws
  `ERR_INVALID_ARG_TYPE` (probed on node 23). Bun has both. Also `filehandle.writev()` in `fsPromises`.
- Native: `misc.writev(fd, views[, position])` → `writev(2)`, or `pwritev(2)` with a position, in batches of
  `IOV_MAX`; returns bytes written or a negative errno like the other `misc` syscalls.
- `lib/fs.js`: `writevSync`/`writev` next to `writeSync` (`lib/fs.js:1218`), errors through `syscallerr`.
- Verify: written file equal to node's for mixed `Buffer`/`Uint8Array`/`DataView` lists, with and without
  position, and more than `IOV_MAX` views. `@portable` where the API is shared.

### 28.5 Blob as an immutable segment list

- Today a Blob is one contiguous buffer (`quickjs-blob.h` `union blob`): the constructor copies every part
  (`blob_write`), `slice()` copies (`quickjs-blob.c:286`).
- WHATWG requires an `ArrayBuffer` part to be copied at construction, since the caller may change it later. Only
  immutable sources may be shared: other Blobs, their slices, and file-backed Blobs.
- New layout: a Blob is a list of segments `{ store, offset, length }` over refcounted immutable stores:
  - an `ArrayBuffer`/view part is copied once into a store, a string part encoded once
  - a Blob part (or slice) adds its segments, no copy
  - `slice()` builds a new segment list, no copy
- Reading: `arrayBuffer()`/`bytes()` flatten once, `text()` decodes once, `stream()` enqueues one chunk per segment
  without flattening.
- `fs.openAsBlob(path[, { type }])` (Node, returns a `Promise<Blob>`): one store over an `mmap` of the file (`mmap`
  module). `Bun.file()` can share it later.
- Writing a Blob without flattening: an internal `Symbol.for('qjs.blob.segments')` method returns the segments as
  views, which `fs.writevSync` (28.4) writes in one call, and `Bun.write(path, blob)` uses it.
- Verify:
  - existing `test-blob.js` passes
  - new cases: a slice of a slice, Blob parts inside Blobs, a source `ArrayBuffer` changed after construction does
    not change the Blob, `openAsBlob` of a file, `stream()` chunk boundaries
  - memory: 200 slices of a 4 MiB Blob add no RSS, measured as in the slice test above

### 28.6 Lift byte loops in `lib/` into C

Survey 2026-10-20 of JS loops in `lib/` that search, copy, convert or check bytes one at a time. A native helper
exists or a standard API covers most of them, so few new C functions are needed.

| Where | Loop | Lift to | New C |
| --- | --- | --- | --- |
| ~~`lib/node/buffer.js:162` `decodeUtf8`, `lib/node/util.js:2019` `decodeUtf8` (its `TextDecoder`)~~ | WHATWG UTF-8 decode | **done 2026-10-20**: the native `TextDecoder`, `node:util` re-exports it | fix |
| ~~`lib/node/buffer.js:205`~~ | decoded twice when the result had U+FFFD | **done 2026-10-20**: one `TextDecoder` call, `ignoreBOM: true` as `Buffer.toString()` keeps a BOM | – |
| `lib/node/buffer.js:238` | utf16le decode | **done 2026-10-20**: `TextDecoder('utf-16le')`, the JS loop only when U+FFFD comes out (Node keeps lone surrogates, WHATWG replaces them) | fix |
| `lib/node/buffer.js:245` | latin1/ascii decode | Node's latin1 is ISO-8859-1 (byte = code point), WHATWG's `latin1` label is windows-1252, so no `TextDecoder`. An internal `textcode` helper, with the encode one below | yes |
| `lib/node/buffer.js:100`, `:113` | utf16le and latin1/ascii encode | internal `textcode.encode(str, 'utf-16le' / 'latin1')` → `ArrayBuffer` (no standard API encodes these) | yes |
| `lib/node/buffer.js:117`, `:132`, `:207`, `:214` | hex and base64/base64url encode and parse | `Uint8Array.prototype.toHex`/`toBase64`, `Uint8Array.fromHex`/`fromBase64` (TC39, already in the engine, node 26 and bun have them too). Keep Node's leniency: cut the hex input at the first bad pair, strip non-alphabet characters before `fromBase64(s, { alphabet, lastChunkHandling: 'loose' })` | – |
| `lib/node/buffer.js:381`, `:394` | `b[i] = value[i] & 255` from a typed array or array-like | `b.set(value)`: ToUint8 equals `& 255` for every Number, and both throw on BigInt | – |
| `lib/node/buffer.js:757` `swap` | byte swap for `swap16`/`swap32`/`swap64` | `misc.byteSwap(view, size)`, `bswap` per word in place | yes |
| `lib/node/buffer.js:690` `lastIndexOf` | calls `find` forward again and again, O(n) calls | a reverse mode for `misc.searchArrayBuffer` (`memrchr` for a one-byte needle) | yes |
| `lib/node/buffer.js:515` `find` | copies a needle that is a view (`new Uint8Array(needle).buffer`) | `searchArrayBuffer` accepting a view, or offset and length for the needle | yes |
| `lib/node/buffer.js:928` `isAscii`/`isUtf8` | per-byte checks | 28.2 | 28.2 |
| `lib/readline.js:479` | scans back for the last `\n`/`\r` | `searchArrayBuffer` in reverse mode (above) | – |
| `lib/node/child_process.js:177` | holds back an incomplete trailing UTF-8 sequence | **done 2026-10-20**: `TextDecoder` with `{ stream: true }`, flushed at end | – |
| `lib/readline.js:201` `completeUtf8` | the same | `TextDecoder` with `{ stream: true }` | – |
| `lib/textLineStream.js:31` | splits a string into lines one character at a time | `String.prototype.indexOf` or a sticky `/\r\n?\|\n/g` regex, already native | – |
| `lib/jsonl.js:37` `valueEnd`, `lib/jsonStreams.js:71` | finds where a JSON value ends, one unit at a time | the `json` engine (`quickjs-json.c`) reporting the end offset of each value it parses. Engines stay in C, `JSONL` stays a `lib/` composition (CLAUDE.md, API design principle 4) | maybe |

Left as they are: `lib/node/buffer.js:746` (backs up at most 3 bytes), `lib/jsonl.js:30` (portable fallback for
runtimes without `TextDecoder`), `lib/wasi.js`/`lib/globals.js` (chunked syscalls, no per-byte work).

Native `TextDecoder`, done 2026-10-20: WHATWG labels and names, U+FFFD per maximal invalid subpart, `fatal`,
`ignoreBOM`, `stream`, utf-16le/be and the single-byte encodings tutf8e has tables for (plus windows-874 and
iso-8859-8-i), errors with Node's `code`. Same output as node 26 on all 256 bytes of each encoding and on every split
of a stream. Invalid UTF-8 in `Buffer.toString()`: 1012 → 36 ms for 5.75 MB, utf16le 1196 → 70 ms for 8.5 MB.
Valid non-ASCII UTF-8 got slower, 22 → 36 ms for 5.75 MB: the input is validated once before `JS_NewStringLen`,
which validates it again (ASCII runs are skipped 8 bytes at a time). Remaining difference: a label with a NUL in it is cut at the NUL in the error message.

New C functions: `misc.byteSwap`, `searchArrayBuffer` reverse mode and needle views, internal `textcode.encode`, and
maybe a value-end report in `json`. Each one replaces a loop above, so no helper ships without a user.

Verify:
- `test-node-buffer.js` and the textcode tests pass, with new cases for every row and results equal to node's on
  the same input (invalid UTF-8 table, base64 with junk and missing padding, hex with a bad pair, `lastIndexOf`
  with an offset)
- `test-readline.js`, `test-jsonl.js`, the `child_process` and `textLineStream` tests pass
- timing on 1 MiB for decode, base64, `lastIndexOf` and `swap32`, before and after, in the commit message

### How the pieces combine (Tier 27 option B, `resolve-imports.js`)

```js
const bytes = new Uint8Array(readFileSync(path));            // or (await openAsBlob(path)).bytes()
const ascii = isAscii(bytes);                                 // 28.2, offsets are byte offsets then
const at = ascii ? i => i : offsetsOf(bytes, chunkBounds);    // 28.1 utf8Offsets for the rest
writevSync(fd, chunks.map(c => c.edited ? encode(c.content) : bytes.subarray(at(c.start), at(c.end))));  // 28.4
```

`toString()` still returns a string, so it pays one copy; the zero-copy path is the file or Blob output.

## Tier 29 — `node:os` gaps that need native backing (lib/node/os.js, 2026-10-21)

`lib/node/os.js` implements the whole `node:os` surface; these parts throw `InternalError` (marked
`/* XXX: TODO */` in the source) or approximate, because qjs-modules has no C function behind them. The native
pieces belong in `misc`, each a thin syscall wrapper; `lib/node/os.js` then drops the stub.

| # | Node API | Today | Native backing needed |
| --- | --- | --- | --- |
| 29.1 | `os.networkInterfaces()` | `InternalError` | `misc.getifaddrs()`: `getifaddrs(3)`, per entry name, family, address, netmask, MAC (`AF_PACKET`), scope id; JS builds Node's `{ address, netmask, family, mac, internal, cidr, scopeid }` |
| 29.2 | `os.setPriority([pid,] priority)` | `InternalError` after argument checks | `misc.setpriority(which, who, prio)` and `getpriority`: `setpriority(2)`, errno to Node's `SystemError` (`EACCES`, `ESRCH`) |
| 29.3 | `os.userInfo()`, `os.homedir()` without `$HOME` | `/etc/passwd` only, NSS users (LDAP, sssd) give `ENOENT` | `misc.getpwuid(uid)`: `getpwuid_r(3)` |
| 29.4 | `cpus()`, `freemem()`, `totalmem()`, `loadavg()`, `uptime()`, `getPriority()` on macOS, BSD, Windows | `InternalError`: no `/proc` | `sysctl(3)` / `sysinfo(2)` / `GetSystemTimes` per platform, or one `misc.sysinfo()` |

Verify: `tests/unittests/test-node-os.js` (`@portable`) gains a case per row and keeps passing on node, bun, deno;
for 29.1 compare against `node -p 'os.networkInterfaces()'` on the same machine.

## Tier 30 — `lib/repl.js` editing and UX (survey 2026-10-21)

Features seen in other REPLs (Node, Deno/rustyline, Bun, Python 3.13, IPython, fish, PSReadLine, Chrome
DevTools, Emacs) that `lib/repl.js` lacks. 30.0 is the requested bracket aid; 30.1 is a prerequisite for its
cross-line part.

**Status 2026-10-21:** 30.0 to 30.3 are done (`lib/repl.js`, `doc/js/repl.md`, `tests/unittests/test-repl.js`,
checked in a 40-column tmux pane). Left open from them:

- typing a closer at the start of an indented line does not dedent it (IPython does)
- a buffer taller than the terminal cannot be redrawn: `render()` moves up past the top row
- accepting a ^R match prints the output over the second row of a wrapped search line
- the `qjs-debugger` override of `repl.controlC` has no effect: the key table holds the prototype's method

| # | Item | Seen in | Notes |
| --- | --- | --- | --- |
| 30.0 done | Bracket aid: from the first mismatched closer to the end of the buffer in an error style (red background), underline the innermost unclosed opener around the cursor, highlight the opener that matches a closer before the cursor; Alt-Backspace with a mismatch kills from it to the end (into the kill buffer, `^Y` restores), else `backwardKillWord` | Emacs `show-paren-mode`, rustyline `MatchingBracketHighlighter` | `colorizeJs()` returns `mismatch`, opener positions and the stack at the cursor; `printColorText()` needs an attribute layer (SGR 4) on top of the color. Enter on a buffer with a mismatch evaluates it instead of asking for another line, since `foo(]` keeps `(` open forever |
| 30.1 done | Multi-line buffer editing: Up/Down move between lines, every line is redrawn, history recalls a whole entry | Deno, Python 3.13, IPython, PSReadLine | today only the current line is redrawn, earlier lines sit in `mexpr` |
| 30.2 done | Node REPL semantics: errors print as `Uncaught Error: ...`, `_` and `_error` on `globalThis` (assigning `_` disables the update), first `^C` clears the line and the buffered lines (`.break`), second `^C` on an empty line exits, an `.editor` mode for pasting | Node | `printEvalResult` sets `this._` today |
| 30.3 done | Bracketed paste (`\e[200~` ... `\e[201~`): pasted text is inserted as is, not run line by line | Node, IPython, fish | |
| 30.4 | Grey inline suggestion from history, Right or `^E` accepts it | fish, IPython, PSReadLine | |
| 30.5 | Grey preview of the one remaining completion; the completion list shows the kind (function, value) and a function's arity | Node | |
| 30.6 | Live result preview while typing | Node, Chrome DevTools | QuickJS has no side-effect-free eval: only literals and plain property chains (`a.b.c`), getters skipped |
| 30.7 | Prompt turns red while the input cannot parse | PSReadLine; Bun exports `isValidSyntax()` | |
| 30.8 | Introspection: `obj?` prints type, prototype chain and function source; a `.type expr` command | IPython, GHCi | |
| 30.9 | Helpers: `copy(x)` over OSC 52, `keys()`, `values()`, `table()`, a `.time expr` command | Chrome DevTools, IPython `%timeit` | `\t` only toggles the timing display |
| 30.10 | Optional auto-closing brackets that can be typed over, Alt-Enter inserts a newline, numbered results `$1`, `$2`, a vi mode, column width of wide characters via `Intl.Segmenter` | IPython, DevTools, readline | `ucsLength()` only handles surrogates |

Verify: a piped test in `tests/unittests/test-repl.js` per item where the behavior is visible without a tty;
the rest by hand in a terminal.

## Tier 31 — terminal I/O on a Windows console without VT (2026-10-22)

`lib/terminal.js` writes only through `misc.ttyWrite()`, reads through `misc.ttyRead()`;
`readline`, `repl`, `tty`, `process` and `console` take raw mode, window size, reads and
writes from it.

**Done:** `src/vtparse.c` (VT parser, `tests/c/vtparse-events.c`), `src/win32/vtconsole.c`
(console calls for CSI cursor/erase/SGR/?25, key events to VT bytes), `misc.ttyWrite`,
`misc.ttyRead`, `misc.ttyGetWinSize` (window, not buffer), `misc.ttySetRaw` saves and restores
console modes and code pages, sets UTF-8, returns whether VT output is native. Smoke-tested
under wine only (a C check of vtconsole output and injected key events), not on Windows.

| # | Item | Notes |
| --- | --- | --- |
| 31.1 | Run qjsm.exe on a real Windows console, legacy (conhost, VT off) and Windows Terminal | does ReadConsoleInputW with ENABLE_VIRTUAL_TERMINAL_INPUT deliver the sequence characters as key events, or also the raw arrow keys (then 31.2 sends them twice)? |
| 31.2 | Decide VT input on or off in `ttySetRaw`, once 31.1 is known | `vtconsole_read` translates virtual keys itself, VT input only matters for `os.read` users |
| 31.3 | `repl.js` `supportsAnsi` is still false on Windows | colors now go through `ttyWrite`; `inspect()` output printed with `console.log` does not, it needs `ttyWrite` too before colors can be on |
| 31.4 | Resize events are dropped by `vtconsole_read` | report them, so `tty.WriteStream` can emit `'resize'` where there is no SIGWINCH |
| 31.5 | Untranslated sequences: `CSI S/T` scroll, `CSI r` region, `CSI ?1049 h/l`, `CSI @ P L M` | `ScrollConsoleScreenBuffer`, `CreateConsoleScreenBuffer` + `SetConsoleActiveScreenBuffer` |
| 31.6 | Mouse input (`mousetrackingEnable`) on a console | `ENABLE_MOUSE_INPUT` without QuickEdit, MOUSE_EVENT to SGR 1006 reports |
| 31.7 | `readKey()` polls with `os.sleep(10)` on a console | `ttyRead` never blocks there; a waiting variant would need WaitForSingleObject |
| 31.8 | `util.stripAnsi` and `terminal.stripAnsi` differ | util's also strips `ESC (B` and similar, terminal's only CSI; one of them should go |
