# TODO

Unfinished work found by scanning the whole codebase (native `quickjs-*.c` bindings, `src/`,
`include/`, `lib/*.js`, `doc/*.md`, `tests/test_*.js`), ordered by leverage: highest-impact,
cheapest-to-fix items first, general cleanup last. Every item below was verified by reading
the code (and, where noted, by actually running it) — not just grepped.

This supersedes the sparse root-level `TODO` file; its four items are folded in below (marked
*(pre-existing)*).

## Roadmap

Four standing goals for this project, in priority order. Every tier below should be read
against these — they're the "why" behind what gets picked up next. See `ASSESSMENT.md` for
the full architecture/gap survey behind Tier 6-8.

1. **Be the standard library QuickJS deserves** — WHATWG-spec'd web APIs (streams, URL,
   events, encoding, DOM) and Deno/Bun-like runtime APIs (fs, process, timers, readline,
   child_process, ...), with a coherent, documented JS surface over the native bindings.
2. **Be a toolbox for working with QuickJS itself** — inspection/reflection/deep-object/pointer
   utilities for debugging and metaprogramming.
3. **Be a lexer/parser toolkit** — a general, reusable grammar/lexer framework, not just
   glue code for one format.
4. **Support archives, filesystem, sockets, serial, and databases** as first-class, ergonomic
   JS APIs, not just raw native bindings.

## Tier 2 — public API documented as working, but isn't

~~**`Blob.prototype.stream()` returns `undefined` instead of a `ReadableStream`**~~ — **FIXED** in commit `bfa9b603`.
  Implemented `Blob.prototype.stream()` to return a `ReadableStream` over the blob's bytes.
  The implementation copies the blob data (since the blob may be garbage collected before
  the stream finishes reading) and wraps it in a Reader that's passed to
  `js_readable_stream_from_reader()`. All three tests in `tests/test_blob.js` now pass.

## Tier 3 — known perf/architecture debt (already flagged) and spec-compliance gaps

- **`js_is_*` type-check helpers (`js_is_arraybuffer`, `js_is_date`, `js_is_map`, etc. in
  `src/utils.c:2664-2745`) are slow** *(pre-existing TODO item)* — each does
  `instanceof || Object.prototype.toString comparison` via a string compare instead of a tag
  check. These sit on hot paths (serialization, `deep`, `inspect`), so worth profiling once
  Tier 1 is fixed and traffic patterns are trustworthy again.

- ~~**Streams `respondWithNewView()` (BYOB) is missing spec-required safety checks**~~ — **FIXED** in commits `312df027`, `ae4f9992`, and `d209f470`.
  Replaced `lib/stream.js` with qjs-lws version which has complete BYOB implementation.
  Added `isDataViewConstructor` helper function and fixed all `pendingPullIntos` property
  access to use `CTRL()` wrapper. Added `noop` and `assert_default` exports to lib/assert.js
  for compatibility. Fixed `ReadableByteStreamControllerCallPullIfNeeded` to pull when there
  are pending read requests, even if desiredSize <= 0. Fixed async iterator cleanup in
  `_returnSteps` to use `this._reader` instead of `STRM(this).reader`. Fixed
  `WritableStreamDefaultWriterWrite` to handle undefined stream and undefined
  `strategySizeAlgorithm` correctly. All 41 stream tests now passing (100%).

## Tier 4 — structural/maintenance risk and test-coverage gaps

- **`internal.h` and `quickjs-internal.h` are two hand-forked copies of the same QuickJS
  internals header**, both carrying matching `XXX:` design-debt comments at nearly the same
  line numbers. Any future fix to one needs manual re-application to the other; worth
  collapsing to a single source of truth (or confirming they've already diverged and
  documenting why two copies exist).

- **`tests/test_list.js` isn't a real test** — it's an ad-hoc script (not using the
  `assert`/`assertEq` pattern every other `test_*.js` file uses) that ends in an unguarded
  `while(!skip()) {}` loop. It's exactly the kind of gap that let `List.prototype.at()`
  (registered but its case body commented out, always returning `undefined`) go unnoticed —
  since removed entirely (`quickjs-list.c`, `doc/native/list.md`). Worth rewriting properly so the
  next dead/wrong method doesn't slip through the same way.

