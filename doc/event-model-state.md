# Event model v2: state of the refactor

Living status of the work specified in [api-event-model.md](api-event-model.md) (design, sections 1 to 10; end state, section 12). The
backlog and gates are in `TODO.md`, "Event model v2". This file records what is **done and how it was checked**, file by file, so a later
session can continue without re-deriving it. Update it in the same change as the code.

Last update: 2026-10-18. P1 to P8 (include/ and src/) are committed as `21a5513f`; P11 is in the working tree.

## 1. Where things stand

| Layer | State |
| --- | --- |
| `include/` + `src/` engines: events, pull and push sources, walkers as sources, serials, tee, JS-object sinks, builder queue | **done and tested** (`tests/c/event-model.c`) |
| bindings `quickjs-json.c`, `quickjs-xml.c` | **not lifted**: they still call `json_parse`, `xml_parse`, `jr_read`, `xr_read`; one edit made (`case JSON_EVENT_RAW` in two switches) |
| handshake symbols, `pump()`, `connect()`, JS surface | not started (P6 binding part) |
| `lib/` glue (`jsonl.js`, `json5.js`, `jsonc.js`, `bun.js`, `dom.js`, ...) | not started (P9) |
| P11: `code` and `span` on error events | **done** 2026-10-18 (uncommitted), see 3.3 |
| end-state clean-up (`api-event-model.md` 12): P12 to P16 | not started; P12 comes with the lift |

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

### 3.3 P11: error code and span

| File | Change |
| --- | --- |
| `include/json/jevent.h`, `include/xml/xevent.h` | `JsonEvent` and `XmlEvent` get `code` (static string, the stable kebab-case name) and `span` (bytes from `at` through the offending byte, 0 at the end of input), set on error events only |
| `src/json/jnext.c` | `json_next()` fills them from `err_code` / `err_len`; new `json_next_end()`, the event form of `json_end()`: the pull parser cannot tell the end of its input from a pause, so a caller that knows no more data comes calls it (until `JSON_PULL_END`) to get a trailing number or the `unexpected-end` error. Only strict flags report an unfinished document (`JR_LEGACY` never does) |
| `src/json/jpump.c` | `jr_feed()` / `jr_end()` fill them from `jr_error_name(err_code)` / `err_len` |
| `src/xml/xnext.c`, `src/xml/xpump.c` | `xml_next()` (`mismatched-end-tag`, span of `</b>` is 4) and `xr_feed()` / `xr_end()` (`xr_error_name`; `unexpected-end` span 0) |
| `tests/c/event-model.c` | `json_error_info`, `xml_error_info`: pull and push give the same code, span and position at chunk sizes 1, 2, 3 and whole; truncated JSON through `json_next_end` |

With these a binding no longer reads `err_code`, `err_len`, `tag_line` of the parsers for an error; the remaining reads are the non-error ones (`tag_line` of events).

### 3.3a P12 progress (lift of quickjs-json.c / quickjs-xml.c)

Done (2026-10-18):

- `include/json/jopts.h` + `src/json/jopts.c`: `jsonspace_parse/apply/args` (was `json_space_*`, `json_replacer_space`) and
  `jwrite_options_from_js` (was `js_jsonwriter_options`). `quickjs-json.c` 1961 -> 1850 lines.
  `ctest -R "json|event"`: only the known `c-json-dialects` failure.

- `js_syntax_error_at` / `js_throw_syntax_error_at` in `quickjs-location.c/.h` (SourcePos -> SyntaxError, shared by json and xml
  bindings; replaces `json_error_new`/`json_throw` and the inline copy in `xml_pushparser_report`).
- `include/json/jevent-js.h` + `src/json/jevent-js.c`: `js_jsonevent_value`. `JsonPushParser` now runs on `jr_feed`/`jr_end`
  with one `JsonEvent` handler (no `jr_str_t`, no `jread_callback*`). `quickjs-json.c` is 1780 lines.

