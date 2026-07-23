# Architecture & Compatibility Assessment

A snapshot assessment of the native (`quickjs-*.c`) and JS (`lib/*.js`) layers, plus forward
compatibility with the `quickjs-2026-*` reference tree this project builds against (see
`/mnt/data/Projects/plot-cv/quickjs`, tagged around `quickjs-2026-06-04`). Produced by reading
the code (not just grepping); every claim below has a file:line citation. Actionable items have
been filed into `TODO.md` (unfinished work / roadmap gaps) and `BUGS.md` (concrete defects) —
this file is the narrative context for those, not a duplicate of them.

## 1. What this project is, structurally

Two layers:

- **Native layer** (`quickjs-*.c`/`.h` in the repo root, ~35 modules) — C bindings exposing
  OS/library functionality as QuickJS classes/modules: streams, XML/DOM, deep-object ops,
  filesystem/directory/mmap, sockets/serial, databases (sqlite/mysql/pgsql), archives, a
  lexer, a bcrypt/bjson pair, GPIO, child processes, and assorted introspection helpers
  (`pointer`, `predicate`, `inspect`, `internal`). Shared plumbing lives in `src/*.c` and
  `include/*.h` (buffer/dbuf helpers, path utilities, vectors, glob).
- **JS layer** (`lib/*.js`, ~55 files plus `lib/xml/`, `lib/lexer/`, `lib/parser/`) — polyfills,
  WHATWG-spec ports, Deno/Bun-style runtime APIs, and a lexer/parser toolkit, built on top of
  the native modules and on each other.

The two layers are asymmetric: several native modules have **no JS wrapper at all** — `blob`,
`child-process`, `gpio`, `serial`, `mmap`, `directory`, `queue`, `repeater`, `virtual`, `magic`,
`bcrypt`, `syscallerror`, `location` are consumed directly as native modules, with no `lib/*.js`
giving them a documented, higher-level, WHATWG/Deno-shaped surface. `sockets` has only a
low-level `lib/socklen_t.js` helper, not a `net`/`dgram`-style wrapper. This is the main
structural gap standing between the current state and roadmap goal 1 below.

## 2. Roadmap alignment (see also `TODO.md`'s new roadmap section)

| Goal | Current state |
|---|---|
| **Standard library QuickJS deserves** (WHATWG + Deno/Bun APIs) | Strong core: `ReadableStream`/`WritableStream`/`TransformStream` (`lib/stream.js`, a near-verbatim spec port), `AbortController`/`AbortSignal` (`lib/abort.js`), `EventTarget`/`EventEmitter` (`lib/events.js`), `URL`/`URLSearchParams` (`lib/url.js`), `TextEncoder`/`TextDecoder` (native `textcode`). Missing or stubbed: `CustomEvent`, `fetch`, `Blob`/`File` JS wrapper (native `quickjs-blob.c` exists, unwrapped), `structuredClone` (only feature-detected, `lib/stream.js:533`), `Worker` (test-infra only). Deno/Bun-shaped runtime APIs are present but thin: `lib/readline.js` (9 lines, cursor-only), `lib/buffer.js` (12 lines, `from`/`concat` only), `lib/perf_hooks.js` (12 lines, no marks/measures). |
| **Toolbox for working with QuickJS** | Solid — `lib/reflect.js`, `lib/deep.js`, `lib/pointer.js`, `lib/misc.js`, `quickjs-inspect.c`/`console.js`. |
| **Lexer/parser toolkit** | The core (`lib/parser/grammar.js`, native `lexer` module, `lib/lexer/{bnf,c,csv,ecmascript}.js`) is genuinely general and reused across 4 independent grammars — real evidence of generality. But the project's own most complex parsing use case, CSS selectors, bypasses it: `lib/css3-selectors.js` hand-rolls its own tokenizer instead of building on `lib/lexer`/`grammar.js`, and XML parsing (`lib/xml/read.js`) is likewise bespoke. Not yet dogfooded by the rest of the codebase. |
| **Archives, filesystem, sockets, databases** | Filesystem and archives are well covered (`lib/fs.js`, `lib/vfs.js` unifying real FS + `Archive`). Databases are unified via `lib/dbi.js`/`lib/db.js` over `sqlite`/`mysql`/`pgsql`. Sockets and serial have native bindings but no `lib/` ergonomics layer. |