## Tier 5 — lower-value cleanup (dead alternate code, disabled diagnostics, unfinished scaffolding)

Not urgent individually, but worth a pass since dead/disabled code in the same functions as
live logic is exactly what produced every Tier 1 bug above — cleaning it up now prevents the
next one.

- Disabled alternate implementations with no remaining purpose: `src/js-utils.c:70-75`
  (old `promise_free(JSContext*, ...)` overload), `src/js-utils.c:153-159` (old
  `promise_forward()` body), `src/utils.c:2017-2023` (old `js_values_free(JSContext*, ...)`
  overload), `src/utils.c:3050-3057` + `:3363-3372` (abandoned zero-copy
  `js_arraybuffer_fromstring`/finalizer pair — current version always copies), `src/glob2.c:22-28`
  (`range_free()`, unused).
- Duplicated disabled `FROM_UNIXTIME(...)` date-formatting block in both
  `quickjs-mysql.c:109-120` and `quickjs-pgsql.c:220-231`, plus an unused
  `js_pgconn_print_fields()` in `quickjs-pgsql.c:296-309`.
- `quickjs-misc.c:1267-1277` — disabled alternate glob implementation using the project's own
  `my_glob()`/`src/glob2.c` engine; the system `glob()` is used instead, meaning `glob2.c` is
  currently built but not actually wired up to `misc.glob()`. Worth confirming this is
  intentional or finishing the wiring.
- `quickjs-sockets.c:2098-2133` — a whole abandoned `PROP_SYSCALL/PROP_ERRNO/PROP_ERROR/
  PROP_RET/PROP_AF` property block (enum + switch cases + both `Socket`/`AsyncSocket`
  registrations, all consistently disabled together) plus an unused `js_sockopt()` helper at
  `:2236-2239`.
- `quickjs-pointer.c:919-927` — disabled forwarding of `Array.prototype.map/reduce/forEach/
  keys/values` onto `Pointer.prototype`; currently not exposed at all. `:707-713` — an
  abandoned `STATIC_COMMON` draft that was never wired into any function table.
- `quickjs-predicate.c:754-758`, `quickjs-inspect.c:838-871` (34-line disabled exponent-
  stripping number formatter), `quickjs-inspect.c:1156-1175` (disabled `[ClassName]` fallback
  tag) — superseded alternates, safe to delete.
- `quickjs-lexer.c:834-849` — `Lexer.prototype.back()` only accepts a token/location object;
  a disabled branch would have let callers pass a raw string instead (currently throws
  `TypeError` for that case).
- `quickjs-path.c:140-152` — a disabled, superseded duplicate of `PATH_REALPATH` handling
  inside `js_path_method` (the live implementation is `js_path_method_dbuf` +
  `path_realpath3`, registered and working at `quickjs-path.c:698` — **not** a missing
  feature, just dead leftover code confusingly shaped like one).
- `src/glob.c:582` *(pre-existing TODO-style comment)* — `/* TODO: don't call for ENOENT or
  ENOTDIR? */`, minor optimization.
- `wasm` module was scaffolded in `CMakeLists.txt` (the `option(MODULE_WASM ...)` declaration
  itself is commented out at line 51, `BUILD_LIBWASM` defaults off) but no `quickjs-wasm.c`
  exists anywhere — either finish it or remove the dead `if(MODULE_WASM)` block
  (`CMakeLists.txt:418-447`).
- Minor hygiene: stray `src/utils.c.orig` backup file left in the tree; `quickjs-stream.c` has
  ~13 functions with unfilled Doxygen placeholder text (`{ function_description }` etc.).

## Tier 6 — quickjs-2026 forward-compatibility (found during 2026-07-23 assessment, see `ASSESSMENT.md`)