- `JsonParser.parse()` runs on `json_next` (`js_jsonparser_next`: END -> JSON_NONE, AGAIN -> JSON_NEED_DATA, error event ->
  SyntaxError with the file of `p->loc`). The `pos`/`token`/`state`/`depth` getters still read `JsonParser` fields (P13).
- `JsonSerializer` runs on `JwalkSource` + `JsonSerial` (`jserial_read` got `chars == 2`: n counts characters). `write()` runs on
  `JwalkSource` + `jwrite_sink`; the stringify `WalkInterface` (`js_stringify_*`) is gone.
- `read()` runs on `JsonParser` (flags `JR_JSON`) + `JsonBuilder`; the vendored sj reader is gone. Documented change: `read()`
  now decodes string escapes like JSON.parse (test 'read: decodes string escapes like JSON.parse' replaces the old
  'known limitation' test).
- `quickjs-json.c`: 1961 -> 1557 lines. Full ctest: the same 8 failures as before the lift.

- `quickjs-xml.c`: `xml_throw_at` replaced by `js_throw_syntax_error_at` (2158 -> 2126 lines).
- XML builder markup (2026-10-18): `xbuild_handler` (`XmlEventHandler` form) and the `xbuild_sink` members now build the
  typed markup: comment / declaration / cdata as a leaf node (`!--text--`, `!text`, `![CDATA[text]]`, flat mode leaves it open
  until the next call), instruction as an open element `?name` with attributes re-split from its text, as xml_parse delivers it
  (dom.js `Parser` relies on `?xml` holding the document). `XmlEvent` got `self_closing` (xml_next sets it) and an empty
  attribute value is `""`, not NULL. `js_xml_parse` (read) runs on `xml_next` + `xbuild_handler`. Known difference: a `</a>`
  that directly follows an instruction is swallowed by xml_next (the PI ends on it), so the tree of `<a><?p?></a><b/>` differs.
  Test 'xml.read: markup nodes in tree and flat mode'. Full ctest: the same 8 failures.

- `XMLPushParser` runs on `xr_feed`/`xr_end` with `xml_pushparser_event(_build)` handlers (no `xr_str_t`, no `xread_callback*`).
- `XMLTreeSerializer` runs on `XwalkSource` + `XmlSerial` (flat balanced when the last node has no children array, as
  `xml.write()`); 600 lines of replay code gone. Two golden cases changed on purpose: serializer pi (was `<?xml?>`, now the
  instruction with its attributes, then its children) and serializer marker (was `<a />t</a />`, now `<a>t</a>`).
  `xwalkev.c` now queues an instruction before its children, not at the end of the element.
- `quickjs-xml.c`: 2158 -> 1706 lines. Full ctest: the same 8 failures.
- XMLParser and XMLNodeParser stay on `xml_parse` on purpose: `parse()` returns raw xml_parse ids and the tests pin that
  markup arrives as start+end. They change with their redesign (typed `next()`), not in the lift.

- Second round (2026-10-18), three steps:
  1. `JsonWriter` is a JsonEventSink object (`js_jsonsink_register` + `js_jsonsink_define`, plus one `value(v)` method);
     `js_walk_register`, `js_walk_define`, `js_walk_wrap`, `WalkFromOpaque` and the WalkInterface JS class are deleted
     from `walk.c`/`walk.h` (-190 lines). The `WalkInterface` struct itself stays until P15.
  2. JSONL moved to `lib/jsonl.js` (plain JS, runs on node too, tests import it from there, `doc/js/jsonl.md`);
     it is no longer an export of the `json` module. `quickjs-json.c`: 1305 lines.
  3. P13 for the push parsers and pumps: `jr_read`/`jr_finish`/`jr_callback`/`jr_str_t` and `xr_read`/`xr_finish`/`xr_callback`/
     `xr_str_t` are private to `jread.c`/`xread.c`; `jr_feed`/`jr_end`/`xr_feed`/`xr_end` and `jr_flags_from_name` live there
     too. `jpump.c` and `xpump.c` are deleted together with `jread_pump(_finish)`, `json_pump`, `xread_pump(_finish)`,
     `xml_pump`, `JSON_PUMP_*`, `XML_PUMP_*` (use `jr_feed(..., jevent_handler, &sink)` and `jevent_pump(json_source(p), &sink)`).
     The C tests jread-positions, jread-dialects, json-dialects and pump-roundtrip run on the handler API.
