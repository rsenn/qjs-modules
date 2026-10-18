# XML: how the implementation is split

> **Status 2026-10-18 (implemented).** The event model is in: every class is a source or sink over `JsonEvent` / `XmlEvent`, `pump()`/`connect()`/`tee`/`JsonBuilder`/`XmlBuilder` exist, `parse()` and `XMLNodeParser` are removed (`next()` and the iterator replace them), `xml.write()` is `xwalk` pumped into an `XmlWriter` (`XW_PRETTY`, indent 2), `quickjs-json.c` is 1412 and `quickjs-xml.c` 1339 lines. The layering described below is done except the `lib/` glue for `read`/`write`, which stay native.
> Running state and what is left: [event-model-state.md](event-model-state.md). Text below that describes `parse()`, `XMLNodeParser`,
> `eventName`/`eventValue`, `PARSE_*`, `xtreewrite` or `xlayout` is the plan or the history, those are gone.

`quickjs-xml.c` is meant to become what `quickjs-json.c` became: classes, argument parsing and
error mapping, over engine-free pieces in `include/` + `src/`. This document records where the
code is now, which existing header could take over which part (every candidate was checked
against the code), and what remains in the binding. Companions:
[api-compare-xml.md](api-compare-xml.md), [api-interface-xml.md](api-interface-xml.md); the
phase-by-phase plan and its status are in `TODO.md` Tier 20.

State 2026-10-17 (nothing committed): `quickjs-xml.c` is 2713 lines (HEAD had 3064). New pieces
exist but have few callers yet.

## 1. The pieces

| Piece | Files | Lines | Engine | State |
| --- | --- | --- | --- | --- |
| pull parser | `xml.[ch]` | 910 + 157 | `Location` only | in use: `read()`, `XMLParser`, `XMLNodeParser`; resumable (`XML_PARSE_AGAIN`), tolerant, flat and implied-end modes, tag line/column |
| push tokenizer | `xread.[ch]` | 409 + 92 | none | in use: `XMLPushParser`; lacks tolerant mode, implied ends, flat, raw text, locations |
| event writer | `xwrite.[ch]` | 279 + 112 | none | **new**, no caller yet; compact and pretty layouts, typed markup, `xwrite_put_escaped` |
| generic walker | `walk.[ch]` | 531 + 122 | engine | in use through `jwalk` (JSON) |
| element-tree walker | `xwalk.[ch]` | 149 + 61 | engine | **new**, no caller yet; `tagName`/`attributes`/`children` through two `WalkHooks` |
| builder | `jbuilder.[ch]` | 233 + 40 | engine | JSON only; `xbuilder` is planned on top of it |
| entities | `xml_entities.h` | | none | decode side only |

Dataflow of the public API, now and after the plan:

```
now
read(text)          xml.c --events--> XRFrame adapter (quickjs-xml.c) --> tree
XMLParser           xml.c --> JS callbacks / XMLBuilder --> .root
XMLNodeParser       xml.c --> flat node assembly (own code)
XMLPushParser       xread.c --callback--> XMLBuilder --> .root
write(tree|list)    own traversal (property_enumeration) + xml_write_* --> DynBuf --> string
XMLTreeSerializer       own traversal + xsw_* + replay (skip/delivered/capped) --> read(n|buffer)
XMLWriter           own methods --> Writer (no escaping)

planned
read / XMLParser / XMLNodeParser / XMLPushParser:  xml.c | xread.c --events--> xbuilder (on jbuilder) --> tree
write / XMLTreeSerializer:  xwalk (on walk) --WalkInterface--> adapter --events--> xwrite --Writer--> DynBuf / buffer
XMLWriter:              JS calls --> xwrite --> Writer
```

## 2. What the binding holds (`quickjs-xml.c`, 2713 lines)

| Section | Lines | Content | Fate |
| --- | --- | --- | --- |
| write helpers | 350 | `dbuf_put_escaped_xml`, `xml_write_attributes/indent/string/text/element`, `xml_close_element`, `xml_enumeration_next`, `xml_num_children` | to `xwrite` (steps 3c, 3d, 4d), about 230 |
| `read()` adapter | 260 | `XRFrame`, `js_xml_parse` (203), `js_xml_read` (58) | to `xbuilder`, about 150 |
| `write()` | 140 | `js_xml_write_tree/list`, `js_xml_write` | to the `xwalk` adapter (step 4d), about 100 |
| `XMLTreeSerializer` | 600 | resumable traversal, `xsw_*`, `XMLCappedBuf`, class | to `xwalk` + `xwrite` (Phase 5), about 450 |
| `XMLBuilder` and push glue | 250 | `xml_builder_*`, `xread_callback*`, `XMLPushParser` | builder to `xbuilder`; class stays |
| `XMLParser` | 330 | events to JS callbacks, builder hook, location glue, getters | class stays; shared glue (Phase 7) |
| `XMLNodeParser` | 240 | flat node assembly | to `xbuilder` flat mode |
| `XMLWriter` | 210 | method switch, getters | to `xwrite` (step 3c), about 120 |
| init and exports | 84 | | stays |

