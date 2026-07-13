# TODO

Unfinished work found by scanning the whole codebase (native `quickjs-*.c` bindings, `src/`,
`include/`, `lib/*.js`, `doc/*.md`, `tests/test_*.js`), ordered by leverage: highest-impact,
cheapest-to-fix items first, general cleanup last. Every item below was verified by reading
the code (and, where noted, by actually running it) — not just grepped.

This supersedes the sparse root-level `TODO` file; its four items are folded in below (marked
*(pre-existing)*).

## Tier 1 — silent correctness bugs, one disabled line each, wide blast radius

These all follow the same shape: someone commented out a guard or a case while
debugging/experimenting, and it was never restored. Each is a one-line-ish fix, but until
fixed the affected API silently does the wrong thing rather than failing loudly.

- **`deep.select()`/`deep.find()` return `[]`/skip everything when called without an explicit
  key filter (the common case)** — `quickjs-deep.c:559` and `:637`:
  ```c
  BOOL filter = FLAGS_FILTER(flags) == FILTER_KEY_OF && /*!vector_empty(&atoms) &&*/ (atom_skip(&atoms, atom) ^ FLAGS_NEGATE_FILTER(flags));
  ```
  `FILTER_KEY_OF == 0` is also the *default* (no-filter) flag value, and `atom_skip()` against
  an empty allow-list returns `TRUE` — so with the `!vector_empty(&atoms)` guard disabled,
  `filter` is `TRUE` for every property whenever the caller didn't pass an explicit key list.
  Confirmed via gdb: the predicate callback is never invoked at all in the default-flags case.
  Used by `lib/deep.js`, `lib/dom.js`, `lib/assert.js`, and exercised by `tests/test_deep.js`
  and `tests/test_xml.js`. Fix: restore the `!vector_empty(&atoms) &&` guard at both sites.

- **`WritableStream.prototype.close()` is a silent no-op** — `quickjs-stream.c:1984-1987`:
  ```c
  /* case WRITABLE_METHOD_CLOSE: {
     ret = writable_close(st,  ctx);
     break;
   }*/
  ```
  The case is commented out but the method is still registered
  (`quickjs-stream.c:2097`, `JS_CFUNC_MAGIC_DEF("close", 0, ...)`), so calling
  `writableStream.close()` returns `undefined` and does nothing — `ret` stays
  `JS_UNDEFINED` from the top of `js_writable_method`. `writable_close()` itself works fine and
  is already called correctly from the transform-stream teardown path
  (`quickjs-stream.c:2303`), so this is purely a matter of uncommenting. Matches the disabled
  `//await wr.close();` in `tests/test_stream.js:37`.

- **`List.prototype.at(index)` always returns `undefined`** — `quickjs-list.c:939-947`:
  the whole `case LIST_AT:` body is commented out, but `.at()` is registered
  (`quickjs-list.c:1522`). `tests/test_list.js` doesn't exercise `.at()` (see Tier 4), so this
  has no regression coverage at all.

- **Native `xml.write()` silently truncates output whenever a node has an empty
  `children: []` array** *(this is the pre-existing `TODO` item "fix XML enumeration")* —
  `quickjs-xml.c:383-416`, in `xml_enumeration_next()`. On an empty children array,
  `property_recursion_push()` pushes a new (empty) enumeration frame, but
  `property_enumeration_setpos(it2, 0)` fails (nothing to point at) and the function falls
  through *without popping that empty frame* — leaving it stuck on top of the stack, orphaning
  the real traversal state. Reproduced directly:
  ```js
  xml.write([{ tagName: 'a', children: [] }, 'some text', { tagName: 'b', children: [...] }]);
  // -> "<a />"  (the text node and the whole second element are silently dropped)
  ```
  This matters because `lib/xml/read.js` always sets `children: []` on leaf elements, so any
  parsed-then-rewritten document with a sibling after a leaf loses data. Fix: pop/free the
  empty frame (mirror what the non-empty branch does) before falling through to the
  `while(!property_enumeration_next(it))` loop.

## Tier 2 — public API documented as working, but isn't

