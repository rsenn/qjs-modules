# Event model v2: state of the refactor

Living status of the work specified in [api-event-model.md](api-event-model.md) (design, sections 1 to 10; end state, section 12). The
backlog and gates are in `TODO.md`, "Event model v2". This file records what is **done and how it was checked**, file by file, so a later
session can continue without re-deriving it. Update it in the same change as the code.

Last update: 2026-10-18. Nothing of it is committed by the assistant; the working tree holds it.

## 1. Where things stand

| Layer | State |
| --- | --- |
| `include/` + `src/` engines: events, pull and push sources, walkers as sources, serials, tee, JS-object sinks, builder queue | **done and tested** (`tests/c/event-model.c`) |
| bindings `quickjs-json.c`, `quickjs-xml.c` | **not lifted**: they still call `json_parse`, `xml_parse`, `jr_read`, `xr_read`; one edit made (`case JSON_EVENT_RAW` in two switches) |
| handshake symbols, `pump()`, `connect()`, JS surface | not started (P6 binding part) |
| `lib/` glue (`jsonl.js`, `json5.js`, `jsonc.js`, `bun.js`, `dom.js`, ...) | not started (P9) |
| end-state clean-up (`api-event-model.md` 12): P11 to P16 | not started; P11 and P12 come before the lift |

## 2. How to check it

```sh
cmake -S . -B build/x86_64-linux-gnu                  # after adding or removing a src/*.c file (sources are globbed)
cmake --build build/x86_64-linux-gnu -j8
timeout 60 build/x86_64-linux-gnu/event-model         # exit 0 and no output: all checks pass
timeout 120 valgrind --leak-check=full -q build/x86_64-linux-gnu/event-model   # only "realloc() with size 0" from dbuf_free (libc allocator), no leaks
timeout 600 ctest --test-dir build/x86_64-linux-gnu --timeout 60 -j4
```

Baseline of `ctest` on 2026-10-18 (145 of 153 pass). Failing, none of them caused by this work:

| Test | Why it is not this work |
| --- | --- |
| `c-json-dialects` | exercises `jread.c` and `json.c` directly, neither modified; `JR_LEGACY` on `[1,,2,]` makes the two tokenizers disagree. It is the equivalence test of the tokenizer merge (P14) and must be made green first |
| `test-globals`, `test-lexer-cpp`, `test-misc`, `test-mmap`, `test-sql` | on the list of 2026-10-17 in `TODO.md` |
| `test-directory`, `test-extract-c` | lexer and directory code, not touched; **not checked against a clean checkout** |

## 3. Files

`C` = engine-free, `E` = engine-bound (`JSContext`, `JSValue`). "Binding users" is the number of uses in `quickjs-json.c` + `quickjs-xml.c`
on 2026-10-18 (the figure that has to reach 0 for a symbol to become private, `api-event-model.md` 12.2).

### 3.1 New files

| File | Lines | Kind | Provides | Tested by |
| --- | --- | --- | --- | --- |
| `src/json/jnext.c` | 67 | C | `json_next`, `json_source` over `json_parse` | `json_events`, `json_error_event`, the pumps |
| `src/xml/xnext.c` | 143 | C | `xml_next`, `xml_source`; types comment, declaration, CDATA, instruction | `xml_typed`, `xml_pull_serial`, `xml_js_sink` |
| `src/json/jwalkev.c` | 177 | E | `JwalkSource`, `jwalk_event`, `jwalk_source`, `jwalk_source_close*` | `json_walker`, `json_walker_throws` |
| `src/xml/xwalkev.c` | 427 | E | `XwalkSource`, `xwalk_event`, `xwalk_source`, flags `XWALK_FLAT`, `XWALK_BALANCED`, `max_depth` | `xml_walker` (tree, instruction, comment, flat balanced, `max_depth`) |
| `src/json/jserial.c`, `include/json/jserial.h` | 99, 65 | C | `JsonSerial`, `jserial_read` | `json_walker` at read sizes 1, 7, 49, 343, 2401 |
| `src/xml/xserial.c`, `include/xml/xserial.h` | 99, 49 | C | `XmlSerial`, `xserial_read` | `xml_pull_serial` at the same sizes |
| `src/json/jtee.c`, `src/xml/xtee.c` | 72, 66 | C | `jtee_sink`, `xtee_sink` | `json_push_tee_build` (tee only; `xtee_sink` has no test) |
| `tests/c/event-model.c` | 451 | E | ctest `c-event-model`, 12 test groups | |