## 3. Lift candidates, header by header

Each header of the candidate list, with the call sites in `quickjs-xml.c` found by `grep`:

| Header | Used now | What more could move there or be used | Verdict |
| --- | --- | --- | --- |
| `buffer-utils.h` | `js_input_chars` / `js_input_args` (4 sites), `InputBuffer` | the input argument (string, buffer, reader) is turned into a `Reader` at several sites with different helpers (`reader_from_js` 2x, `reader_from_bytes` 1x, `js_input_args`/`js_input_chars` 4x) | **lift** one `reader_from_js_input()` next to `reader_from_js` in `stream-utils` |
| `char-utils.h` | `byte_chr`, `str_count`, `is_whitespace_char` in the write helpers | nothing; those users move to `xwrite` | after step 3d: not used |
| `iteration.h` | no | consumes iterables; `XMLParser`/`XMLNodeParser` only produce | not applicable |
| `jbuilder.h` | no | `xbuilder` layers on it: events to `push/key/value/pop`, plus a prototype option (plain objects) and `jbuild_value_js()` | **use** (Phase 2); replaces `XRFrame`, `XMLBuilder`, `XMLNodeParser` assembly, about 400 lines |
| `jread.h` / `json.h` / `jwrite.h` | no | none (JSON-specific) | not applicable |
| `jwalk.h` | no | none (JSON rules) | not applicable |
| `walk.h` | no | the stepper under `xwalk`; stack, skip, depth, circular check | **use** (done in `xwalk`, not yet called) |
| `xwalk.h` | no | the `write()` and `XMLTreeSerializer` traversals | **use** (step 4d, Phase 5) |
| `xwrite.h` | no | every byte `write()`, `XMLTreeSerializer`, `XMLWriter` emit | **use** (steps 3c to 3e) |
| `xml.h` | yes | one more consumer-neutral entry: `tag_line`/`tag_column` are there, the location glue in the binding still re-derives them in two places | **lift** `xml_nodeparser_sync_location` and `xml_parser_sync_location` into one helper (Phase 7) |
| `xread.h` | `XMLPushParser` | would be unnecessary if `xml.c` ran over a push `Reader` (it already resumes) | decide after measuring (Phase 6), recommendation: delete `xread` |
| `location.h` | `location_new`, `location_free`, `location_set_filename` (8 sites) | nothing | done |
| `virtual-properties.h` | `virtual_properties_map` + `virtual_set` for the per-element location side table in `read(..., {location: true})` | belongs in `xbuilder`'s location option, not in the binding | move with `xbuilder` |
| `property-enumeration.h` | 6 uses in the write helpers, 7 `property_recursion_push` in `XMLTreeSerializer` | replaced by `xwalk`/`walk`, which already uses it | **drop** from the binding after Phase 5 |
| `stream-utils.h` | `Reader`, `Writer`; a private capped writer (`xml_write_capped`, `xml_write_skip`) for `XMLTreeSerializer` | `writer_counted()` already counts bytes (`XMLWriter.written` is counted by hand); the capped/skip pair is the replay design `JsonSerializer` dropped | **use** `xwrite.written`; capped/skip **deleted** in Phase 5 |
| `utils.h` | `js_get_propertystr_*` (10 sites), `js_has_propertystr` (6), `js_array_length`, numeric converters | the option parsing (`tolerant`, `flat`, `selfClosingTags`, `location`) is written four times: `read`, `XMLParser`, `XMLNodeParser`, `XMLPushParser` | **lift** into one `xml_options_from_js()` inside the binding (Phase 7) |
| `vector.h` | stacks, frames | nothing | done |
| `xml_entities.h` | decode only | the escape rule lives in `xwrite_put_escaped`; entity names are in two places | keep the encode side next to the decode table when `xwrite` has callers |

Also checked and rejected: `buffer-utils.h` `dbuf_put_escaped_*` and `stream-utils.h` `writer_escaped`
escape with backslashes (C style), not with entities, so they cannot replace `xwrite_put_escaped`.

## 4. Order and expected size

