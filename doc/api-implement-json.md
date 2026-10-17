# JSON: how the implementation is split

`quickjs-json.c` should be a binding layer: classes, argument parsing, error mapping. The
parsing, walking, building and writing live in `include/` + `src/` pieces that know nothing of
JS classes, and most of them nothing of the engine. This document records where the code is
now, what each shared header could still take over, and what is left in the binding.
Companions: [api-compare-json.md](api-compare-json.md) (against other runtimes),
[api-interface-json.md](api-interface-json.md) (the interfaces between the pieces),
`TODO.md` Tier 18.

State 2026-10-17: `quickjs-json.c` is 1809 lines, `src/` pieces 3106 (`jread` 794, `json` 653,
`jwrite` 527, `walk` 531, `jwalk` 368, `jbuilder` 233). 71 tests in `test-json.js`; `write()`
equals `JSON.stringify` on 420 cases through both `write` and `JsonSerializer`.

## 1. The pieces

| Piece | Files | Lines | Engine | Role |
| --- | --- | --- | --- | --- |
| push tokenizer | `jread.[ch]` | 794 + 103 | none | chunks in, `jr_type_t` callbacks out; resumable mid-token; resyncs after an error |
| pull tokenizer | `json.[ch]` | 653 + 99 | `JSContext` only for a `Location` | `Reader` in, one `JsonValueType` per `json_parse()`; `JSON_NEED_DATA` when the reader is dry; optional comment tokens |
| builder | `jbuilder.[ch]` | 233 + 40 | `JSContext`, `JSValue` | `jr` events (push/pop/key/value) to a JS value; the partial tree is always reachable |
| generic walker | `walk.[ch]` | 531 + 122 | `JSContext`, `JSValue` | JS value to `WalkInterface` events, one per `walk_next()`; hooks decide what a property is |
| JSON walker | `jwalk.[ch]` | 368 + 81 | same | the `JSON.stringify` rules as `WalkHooks` on top of `walk` |
| writer | `jwrite.[ch]` | 527 + 159 | none | events to text through a `Writer`; indent, JSON5 options, `JsonWriteInterface` |
| stream glue | `stream-utils.[ch]` | 1732 + 126 | partly | `Reader` and `Writer`, from fd, buffer, `DynBuf`, JS function, stream, `ArrayBufferSink` |

Dataflow of each public class:

```
read(text)            sj.h tokenizer ----------------------------> JSValue            (to be replaced)
write(v, r, s)        jwalk (walk + hooks) --WalkInterface--> jwrite --Writer--> DynBuf --> string
JsonSerializer        jwalk --> jwrite --> DynBuf kept per step --> read(n) / read(buffer)
JsonWriter            JS calls --> jwrite --> Writer (fd, function, buffer, sink)
JsonPushParser        write(chunk) --> jread --jr_callback--> jbuilder (root) and/or JS callback
JsonParser            Reader --> json_parse() --> token / state / location
JSONL.parse           byte scanner -> JS_ParseJSON per value (engine)
Blob.prototype.json   JS_ParseJSON (engine)
```

## 2. What the binding still holds (`quickjs-json.c`, 1809 lines)

| Section | Lines | Content | Can leave? |
| --- | --- | --- | --- |
| `read()` | 185 | `ParseFrame`, `parse_val` over `sj.h`, trailing-data check, `js_json_read` | yes, see 3.1 |
| `write()` + space | 165 | `JsonSpace`, `json_space_parse/apply`, `json_replacer_space`, `js_json_write` | the pure part, see 3.3 |
| `JSONL` | 195 | value-end scanner, unit/byte offset maps, `js_jsonl_parse` | the scanner, see 3.2 |
| `JsonPushParser` | 330 | `jread` callback to JS, builder hook, getters, class | no: binding |
| `JsonSerializer` | 265 | step loop on `jwalk` + `jwrite`, `read()` into string or buffer | no: binding |
| `JsonWriter` | 325 | options, ten methods, `WalkInterface`/`JsonWriteInterface` bridges | no: binding |
| `JsonParser` | 240 | class over `json.c` | no: binding |
| init and exports | 86 | | no |

## 3. Lift candidates, header by header

