# json

Source: `quickjs-json.c` — module exports **`JSONL`**, **`JsonParser`**, **`JsonPushParser`**, **`JsonSerializer`**, **`JsonWriter`** and a function list.

A streaming/extended JSON reader plus simple read/write helpers.

**Comments:** `JsonParser` and `JsonPushParser` are tolerant of C++ (`// ...` to end of
line) and C (`/* ... */`) comments wherever whitespace is allowed, including between a
value and its comma, and across `write()` chunk boundaries. Comment markers inside strings
are plain text. A `/` that doesn't start a comment is a syntax error. `read()` does not
accept comments.

Set `parser.comments = true` on a `JsonParser` to get each comment back as a
`JsonParser.COMMENT` token (`parser.token` is the text including `//` or `/* */`) instead of
having it skipped. `JsonPushParser` always skips them.

## Module functions

| Function | Args | Description |
| --- | --- | --- |
| `read(input, inputName?)` | 1–2 | Parses JSON text into a JS value. `input` is a string or buffer. `inputName` is an optional filename for error messages. Throws on trailing data after the root value. |
| `write(value, indent?)` | 1–2 | Serializes a JS value to JSON text. `indent` (default 0) controls pretty-printing — when positive, each nesting level adds that many spaces of indentation. |

## JSONL

Bun's `JSONL` namespace: values separated by newlines (`\n` or `\r\n`, blank lines
allowed, a value may span lines). Same results as `Bun.JSONL` for every case in
`tests/unittests/test-json.js`.

| Function | Description |
| --- | --- |
| `JSONL.parse(input)` | `input` is a string or `Uint8Array`. Returns the array of values read; an incomplete trailing value is ignored; a bad value ends the list. Throws `SyntaxError` only when the first value is bad. |
| `JSONL.parseChunk(input, start?, end?)` | Never throws on bad data. Returns `{ values, read, done, error }`. `start`/`end` are chars for a string, bytes for a `Uint8Array`. |

`parseChunk` result:

