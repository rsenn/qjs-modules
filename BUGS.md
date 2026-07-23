# Known bugs

Bugs discovered while writing unit tests for `tests/test_*.js`, not fixed as
part of that work (test-writing only, per explicit instruction). Each entry
names the file/function, the concrete failure, and how to reproduce it.

Newly discovered bugs are appended to the end of this file, in the order found.

Found during the 2026-07-23 architecture/quickjs-2026-compat assessment (see `ASSESSMENT.md`):

- directory-default-export-missing: `quickjs-directory.c:322-327` has the `"default"` module
  export wired up but commented out, and even restoring it wouldn't fully fix it -- unlike
  `quickjs-child-process.c:597/618`, `quickjs-deep.c:1137/1156`, `quickjs-xml.c:911/928`
  (which all declare `"default"` via `JS_AddModuleExport` *and* set it via
  `JS_SetModuleExport`), `quickjs-directory.c` never calls `JS_AddModuleExport(ctx, m,
  "default")` at all, so the export slot doesn't exist for `JS_SetModuleExport` to fill even
  once uncommented.

    import Directory from 'directory';
    console.log(Directory); // undefined -- should be the Directory constructor

- sockets-default-export-undeclared: `js_sockets_init` (`quickjs-sockets.c:3107-3111`)
  unconditionally runs `JS_SetModuleExport(ctx, m, "default", socket_ctor)` when the module is
  loaded as `"socket"`, but the matching `JS_AddModuleExport(ctx, m, "default")` is commented
  out (`quickjs-sockets.c:3145`). Pre-existing (predates the 2026-07-20 `dbuf_claim` commit).

    import Socket from 'socket';
    console.log(Socket); // likely undefined or UB -- export slot was never declared

- vector-copy-size-underflow: `vector_copy()` (`src/vector.c:207`) resets `dst->buf` and
  `dst->allocated_size` to 0 but not `dst->size`, then computes the `dbuf_claim()` delta as
  `src->size - dst->size` -- a `size_t` subtraction that underflows to a huge value if `dst`
  is a reused (non-zero-initialized) `Vector`. Introduced by the mechanical `dbuf_realloc` ->
  `dbuf_claim` conversion (commit `3e8d44cc`), which changed the second argument from a total
  size to a delta. Currently safe only because the sole call site,
  `src/predicate.c:1263` (`vector_copy(&ret->charset.chars, &pr->charset.chars)`), passes a
  `js_mallocz`'d (zeroed) struct -- not yet isolated to a concrete JS repro, since no current
  caller reuses a non-zeroed `Vector` as the destination. Fix: add `dst->size = 0;` alongside
  the other two resets.

- deep-iterator-pointer-leak: `quickjs-deep.c:487` has a commented-out `pointer_free()` call
  in `DeepIterator`'s cleanup path. Not yet confirmed as a real leak -- needs a full ownership
  trace of `iter->pointer` (it may be intentionally unowned at this point) before treating as
  more than a suspicion.

    let it = deep.iterate({ a: { b: { c: 1 } } });
    for (const _ of it) {}
    // suspected: iter->pointer never freed; not yet confirmed via a leak-counting repro

- predicate-operators-leak: `quickjs-predicate.c:1065` has a commented-out
  `JS_FreeValue(ctx, predicate_operators)`. `predicate_operators` looks like a module-level
  singleton table; if it's meant to be freed on module teardown this is a leak, but tables
  like this are often intentionally kept for the process lifetime -- needs confirming which
  before treating as a real bug. No isolated JS repro yet (would require observing the
  singleton across repeated context teardown, not just normal use).
