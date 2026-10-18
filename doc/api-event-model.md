# Event model: sources, sinks, pull and push; the C modules are engines, the APIs are glue (plan 2026-10-18)

> **Status 2026-10-18 (implemented).** The event model is in: every class is a source or sink over `JsonEvent` / `XmlEvent`, `pump()`/`connect()`/`tee`/`JsonBuilder`/`XmlBuilder` exist, `parse()` and `XMLNodeParser` are removed (`next()` and the iterator replace them), `xml.write()` is `xwalk` pumped into an `XmlWriter` (`XW_PRETTY`, indent 2), `quickjs-json.c` is 1412 and `quickjs-xml.c` 1339 lines.
> Running state and what is left: [event-model-state.md](event-model-state.md). Text below that describes `parse()`, `XMLNodeParser`,
> `eventName`/`eventValue`, `PARSE_*`, `xtreewrite` or `xlayout` is the plan or the history, those are gone.

Shared design of `json` and `xml`. [api-interface-json.md](api-interface-json.md) and [api-interface-xml.md](api-interface-xml.md)
refer here for everything about how parsers, builders, walkers, writers and serializers connect; the step list is `TODO.md`,
"Event model v2". The implemented parts are listed in the status note above.

## 1. The decision

| # | Decision | Reason |
| --- | --- | --- |
| M1 | every class of `quickjs-json.c` / `quickjs-xml.c` is a **source** or a **sink**, and **pull** or **push** | one vocabulary for every connection; any source reaches any sink of its family without JS in between |
| M2 | a source produces `JsonEvent` / `XmlEvent`; a sink consumes them as `JsonEventSink` / `XmlEventSink` | exists (`jevent.h`, `xevent.h`) |
| M3 | the pull parsers write the event they found into a caller's `JsonEvent` / `XmlEvent`: `json_next(p, &ev)`, `xml_next(p, &ev)` (`json_parse()` / `xml_parse()` stay as the low-level step) | a forwarding loop is 3 lines; the token text, the position and the type travel together |
| M4 | the push parsers hand the same `JsonEvent` / `XmlEvent` to a handler; a sink is a handler (`jevent_send`) | no second callback shape (`jr_callback`, `xr_callback`, `jr_str_t`, `xr_str_t` go) |
| M5 | the walkers are pull sources of the same events (`jwalk_event`, `xwalk_event`) | walker plus writer = serializer, walker plus builder = clone, with no `WalkInterface` adapter per writer |
| M6 | the native modules export engines only: parsers, builders, walkers, writers, serializers, sink adapters | `JSONL`, `JSON5`, `JSONC`, `read`, `write`, `DOMParser`, `XMLSerializer`, `XML.parse`, `Blob.json()` ... are **not** C |
| M7 | every dialect and runtime API is a thin composition in JS under `lib/` (`lib/bun.js`, `lib/dom.js`, `lib/jsonStreams.js`, ...) | the portable surface follows the standards in `CLAUDE.md` order; the engines stay replaceable |
| M8 | a dialect that needs a different tree shape (Bun's `XML.parse` with `'@name'`, `'#text'`) starts as a **JS sink** in the glue | thin first; a C builder mode only when measured slow |

## 2. The roles

|  | **source** (produces events) | **sink** (consumes events) |
| --- | --- | --- |
| **pull** (the client asks) | `JsonParser`, `XMLParser`, `XMLNodeParser` (over a `Reader`); the walkers `jwalk`, `xwalk` (over a JS value) | the **serializer**: a writer that runs its source only as far as the client has read bytes |
| **push** (the producer calls) | `JsonPushParser`, `XMLPushParser`: the client feeds chunks, events go to a connected sink | `JsonWriter`, `XMLWriter`, `JsonBuilder`, `XmlBuilder` (also flat), JS-object sinks, tee, rewriters (a sink that owns a sink) |

How they connect. "pump" is the C loop `while(next(source, &ev) == EVENT) send(sink, &ev)`.

| from \ to | push sink (writer, builder, JS sink, tee, rewriter) | pull sink (serializer) |
| --- | --- | --- |
| pull source | pump, one loop, no JS call per event | the serializer is exactly this pump, stopped when the demand is met |
| push source | direct: the parser holds the sink and calls it while `write(chunk)` runs | not offered: it would need a queue between the two; feed a push sink and read its buffer |

Everything else is composition of these cells:

```
JsonParser(Reader) ──pull──► pump ──► JsonBuilder              JSON.parse, JSON5.parse, JSONC.parse, JSONL.parse
JsonParser(Reader) ──pull──► pump ──► JsonWriter               reformat, minify, JSON5 to JSON
JsonPushParser ──push──► tee ──► JsonBuilder + JsonWriter      streams, onValue
JwalkIterator(value) ──pull──► pump ──► JsonWriter             JSON.stringify, JSON5.stringify
JwalkIterator(value) ──pull──► Serializer ──► read(n)          JsonStringifyStream, Response body
JsonParser ──pull──► Serializer ──► read(n)                    streaming reformat of a file
XMLParser(flat) ──pull──► pump ──► XmlBuilder(flat)            XMLNodeParser (a flat object list)
XMLParser ──pull──► pump ──► XmlBuilder                        xml.read(), DOMParser tree
xwalk(tree) ──pull──► pump ──► XmlWriter                       xml.write(), XMLSerializer
JS value ──walker──► JS sink (object with methods)             Bun.XML.stringify, custom shapes
```

## 3. The pull contract

### 3.1 JSON (`include/json/json.h`, `jevent.h`)

```c
typedef enum {
  JSON_PULL_FAIL = -1, /* an exception is pending; only engine-bound sources (walkers) */
  JSON_PULL_END = 0,   /* no more events */
  JSON_PULL_AGAIN = 1, /* the Reader has no data now; call again once it may */
  JSON_PULL_EVENT = 2, /* *ev is filled */
} JsonPull;

JsonPull json_next(JsonParser*, JsonEvent* ev);
JsonPull jwalk_event(JwalkSource*, JsonEvent* ev);   /* JwalkSource = a JwalkIterator + the capture of its last step */
```

| Rule | |
| --- | --- |
| errors are events | a malformed span is `ev->type == JSON_EVENT_ERROR`, `str` the message, `at` its start; the parser resyncs, the next call goes on (lenient) or returns `JSON_PULL_END` (strict dialects stop at the first one) |
| text | `str` / `len` of error, number, string, key, comment point into the parser, valid until the next call |
| position | `at` and `has_at = 1`, always set by the parsers: the start of the token (`SourcePos`), not the end |
| comments | `JSON_EVENT_COMMENT` only with the `comments` flag, otherwise skipped inside the parser (not by the pump) |
| status vs event | statuses are never event types, so the `-1` that means `JSON_NONE` in `json_parse()` today and `XE_ERROR` in `xml_parse()` can no longer be confused |
| engine | none for the parsers; the walkers hold `JSValue`s and may return `JSON_PULL_FAIL` (a `toJSON` that throws, a bigint) |
| numbering | `JSON_PULL_END` / `JSON_PULL_AGAIN` equal `JSON_PUMP_END` / `JSON_PUMP_AGAIN`, so the pump returns the status that stopped it |

### 3.2 XML (`include/xml/xml.h`, `xevent.h`)

```c
typedef enum { XML_PULL_FAIL = -1, XML_PULL_END = 0, XML_PULL_AGAIN = 1, XML_PULL_EVENT = 2 } XmlPull;

XmlPull xml_next(XMLParser*, XmlEvent* ev);
XmlPull xwalk_event(XwalkSource*, XmlEvent* ev);
```

| Rule | |
| --- | --- |
| text | `name` / `nlen` and `value` / `vlen` point into the parser's `ev_name` / `ev_value`, valid until the next call |
| one text slot | `XmlEvent` gets anonymous-union aliases `str` / `len` for `name` / `nlen`: text, comment, declaration, instruction, cdata and error carry their text in `str` / `len` as `JsonEvent` does; `XE_ATTRIBUTE` is the only event with `value` (`NULL`: a bare attribute) |
| strict error | `XE_ERROR` event, then `XML_PULL_END` (a mismatched end tag is terminal); lenient mode never produces it, repairs are reported through `onParseError` (`api-interface-xml.md` 11) |
| typed markup | the parsers produce `XE_COMMENT`, `XE_DECLARATION`, `XE_INSTRUCTION`, `XE_CDATA` instead of elements with magic names (`!--`, `!`, `?`); the builder maps them back for `read()` compatibility |
| position | `at` from `tag_line` / `tag_column` (the `<` of the tag, a closing tag's own `<` for the ends it causes) |
| modes | `tolerant`, `flat`, `no_implied_end`, `self_closing_tags` stay parser options; `flat` makes `XMLNodeParser` a plain `XMLParser` feeding a flat builder |

### 3.3 The loop

```c
ssize_t
jevent_pump(JsonEventSource src, const JsonEventSink* sink) {
  JsonEvent ev;
  JsonPull st;
  ssize_t r;

  while((st = src.next(src.opaque, &ev)) == JSON_PULL_EVENT)
    if((r = jevent_send(sink, &ev)) < 0)
      return r;
  return st;
}
```

`json_pump()` and `xml_pump()` are this (`json_source()`, `xml_source()`, `jwalk_source()`, `xwalk_source()` make the `JsonEventSource` / `XmlEventSource`). A client that wants to look at the events
(skip, count, branch) writes the loop itself, the contract is the same.

## 4. The push contract

```c
typedef ssize_t JsonEventHandler(void* user, const JsonEvent*);   /* jevent_send(sink, ev) has this shape, user = sink */

ssize_t jr_feed(jr_state_t*, const char* chunk, size_t len, JsonEventHandler*, void* user);
ssize_t jr_end(jr_state_t*, JsonEventHandler*, void* user);
```

| Rule | |
| --- | --- |
| handler | gets a `JsonEvent`, the same struct a pull parser fills; `has_at` set, text valid inside the call |
| result | the first negative handler result stops the parse and is returned; 0 otherwise |
| chunking | unchanged: output is the same at every chunk size, tokens may span chunks |
| to a sink | `jr_feed(st, chunk, n, jevent_handler, &sink)`; `jread_pump` / `xread_pump` shrink to that call and then go |
| `XMLPushParser` | the same for `XmlEvent`; gains `tolerant`, implied ends, flat and locations by running `xml.c` over a push `Reader` (Phase 6 of `TODO.md` Tier 20), or `xread` stays a tokenizer feeding the same handler |

The JS-callback mode of `JsonPushParser` / `XMLPushParser` (`onValue`, `onError`, the raw `(type, ...)` function) is a **handler that calls
a JS function**: the JS-sink adapter of section 6, not a third mechanism.

## 5. Walkers, builders, writers, serializers

### Walkers are pull sources

`WalkIterator` keeps the JS-value stack, skip and the circularity check. `jwalk` and `xwalk` gain an internal capturing `WalkInterface` that stores the
event it was just handed, and `*_event()` turns it into a `JsonEvent` / `XmlEvent`:

| walker | JS value | event |
| --- | --- | --- |
| `jwalk` | string, key | `str` / `len` as CESU-8 (`JS_ToCStringLen2(..., 1)`), so `jwrite` pairs or escapes surrogates; owned by the iterator until the next call |
| `jwalk` | finite number | `JSON_EVENT_NUMBER`, text = the shortest round-trip form (what `JSON.stringify` writes); `NaN`, `Infinity` -> `JSON_EVENT_NULL` |
| `jwalk` | `JSON.rawJSON` | `JSON_EVENT_NUMBER` with the raw text when it is a number, else a `raw` pass-through (an optional `JsonEventSink.raw`, see M-Q3) |
| `xwalk` | `{tagName, attributes, children}` | `XE_ELEMENT_START`, `XE_ATTRIBUTE`*, `XE_TEXT` / nested starts, `XE_ELEMENT_END`; the interpretation now in `xtreewrite.c` moves here, `xtreewrite` becomes a pump into `xwrite` |
| `xwalk` | flat list `[{tagName}, 'text', {tagName: '/x'}]` | the same events; `{tagName: '/x'}` is `XE_ELEMENT_END` |

### Builders are push sinks

`JsonBuilder`, `XmlBuilder` (also `XB_FLAT`) are `JsonEventSink` / `XmlEventSink` already. Additions the dialects need:

| Addition | For |
| --- | --- |
| `proto` option: ordinary objects | every `*.parse` result (D3 in `api-interface-json.md`) |
| a queue of completed roots, each with the `SourcePos` where it ended; `take()` returns and clears it | `JSONL.parse` (every document), `JSONL.parseChunk` (`read` = end of the last complete root), `onValue` |
| builder error policy: an `JSON_EVENT_ERROR` is recorded, not thrown | the pump decides (3.1); the builder is never the one that throws |
| `jbuild_value_js()` | `Bun.XML` object shapes written as a JS sink can still push finished values into a C builder |

### Writers are push sinks

`JsonWriter` and `XMLWriter` keep their options (`indent`, `bare_keys`, `single_quotes`, `hex_numbers`, `minify`; `XmlWriteOptions`). Their JS methods are the
sink methods (`objectStart`, `key`, `number`, `elementStart`, `attribute`, ...) from `js_jsonsink_define` / `js_xmlsink_define`, so a JS script drives them by hand
or the pump drives them in C. `JsonWriter.value(v)` (a JS value) stays as sugar: a walker pumped into itself.

### Serializers are the pull sink

```c
typedef struct {
  JsonPullSource src;   /* a JsonParser or a JwalkIterator behind one function pointer */
  JsonWriter wr;        /* writes into out */
  DynBuf out;           /* unbounded: one step may write a lot, nothing is replayed */
  size_t pos;           /* bytes already served */
  JsonPull status;
} JsonSerializer;

/* read(n): while out holds fewer than n unserved bytes and status == EVENT, take one event from src, send it to wr. */
```

| Rule | |
| --- | --- |
| demand | `read(n)` steps the source one event at a time until `n` bytes are buffered or the source ends; a step is never re-run (its JS side effects, `toJSON` and getters, run once) |
| source | a value (walker made for it) **or** any pull source: `new JsonSerializer(parser)` reformats a file as far as it is read |
| boundary | `read(n)` may cut a UTF-8 character; the JS class offers `read(n)` in bytes and `readText(n)` on character boundaries (`BUGS` `xmlserializer-read-n-splits-utf8`) |
| XML | `XMLTreeSerializer` is the same struct over `XmlEvent`; the replay code (`skip`, `delivered`, `capped`) is deleted |
| streams | `pull(controller)` of a `ReadableStream` is `read(highWaterMark)`; that is all `JsonStringifyStream` and a `Response` body need |

## 6. The JS surface of the engines

### 6.1 Handshake and the connect methods

Each module `.so` has its own copy of the class ids (static core lib, `quickjs-native-bindings` skill section 8), so `json` cannot recognise an `xml` object by
class. Every engine class that can be a source or a sink carries a native method on its prototype that answers for its own class:

| Symbol | answered by | returns |
| --- | --- | --- |
| `Symbol.for('qjs.jsonsink')` / `Symbol.for('qjs.xmlsink')` | `JsonWriter`, `JsonBuilder`, tee, rewriter, `JsonPushParser`'s inner sink ... | the `JsonEventSink` (an address for the receiving module) |
| `Symbol.for('qjs.jsonsource')` / `Symbol.for('qjs.xmlsource')` | `JsonParser`, `XMLParser`, `XMLNodeParser`, walkers | the pull source |

| Method | On | Does |
| --- | --- | --- |
| `source.pump(sink)` | pull sources | runs the loop of 3.3 in C; sink is a handshake object or any object with the sink methods (then wrapped by the JS-sink adapter, one JS call per event); returns `END`, or `AGAIN` for a `Reader` that ran dry; throws the `SyntaxError` of the first error event unless `onError` is set (`api-interface-json.md` 11.1) |
| `pushParser.connect(sink)` | push sources | sets the sink that `write(chunk)` / `close()` drive; `pushParser.sink` reads it |
| `new JsonSerializer(source_or_value, options)` | pull sink | section 5 |
| `tee(a, b, ...)` | sinks | a sink that sends every event to each; the result is a sink |

`JsonParser.next()` / the iterator protocol stay as the **pull-by-hand** form and return event objects shaped like `JsonEvent`
(`{type, value, loc}`), so token ids are the `JsonEventType` / `XmlEventType` ones, not private numbers.

### 6.2 JS-object sinks (the other direction)

`js_jsonsink_wrap` / `js_xmlsink_wrap` (exist) turn a C sink into a JS object. The missing twin, a JS object turned into a C sink
(`jsonsink_from_js(ctx, obj)`): each member calls `obj.<method>`; used by `pump()` when the target has no handshake, and by the JS-callback mode of
the push parsers. A JS sink may return a negative number or throw: that stops the pump and the exception propagates unchanged (`api-interface-json.md` 11.1).

### 6.3 What the native modules export after the plan

| `json` | `xml` |
| --- | --- |
| `JsonParser` (options: dialect flags, `comments`, `filename`), `JsonPushParser`, `JsonBuilder`, `JsonWalker`, `JsonWriter`, `JsonSerializer`, `tee` | `XMLParser` (options: `tolerant`, `flat`, `selfClosingTags`, `strict`, `entities`, `location`), `XMLPushParser`, `XmlBuilder`, `XmlWalker`, `XMLWriter`, `XMLTreeSerializer`, `tee` |
| `TYPE_*` / event type constants | `XE_*` constants |
| during the migration only: `read`, `write`, `JSONL` (deprecated, then deleted) | during the migration only: `read`, `write` |

`XMLNodeParser` becomes `new XMLParser(input, {flat: true})` piped into `new XmlBuilder({flat: true})`; the class name stays as a one-line alias in the glue
until nothing imports it.

## 7. The glue in `lib/`

The glue imports engine classes and nothing from the layer above; each file is the portable surface of one runtime or standard. Every function below is
composition of the engines, no parsing code.

| File | Surface (standard) | Composition |
| --- | --- | --- |
| `lib/jsonl.js` (new; `bun.js` re-exports) | `JSONL.parse(text)`, `JSONL.parseChunk(chunk, start?, end?)` (Bun) | `JsonParser(text, {dialect: 'jsonl'})`, `pump` into `JsonBuilder({proto: true})`, `take()` the roots; `parseChunk`: `read` from the end of the last root, `error` from the first error event |
| `lib/json5.js` (new) | `JSON5.parse`, `JSON5.stringify` (Bun, npm `json5`) | parse: dialect `json5`, `JsonBuilder`; stringify: walker + `JsonWriter({bareKeys, singleQuotes, hexNumbers?})` through `pump` |
| `lib/jsonc.js` (new) | `JSONC.parse` (Bun, `@std/jsonc`) | dialect `jsonc`, `JsonBuilder` |
| `lib/bun.js` | `Bun.JSONL`, `Bun.JSON5`, `Bun.JSONC`, `Bun.XML` | re-exports of the three files above and `lib/xmlbun.js`; `Bun.XML` parse = `XMLParser({strict: true})` into a JS sink that builds `'@name'` / `'#text'` objects (M8) |
| `lib/jsonStreams.js` | `@std/json` `JsonParseStream`, `ConcatenatedJsonParseStream`, `JsonStringifyStream` (Deno, WHATWG streams) | `JsonPushParser` connected to a `JsonBuilder` with `onValue`; stringify = `JsonSerializer.read` in `pull()` |
| `lib/dom.js` | Browser `DOMParser`, `XMLSerializer`, `Document` | `XMLParser({tolerant: false})` into `XmlBuilder`, then the existing DOM constructors; serializing = `xwalk` pumped into `XMLWriter`; a strict failure becomes the `<parsererror>` document |
| `lib/blob-json.js` (or `blob.js`) | `Blob.prototype.json()` | `text()` then `JSON.parse`-equivalent through `JsonParser(strict)` for the 100000-level nesting |
| `JSON.parse`-compatible `read` / `write` | deprecated | one-line wrappers over the engines for the callers (`plot-cv/yt-playlist.js`, `clang-ast.js`), then removed |
| Node | nothing built in | an npm-shape adapter (`json5`, `jsonc-parser`, `@streamparser/json`, `sax`, `fast-xml-parser`) only when a script asks |

Rules for the glue:

- No engine class is subclassed to add a standard API: the standard API is a function or class in `lib/` that *uses* one.
- A dialect is a **name** passed to the parser (`'json'`, `'jsonc'`, `'json5'`, `'jsonl'`, and the `JR_*` flags behind them), never a parallel entry point.
- Error shaping (`SyntaxError` with `loc`, `length`, `code`; `AggregateError`; `onError`) is done once, in the engines' `pump()` / push-parser code
  (`api-interface-json.md` 11); the glue only chooses strict or collecting.
- A runtime that already has the standard (`Bun.JSON5`, `DOMParser`) is not shadowed: the glue installs only what is missing, as `lib/bun.js` does for `Bun.*`.

## 8. What moves out of the C modules

| Today in `quickjs-json.c` / `quickjs-xml.c` | After | Step |
| --- | --- | --- |
| `JSONL.parse`, `JSONL.parseChunk` | `lib/jsonl.js` | P9 |
| `read(text)`, `write(v, ...)` | deprecated wrappers in the glue, then deleted | P9 |
| `js_xml_parse` event loop of `read()` | `new XMLParser(...).pump(new XmlBuilder(...))` in a 5-line `xml.read` in `lib/xml.js` | P9 |
| `XMLNodeParser` assembly | `XMLParser({flat})` + flat `XmlBuilder` | P7 |
| the option parsing (4 copies in `quickjs-xml.c`) | one `xml_options_from_js()` shared by `XMLParser`, `XMLPushParser` | P8 |
| the pull parsers' private token ids (`JsonParser` numeric tokens, `XMLParser` `PARSE_*` statuses) | `JsonEventType` / `XmlEventType` + `JsonPull` / `XmlPull` | P2 |
| the push parsers' `(type, name, value)` callbacks | `JsonEvent` / `XmlEvent` handler | P3 |
| replay machinery of the serializers | demand-driven serializer on a pull source | P5 |

## 9. Open questions

Taken as decided in this plan; reverse by editing this table and the steps that name them.

| # | Question | Taken as |
| --- | --- | --- |
| Q1 | does `JsonEventSink` get a `comment` member (JSONC round trip, formatters)? | yes, `JsonEventSink.comment`; writers that cannot write comments drop them (as `xwrite` does for none) |
| Q2 | status enums or reuse of the negative numbers | enums of 3.1 / 3.2; no negative number means two things any more |
| Q3 | a `raw` member for `JSON.rawJSON` values and `JsonWriter.raw` | `JsonEventSink.raw(str, len)`: text already JSON; `jwrite_raw` exists |
| Q4 | `at` always filled by parsers | yes; the cost is a struct copy per event, measured in P2 against `jr_read` throughput |
| Q5 | `xread` kept as a second tokenizer | measured 2026-10-18: `xread` is 3.5x `xml.c` in `XR_COMPAT` mode (48 against 13 MB/s) with identical events (`c-xread-compat`), so `xread` is the one to keep; `xml.c` and `json.c` are NOT deleted yet, only after the adapter exists: `xml_next` stays as the pull API, implemented as a queue over `xr_feed` (events copied into the queue, a dry Reader gives `XML_PULL_AGAIN`) |
| Q6 | `Bun.XML` object shape in C or in the glue | glue first (M8) |
| Q7 | JSON `JsonParser.next()` iterator result shape | `{type, value, loc}` with `type` a `JsonEventType` name string or constant; decided at P2 with the tests |

## 10. Steps

`TODO.md`, "Event model v2". P1 to P10, each keeping ctest green; summary (P11 to P16, the end state after the bindings are lifted, are in section 12):

| # | Step | Gate |
| --- | --- | --- |
| P1 | events: `JsonEventSink.comment`, `.raw`; `XmlEvent` `str` / `len` aliases; `JsonPull` / `XmlPull`; `JsonEventHandler` / `XmlEventHandler` | `tests/c/events-sinks.c` |
| P2 | `json_next(p, &ev)`, `xml_next(p, &ev)`; `jevent_pump` / `xml_pump` are the loop; typed markup events (B4); the pull classes use them | `pump-roundtrip.c`, `test-json.js`, `test-xml.js` unchanged |
| P3 | push parsers deliver events to a handler; `jread_pump` / `xread_pump` removed | chunk sizes 1, 2, 7, 1000 |
| P4 | `jwalk_event`, `xwalk_event`; `xtreewrite.c` becomes a pump | `write()` golden byte-identical |
| P5 | demand-driven `JsonSerializer` / `XMLTreeSerializer` over any pull source | equality with `write()` at read sizes 1, 7, 4096 |
| P6 | handshake, `pump()`, `connect()`, JS-sink adapter, `tee` (EV8) | a push parser plugged into a writer, a builder, a tee |
| P7 | builder options: `proto`, root queue with end positions, flat `XmlBuilder` for `XMLNodeParser` | differential 0 |
| P8 | parser options: dialect flags in the `JsonParser` constructor, one `xml_options_from_js()`, strict mode | `test-json-dialects`, `test-xml-errors.js` |
| P9 | the glue of section 7; C `JSONL`, `read`, `write` deprecated and deleted | Bun / Browser / Deno differential batteries |
| P10 | clean-up: duplicated token ids, `jr_callback`, `xr_callback`, `property-enumeration.h` and `char-utils.h` out of the bindings | line counts in `api-implement-*.md` |

## 11. State 2026-10-18: `include/` and `src/` done, bindings not yet

Implemented in `include/{json,xml}/` and `src/{json,xml}/`, covered by `tests/c/event-model.c` (ctest `c-event-model`, clean under valgrind):

| Step | Done | Names |
| --- | --- | --- |
| P1 | events | `JsonEventSink.comment` / `.raw` (last members, positional initializers stay valid), `JSON_EVENT_RAW`; `XmlEvent` `str` / `len` unions; `JsonPull` / `XmlPull`; `JsonEventHandler` / `XmlEventHandler` with `jevent_handler()` / `xevent_handler()`; `JsonEventSource` / `XmlEventSource`; `JsonTee` / `XmlTee` (`jtee_sink`, `xtee_sink`) |
| P2 | pull parsers | `json_next()`, `xml_next()` with `json_source()`, `xml_source()`; `xml_next()` types comments, declarations, CDATA and processing instructions (the instruction ends at the first non-attribute event, because `xml_parse()` never ends it: `XMLParser.mk_replay`); `jevent_pump()`, `xevent_pump()`; `json_pump()` / `xml_pump()` are those; `XML_PUMP_ERROR` is no longer returned (an `XE_ERROR` event, then end) |
| P3 | push parsers | `jr_feed()` / `jr_end()`, `xr_feed()` / `xr_end()`; `jread_pump*` / `xread_pump*` are one call of them; `jr_callback` / `xr_callback` stay for the bindings |
| P4 | walkers | `JwalkSource` / `jwalk_event()` (CESU-8 text, number spelling, `JSON.rawJSON` as `JSON_EVENT_RAW`); `XwalkSource` / `xwalk_event()` (tree and flat lists, `XWALK_FLAT`, `XWALK_BALANCED`, `max_depth`, typed markup both ways); `xtreewrite.c` is **unchanged**: it still writes through `xlayout`, because moving `write()` onto `xwrite` is the open X1 decision |
| P5 | serials | `JsonSerial` / `jserial_read()`, `XmlSerial` / `xserial_read()` over any source; `chars` cuts on character boundaries; `JSERIAL_AGAIN` for a dry Reader |
| P6 (C part) | sinks from JS | `js_jsonsink_from_js()`, `js_xmlsink_from_js()` (a JS object as a C sink), `comment` and `raw` methods in `js_jsonsink_define()`; the handshake symbols and `pump()` / `connect()` are binding code |
| P7 | builder | `JsonBuilder.done`, `jbuild_take()`, `on_root` / `on_root_user`, `jbuild_value_js()`, `raw` member; `plain_objects` was there; `XmlBuilder` flat mode was there |
| P8 (part) | dialects | `jr_flags_from_name()` (`json`, `jsonc`, `json5`, `jsonl`, `legacy`) |

Behaviour worth knowing:

- A flat list with `XWALK_BALANCED` ends what a marker for an outer element leaves open (`[p, 'hi', br, 'x', /p]` gives `<p>hi<br>x</br></p>`); knowing void elements is the consumer's business.
- `max_depth` is counted as `xml.write()` counts it (two walk levels per element level); a cut element has no end event.
- `json_next()` reports `at` as the start of the token, line and column 1-based; `jr_feed()` takes it from `jr_state_t.tok`.

Left for the bindings and the glue (outside `include/` and `src/`): P2 to P5 in `quickjs-json.c` / `quickjs-xml.c` (classes read `JsonEvent` / `XmlEvent`, serializers use `JsonSerial` / `XmlSerial`, `JsonBuilder` options), P6 handshake and `pump()` / `connect()`, P8 constructor options, P9 the `lib/` glue, P10 clean-up. The old entry points (`json_parse`, `xml_parse`, `jr_read`, `xr_read`, `jr_callback`) stay until the bindings are off them.

## 12. After the bindings are lifted: what lives in `include/` and `src/`

Measured 2026-10-18 by grep of `quickjs-json.c` (1961 lines) and `quickjs-xml.c` (2158 lines) against `include/` and `src/` (`src/` and `include/` of json 6030, xml 5263, walk 778 lines).
Three kinds of change: code **moves in** from the bindings (engine-bound helpers that are not classes), code **becomes private or goes** (the old entry
points the bindings no longer call), and **duplicate engines merge**. Order: P11 and P12 first (they make the binding lift small), then the lift
itself, then P13 to P16. Every deletion has the same gate: `grep` shows no user outside its own file, `ctest` unchanged (the same eight known failures).

### 12.1 What moves in

The bindings keep only the classes: constructor, getters, finalizer, argument checks, the `JSCFunctionListEntry` tables. Everything that converts
between JS and events without being a class goes down, so the other module (`json` / `xml`) and later bindings can reuse it.

| New file (P12) | Content | Replaces in the bindings (functions, lines) |
| --- | --- | --- |
| `include/errinfo.h` + `src/js-syntax-error.c` | `js_syntax_error_from_event(ctx, const JsonEvent* / const XmlEvent*, filename, Location* base)`: the `SyntaxError` with own `loc`, `length`, `code` of `api-interface-json.md` 11; `js_syntax_error_new` / `js_throw_syntax_error` move here from `quickjs-location.c` | `json_error_new`, `json_throw`, `parse_throw`, `js_jsonparser_throw`, `js_jsonpushparser_throw`, `json_pushparser_error`, `xml_error_new`, `xml_throw_at`, `xml_nodeparser_record_error`, `xml_pushparser_report`: about 10 functions, 220 lines |
| `src/json/jopts.c`, `src/xml/xopts.c` | `json_options_from_js(ctx, obj, JsonOptions*)` (dialect name, `comments`, `filename`, `plain`), `jwrite_options_from_js` with the `space` rules (`json_space_parse`, `json_space_apply`, `json_replacer_space` become `jwrite_options_indent*`), `xml_options_from_js(ctx, obj, XmlOptions*)` (`tolerant`, `flat`, `selfClosingTags`, `strict`, `location`, `entities`), `xwrite_options_from_js` | the four copies of option parsing in `quickjs-xml.c`, `js_jsonwriter_options`, the `space` code: about 250 lines |
| `src/json/jevent-js.c`, `src/xml/xevent-js.c` | `js_jsonevent_new(ctx, const JsonEvent*)` and `js_xmlevent_new` (`{type, value, loc}` for `next()` and the iterator), `js_jsonevent_callback(ctx, fn)` and `js_xmlevent_callback`: a `JsonEventHandler` / `XmlEventHandler` that calls a JS function, the exported constant tables (`TYPE_*`, `XE_*`) | `json_pushparser_tojs`, `jread_callback`, `jread_callback_build`, `xread_callback`, `xread_callback_build`, `xml_callback`, `js_xml_parser_callback`: about 200 lines |
| `src/json/jconnect.c`, `src/xml/xconnect.c` | the handshake and the driver: `js_jsonsink_resolve(ctx, obj, JsonEventSink*, JsonJsSink**)` (handshake symbol, else the JS-object adapter), `js_jsonsource_resolve`, `js_json_pump(ctx, source, sink_obj, flags)` (throws the first error event, answers `END` / `AGAIN`), `js_json_connect` for the push parsers; `Symbol.for('qjs.jsonsink')` / `qjs.jsonsource` atoms and the pointer exchange | new, about 250 lines (no binding code to remove; P6) |
| `src/json/jserial-js.c`, `src/xml/xserial-js.c` | `js_jserial_read(ctx, JsonSerial*, argc, argv)`: `read(n)`, `read(buffer, offset, length)`, `readText(n)`, `JSERIAL_AGAIN` as a rejected or pending state | `js_jsonserializer_read`, `js_xmlserializer_read`, `json_serializer_step`, `json_serializer_close`, the `XMLCappedBuf` replay: about 260 lines |
| `src/location.c` | `location_from_srcpos(Location*, const SourcePos*)` | `xml_sync_location`, `xml_nodeparser_sync_location`, the JSON twin: about 40 lines |
| `src/stream-utils.c` | `reader_from_js_input(ctx, JSValueConst, Reader*)`: string, buffer, view or reader | `js_input_args` / `js_input_chars` / `reader_from_js` / `reader_from_bytes` at 7 sites in the XML binding |

Also moves in, as data not code: the `JsonEvent` and `XmlEvent` gain `code` (the stable kebab-case error name) and `span` (bytes the offending
token covers). Today a binding reads them from parser internals (`err_code` 10 sites, `err_len` 9, `tag_line` 3); with the fields on the event
the parsers' structs can be private (12.2) and push and pull errors look the same. This is P11 and is a header change only.

### 12.2 What becomes private or goes

"Private" means: out of the public header into `src/<lib>/<lib>-internal.h`, or `static`. Counts are the binding uses of 2026-10-18; every one is 0 when the binding lift is done.

| Symbol / file | Binding uses now | After | Step |
| --- | --- | --- | --- |
| `json_parse()` and `JSON_NEED_DATA` / `JSON_NONE` / `JSON_ERROR` / `JSON_RESYNC`, `json_end()`, `json_getc()`, `json_ungetc()` | 12, 2, 1, 2 | `static` in `json.c`, the statuses in `json-internal.h`; `json_next()` is the only entry | P13 |
| `JsonParser` fields `token`, `error`, `err_*`, `last_start`, `tok_start`, `pending`, `state`, ... | read by the binding | private struct; the binding holds `JsonParser*` from `json_new()` / `json_free()` (it does already) | P13 |
| `jr_read()`, `jr_finish()`, `jr_callback`, `jr_str_t` | 1, 1, 0, 3 | `static` in `jread.c`; `jr_feed()` / `jr_end()` are the push entry | P13 |
| `xml_parse()`, `XML_PARSE_*`, `event_name` / `event_value` / `event_has_value` / `xml_str_t` | 6, 21, 12, 10 | `static`; the buffers `ev_name` / `ev_value` are the event text; `xml_next()` the only entry; an `xml_new()` / `xml_free()` pair makes `XMLParser` opaque (it is embedded by value in 3 places now) | P13 |
| `xr_read()`, `xr_finish()`, `xr_callback`, `xr_str_t`, `jread_pump*`, `xread_pump*`, `json_pump()`, `xml_pump()` | 2, 3, 0, 2, 0 | the pump wrappers are deleted (one way to do it: `jr_feed` / `jevent_pump(json_source(p), sink)`); the rest `static` | P13 |
| `jbuild_push` / `jbuild_pop` / `jbuild_key` / `jbuild_value`, `xbuild_start` / `xbuild_attribute` / `xbuild_text` / `xbuild_end` / `xbuild_finish` | 0 for JSON; 5 for XML until `read()` and `XMLNodeParser` are on the sink | `static` in the builders; the public surface is `jbuild_init`, `jbuild_sink`, `jbuild_root`, `jbuild_take`, `jbuild_path`, `jbuild_value_js`, `jbuild_free` and the XML twins | P13 |
| `JsonWriteInterface`, `jwrite_interface()` | 0 (dropped in `9b8747dd`) | gone from `jwrite.h` | P13 |
| `js_walk_register` / `js_walk_define` / `js_walk_wrap` (`walk.c`) | 1, 1, 0 | deleted: `js_jsonsink_define` / `js_xmlsink_define` are the JS surface of a sink | P15 |
| `WalkInterface` (callbacks with `JSContext` and `JSValue`) | 5 (in the binding's own writers) | `walk_next()` returns the event kind and leaves the value in the iterator (`it->event`); the capture layer of `jwalkev.c` / `xwalkev.c` (about 110 lines) and `jwalk_init(..., iface)` lose their argument | P15 |
| `xtreewrite.c`, `xlayout.c`, `xwrite_tree()` | 1 | deleted once `write()` is `xwalk_source` pumped into an `XmlWriter` (X1 decided: output changes, golden regenerated): about 560 lines | P16 |
| `quickjs-json.c` vendored reader (`js_json_parse`, `parse_val`, `parse_primitive`, `parse_make_container`): lines 29 to 290 | | deleted with `read()` (P9) | P9 |

### 12.3 What merges (measure first)

Two duplicate engines exist per format; both directions of the merge are possible, so this is a measurement, not a decision.

| Pair | Why they exist | Merge | Gate |
| --- | --- | --- | --- |
| `jread.c` (push, 1546 lines) and `json.c` (pull, 1148 lines, deleted 2026-10-18, `json_next` is now a queue over `jread`) | `json.c` resumes on `JSON_NEED_DATA`, so it is a push parser over a chunk `Reader` that answers "dry"; `jread` over a pulled chunk plus a queue is a pull parser. Both carry the `JR_*` dialects and drift: `c-json-dialects` fails today on `[1,,2,]` with `JR_LEGACY` | keep the one that wins on throughput and the dialect matrix, express the other as a thin adapter, delete about 1200 lines | `c-json-dialects` green first (it is the equivalence test), then MB/s at chunk sizes 1, 64, 4096 |
| `xread.c` (push, 442 lines) and `xml.c` (pull, 908 lines) | `xml.c` is resumable (`XML_PARSE_AGAIN`), tolerant, flat, with implied ends and locations; `xread` has none of these (Q5) | `xread` got `XR_COMPAT` (everything `xml.c` does, equal events, 48 against 13 MB/s); keep `xml.c` for now, later it becomes an adapter over `xr_feed` | `c-xread-compat`, `test-xml.js`, chunk sizes 1, 2, 7, MB/s |

### 12.4 End state

| Layer | Files | Lines now | Lines after (estimate) |
| --- | --- | --- | --- |
| `quickjs-json.c` | classes only | 1961 | about 900 |
| `quickjs-xml.c` | classes only | 2158 | about 1000 |
| `src/json/` + `include/json/` | parser, events, builder, writer, walker, serial, JS adapters | 6030 | about 6400 after P12, P13, P15 (-400 legacy, +800 helpers), about 5200 if a tokenizer merges |
| `src/xml/` + `include/xml/` | the same | 5263 | about 4400 after P12, P13, P16 and the `xread` merge (-200 legacy, -570 `xread`, -670 `xtreewrite` / `xlayout`, +600 helpers) |
| `src/walk.c` + `include/walk.h` | the stepper | 778 | about 650 after P15 |

The rule that makes it stay this way: a function that does not need a `JSClassID` and does not belong to one class goes to `src/`; the binding file
is read as the list of classes the module exports.

### 12.5 Steps

| # | Step | Gate |
| --- | --- | --- |
| P11 (done) | `JsonEvent` / `XmlEvent` get `code` and `span`; `json_next`, `xml_next`, `jr_feed`, `xr_feed` fill them; `json_next_end()` gives a pull parse its end-of-input events | `tests/c/event-model.c` checks code and span of an error at chunk sizes 1 and whole |
| P12 | the files of 12.1, each with a test in `tests/c/event-model.c` or `tests/unittests/test-json-errors.js` / `test-xml-errors.js`; `location_from_srcpos`, `reader_from_js_input` | the bindings call them in the same change, line counts of `quickjs-*.c` drop as in 12.4 |
| (lift) | P2 to P6 and P8 on the binding side: classes read events, serializers on `JsonSerial` / `XmlSerial`, handshake, `pump()` / `connect()`, constructor options | `test-json.js`, `test-xml.js`, `test-xml-write-golden.js` unchanged |
| P13 | privatize and delete the entries of 12.2 (not the walk ones) | `grep` of each symbol outside its file is empty |
| P14 | the tokenizer measurements and merges of 12.3 | 12.3 gates |
| P15 | `walk_next()` with the event in the iterator; `WalkInterface` and `js_walk_*` deleted | `write()`, `JsonSerial`, `XwalkSource` byte-identical |
| P16 | X1: `write()` on `xwalk_source` + `XmlWriter` (`XW_PRETTY`); `xtreewrite`, `xlayout` deleted | `test-xml-write-golden.js` regenerated and reviewed |

The detailed state of what is done, file by file, is in [event-model-state.md](event-model-state.md).