The candidate set is every header of `include/` that either file could use. "Used" means a
call in `quickjs-json.c` today.

| Header | Used now | What more could move there | Verdict |
| --- | --- | --- | --- |
| `buffer-utils.h` | `InputBuffer` (`js_input_chars/args`), `DynBuf` helpers | nothing; `JSONL` and `read` already take `InputBuffer` | done |
| `char-utils.h` | `scan_double` (read, `jbuilder`) | `jsonl_unit_to_byte` and `jsonl_byte_to_unit` (35 lines) are UTF-16-unit/UTF-8 offset maps; `utf8_byteoffset` there counts code points, not units | **lift**: add `utf8_utf16_offset` / `utf16_utf8_offset` pair to `char-utils`, generic |
| `iteration.h` | no | it consumes JS iterables; `JsonParser` and `JsonSerializer` only produce iterators | not applicable |
| `jbuilder.h` | `JsonPushParser.root` | prototype option (ordinary objects, not null-prototype) and `jbuild_value_js()` for already-made values | **extend**, needed by `JSON5.parse`, `JSONC.parse`, `Blob.json()` (and by `xbuilder`) |
| `jread.h` | `JsonPushParser` | a strict mode (no comments, no resync, no trailing commas), non-recursive already; a JSON5 and a JSONC dialect flag | **extend**, replaces `sj.h` and gives Bun parity on depth |
| `json.h` | `JsonParser` | a second tokenizer next to `jread` (string escapes, numbers, literals written twice) | keep both; merging means a pull front over `jread`'s tokens, only worth it after the strict mode lands |
| `jwalk.h` / `walk.h` | `write`, `JsonSerializer` | an accessor for the container path: `JsonSerializer.path` reads `js->walk.walk.stack` directly | **lift**: `walk_path(const WalkIterator*, ctx)`, removes the reach into the stack |
| `jwrite.h` | `write`, `JsonWriter`, `JsonSerializer` | the pure half of `json_space_parse/apply` (number clamps to 0..10; string cut at 10 bytes on a character boundary; `minify` when empty), about 35 lines | **lift**: `jwrite_options_indent(opts, double)` and `jwrite_options_indent_string(opts, s, n)`; the JS argument shuffle (`json_replacer_space`) stays |
| `location.h` | `JsonParser`, `JsonPushParser` | nothing | done |
| `property-enumeration.h` | one call, `property_recursion_path` | nothing; `walk` owns the stack now | after `walk_path`: not used |
| `stream-utils.h` | `Reader`, `Writer` | nothing | done |
| `utils.h` | `js_get_propertystr_bool`, numeric converters | nothing | done |
| `vector.h` | stacks | nothing | done |
| `virtual-properties.h` | no | per-value locations as side tables (what `read()` of XML does); JSON has no consumer | not applicable |
| `xml.h`, `xread.h`, `xwalk.h`, `xwrite.h` | no | none | not applicable |
| `sj.h` (vendored, not in the list) | `read()` | gone with 3.1 | **remove**, with its `.c` |

### 3.1 `read()` off `sj.h`

`read()` is the one duplicate of `JSON.parse` and the least correct (`api-compare-json.md` 3). Replace its body by `jread`
strict + `jbuilder` with ordinary prototypes, or drop it for `JSON.parse` after the deprecation
(Tier 18 step 5). Either way `ParseFrame`, `parse_val`, `parse_primitive`, `parse_make_container`, `parse_throw`,
`js_json_parse` (about 185 lines) and the vendored `sj` go. Interim fix: decode escapes (`BUGS`).

### 3.2 `JSONL` scanner

`jsonl_is_ws` and `jsonl_value_end` (45 lines) are pure C. Move them to `jread` as `jr_value_end(buf, n, i)` or to a
`jsonl.c` beside it; the per-value `JS_ParseJSON` stays (it is exactly Bun's semantics, and recursion depth per line
is bounded by the line). With 3 above, `JSONL` shrinks from 195 to about 90 lines.

### 3.3 Options

`JsonWriteOptions` already carries `indent`, `indent_str`, `bare_keys`, `single_quotes`, `hex_numbers`, `minify`.
`JSON5.stringify` needs no new engine code, only the binding to set them; `jwrite_options_indent*` lets the
constructor of `JsonWriter` and `JSON5.stringify` share the `space` rule with `write()`.