- Full ctest after the second round: the same 8 failures.

- P6 handshake (2026-10-18): `include/{json,xml}/{j,x}connect.h` + `src/{json,xml}/{j,x}connect.c`
  (`js_jsonsink_resolve`: the sink behind `obj[Symbol.for('qjs.jsonsink')]()` by address, else the JS-object adapter;
  `js_jsonsink_define_handshake`; XML twin), `js_{json,xml}sink_from_js_free_rt`. Bindings: `JsonWriter`/`XMLWriter`
  and the new `JsonBuilder`/`XmlBuilder` and `tee` answer the handshake; `JsonParser.pump(sink, end)`, `XMLParser.pump(sink)`,
  `JsonPushParser.connect(sink)`, `XMLPushParser.connect(sink)` plus the `sink` getter. Tests in test-json.js and test-xml.js
  ('handshake: pump(), connect(), tee'). `quickjs-json.c` 1727 lines, `quickjs-xml.c` 2126. Full ctest: the same 8 failures.
  Known limit: no self-closing flag through a sink, so a built tree gives `<b/>` an empty `children`.

- P8 (2026-10-18): `JsonParser(input, {dialect, strict, comments, filename})` and `JsonPushParser({dialect, strict, ...})`;
  `jevent_number()` (src/json/jevent.c) converts NUMBER text of every dialect, used by the builder and `js_jsonevent_value`;
  `include/xml/xopts.h` + `src/xml/xopts.c` (`xml_options_from_js`) is the one XML option reader for `read()` and `XMLParser`
  (fixes a leak of the selfClosingTags array in `read()`). `strict` and `entities` for XML wait for the engine.

- Typed pull (2026-10-18): `JsonParser.next()` and `XMLParser.next()` return `{type, value?/name?, loc}` events (`js_jsonevent_new`,
  `js_xmlevent_new`), both parsers are iterable. `parse()` and the raw getters stay, deprecated: `lib/html.js`,
  `examples/html2md.js`, `tools/site/markdown.js` and the sibling `qjs-lws/examples/crawler/lib/html2md.js` still use them, so
  `xml_parse` / `json_parse` cannot become private before those move to `next()`.

Open, in order: migrate those four consumers to `next()` then delete `parse()` + make `json_parse`/`xml_parse` private (rest of P13),
`XMLNodeParser` as a one-line alias in the glue,
P16 (user decides: changes `xml.write()` layout).

Earlier list: the `XMLParser`/`XMLNodeParser` redesign (typed `next()`, then P13 for XML),
P16 (user decides: changes `xml.write()` layout), JSON side of P13 (`json_parse`, `JsonParser` fields).

Earlier list: P8 (parser options: dialect names, strict), the `XMLParser`/`XMLNodeParser` redesign (typed `next()`),
P16 (user decides: changes `xml.write()` layout), JSON side of P13 (`json_parse`, `JsonParser` fields).

Older open list (superseded by the line above where they overlap): the handshake (P6: jconnect.c, xconnect.c, `pump()`/`connect()`), P16 (`write()` through `xwalk_source` +
XmlWriter, deletes `xtreewrite.[ch]` and `xlayout.[ch]`, about 640 lines, changes the layout of `xml.write()`: the user decides),
the `XMLParser`/`XMLNodeParser` redesign, JSON side of P13 (`json_parse`, `JsonParser` fields), then the older list below.