### 3.2 Changed files

| File | What changed | Old API |
| --- | --- | --- |
| `include/json/jevent.h`, `src/json/jevent.c` | `JSON_EVENT_RAW`; sink members `comment`, `raw` (last); `JsonPull`, `JsonEventHandler`, `jevent_handler`, `JsonEventSource`, `jevent_pump`, `JsonTee` | all kept |
| `include/xml/xevent.h`, `src/xml/xevent.c` | `XmlEvent` `str`/`len` unions; `XmlPull`, `XmlEventHandler`, `xevent_handler`, `XmlEventSource`, `xevent_pump`, `XmlTee`, `xevent_markup` | all kept; `XML_PUMP_ERROR` is no longer returned |
| `src/json/jpump.c` | `jr_feed`, `jr_end`, `jr_flags_from_name`; `jread_pump*`, `json_pump` reduced to calls of them | pump names kept |
| `src/xml/xpump.c` | `xr_feed`, `xr_end`; `xread_pump*`, `xml_pump` reduced to calls of them | pump names kept |
| `include/json/json.h`, `include/json/jread.h` | declarations of `json_next`, `json_source`, `jr_feed`, `jr_end`, `jr_flags_from_name` | `json_parse`, `jr_read`, `jr_finish` kept |
| `include/xml/xml.h`, `src/xml/xml.c` | `XMLParser` fields `mk`, `mk_type`, `mk_skip_end`, `mk_pi`, `mk_replay`; `xml_next`, `xml_source`; init and free of `mk` | `xml_parse` kept |
| `include/xml/xread.h` | `xr_feed`, `xr_end` | `xr_read`, `xr_finish` kept |
| `src/json/jbuilder.c`, `include/json/jbuilder.h` | `done`, `done_len`, `on_root`, `on_root_user`; `jbuild_take`, `jbuild_value_js`; `raw` member; a second top-level value no longer leaks the first (`root` was overwritten) | `jbuild_*` kept |
| `src/json/jwrite.c` | `raw` member wired to `jwrite_raw`; `comment` NULL (dropped) | |
| `src/json/jsink.c`, `include/json/jsink.h` | methods `comment`, `raw`; `js_jsonsink_from_js`, `js_jsonsink_from_js_free` | wrap, define, register kept |
| `src/xml/xsink.c`, `include/xml/xsink.h` | `js_xmlsink_from_js`, `js_xmlsink_from_js_free` | |
| `include/json/jwalk.h`, `include/xml/xwalk.h` | `JwalkSource`, `XwalkSource` and their functions | `JwalkIterator`, `XwalkIterator` kept |
| `quickjs-json.c` | two lines: `case JSON_EVENT_RAW` next to `JSON_EVENT_COMMENT` | |
| `CMakeLists.txt` | target and test `event-model` | |

### 3.3 Not touched on purpose

`src/json/json.c`, `src/json/jread.c`, `src/xml/xread.c`, `src/walk.c`, `src/xml/xtreewrite.c`, `src/xml/xlayout.c`, `src/xml/xbuilder.c`
(its `xbuild_sink` already existed). `xtreewrite.c` still writes through `xlayout`; `write()` is byte-identical, which is why `test-xml-write-golden.js` is unchanged.

## 4. Decisions taken while implementing (not in the first plan)