| Field | Meaning |
| --- | --- |
| `values` | the values parsed |
| `read` | unit just after the last value (absolute, in the input's units); carry `input.slice(read)` into the next chunk |
| `done` | `true` when the rest of the range is only whitespace |
| `error` | `SyntaxError` for a bad value or for content after a value on the same line, else `null` |

```js
import { JSONL } from 'json';

JSONL.parse('{"a":1}\n[2]\n3');        // [{a: 1}, [2], 3]
JSONL.parseChunk('1\n2\n[3');          // { values: [1, 2], read: 3, done: false, error: null }
```

A value must end its line: `1 2` is an error, as is `{}{}`. A last token without a newline
(`12`) is a value; one that does not parse (`xx`, `1e`) is incomplete, not an error.

## JsonParser

An incremental JSON tokenizer, built on `Reader`/`Location` from `stream-utils.h`. Each call
to `.parse()` either advances one token (a state change), signals that the reader ran dry
before a token was complete (`"NEED_DATA"`), or throws a `SyntaxError` (with the offending
line:column) on malformed input.

```js
new JsonParser(input, filename?)   // length 1; filename is optional, reflected in .location.file
```

`input` may be:
- a buffer (string, `ArrayBuffer`, or typed array) holding the whole document, or
- a pull function `(buf, len) => bytesRead`, called as needed to fill `buf` (up to `len` bytes), or
- an object exposing such a function as its `read` method — called with the object as `this`, e.g. a file wrapper: `{ read(buf, len) { return f.read(buf, 0, len); } }`, or
- a file descriptor number (e.g. `f.fileno()`), read with `read(2)`; the parser does not close it.

The function/method/fd forms let the parser pull raw bytes on demand instead of requiring the whole document up front. Input is pulled in blocks of up to 16384 bytes (`len` is 16384; a read may return fewer bytes, and returning 0 yields `"NEED_DATA"`), so the parser can read past the end of the JSON document — don't share the source with another consumer.

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `parse()` | 0 | method | Advances one token. Returns one of `"NEED_DATA"`, `"NONE"`, `"OBJECT"`, `"OBJECT_END"`, `"ARRAY"`, `"ARRAY_END"`, `"KEY"`, `"STRING"`, `"TRUE"`, `"FALSE"`, `"NULL"`, `"NUMBER"`, plus `JsonParser.COMMENT` when `comments` is set. Throws on malformed input. |
| `pos` | — | getter | Current parse position, in characters consumed (enumerable). |
| `token` | — | getter | The current token's decoded text — e.g. string/key content has escapes and `\uXXXX` (including surrogate pairs) already resolved (enumerable). |
| `state` | — | getter | Internal parser state bitmask (enumerable). |
| `depth` | — | getter | Current nesting depth (enumerable). |
| `location` | — | getter | A `Location` reflecting the current input position (line/column/byte offset/filename); live, like `JsonPushParser`'s (enumerable). |
| `comments` | — | getter/setter | Boolean, default `false`. When `true`, `parse()` returns `JsonParser.COMMENT` for each `//` or `/* */` comment (text in `token`) instead of skipping it. |

```js
import { JsonParser } from 'json';

let p = new JsonParser('{"a":1,"b":[2,3]}');
let t;

while((t = p.parse()) !== 'NEED_DATA') console.log(t, JSON.stringify(p.token));
// OBJECT "{"  KEY "a"  NUMBER "1"  KEY "b"  ARRAY "["  NUMBER "2"  NUMBER "3"  ARRAY_END "]"  OBJECT_END "}"
```

`"NEED_DATA"` at the top level (after the root value is fully closed) simply means there's
nothing left to parse — this class has no `.write()` to feed it more, unlike `JsonPushParser`.

## JsonPushParser

A "push" JSON parser: instead of pulling from an input, data is fed to it via `.write()`,
at any byte boundary — including mid-string, mid-number, or mid-escape. It builds the
parsed value incrementally into `.root`, and while a container is still open, `.path`
reports the current nesting as an array of keys/indices (e.g. `["a", "b", 2]`).

```js
new JsonPushParser(callback?)   // length 0; optional callback function or options object
```

The optional argument may be:
- A single **callback function** `(type, value)` invoked for every token. `type` is a `jr_type_t` integer, `value` is the decoded JS value. The prototype's `TYPE_*` constants (see below) can be used to match against `type`.
- An **options object** with per-event callback methods. Recognized properties: `error`, `value` (for null/true/false/number/string), `objectStart`, `objectEnd`, `arrayStart`, `arrayEnd`, `key`. Each is invoked with a single decoded JS value argument. The options object is used as `this` for all callback invocations.

When no callbacks are given (or not all are present), the parser operates in **builder mode**: it builds the parsed value internally, retrievable via `.root`.

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `write(chunk)` | 1 | method | Feeds a chunk of input text (string or buffer). Throws a `SyntaxError` on malformed input, but the parser remains usable — it resyncs at the next structural boundary (a comma or a closing bracket) and subsequent `.write()` calls continue from there. |
| `close()` | 0 | method | Signals end of input: flushes a trailing top-level scalar (e.g. a bare `42` with nothing after it) that couldn't otherwise be told apart from "more digits might follow". Throws if the document is incomplete (unclosed container, mid-token, or nothing written yet). |
| `root` | — | getter | The value parsed so far. `undefined` until the top-level value is complete (enumerable). |
| `path` | — | getter | Array of keys/indices describing where the next byte will land while a container is still open; empty once parsing is done (enumerable). |

### Prototype constants

The following integer constants are exposed on the prototype for matching `type` values in callbacks:

| Constant | Description |
| --- | --- |
| `TYPE_ERROR` | Parse error |
| `TYPE_NULL` | `null` literal |
| `TYPE_TRUE` | `true` literal |
| `TYPE_FALSE` | `false` literal |
| `TYPE_NUMBER` | Numeric value |
| `TYPE_STRING` | String value |
| `TYPE_OBJECT` / `TYPE_OBJECT_START` | Object opening `{` |
| `TYPE_OBJECT_END` | Object closing `}` |
| `TYPE_ARRAY` / `TYPE_ARRAY_START` | Array opening `[` |
| `TYPE_ARRAY_END` | Array closing `]` |
| `TYPE_KEY` | Object key |

```js
import { JsonPushParser } from 'json';

let p = new JsonPushParser();

p.write('{"a":{"b":[1,2,');
console.log(p.path); // ["a", "b", 2]

p.write('3]}}');
console.log(p.root); // { a: { b: [1, 2, 3] } }
```

## Planned: streaming `jread` events into a `JsonBuilder` or a `JsonWriter`

> **Not yet implemented, unsure if it ever will be.** This section is a design sketch kept
> for reference. None of the names below exist; `JsonPushParser` today works as described
> above.

`jr_read(cb, chunk, len, user_data, state)` already reports every token through a callback.
The idea is two ready-made callbacks, so that the C parser can feed either sink directly,
chunk by chunk, with no JS value built in between.

### Sinks

| Sink | Callback | `user_data` | Notes |
| --- | --- | --- | --- |
| `JsonBuilder` (`jbuild.h`) | `jbuild_callback` | the `JsonBuilder*` | The existing builder-mode callback, moved out of `quickjs-json.c`. Needs no new logic. |
| `JsonWriter` (`jwrite.h`) | `jwrite_callback` | a `JsonWriteSink*` | New. Writes formatted JSON text as tokens arrive. |

```c
typedef struct { JsonWriter* wr; ssize_t error; } JsonWriteSink;   /* error: first -JWRITE_E_*, or 0 */

void jbuild_callback(jr_type_t, const jr_str_t*, void* user_data);
void jwrite_callback(jr_type_t, const jr_str_t*, void* user_data);
```

`jread.h` itself stays generic and gains nothing: the callbacks sit next to their sinks.

### The writer path

- **No JS values, no `JSContext`.** `jwrite.c` is pure C: key and value tokens go straight
  to `jwrite_key(wr, str, len)`, `jwrite_string(wr, str, len)` and `jwrite_raw(wr, str, len)`.
  They share the writer's ordering checks, commas and indentation, so the writer's options
  apply as usual (`indent`, `bareKeys`, `singleQuotes`, `minify`).
- **Number text passes through as written.** `12345678901234567890` and `1e2` are not
  re-formatted: the callback uses `jwrite_raw`. With `hexNumbers` set, it would parse the
  token and call `jwrite_int64` for a safe integer, and fall back to the raw text otherwise.
- **Errors.** A `jr_callback` returns `void`, so the first failure (a negative `jwrite_*`
  result, `-JWRITE_E_*`) is kept in the sink and later events are ignored. `write()` and
  `close()` then throw, using the message from `jwrite_error_message()`.

### `JsonPushParser` modes

| Mode | Chosen when | Callback | `user_data` |
| --- | --- | --- | --- |
| builder | default, as today | `jbuild_callback` | the parser's builder |
| writer | the constructor argument is a `JsonWriter` | `jwrite_callback` | the parser's `JsonWriteSink` |
| JS callbacks | a function or an options object, as today | `jread_callback` | the parser |

In writer mode the parser keeps a reference to the `JsonWriter` so it stays alive, and
`write()` and `close()` throw when the sink reports an error instead of swallowing it.
`root` is `undefined` and `path` is empty, since no tree is built.

```js
const w = new JsonWriter(out, { indent: 2 });
const p = new JsonPushParser(w);   // events go straight to the writer
p.write('{"a":[1,');
p.write('2]}');
p.close();                         // out now holds the reformatted document
```

### Relation to `WalkInterface`

`WalkInterface` (`walk.h`) is the generic sink shape used by the value walker. These two
callbacks are fast paths beside it that skip the `JSValue` round trip. A third adapter,
`jread` to `WalkInterface`, could serve arbitrary sinks later.

### Steps, if it is ever built

1. Move `jread_callback_build` to `jbuild.c` as `jbuild_callback`; the existing
   `JsonPushParser` tests must still pass.
2. Add `JsonWriteSink` and `jwrite_callback`, built on the existing pure `jwrite_*` events;
   test with a C harness feeding chunked input.
3. Add the constructor mode and the `write()`/`close()` error checks. Tests: reformatting
   equals `JSON.stringify(JSON.parse(s), null, 2)`; large integers survive; a too-small
   output buffer throws.

### Open question

Pass number tokens through as written (`1e2` stays `1e2`), or normalize them as
`JSON.parse` followed by a write would (`100`)? Pass-through is the recommendation.

## JsonSerializer

A "pull" JSON serializer: it traverses the value lazily, producing only as much text as
requested per `.read()` call, rather than building the whole string up front.

```js
new JsonSerializer(value, indent?)   // length 1; indent defaults to 0 (compact)
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `read(n)` | 1 | method | Returns a string of up to `n` characters of serialized JSON, `''` once exhausted. |
| `read(buffer, offset?, length?)` | 1–3 | method | Writes serialized bytes directly into the given `ArrayBuffer`/`TypedArray` (no intermediate copy), starting at `offset` for up to `length` bytes — or the whole buffer if omitted. Returns the number of bytes written, `0` once exhausted. Suited to chunking output straight into e.g. a network buffer. |
| `location` | — | getter | A `Location` reflecting how far into the output stream production has advanced (enumerable). |

```js
import { JsonSerializer } from 'json';

let s = new JsonSerializer({ a: 1, b: [2, 3] });
let out = '';
let chunk;

while((chunk = s.read(4)) !== '') out += chunk;

console.log(out); // {"a":1,"b":[2,3]}
```

```js
// Zero-copy: write straight into a fixed-size buffer, e.g. for a socket.
let s = new JsonSerializer(bigValue);
let buf = new Uint8Array(4096);
let n;

while((n = s.read(buf)) > 0) socket.send(buf.subarray(0, n));
```

## JsonWriter

A push-based incremental JSON writer: instead of serializing a JS value tree, the caller drives
output by calling `objectStart()`, `arrayStart()`, `key()`, `value()`, `arrayEnd()`, and
`objectEnd()` in document order. Output goes to a `Writer` (from `stream-utils.h`) — a buffer,
fd, or any writable sink. Handles comma separation, indentation, and key/value ordering
automatically, and throws `TypeError` on structural mistakes (e.g. a `key()` outside an object,
a missing value after a key, mismatched end calls), and an `Error` when the output buffer is full.

```js
new JsonWriter(output?, options?)   // output is a buffer or writer; options may be a number (indent) or {indent, bareKeys, singleQuotes, hexNumbers, minify}
```

`output` may be:
- a writable buffer (`ArrayBuffer`, typed array), or
- an object exposing a `write(buf, offset, length)` method.

`options` is either a number (the indent width, default 0 = compact) or an object with any of these keys (all default off). They are read once, when the writer is constructed; the writer has no properties for them.

| Key | Effect |
| --- | --- |
| `indent` | Spaces per nesting level; 0 writes no newlines. |
| `bareKeys` | Write keys that are plain identifiers (`[A-Za-z_$][A-Za-z0-9_$]*`) unquoted: `id: 1`. Other keys stay quoted. JSON5. |
| `singleQuotes` | Write strings and quoted keys in `'...'`; `'` is escaped, `"` is not. JSON5. |
| `hexNumbers` | Write integral numbers (\|n\| up to 2^53-1) as `0xff` / `-0x1f`; other numbers are unchanged. JSON5. |
| `minify` | No whitespace at all (no newlines, indentation or space after `:`), whatever `indent` says. |

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `objectStart()` | 0 | method | Writes `{` and opens a new object context. Increments the nesting level. Returns bytes written. |
| `objectEnd()` | 0 | method | Writes `}`, closing the current object. Throws if not inside an object, or if the last key has no value. Returns bytes written. |
| `arrayStart()` | 0 | method | Writes `[` and opens a new array context. Increments the nesting level. Returns bytes written. |
| `arrayEnd()` | 0 | method | Writes `]`, closing the current array. Throws if not inside an array. Returns bytes written. |
| `key(name)` | 1 | method | Writes `"name":` inside the current object. Throws if not inside an object, or if the previous key still expects a value. Returns bytes written. |
| `value(val)` | 1 | method | Writes a JSON primitive (string, number, boolean, null). Inside an object, must follow a `key()`. Inside an array, writes the next element. Returns bytes written. |
| `written` | — | getter | Total number of bytes written so far, counting every byte of output. |

```js
import { JsonWriter } from 'json';

let buf = new Uint8Array(1024);
let w = new JsonWriter(buf, 2);

w.objectStart();
  w.key('name');   w.value('Alice');
  w.key('scores'); w.arrayStart();
    w.value(95);
    w.value(87);
    w.value(92);
  w.arrayEnd();
w.objectEnd();

let text = new TextDecoder().decode(buf.subarray(0, w.written));
console.log(text);
// {
//   "name": "Alice",
//   "scores": [
//     95,
//     87,
//     92
//   ]
// }
```