- **No fallback if `HAVE_DBUF_CLAIM` is false against a given reference tree.** The
  `dbuf_realloc()` → `dbuf_claim()` migration (commit `3e8d44cc`) converted all 15 call sites
  to call `dbuf_claim(buf, delta)` directly, with `CMakeLists.txt:555-571` defining
  `-Ddbuf_realloc=dbuf_claim` only for the `HAVE_DBUF_CLAIM` case. There's no inverse shim
  (`#define dbuf_claim(...) ...` in terms of `dbuf_realloc`) for building against an older
  reference tree that only has `dbuf_realloc()` — which is what
  `/mnt/data/Projects/plot-cv/quickjs`'s current `cutils.h`/`cutils.c` actually expose. Worth
  adding a small inline shim (delta → total-size wrapper) so the build works both ways instead
  of only forward.
- **Dead, now-backwards `#define dbuf_realloc dbuf_claim`** at `include/defines.h:9-11`
  (already `#if 0`'d out) — safe to delete now that the CMake-level wiring is the real
  mechanism; keeping it around next to live compat logic invites confusion about which one
  actually does the job.
- Additional disabled-code items found by the same pass, same shape as Tier 1/5 (commented-out
  case/branch, feature silently missing rather than erroring): `quickjs-lexer.c:1611,1655`
  (iterator `next`/`values` on `Lexer` disabled), `quickjs-list.c:567` (iterator `next`
  disabled — verify this isn't already superseded by the `List.prototype.at()` removal noted
  in Tier 4), `quickjs-misc.c:3592,3722,3725,3851` (+ matching disabled `case`s at `2806-2808`,
  `2822`) — `realpath`, `resizeArrayBuffer`/`searchArrayBuffer` alias, and `isHTMLDDA`/
  function-type magic dispatch all disabled, `quickjs-pgsql.c:1312,1855` (`escapeString` and
  iterator `next` disabled), `quickjs-internal.c:558,571` (opcode-name introspection
  properties disabled), `quickjs-tree-walker.c:167,486` (`setroot()`'s return value discarded
  — minor, probably harmless but worth a look).
- **Test coverage gaps**: no dedicated test file for `arraybuffer-sink`, `bcrypt`, `queue`,
  `syscallerror`, or `virtual` (`bjson` is upstream-documented as test-only, lower priority).

## Tier 7 — roadmap gaps: JS standard-library surface (goal 1) vs. what exists

Native bindings that currently have **no `lib/*.js` wrapper at all**, so they're usable only as
raw native modules rather than as part of a documented "standard library" surface: `blob`
(despite `Blob.prototype.stream()` already being tracked in Tier 2 — there's no `lib/blob.js`
at all, not just an incomplete method), `child-process`, `gpio`, `serial`, `mmap`, `directory`,
`queue`, `repeater`, `virtual`, `magic`, `bcrypt`, `syscallerror`, `location`. `sockets` has only
a low-level `lib/socklen_t.js` helper, not a `net`/`dgram`-style ergonomic wrapper.

WHATWG/Deno/Bun API gaps in `lib/`:
- `fetch` — missing; only appears in vendored test-infra comments (`lib/testharness.js`).
- `structuredClone` — only feature-detected (`lib/stream.js:533`), never implemented.
- `Worker` — missing; only referenced by vendored test-infra (`lib/testharness.js:254`).
- `lib/readline.js` (9 lines: `cursorTo`/`clearLine` only), `lib/buffer.js` (12 lines:
  `from`/`concat` only), `lib/perf_hooks.js` (12 lines: `now`/`timeOrigin` only, no marks or
  measures) are all much thinner than their Node/Deno/Bun namesakes.
- `lib/extendAsyncFunction.js:3` — declared but empty (`AsyncFunctionExtensions =
  nonenumerable({})`, no members added yet).

## Tier 8 — architecture cleanup (goal 3 dogfooding, code duplication)

- **Three independent CSS-selector implementations**: `lib/parsel.js` (ported `parsel-js`),
  `lib/css-selectors.js` (compiler built on `parsel.js`), and `lib/css3-selectors.js` (a
  *second*, independent compiler with its own hand-rolled tokenizer, duplicating helper
  functions nearly verbatim from `css-selectors.js`, e.g. `escapeRegExp`/`getAttribute`/
  `hasAttribute`/`isElement`/`childElements`). `lib/css-selectors.js` appears dead: nothing in
  the active source tree imports it (`lib/dom.js:3`, `tests/test_dom.js:4`, and
  `tests/test_css3_selectors.js:2` all import `css3-selectors.js` instead; only stale build
  output under `inst/` still references `css-selectors.js`/`parsel.js`). Worth either deleting
  `css-selectors.js` or consolidating `css3-selectors.js` to build on the shared
  `lib/lexer`/`lib/parser/grammar.js` toolkit (goal 3) instead of duplicating a tokenizer.
- **Lexer/parser toolkit (goal 3) isn't dogfooded by the project's own hardest parsing
  problems.** `lib/parser/grammar.js` + the native `lexer` module are genuinely reused across 5
  independent grammars (`lib/lexer/{bnf,c,csv,ecmascript,xml}.js` — `lib/xml/read.js` was
  rewritten to drive `XMLLexer`/`lib/lexer/xml.js` as a JS port of `js_xml_parse()`, verified
  against the native `xml.read()`/`xml.write()` for tree shape, option surface, and formatting
  quirks; `lib/xml/write.js` is a matching port of `js_xml_write()`), which is good evidence of
  real generality — but `css3-selectors.js` still hand-rolls its own tokenizer instead of
  building on the toolkit.
- **Inconsistent non-enumerable-property idiom across `lib/extend*.js`.** Most files
  (`extendArray.js`, `extendArrayBuffer.js`, `extendAsyncFunction.js`, `extendFunction.js`,
  `extendMap.js`, `extendSet.js`) wrap their extension object in the shared `nonenumerable()`
  helper from `lib/util.js`. `extendMath.js` and `extendGenerator.js`/`extendAsyncGenerator.js`
  instead re-implement the same marking logic inline. `extendObject.js` uses a third helper,
  `extend()`, with no non-enumerable marking at all. Worth converging on one convention.
- Stray untracked working-tree files noticed during the survey (not a code bug, just hygiene):
  `lib/blah.tmp*` (six 0-byte scratch files), `lib/repl.js.orig` (a stale backup that differs
  from the current `lib/repl.js`).

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

Comprehensive test suite exists in `tests/test_dom.js` (210 tests),
`tests/test_event_and_fragment.js`, `tests/test_event_subclasses.js` (50+ tests),
`tests/test_history.js` (28 tests), `tests/test_geometry.js` (30 tests),
and `tests/test_range_selection.js` (60+ tests).

Remaining items ordered by leverage:

### 9.1 Fetch API (LOWER - modern HTTP client, see also Tier 7)
**Why:** Network requests for dynamic content.

**Implementation:**
- `fetch(url, options)` function
- `Request` class: `url`, `method`, `headers`, `body`, `mode`, `credentials`
- `Response` class: `status`, `statusText`, `headers`, `body`, `ok`, `json()`, `text()`, `blob()`
- `Headers` class: `get()`, `set()`, `has()`, `delete()`, `append()`, iteration
- `AbortController` + `AbortSignal` for request cancellation
- Promise-based API

**Files:** `lib/fetch.js` or `lib/dom.js`

**Status:** Not implemented (also tracked in Tier 7).

### 9.2 FormData (LOWER - form data collection)
**Why:** Collecting form data for submission.

**Implementation:**
- `FormData` class: `append()`, `delete()`, `get()`, `getAll()`, `has()`, `set()`
- `FormData(form)` constructor to collect from `<form>` element
- Iteration support: `entries()`, `keys()`, `values()`

**Files:** `lib/dom.js`

**Status:** Not implemented.

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

### 9.6 File + Blob remaining APIs (LOWER - see also Tier 2/7)
**Why:** File uploads, downloads, binary data.

**Implementation:**
- `Blob.prototype.stream()` — broken (see Tier 2)
- `FileList`: array-like collection of Files
- `FileReader`: `readAsText()`, `readAsDataURL()`, `readAsArrayBuffer()`, `onload`, `onerror`
- `URL.createObjectURL(blob)`, `URL.revokeObjectURL(url)`

**Files:** `lib/dom.js`, `lib/file.js`

**Status:** `File` class (in `lib/file.js`) and `Blob` (native binding) exist. `stream()`, `FileList`, `FileReader`, and object URL methods are missing.

### 9.7 WebSocket (LOWER - real-time communication)
**Why:** Bidirectional real-time data.

**Implementation:**
- `WebSocket` class: constructor with URL
- Properties: `readyState`, `bufferedAmount`, `protocol`
- Methods: `send(data)`, `close()`
- Events: `onopen`, `onmessage`, `onerror`, `onclose`

**Files:** `lib/websocket.js`

**Status:** Not implemented.

### 9.8 Canvas API (LOWER - 2D graphics, games)
**Why:** Image manipulation, games, visualizations.

**Implementation:**
- `HTMLCanvasElement`: `width`, `height`, `getContext()`
- `CanvasRenderingContext2D`: drawing methods, transformations, gradients, patterns
- This is large—implement incrementally based on usage

**Files:** `lib/canvas.js`

**Status:** Not implemented. `HTMLCanvasElement` stub exists (just `width`/`height`).

### 9.9 Web Workers (LOWER - background threads, see also Tier 7)
**Why:** Heavy computation without blocking main thread.

**Implementation:**
- `Worker` class: constructor with script URL
- `postMessage(data)`, `terminate()`
- Events: `onmessage`, `onerror`
- Requires separate execution context

**Files:** `lib/worker.js`

**Status:** Not implemented (also tracked in Tier 7).

## Tier 10 — C API consolidation and cleanup

Low-usage or redundant C APIs in `include/` and `src/` that should be consolidated,
inlined, or removed to reduce maintenance burden and code duplication.

### 10.1 BitSet only used by one module (LOW - inline)
**Problem:** `bitset.h/bitset.c` (106 lines) has only 2 uses:
- Used only by `src/bitset.c` itself
- Used only by `include/json.h` (which is used by `quickjs-json.c`)

This is a small utility that's only needed by one consumer.

**Recommendation:** Inline `bitset.h/bitset.c` into `json.c` or keep it as a simple
dependency since it's small and well-isolated.

**Files:** `include/bitset.h`, `src/bitset.c`

**Impact:** Removes 106 lines if inlined, or keep as-is (minor cleanup opportunity).

### 10.2 async-closure.h only used by MySQL (LOW - inline)
**Problem:** `async-closure.h/async-closure.c` (166 lines) has only 2 uses:
- Used only by `quickjs-mysql.c`
- Used only by itself (`src/async-closure.c`)

This is a MySQL-specific async handler pattern.

**Recommendation:** Inline `async-closure.h/async-closure.c` into `quickjs-mysql.c` or
move to `quickjs-mysql.h`. This is MySQL-specific functionality that doesn't need to be
a general-purpose API.

**Files:** `include/async-closure.h`, `src/async-closure.c`

**Impact:** Removes 166 lines, clarifies that this is MySQL-specific code.

### 10.3 child-process.h only used by one module (LOW - inline)
**Problem:** `child-process.h/child-process.c` (525 lines) has only 2 uses:
- Used only by `quickjs-child-process.c`
- Used only by itself (`src/child-process.c`)

This is a large module that's only consumed by one binding.

**Recommendation:** Inline `child-process.h/child-process.c` into `quickjs-child-process.c`.
The header can remain as `quickjs-child-process.h` if needed for external use, but the
internal implementation doesn't need to be a separate module.

**Files:** `include/child-process.h`, `src/child-process.c`

**Impact:** Simplifies module structure, though no line count reduction (just consolidation).

### Summary

**Total lines to potentially consolidate:** ~797 lines
- BitSet: 106 lines (inline into JSON parser or keep as-is)
- async-closure: 166 lines (inline into MySQL)
- child-process: 525 lines (inline into binding, no net reduction)

**Priority order:**
1. **LOW:** Inline BitSet (10.1) - simplifies dependencies (minor)
2. **LOW:** Inline async-closure (10.2) - clarifies MySQL-specific code
3. **LOW:** Inline child-process (10.3) - simplifies structure

**Completed:**
- ~~10.3 RingBuffer~~ - Removed in commit `7cda4dec` (163 lines deleted)

**Removed (incorrect assessment):**
- ~~10.1 JSON parsers~~ - Not duplicates: `json.h` is a pull parser, `jread.h` is a push/SAX parser, `sj.h` is a simple one-shot parser. They serve different APIs.
- ~~10.2 XML parsers~~ - Not duplicates: `xml.h` is a pull parser, `xread.h` is a push/SAX parser. They serve different APIs.
- ~~10.5 ioctlcmd.h~~ - Actually used by `src/readlink.c` for Windows symlink support.

## Tier 11 — `src/qjsm.c` refactoring opportunities (found during 2026-08-16 read-through)

A full read-through of `src/qjsm.c` (the `qjsm` entry point / module loader) turned up no
correctness bugs, but several structural spots worth revisiting. Dead/commented-out code and
one obscure reversed-subscript idiom (`(dsl = path_dirlen1(path))[path]`) were already cleaned
up in place during this pass; what's left below is genuine restructuring, deliberately not
done inline since each is either large or a judgment call on API shape.

- **`main()` is a ~450-line monolith** doing CLI parsing, runtime/context setup, script and
  `-I`/`-m` loading, REPL bootstrap, and (behind `--dump --quit`) an unrelated instantiation-time
  microbenchmark, all in one function. Splitting into `jsm_parse_args()`,
  `jsm_setup_runtime()`, `jsm_run_scripts()`, and `jsm_bench_instantiation()` would make each
  piece testable/readable in isolation. Nontrivial: the pieces share a lot of local state
  (`had_error`, `sargs`, `include_list`, ...) that would need to move into a small context
  struct or be threaded through as parameters.
- **`jsm_module_func()` is a single ~270-line function** dispatching on a 20-case `magic` enum
  that mixes unrelated concerns: module bookkeeping (`ADD_MODULE`/`FIND_MODULE`/
  `FIND_MODULE_INDEX`), path resolution (`NORMALIZE_MODULE`/`LOCATE_MODULE`/`LOAD_MODULE`), and
  a block of ~11 near-identical `#if QUICKJS_INTERNAL` one-liner accessors
  (`GET_MODULE_NAME`/`GET_MODULE_VALUE`/`GET_MODULE_INDEX`/...). The accessor block in
  particular is repetitive enough to be table-driven (an array of `{magic, module_*_fn}`
  pairs looked up once) instead of 11 near-identical `case:`/`#if`/`#endif` blocks.
- **`jsm_module_loader()` (the `JSModuleLoaderFunc` implementation) does six distinct things
  in one ~140-line function** with several `goto end;`/`goto again;` jumps: `data:` URL
  handling, dispatch through the external loader chain (`jsm_call_loaders`), circular-import
  detection, `package.json` alias resolution, builtin-module lookup, and filesystem
  resolution + the "could not load module" error formatting. Worth splitting along those
  seams (e.g. `jsm_resolve_data_url`, `jsm_run_external_loaders`, `jsm_warn_circular`) so each
  piece can be reasoned about independently — risky to do without first having a test that
  exercises each branch (see the `tests/test_list.js` gap noted in Tier 4 for why that
  matters here).
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
the eagle-agent EDA parts catalog export (see `doc/eagle-agent.md`). Parsing was explicitly
out of scope for that work and is deferred:

- A `read(text)` function, symmetric with `write()`, decoding the same block-YAML subset
  back into a JS value.
- If/when that's built, model it as a computed-goto push/SAX parser in
  `src/yread.c`/`include/yread.h`, the same pattern `src/jread.c`/`include/jread.h` (JSON) and
  `src/xread.c`/`include/xread.h` (XML) already use, rather than a recursive-descent parser
  inline in `quickjs-yaml.c`.
- Scope stays pinned to the writer's own subset (block style, plain/quoted scalars, no
  anchors/aliases/multi-doc/flow/tags) — no need to handle full YAML 1.1/1.2.