## 3. quickjs-2026 forward-compatibility

The reference tree renamed `dbuf_realloc()` → `dbuf_claim()` with a **changed signature**
(second arg is now a size *delta* to add, not a new *total* size) sometime between
2025-09-13 and 2026-06-04. This repo already has CMake-level detection for it
(`HAVE_DBUF_CLAIM`/`HAVE_DBUF_REALLOC`, `CMakeLists.txt:555-571`) and all 15 call sites across
the tree were migrated in commit `3e8d44cc` to call `dbuf_claim()` with a correctly-computed
delta. That migration is complete and internally consistent — see `TODO.md` for the one
gap it left (no fallback shim if `HAVE_DBUF_CLAIM` is false against a given reference tree)
and `BUGS.md` for a latent bug (`src/vector.c` `vector_copy()`) introduced by the same
mechanical conversion.

Separately, and **not** part of that commit: `src/utils.c:3384` calls `js_module_loader()`
with a 4th argument, unconditionally. The 3-argument (older) signature is still what
`/mnt/data/Projects/plot-cv/quickjs`'s own `quickjs-libc.h:48` declares, and this project's
build system already has a working detection+guard for exactly this ambiguity
(`CMakeLists.txt:509-533` → `JS_MODULE_LOADER_OLD`, correctly applied in `src/qjsm.c:1022-1027`).
`src/utils.c:3384` just doesn't use the guard. Filed as a bug in `BUGS.md` — it's the clearest
concrete regression found.

## 4. Cross-cutting code-health notes

- **Disabled/commented-out code is the dominant defect shape in this codebase.** Nearly every
  bug found in this and prior sessions (`deep.select`, `WritableStream.close()`, XML
  enumeration, `Blob.stream()`, and the new `directory`/`sockets` default-export bugs below)
  has the same shape: a guard, case, or export declaration was commented out mid-edit and never
  restored. `TODO.md` Tier 5 already tracks a long list of these; this session adds more
  (`quickjs-directory.c`, `quickjs-sockets.c`, `quickjs-misc.c`, `quickjs-pgsql.c`,
  `quickjs-lexer.c`, `quickjs-list.c`, `quickjs-internal.c`). Given the volume, it may be worth
  a dedicated pass (or a lint rule / grep-based CI check flagging commented-out
  `case`/`JS_CFUNC*`/`JS_AddModuleExport` lines) rather than fixing these one at a time forever.
- **Three independent CSS-selector implementations** (`lib/parsel.js`, `lib/css-selectors.js`,
  `lib/css3-selectors.js`) exist; `lib/css-selectors.js` appears to be dead code superseded by
  `css3-selectors.js` (nothing in the active source tree imports it — only stale build output
  under `inst/` does). See `TODO.md`.
- **Inconsistent non-enumerable-property idiom** across `lib/extend*.js` — three different
  patterns for marking added prototype members non-enumerable instead of one shared
  convention. See `TODO.md`.
- **Test coverage gaps**: `arraybuffer-sink`, `bcrypt`, `queue`, `syscallerror`, `virtual` have
  no dedicated test file (`bjson` is upstream-documented as test-only, lower priority).
- **Stray untracked files** in `lib/` (`lib/blah.tmp*` — six 0-byte scratch files,
  `lib/repl.js.orig` — a stale backup that differs from `lib/repl.js`) — working-tree hygiene,
  not filed as TODO/BUGS items, just noted here since they showed up during the survey.