| Step | Change | `quickjs-xml.c` after (lines, approx.) |
| --- | --- | --- |
| 3c | `XMLWriter` on `xwrite` | 2590 |
| 3d | `write()` emits through `xwrite` | 2360 |
| 4d | `write()` traversal on `xwalk` | 2260 |
| 3e + 5 | `XMLTreeSerializer` on `xwalk` + `xwrite`, replay removed | 1810 |
| 2 (+1) | `xbuilder`; `XRFrame`, `XMLBuilder`, `XMLNodeParser` assembly removed | 1410 |
| 6, 7 | push side, shared glue and option parsing | 1200 |

Rules: `test-xml.js` (150 tests) and `test-xml-write-golden.js` (98 cases) stay green except for the visible changes listed in
`TODO.md` (`XMLWriter` escapes; value-less attributes written bare; `indent < 0` stops breaking at level 0); the `read()`
differential against the pre-refactor parser stays at 0 on well-formed input.

## 5. Error and location plumbing

### 5.1 Where errors are made today (`quickjs-xml.c`)

| Sites | Class used | Problem |
| --- | --- | --- |
| `js_xml_parse` mismatch | `SyntaxError` `mismatch </c> at f.xml:2:5` | position only in the message; lenient parsing repairs without saying so |
| `js_xml_read` | `ReferenceError` `expecting buffer or string` | should be `TypeError` |
| `XMLPushParser` write and close (4 sites) | `SyntaxError` `malformed input`, `malformed or truncated input`, `unclosed element` | no position, and `write()` is silent on a mismatch |
| `XMLParser` | `TypeError` `no input to parse`; `PARSE_ERROR` status | no object describes the failure |
| `XMLWriter` | none | ordering mistakes are written as given |

8 `JS_Throw*` sites in total.

### 5.2 Throwing

One helper for every parser binding, in `quickjs-location.[ch]` (the module that owns `Location`):

```c
/* builds a SyntaxError with own, non-enumerable loc (a Location clone), length and code.
 *
 *   const Location*  at      start of the offending token
 *   size_t           length  characters it spans (0 at end of input)
 *   const char*      code    stable kebab-case name
 *
 *   returns JSValue  the error object (refcount: a new reference), or JS_EXCEPTION */
JSValue js_syntax_error_new(JSContext*, const Location* at, size_t length, const char* code, const char* fmt, ...);
/* ... and throws it: returns JS_EXCEPTION with the SyntaxError pending. */
JSValue js_throw_syntax_error(JSContext*, const Location* at, size_t length, const char* code, const char* fmt, ...);
```

The object form is needed where nothing is thrown: `JSONL.parseChunk().error`, `onError(err)` / `onParseError(err)`, `parser.error`, the members of
an `AggregateError`, a promise rejection. The message is `"<text> at <file>:<line>:<column>"`.

### 5.3 Position in the engine-free pieces

| Piece | Add | Notes |
| --- | --- | --- |
| `xml.c` (`XMLParser`) | `err_code`, `err_len`; keeps `tag_line`/`tag_column` as the start of the offending tag; repairs recorded as non-fatal errors (`err_fatal` bit) for `onParseError` | `Location loc` already advances per character; make it count `\r\n` as one line break |
| `xread.c` | `SourcePos` in `xr_state_t`, error info as in `jread` | or delete `xread` (Tier 20 phase 6) and inherit `xml.c` |
| `xwrite.c` | none: `-XWRITE_E_*` already names the failure; the binding maps it to `TypeError` + `code` | |
| `xbuilder` | the per-element `Location` side table (`location` option) stays the client-facing form of location tracking | |
| `srcpos.h` | shared with `jread` (see `api-implement-json.md` 5.3) | |

### 5.4 Tests

`tests/unittests/test-xml-errors.js`: strict and lenient inputs (mismatched end tag, EOF in a tag, EOF in an element, duplicate attribute,
unquoted value, undefined entity, two roots, text outside the root, control character, unterminated comment and CDATA, bad declaration) for
`read`, `XMLParser`, `XMLNodeParser`, `XMLPushParser`, `DOMParser`; `code`, `loc`, `length`; chunk invariance for the push parser; the
`<parsererror>` document for `DOMParser`; lenient repairs reach `onParseError` and never throw. Message texts of Bun's `XML.parse` are the
oracle for which inputs are errors (`api-compare-xml.md` 3), not for the wording.


## 6. State 2026-10-17