- **`assert.partialDeepStrictEqual()` always throws** — `lib/assert.js:120-123`:
  ```js
  export function partialDeepStrictEqual(actual, expected, message) {
    /* XXX: implement */
    throw new AssertionError(`partialDeepStrictEqual`, message);
  }
  ```
  yet `doc/assert.md:23` documents it as "`expected` is a deep subset of `actual`". Needs an
  actual partial-deep-equal implementation (probably reusing `deepEqual`'s traversal, once
  Tier 1's `deep.select` bug is fixed, since `assert.js` itself imports from `deep`).

- **`Blob.prototype.stream()` returns `undefined` instead of a `ReadableStream`** —
  `quickjs-blob.c:251-254`:
  ```c
  case BLOB_STREAM: {
    ret = JS_UNDEFINED;
    break;
  }
  ```
  `doc/blob.md` documents it as returning a `ReadableStream` over the blob's bytes; the two
  tests for it are commented out in `tests/test_blob.js:355-381` behind `/* TODO: stream() */`.
  Confirmed by running the commented-out tests standalone: both fail exactly as the TODO
  implies (`typeof stream === 'undefined'`). `quickjs-stream.c` already has a working
  `ReadableStream` implementation to wrap the blob's bytes in.

## Tier 3 — known perf/architecture debt (already flagged) and spec-compliance gaps

- **`js_is_*` type-check helpers (`js_is_arraybuffer`, `js_is_date`, `js_is_map`, etc. in
  `src/utils.c:2664-2745`) are slow** *(pre-existing TODO item)* — each does
  `instanceof || Object.prototype.toString comparison` via a string compare instead of a tag
  check. These sit on hot paths (serialization, `deep`, `inspect`), so worth profiling once
  Tier 1 is fixed and traffic patterns are trustworthy again.

- **XML writer builds into a `DynBuf` instead of streaming via a callback** *(pre-existing
  TODO item)* — architectural, would let large documents serialize without buffering the
  whole thing in memory (same shape of improvement as this session's `JsonSerializer`).

- **XML reader isn't streaming** *(pre-existing TODO item)* — same motivation, reader side.

- **Streams `respondWithNewView()` (BYOB) is missing spec-required safety checks** —
  `quickjs-stream.c:1242-1247`: the comment lists exactly what's missing (same underlying
  `ArrayBuffer`, same `byteOffset`, length constraints) but none of it is implemented. Until
  fixed, a caller can hand back a view aliasing a different/incompatible buffer without error.

## Tier 4 — structural/maintenance risk and test-coverage gaps

- **`internal.h` and `quickjs-internal.h` are two hand-forked copies of the same QuickJS
  internals header**, both carrying matching `XXX:` design-debt comments at nearly the same
  line numbers. Any future fix to one needs manual re-application to the other; worth
  collapsing to a single source of truth (or confirming they've already diverged and
  documenting why two copies exist).

- **`tests/test_list.js` isn't a real test** — it's an ad-hoc script (not using the
  `assert`/`assertEq` pattern every other `test_*.js` file uses) that ends in an unguarded
  `while(!skip()) {}` loop. It doesn't exercise `.at()`, which is exactly why Tier 1's
  `List.at()` bug has no regression coverage. Worth rewriting properly.

- **`tests/test_xml.js` + `lib/xml/write.js` mishandle an Array root value** — found while
  investigating the Tier-1 XML bug: `lib/xml/write.js:3` only special-cases
  `o.tagName === undefined` for the "not an element" fallback, so an Array (exactly what
  `readXML()` always returns at the top level) falls through to
  `Array.prototype.toString`/`join` instead of being serialized element-by-element. Running
  `tests/test_xml.js` against a real file currently produces a corrupted, near-empty output
  file rather than a visible failure — worth fixing `write.js` and adding an assertion so this
  fails loudly instead of writing garbage silently.

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
- `src/qjsm.c:1034-1045` — a disabled, more descriptive module-load error message
  (`"could not load module filename '%s'"` with more context); currently module-load failures
  surface with less detail than this dead code would have provided.
- `src/glob.c:582` *(pre-existing TODO-style comment)* — `/* TODO: don't call for ENOENT or
  ENOTDIR? */`, minor optimization.
- `wasm` module was scaffolded in `CMakeLists.txt` (the `option(MODULE_WASM ...)` declaration
  itself is commented out at line 51, `BUILD_LIBWASM` defaults off) but no `quickjs-wasm.c`
  exists anywhere — either finish it or remove the dead `if(MODULE_WASM)` block
  (`CMakeLists.txt:418-447`).
- Minor hygiene: stray `src/utils.c.orig` backup file left in the tree; `quickjs-stream.c` has
  ~13 functions with unfilled Doxygen placeholder text (`{ function_description }` etc.).