| # | Decision | Reason |
| --- | --- | --- |
| I1 | `json_next` / `xml_next` are **added next to** `json_parse` / `xml_parse`, not renamed | the bindings call the old ones; a rename would have broken the build outside `include/` and `src/` |
| I2 | a walker is a `JwalkSource` / `XwalkSource` (iterator + capture), not `jwalk_event(it, ctx, ev)` | the iterator must not move and the capture needs somewhere to live; the context is stored in the source |
| I3 | the XML processing instruction has no end event in `xml.c` (the `?` is dropped, the frame stays open), so `xml_next` ends it at the first event that is not an attribute and replays that event | a quirk of the parser kept "verbatim" (`EXCLAM == HYPHEN` in `xml.c`); not changed because `read()` depends on it |
| I4 | an `XE_ERROR` from `xml_next` carries the stable code (`mismatched-end-tag`) in `str`, not the tag name that `xml_pump` sent before | the error text of an event is its message, as in `JsonEvent` |
| I5 | `XWALK_BALANCED` closes unmatched elements at a marker for an outer one; it does not know void elements | knowing `br` is the consumer's job; `<br>x</br>` is the honest result of `[br, 'x', /p]` |
| I6 | `max_depth` counts levels as `xml.write()` does (walk depth / 2) | byte-identical behaviour with the legacy cut-off |
| I7 | `JsonEventSink.comment` and `.raw` are the **last** members | `jwrite_sink`, `jbuild_sink`, `tests/c/pump-roundtrip.c` initialise the first eleven positionally |
| I8 | the builder's `raw` handler accepts a number, `null`, `true`, `false` and a quoted string without escapes; a string with a backslash is dropped | `JSON.rawJSON` takes only primitives; decoding escapes belongs to the glue |
| I9 | sink members still take no position; `at` exists only on the event | unchanged from `api-event-model.md` Q4 |

## 5. Known limits and open bugs of the new code

| Item | Detail |
| --- | --- |
| JSON `at` for `jr_feed` | taken from `jr_state_t.tok`; checked only for the error case, not for every event type |
| `xml_next` `at` | line and column of the last tag; none for text; no offsets (`SourcePos.byte_offset` / `char_offset` unset) |
| `xread` has no markup events | `xr_feed` never produces comment, declaration, CDATA or instruction events (the parser has none) |
| a builder swallows `comment` and unknown raw strings | by design (I8) |
| `xtee_sink` untested | the code mirrors `jtee_sink`, which is tested |
| `js_*sink_from_js` and exceptions | a throwing JS method returns -1 and leaves the exception pending; the pump driver of P6 must check `JS_HasException` after a negative result |
| `JsonSerial` and a source that returns `JSON_PULL_FAIL` | `jserial_read` returns -1 with `error == 0` and the exception pending; documented in `jserial.h`, tested only through the walker |
| `quickjs-json.c` | builds without warnings; `JSON_EVENT_RAW` is ignored there (the push parser never produces it) |

## 6. Next steps, in order

Full list with gates: `TODO.md` "Event model v2" and `api-event-model.md` 12.5.

1. **P11** `code` and `span` on `JsonEvent` / `XmlEvent` (header change plus the four fillers), test at chunk sizes 1 and whole.
2. **P12** the engine-bound helpers of 12.1, each introduced together with the binding code it replaces so the line count of `quickjs-*.c` falls in the same change.
3. **Lift** `quickjs-json.c`, then `quickjs-xml.c`: classes on `json_next` / `xml_next` / `jr_feed` / `xr_feed`; serializers on `JsonSerial` / `XmlSerial`; `JsonBuilder` / `XmlBuilder` options; handshake, `pump()`, `connect()`; constructor options; `JsonParser.next()` results as `{type, value, loc}`.
4. **P13** privatize and delete (12.2). **P14** make `c-json-dialects` green, then measure and merge tokenizers. **P15**, **P16** as in 12.5.
5. **P9** the `lib/` glue, then delete the C `JSONL`, `read`, `write`.

## 7. Where this is recorded

| What | Where |
| --- | --- |
| design and the end state | `doc/api-event-model.md` (sections 1 to 10, 11, 12) |
| per-format interfaces and decisions | `doc/api-interface-json.md`, `doc/api-interface-xml.md` |
| how the code is split, per piece | `doc/api-implement-json.md`, `doc/api-implement-xml.md` (section 7 each) |
| backlog with gates | `TODO.md`, "Event model v2" (P1 to P16) |
| this status | `doc/event-model-state.md` |