Done: `read()` on `xbuilder`; `write()` on `xwalk` + `xlayout` (`src/xml/xtreewrite.c`, tree and flat list; `quickjs-xml.c` is 2156 lines); `XMLWriter` on
`xwrite`, decorated with the `XmlEventSink` methods by `js_xmlsink_*` (`src/xml/xsink.c`); `XmlEventSink`, pumps, `XmlBuilder`; error contract E0 to E2.
Sections 2 and 3 above predate this; the open items are in `TODO.md`, "Remaining work: json and xml" (X1 to X8, B1 to B9).

## 7. Plan 2026-10-18: engines in C, APIs in `lib/`

Decision (`api-event-model.md`): `quickjs-xml.c` holds **engines**, each a source or a sink, pull or push, over `XmlEvent`; `read`, `write`,
`DOMParser`, `XMLSerializer`, `XML.parse` / `XML.stringify` and `XMLNodeParser` are compositions in `lib/`. What that does to the pieces:

| Piece | Now | After |
| --- | --- | --- |
| `xml.c` (pull) | `xml_parse(p)` returns an event id or `XML_PARSE_*`, data in `event_name` / `event_value` / `event_has_value`, markup as magic-named elements | `XmlPull xml_next(p, &ev)`: fills an `XmlEvent`; typed `XE_COMMENT` / `DECLARATION` / `INSTRUCTION` / `CDATA`; `XE_ERROR` is an event, then end |
| `xread.c` (push) | `xr_callback(type, xr_str_t* name, xr_str_t* value, user)` | `XmlEventHandler(user, const XmlEvent*)`; kept as a tokenizer or deleted if `xml.c` over a push `Reader` measures as fast (Q5) |
| `xpump.c` | `xread_pump`, `xread_pump_finish`, `xml_pump` | `xevent_pump(xml_source(parser), sink)`; push pumps are one `xr_read` call with `xevent_send` as handler |
| `xwalk.c` | element-tree walk through `WalkInterface` | also `xwalk_event(&src, &ev)`: the tag / attributes / children interpretation of `xtreewrite.c` lives here |
| `xtreewrite.c`, `xlayout.c` | event interpretation + legacy layout | a pump into `xwrite` once X1 is decided (`XW_PRETTY`), otherwise only the layout stays |
| `xbuilder.c` | `XmlBuilder` (`XB_PLAIN`, `XB_FLAT`) | `XMLNodeParser` runs on `XB_FLAT`; builds from typed markup events |
| `xwrite.c` | `XmlWriter`, `xwrite_sink` | unchanged |
| `xsink.c` | C sink to JS object | adds JS object to C sink and the handshake `Symbol.for('qjs.xmlsink')` |
| serializer (in `quickjs-xml.c`) | `XMLTreeSerializer` with replay (`skip`, `delivered`, `capped`) | one struct: pull source + `XmlWriter` + `DynBuf` + demand; the replay code is deleted |

Lines expected to leave `quickjs-xml.c` (2156 now, target about 1200 in `TODO.md` section 6, about 1000 with this plan): `read` / `write` (P9), the
`XMLNodeParser` assembly and the four option-parsing copies (P7, P8), the replay serializer (P5), the callback glue of `XMLPushParser` (P3).
Rules for every step: `test-xml.js` and `test-xml-write-golden.js` stay green apart from the changes listed in `TODO.md`; the `read()` differential
against the pre-refactor parser stays at 0 on well-formed input; push output is the same at every chunk size.

### 7.1 End state (plan 2026-10-18)

After the binding is lifted `quickjs-xml.c` is the list of classes (about 1000 lines): `XMLParser`, `XMLPushParser`, `XmlBuilder`, `XmlWalker`,
`XMLWriter`, `XMLTreeSerializer`, `tee`. The rest goes to `src/xml/`: error shaping (`xml_error_new`, `xml_throw_at`,
`xml_pushparser_report`, `xml_nodeparser_record_error`), the four copies of option parsing (`xopts.c`), event to JS value and JS-function
handlers (`xevent-js.c`), handshake and `js_xml_pump` (`xconnect.c`), the serializer's `read()` (`xserial-js.c`), `location_from_srcpos`,
`reader_from_js_input`. Deleted: `js_xml_parse` and `XRFrame` (`read()` becomes `lib/xml.js`), `XMLNodeParser` assembly, the replay
serializer (`XMLCappedBuf`, `xml_write_capped`, `xml_write_skip`), the old entry points of `xml.h` / `xread.h` (private),
`xread.c` (442 lines, if `xml.c` over a push `Reader` is as fast), and with X1 `xtreewrite.[ch]` and `xlayout.[ch]` (about 560 lines).
Details, counts and gates: `api-event-model.md` 12; state: [event-model-state.md](event-model-state.md).
