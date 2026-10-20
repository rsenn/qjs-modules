# Plan: the `json` interface against Browser, Bun, Node and Deno (2026-10-17)

> **Status 2026-10-18 (implemented).** The event model is in: every class is a source or sink over `JsonEvent` / `XmlEvent`, `pump()`/`connect()`/`tee`/`JsonBuilder`/`XmlBuilder` exist, `parse()` and `XMLNodeParser` are removed (`next()` and the iterator replace them), `xml.write()` is `xwalk` pumped into an `XmlWriter` (`XW_PRETTY`, indent 2), `quickjs-json.c` is 933 and `quickjs-xml.c` 967 lines (2026-10-18: `json.c` is gone, `JsonParser` is a queue over `jread`; the push parser, the serializer and the pump moved into `src/json/`).
> Running state and what is left: [event-model-state.md](event-model-state.md). Text below that describes `parse()`, `XMLNodeParser`,
> `eventName`/`eventValue`, `PARSE_*`, `xtreewrite` or `xlayout` is the plan or the history, those are gone.

Goal: the JS surface of `import ... from 'json'` follows the standards in this order: WHATWG > Browser > Bun > Node >
Deno (`CLAUDE.md`), adds no custom public API, and keeps the classes nobody else offers as engines under that surface.
Findings are in [api-compare-json.md](api-compare-json.md), the code split in
[api-implement-json.md](api-implement-json.md); the step list that orders all three is `TODO.md` Tier 18. Sections 4 to 10
below are the reference: the C contracts between the pieces and the JS contract of each class.

**Layering (plan 2026-10-18, [api-event-model.md](api-event-model.md)).** The `json` native module exports **engines only**: pull
and push parsers, builder, walker, writer, serializer, sink adapters. Every API of the table below that is a dialect or a runtime
shape (`JSONL`, `JSON5`, `JSONC`, `read`, `write`, `Blob.prototype.json()`, the `@std/json` streams) is a thin composition of those
engines in JS under `lib/` (`lib/jsonl.js`, `lib/json5.js`, `lib/jsonc.js`, `lib/bun.js`, `lib/jsonStreams.js`); the "Action" column
says what the API does, the new column "Lives in" says where. Sections 5 to 8 describe the C contracts as they are; where the event
model changes them (the pull parser writes a `JsonEvent`, the push parser calls a `JsonEvent` handler, the walker is a pull
source, the serializer is demand-driven) the change is marked "plan 2026-10-18" and specified in `api-event-model.md` 3 to 5.

## 1. Target interface

| Export | Target | Standard it follows | Action | Lives in |
| --- | --- | --- | --- | --- |
| `JSONL.parse`, `JSONL.parseChunk` | as now | `Bun.JSONL` | keep the API, move the code | `lib/jsonl.js`: `JsonParser` dialect `jsonl` pumped into a `JsonBuilder`, roots taken from its queue |
| `Blob.prototype.json()` (module `blob`) | as now, plus 100000-level nesting | Bun / Body mixin | keep; close the depth gap with the strict reader | the blob glue: strict `JsonParser` into a `JsonBuilder` |
| `JSON5.parse(text)`, `JSON5.stringify(v, replacer, space)` | new | `Bun.JSON5` (npm `json5` behind it) | add | `lib/json5.js`: parse = dialect `json5` + builder; stringify = walker pumped into a `JsonWriter` with `bareKeys`, `singleQuotes` |
| `JSONC.parse(text)` | new | `Bun.JSONC` | add | `lib/jsonc.js`: dialect `jsonc` + builder |
| `JsonPushParser` | gains `onValue`, `paths`, `end()`, `connect(sink)` | `@streamparser/json` `JSONParser` | extend | engine (C) |
| `JsonParseStream`, `ConcatenatedJsonParseStream`, `JsonStringifyStream` | as now | `jsr:@std/json` | keep | `lib/jsonStreams.js` on `JsonPushParser` + `JsonSerializer` |
| `read(text)` | none: use `JSON.parse` | `JSON.parse` | fix escapes now, deprecate, remove | wrapper in the glue until removed |
| `write(v, replacer, space)` | none: use `JSON.stringify` | `JSON.stringify` | deprecate, remove | wrapper in the glue until removed |
| `JsonParser`, `JsonSerializer`, `JsonWriter`, `TYPE_*` constants | unchanged; plus `JsonBuilder`, `JsonWalker`, `tee`, `source.pump(sink)` | none exists | keep as engines; not part of the portable surface | C (`quickjs-json.c`) |