## 4. Order and expected size

| Step | Change | `quickjs-json.c` after (lines, approx.) |
| --- | --- | --- |
| 1 | `jwrite_options_indent*`, `walk_path`, UTF-16 offset helpers | 1700 |
| 2 | `jread` strict + dialect flags, `jbuilder` prototype option and `jbuild_value_js` | 1700 (adds 40 in `src/`) |
| 3 | `JSON5.parse`, `JSONC.parse`, `Blob.json()` onto the strict reader | 1820 (adds the bindings) |
| 4 | `read()` deprecated, `sj` removed, `JSONL` scanner out | 1500 |
| 5 | `JSON5.stringify`; `JsonPushParser` `onValue`/`paths` | 1620 |

Rules while doing it: no new custom public API (the Bun and `@streamparser/json` shapes only); `test-json.js` stays green at every step; the
`write()` differential against `JSON.stringify` is the gate for anything touching `jwalk`/`jwrite`.

## 5. Error and location plumbing

### 5.1 Where errors are made today (`quickjs-json.c`)

| Sites | Class used | Problem |
| --- | --- | --- |
| `parse_throw` (`read`) | `InternalError`, `error: L:C: msg` | wrong class; file name dropped; no `loc` |
| `js_json_read` | `ReferenceError` `expecting buffer or string` | should be `TypeError` |
| `JsonParser` parse and getters (2 sites) | `SyntaxError` `file:L:C: msg` | position is after the token; only in the message |
| `JsonPushParser` write and close (3 sites) | `SyntaxError` `parse error`, `unexpected end of input` | no position although the binding keeps a `Location` |
| `JSONL` (3 sites), `parseChunk` error object | `SyntaxError`, `TypeError` | no position |
| `JsonWriter` result mapping | `TypeError`, `InternalError` for a failed write | `InternalError` is for engine bugs |
| `Blob.json()` (`quickjs-blob.c`) | rejection with the engine's `SyntaxError` | no position (`JS_ParseJSON` gives none) |

15 `JS_Throw*` sites in total; each moves to `js_throw_syntax_error` / `JS_ThrowTypeError`.

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

The pieces that tokenize must know where they are, because the binding only sees chunks:

| Piece | Add | Notes |
| --- | --- | --- |
| `include/srcpos.h` (header-only, no engine; **done**, `tests/c/test-srcpos.c`) | `SourcePos {byte_offset, char_offset, line, column, cr, utf16}`, `srcpos_init(pos, utf16)`, `srcpos_advance(pos, buf, len)` | counts `\n`, `\r\n` once, lone `\r`; UTF-8 continuation bytes do not advance the column; the `cr` bit carries a `\r` split across chunks |
| `jread` (`jr_state_t`) | `SourcePos pos`, `SourcePos tok_start`, `const char* err_code`, `uint32_t err_len` | the error callback keeps its signature; the binding reads `jr_error(const jr_state_t*)` right after it |
| `json.c` (`JsonParser`) | `err_code`, `err_start`, `err_len`; `loc` moves to the token start | fixes 2:8 to 2:7 |
| `Location` | `location_from_srcpos(Location*, const SourcePos*, file)` | `charOffset` and column in the unit decided in `api-interface-json.md` 11.2 |

### 5.4 Tests

`tests/unittests/test-json-errors.js`, table driven: for every parser (`read`, `JsonParser`, `JsonPushParser`, `JSONL`, `Blob.json`,
`JSON5/JSONC.parse`) and every input class (bad token, EOF inside a string, bad escape, trailing data, trailing comma, comment in strict
mode, CRLF and lone-CR line breaks, an astral character before the error): class, `code`, `loc.line/column/charOffset/byteOffset`, `length`.
Two properties on top: **chunk invariance** (the push parser fed 1 byte at a time reports the same `loc` and `code` as fed whole) and
**differential** `loc` against `json5` (`lineNumber`, `columnNumber`) on 200 generated bad inputs.

Size: `srcpos.h` about 60 lines, `js_syntax_error_new` about 50, per-parser changes about 15 lines each; the throw sites shrink.