Older open list: JSONL (Bun glue in `lib/`, needs `parseChunk` position from the builder's `on_root`), `JsonWriter` class on
the sink members, the handshake (`jconnect.c`, `pump()`/`connect()`), P13 for the JSON side (`json_parse`, `jr_read`,
`jr_finish`, `jr_str_t` users left: none in `quickjs-json.c` except via `json_next`); then the XML twins; then P13.

### 3.4 Not touched on purpose

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
| pull JSON end of input | `json_next` answers `AGAIN` at the end; a binding that knows the input is whole must call `json_next_end()`, or the unfinished-document error is never seen |
| a builder swallows `comment` and unknown raw strings | by design (I8) |
| `xtee_sink` untested | the code mirrors `jtee_sink`, which is tested |
| `js_*sink_from_js` and exceptions | a throwing JS method returns -1 and leaves the exception pending; the pump driver of P6 must check `JS_HasException` after a negative result |
| `JsonSerial` and a source that returns `JSON_PULL_FAIL` | `jserial_read` returns -1 with `error == 0` and the exception pending; documented in `jserial.h`, tested only through the walker |
| `quickjs-json.c` | builds without warnings; `JSON_EVENT_RAW` is ignored there (the push parser never produces it) |

## 6. Next steps, in order

Full list with gates: `TODO.md` "Event model v2" and `api-event-model.md` 12.5.

1. ~~P11~~ done (3.3).
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

## P16 done

`xml.write()` is `xwalk_source` pumped into an `XmlWriter` (`XW_PRETTY`, indent 2) in `quickjs-xml.c` (`xml_write_tree`).
`xtreewrite.[ch]` and `xlayout.[ch]` are deleted. Layout change: nested children are indented by 2 per level (tree and flat),
no trailing newline, inline text keeps mixed content on one line, a `maxDepth` cut element is written `<b />`.
`tests/fixtures/xml-write-golden.json` is regenerated.

## Slimming (after P16)

`XMLParser.parse()`, `eventName`/`eventValue`/`hasValue`, `root`, the `on*` callbacks, the `builder` option, `PARSE_*` and `XMLNodeParser`
are gone, as are `JsonParser.parse()`, `pos`, `token`, `state`, `NEED_DATA` and `NONE`. `next()`, the iterator, `pump()`, `depth`,
`location` and `tolerant`/`comments` remain. `quickjs-xml.c` 2211 to 1708 lines, `quickjs-json.c` 1837 to 1756. The consumers
(`lib/html.js`, `examples/html2md.js`, qjs-lws `examples/crawler/lib/html2md.js`, `utilities/jsonpp.js`) use `next()`. A JSON event has
`raw` for a number and the text as `value` for a comment. The old pull tests run on a `Pull` subclass in `test-xml.js` / `test-json.js`.
Open: `json_parse` / `xml_parse` stay as engine functions under `json_next` / `xml_next` (P13 rest), tokenizer merge (P14), P15.

## Lifted out of quickjs-{json,xml}.c (no new files)

`quickjs-json.c` 1756 to 1412 lines, `quickjs-xml.c` 1708 to 1339. Moved into existing files:
- `jevent-js.c` / `xevent-js.c`: `json_read_value`, `xml_read_value`, `xml_write_value`, `xml_tree_list`, `js_xmlwrite_throw`,
  `js_jsonevent_throw`, `js_xmlevent_error`.
- `jwalk.c`: `jwalk_stringify` (the core of `json.write()`).
- `jopts.c` / `xopts.c`: `js_jsonwriter_ret`, `jwrite_js_value`, `jread_options_from_js` (dialect and strict, shared by both
  JSON parsers), `xml_html_self_closing_tags`.
- `jsink.c` / `xsink.c`: `js_jsonsink_failed`, `js_xmlsink_failed`.
- `jconnect.c` / `xconnect.c`: `JsonJsCallbacks` / `XmlJsCallbacks` (the callback mode of the push parsers) and
  `JsonJsTee` / `XmlJsTee` (the JS-owning tee).
What stays is binding: classes, constructors, getters, finalizers, the push parsers' write/close error rules, module init.
