
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
| `jr_*` (`jread`) | `src/jread.c` | bytes → events | **push**, chunked, resumable mid-token, resyncs after errors | `jr_read(cb, chunk, len, ud, state)`, `jr_finish` | `jr_callback(JsonEventType, jr_str_t*, ud)` | code yes; `jread.h` includes `vector.h` → `quickjs.h` |
| `JsonParser` (`json.h`) | `src/json.c` | bytes → events | **pull**, `Reader`, `JSON_NEED_DATA` to resume, comments, `Location` | `Reader` (4 KiB-block buffered) | one `JsonEventType` per `json_parse()` | no: `json_init(…, JSContext*)`, `json_free(…, JSRuntime*)`, `Location` |
| `jwrite_*` (`JsonWriter`) | `src/jwrite.c` | events → bytes | **push** (caller calls `jwrite_object_start/key/int64/…`), `Writer` sink, backpressure via `Writer` returning 0 | event calls | bytes through `Writer`; `-JWRITE_E_*` | code yes; header includes `stream-utils.h`/`vector.h` → `quickjs.h` |
| `jbuild_*` (`JsonBuilder`) | `src/jbuilder.c` | events → JS value | push (`push/pop/key/value`), consumes `JsonEventType` | events | `JSValue` root | **no** (JSContext, JSValue) |
| `JwalkIterator` / `WalkInterface` | `src/jwalk.c` | JS value → events | **pull** (`jwalk_next()` = one event), or iface callbacks | `JSValue` | 6 callbacks (`key/object_start/array_start/object_end/array_end/value`) taking `JSContext*`, `JSValueConst` | **no** |
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
value traversal that `include/jwalk.h` already provides (`json_serializer_step_inner` ≈
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