## 2. Decisions

| # | Decision | Reason |
| --- | --- | --- |
| D1 | no `JSONL.stringify`, no `JSONC.stringify` | no runtime has them |
| D2 | `JSON5.parse` / `JSONC.parse` take one argument, no reviver; trailing data is a `SyntaxError` | Bun's `length` and behaviour |
| D3 | everything that parses for Bun parity builds ordinary-prototype objects; null-prototype stays only inside internal readers | `JSON.parse` results have `Object.prototype` |
| D4 | error class is the contract (`SyntaxError`, `TypeError`), message texts follow the engine | Bun's texts are not stable either |
| D5 | no Node streams (`stream-json`'s API is not copied) | `CLAUDE.md`; the WHATWG forms of `@std/json` are the target |
| D6 | `read`'s string-escape defect is fixed before it is deprecated | it is wrong today and callers exist (`plot-cv/yt-playlist.js`, `plot-cv/clang-ast.js`) |
| D7 | every parser throws `SyntaxError` with an own `loc` (a `Location`), `length` and `code`; section 11 | no runtime offers a correct structured position (`api-compare-json.md` 7); one property name, `loc`, on the standard class |
| D8 | `TypeError` for bad argument types (not `ReferenceError`); `InternalError` never for input | `read(5)` throws `ReferenceError`, `read('{')` an `InternalError` today |
| D9 | decided 2026-10-18: no dialect or runtime API is C. `JSONL`, `JSON5`, `JSONC`, `read`, `write` are compositions of engine classes in `lib/` | one set of engines serves Browser, Bun, Node and Deno shapes; the engines stay replaceable by a pure-JS port |
| D10 | decided 2026-10-18: every engine class is a source or a sink, pull or push; a source reaches a sink in C through `pump(sink)` / `connect(sink)` | `api-event-model.md` 1 and 2 |
| D11 | the dialect is a name (`'json'`, `'jsonc'`, `'json5'`, `'jsonl'`) given to the parser; the `JR_*` flags behind it are an implementation detail | one entry point per role, no `parseJson5` family |

## 3. Steps

Ordered so a refactor and a behaviour change never share a step; numbering is that of `TODO.md` Tier 18.

| Step | Interface change | Needs (see `api-implement-json.md` 4) |
| --- | --- | --- |
| 0 | `read()` decodes escapes; throws `SyntaxError` (not `InternalError`) and `TypeError` for a bad argument | a fix in `quickjs-json.c` |
| E1 | all JSON parse errors carry `loc`, `length`, `code`; start-of-token columns | `js_throw_syntax_error()` (`quickjs-location.c`), position counting in `jread` and `json` (section 11) |
| E2 | `JsonPushParser` `onError`; `JSONL.parseChunk().error` and `Blob.json()` rejections carry `loc` | E1 |
| 1 | none visible | `jwrite_options_indent*`, `walk_path`, UTF-16 offset helpers |
| 2 | none visible | `jread` strict + dialects; `jbuilder` `proto`, `jbuild_value_js` |
| 3 | `JSON5.parse`, `JSONC.parse` appear; `Blob.json()` handles deep nesting | step 2 |
| 4 | `read()`, `write()` deprecated, then removed | callers migrated |
| 5 | `JSON5.stringify`; `JsonPushParser.onValue`, `paths`, `end()` | step 2 |

The event-model steps P1 to P10 (`api-event-model.md` 10, `TODO.md` "Event model v2") run alongside: P1 to P8 change the engines
(`json_next(p, &ev)`, push handler, walker as source, demand-driven serializer, handshake, builder queue, parser options), P9
moves `JSONL`, `JSON5`, `JSONC`, `read`, `write` into `lib/`. Steps 3 and 5 above are then done in the glue; step 2 (`jread` strict
and dialects) is P8's input.

## 4. Reference: the event protocol

One vocabulary connects reader, builder and writer: `JsonEventType` (`include/json/jevent.h`), in this
order.

| `JsonEventType` | Carries | `jwrite` call | `jbuild` call | Walk event |
| --- | --- | --- | --- | --- |
| `JSON_EVENT_ERROR` | message | `jwrite_error(str, len)` | none | none (a walk throws) |
| `JSON_EVENT_NULL` | | `jwrite_null` | `jbuild_value` | `value()` |
| `JSON_EVENT_TRUE`, `JSON_EVENT_FALSE` | | `jwrite_true`, `jwrite_false` | `jbuild_value` | `value()` |
| `JSON_EVENT_NUMBER` | text | `jwrite_number(str, len)` | `jbuild_value` (`scan_double`) | `value()` |
| `JSON_EVENT_STRING` | decoded UTF-8 | `jwrite_string(str, len)` | `jbuild_value` | `value()` |
| `JSON_EVENT_ARRAY_START`, `_end` | | `jwrite_array_start`, `_end` | `jbuild_push`, `jbuild_pop` | `array_start`, `array_end` |
| `JSON_EVENT_OBJECT_START`, `_end` | | `jwrite_object_start`, `_end` | `jbuild_push`, `jbuild_pop` | `object_start`, `object_end` |
| `JSON_EVENT_KEY` | decoded UTF-8 | `jwrite_key(str, len)` | `jbuild_key` | `key()` |

Strings and keys arrive decoded; `jwrite` re-escapes them. Numbers travel as text so `1e400` and
`12345678901234567890` survive a parse-write chain untouched.

## 5. Reading

### `jr_read` / `jr_finish` (push, `jread.h`)

```c
typedef void (*jr_callback)(JsonEventType type, const jr_str_t* data, void* user_data);
void jr_state_init(jr_state_t*);   void jr_state_free(jr_state_t*);
void jr_read(jr_callback cb, const char* chunk, size_t len, void* user_data, jr_state_t*);
void jr_finish(jr_callback cb, void* user_data, jr_state_t*);
```

| Contract | |
| --- | --- |
| chunking | a chunk may end anywhere (mid-string, mid-escape, mid-number, mid-literal); the state resumes |
| `data` | valid only inside the callback; copied from an internal accumulator |
| errors | `JSON_EVENT_ERROR` once per bad span, then resync at the next comma or closing bracket; `state->just_erred` says whether this call hit one, `state->error` is sticky |
| numbers | a bare number has no terminator, so only `jr_finish()` closes a trailing one |
| engine | none; `Vector` for the nesting stack (allocator set by `jr_state_init`) |
| planned | strict mode (no comments, no resync, no trailing commas) and JSON5 / JSONC dialect flags |

### `json_parse` (pull, `json.h`)

```c
BOOL json_init(JsonParser*, Reader, const char* filename, JSContext*);
int  json_parse(JsonParser*);   /* a JsonEventType, JSON_NEED_DATA, or JSON_ERROR */
```

| Contract | |
| --- | --- |
| result | `JSON_EVENT_*` (`JsonEventType` of `json/jevent.h`, comment included), or `JSON_NEED_DATA` (reader dry, call again), or `JSON_ERROR` (`json->error`; jpump turns it into `JSON_EVENT_ERROR`) |
| payload | the current token text in `json->token` (a `DynBuf`), valid until the next call |
| position | `json->loc` (`Location`), `json->pos` |
| comments | skipped, or `JSON_EVENT_COMMENT` when `json->comments` is set |
| engine | `JSContext` only to allocate the `Location` |

Plan 2026-10-18 (`api-event-model.md` 3.1, 4): `json_next()` is added next to `json_parse()` (which stays the low-level step until the binding is off it): `JsonPull json_next(JsonParser*, JsonEvent* ev)`, with `JsonPull` =
`JSON_PULL_END | AGAIN | EVENT | FAIL`; the event carries type, token text and the start `SourcePos`, and an error is an event
(`JSON_EVENT_ERROR`), so `JSON_NEED_DATA`, `JSON_NONE` and `JSON_ERROR` disappear as return values. The push side becomes
`jr_feed(state, chunk, len, JsonEventHandler*, user)` with the same `JsonEvent`, and `jr_callback` / `jr_str_t` go. After that the pull and push
parsers use one event type, one text rule and one position rule, and `jread_pump` / `json_pump` are the 3-line loop.

## 6. Building: `JsonBuilder` (`jbuilder.h`)

```c
void    jbuild_init(JsonBuilder*, JSContext*);
void    jbuild_push(JsonBuilder*, JsonEventType type);        /* array_start or object_start */
void    jbuild_key(JsonBuilder*, const char* name, size_t len);
void    jbuild_value(JsonBuilder*, JsonEventType type, const char* data, size_t len);
void    jbuild_pop(JsonBuilder*);
JSValue jbuild_path(JsonBuilder*);   JSValue jbuild_root(JsonBuilder*);
void    jbuild_free(JsonBuilder*, JSRuntime*);
```

| Contract | |
| --- | --- |
| attach | a container is attached to its parent when pushed, so `jbuild_root()` is always the partial tree |
| refcount | `jbuild_root()` and `jbuild_path()` return new references; the builder keeps its own |
| objects | null-prototype today (`JS_NewObjectProto(ctx, JS_NULL)`) |
| errors | none returned; allocation failure leaves the event dropped (the caller checks `JS_HasException`) |
| planned | `proto` option (ordinary objects); `jbuild_value_js(b, JSValue)` taking ownership |

## 7. Walking: `WalkInterface` and `WalkHooks` (`walk.h`, `jwalk.h`)

```c
typedef struct {
  void* opaque;
  ssize_t (*key)(void*, JSContext*, JSValueConst);
  ssize_t (*object_start)(void*, JSContext*);   ssize_t (*array_start)(void*, JSContext*);
  ssize_t (*object_end)(void*, JSContext*);     ssize_t (*array_end)(void*, JSContext*);
  ssize_t (*value)(void*, JSContext*, JSValueConst);
  ssize_t (*raw)(void*, JSContext*, JSValueConst);   /* optional: JSON.rawJSON() object */
} WalkInterface;
```

Each callback returns bytes written (>= 0) or -1 with an exception pending. `walk_next()` returns the event
(`WALK_EVENT_KEY` ... `WALK_EVENT_VALUE`, > 0), 0 when done, -1 on error. `WalkHooks`:

| Hook | Question it answers | `jwalk` answer |
| --- | --- | --- |
| `prepare(holder, key, &val)` | value, container, raw, or omit? | `toJSON`, replacer, boxed primitives, `NaN` to null, bigint throws, `undefined`/function/symbol omitted |
| `keys(top, array)` | which keys of an object, in which order? | the array replacer's allow list |
| `circular` | message of the circular `TypeError` | `"Converting circular structure to JSON"` |

`JwalkIterator` embeds a `WalkIterator` and must not move once initialised (the hooks point back at it).
`js_walk_register` / `js_walk_define` / `js_walk_wrap` expose a `WalkInterface` to JS (`key`, `objectStart`, `arrayStart`,
`objectEnd`, `arrayEnd`, `value`), which is how a producer drives a `JsonWriter` without knowing its class.

Plan 2026-10-18 (`api-event-model.md` 5): `jwalk` becomes a **pull source** of `JsonEvent` (`jwalk_event(&src, &ev)`; strings as CESU-8, numbers
as their `JSON.stringify` text, `NaN` / `Infinity` as null, `JSON.rawJSON` through `JsonEventSink.raw`). `WalkInterface` stays as the engine-side
capture inside the iterator, not something a writer implements; `js_walk_*` is replaced by the sink methods of `js_jsonsink_define`.
A walker pumped into a `JsonWriter` is `write()` / `JSON5.stringify`; a walker behind a `JsonSerializer` is the demand-driven stringifier.

## 8. Writing: `JsonWriter` and `JsonWriteInterface` (`jwrite.h`)

Every call returns the bytes written (>= 0) or `-JWRITE_E_*`; `jwrite_error_message(code)` gives the text.

| Code | Meaning |
| --- | --- |
| `JWRITE_E_WRITE`, `_NOMEM` | the `Writer` refused bytes; the frame stack could not grow |
| `_EXPECTED_KEY`, `_KEY_AT_ROOT`, `_KEY_IN_ARRAY`, `_EXPECTED_VALUE`, `_PENDING_KEY` | key and value out of order |
| `_UNMATCHED_OBJECT/ARRAY`, `_NOT_ARRAY`, `_NOT_OBJECT` | end event without or against the open container |
| `_SOURCE` | `jwrite_error()` passed a reader error through |
| `_NUMBER` | number text empty or with a byte no number has |

`JsonWriteOptions`: `indent` (spaces, 0..), `indent_str` + `indent_len` (a string per level), `minify`, and the JSON5 trio
`bare_keys`, `single_quotes`, `hex_numbers`. `JsonWriteInterface` is the same events as function pointers
(`true_`/`false_` because `true`/`false` are macros), with `opaque` first.

## 9. The JS-facing contract

| Class / function | Constructor | Methods | Errors |
| --- | --- | --- | --- |
| `read(text)`, `write(v, replacer, space)` | n/a | n/a | `SyntaxError` / `TypeError` as `JSON.parse` / `JSON.stringify` (read: see `api-compare-json.md`) |
| `JSONL.parse`, `JSONL.parseChunk` | n/a | n/a | `TypeError` for a bad input, `SyntaxError` (parse) or `error` field (parseChunk) |
| `JsonParser(input, filename?)` | buffer, string or reader | `next()`, iterable, `parse()` | `error` string, `NEED_DATA` state |
| `JsonPushParser(callback?)` | optional callback function, options object, or a `JsonWriter` (events go straight to it) | `write(chunk)`, `close()`; `root`, `path`, `location` | `SyntaxError` on `close()` of a truncated stream |
| `JsonSerializer(value, replacer?, space?)` | | `read(n)`, `read(buffer, offset, length)`, `next()`; `finished`, `root`, `path` | `TypeError` as `JSON.stringify` |
| `JsonSerializer(source, options?)` (plan 2026-10-18) | a value, or any pull source (`JsonParser`) | the same; steps the source only as far as `read(n)` needs, never replays | the source's errors, as `pump()` |
| `JsonBuilder(options?)` (plan) | `{proto, flat?}` | sink methods (`objectStart`, `key`, `number`, ...), `root`, `path`, `take()` (completed roots with their end position), `[Symbol.for('qjs.jsonsink')]` | none |
| `source.pump(sink)`, `pushParser.connect(sink)`, `tee(...sinks)` (plan) | sink: a handshake object or any object with the sink methods | | the first error event as `SyntaxError`, or `onError` |
| `JsonWriter(output?, options?)` | buffer or writer; options a number (indent) or `{indent, bareKeys, singleQuotes, hexNumbers, minify}` | `objectStart`, `arrayStart`, `objectEnd`, `arrayEnd`, `key`, `value`; `written`, `depth` | `TypeError` for `JWRITE_E_*` ordering mistakes |

C helpers that cross into JS state their error contract in one of three fixed forms (`CLAUDE.md`):
"returns 0, or -1 with an exception pending", "returns NULL with errno set", "never throws".

## 10. Planned interface changes (Tier 18)

| Interface | Change | Why |
| --- | --- | --- |
| `jread` | strict and dialect flags | `JSON5.parse`, `JSONC.parse`, `Blob.json()` without recursion |
| `jbuilder` | `proto`, `jbuild_value_js` | ordinary-prototype results; shared with `xbuilder` |
| `jwrite` | `jwrite_options_indent*` | one `space` rule for `write`, `JSON5.stringify`, `JsonWriter` |
| `walk` | `walk_path()` | `JsonSerializer.path` without reaching into the stack |
| `JsonPushParser` | `onValue`, `paths`, `end()` | the `@streamparser/json` shape |

## 11. Errors and locations: the contract

### 11.1 Exceptions

| Situation | Class | Properties | Notes |
| --- | --- | --- | --- |
| text is not valid for the dialect | `SyntaxError` | `loc`, `length`, `code` | never `InternalError`; message ends with `at file:line:column` |
| argument of the wrong type (`read(5)`, `JSONL.parse({})`) | `TypeError` | | `ReferenceError` is wrong |
| value cannot be serialized (bigint, circular) | `TypeError` | | as `JSON.stringify` |
| writer called out of order (`JsonWriter`) | `TypeError` | `code` (`expected-key`, ...) | from `JWRITE_E_*` |
| `Reader` or user callback throws | that exception, unchanged | | never wrapped, never given a `loc` |
| `Writer` refuses bytes | `Error` with `code: 'write-failed'` | | not `InternalError` |
| several errors collected (`errors` option, `JSON5`/`JSONC` tolerant use) | `AggregateError` | `errors`: the `SyntaxError`s | Bun's `Bun.build` shape |
| streaming parser with `onError` | none: `onError(err)` per error, parsing resyncs | `err` as the first row | `@streamparser/json` / `sax` / `parse5` style |
| promise API (`Blob.json()`) | rejection, not a throw, with the first row's `SyntaxError` | | a wrong `this` still throws `TypeError` synchronously |

`loc`, `length` and `code` are own, writable, configurable and **non-enumerable** (like `cause`), so `JSON.stringify(err)` and
`Object.keys(err)` stay as for any error.

### 11.2 `loc`

A `Location` (module `location`): `file`, `line`, `column`, `charOffset`, `byteOffset`; `String(loc)` is `file:line:column`.

| Rule | Value | Why |
| --- | --- | --- |
| which position | the **start** of the offending token (`length` characters long, 0 at end of input) | `JSON.parse` tools, `json5` and `jsonc-parser` agree; `JsonParser` reports the position after the token today (2:8 for 2:7) |
| base | line and column 1-based, offsets 0-based | `json5`, `xmldom`, `parse5` |
| line breaks | `\n`, `\r\n` as one, a lone `\r` | HTML and XML normalise them; `Location` counts `\n` only today |
| column unit | open decision: code points (today) or UTF-16 units | JS tooling and `JSONL.parseChunk().read` count UTF-16 units; recommendation: UTF-16 for `column` and `charOffset`, `byteOffset` stays UTF-8 |
| `file` | the `filename` argument; empty for text without a name (`Blob.json()`) | |
| independence | a clone made at throw time, not the live parser `Location` | the parser goes on mutating its own |

Aliases (`lineNumber`, `columnNumber`, `fileName`) are not added to the error; `Location` already accepts them on input.

### 11.3 Codes (JSON)

`unexpected-token`, `unexpected-eof`, `unexpected-comment`, `trailing-comma`, `trailing-data`, `invalid-escape`, `invalid-number`,
`invalid-unicode`, `unterminated-string`, `control-character`, `unquoted-key`, `duplicate-key` (strict dialects only),
`nesting-too-deep` (only when a `maxDepth` is set; the default is unlimited, as Bun's 100000 levels). The dialect (`json`, `json5`,
`jsonc`) decides which of them can occur.

### 11.4 Location tracking in the parsers

| Parser | Position today | Target |
| --- | --- | --- |
| `JsonParser` | live `.location`, end of token | `.location` at the start of the token just returned; errors carry it as `loc` |
| `JsonPushParser` | `.location` (counted per chunk by the binding) | `jread` counts offset, line and column itself, so the error callback sees the exact position inside a chunk |
| `read`, `JSON5/JSONC.parse`, `Blob.json()` | none | `jread` strict mode reports `loc` |
| `JSONL.parse/parseChunk` | `read` (units) | `error.loc` relative to the whole input, with `line` counting the newline-separated values |
| per value | none | optional `onValue` info carries `offset` (planned with `onValue`) |

C side: a small engine-free `SourcePos {offset, line, column}` with `srcpos_advance(pos, bytes, len)` shared by `jread`, `json`,
`xread`, `xml`; `Location` is filled from it at throw time (`api-implement-json.md` section 5).

