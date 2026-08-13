# json

Source: `quickjs-json.c` — module exports **`JsonParser`**, **`JsonPushParser`**, **`JsonSerializer`**, **`JsonWriter`** and a function list.

A streaming/extended JSON reader plus simple read/write helpers.

## Module functions

| Function | Args | Description |
| --- | --- | --- |
| `read(input, inputName?)` | 1–2 | Parses JSON text into a JS value. `input` is a string or buffer. `inputName` is an optional filename for error messages. Throws on trailing data after the root value. |
| `write(value, indent?)` | 1–2 | Serializes a JS value to JSON text. `indent` (default 0) controls pretty-printing — when positive, each nesting level adds that many spaces of indentation. |

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
- an object exposing such a function as its `read` method — called with the object as `this`, e.g. a file wrapper: `{ read(buf, len) { return f.read(buf, 0, len); } }`.

The function/method forms let the parser pull raw bytes on demand (e.g. from an fd) instead of requiring the whole document up front.

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `parse()` | 0 | method | Advances one token. Returns one of `"NEED_DATA"`, `"NONE"`, `"OBJECT"`, `"OBJECT_END"`, `"ARRAY"`, `"ARRAY_END"`, `"KEY"`, `"STRING"`, `"TRUE"`, `"FALSE"`, `"NULL"`, `"NUMBER"`. Throws on malformed input. |
| `pos` | — | getter | Current parse position, in characters consumed (enumerable). |
| `token` | — | getter | The current token's decoded text — e.g. string/key content has escapes and `\uXXXX` (including surrogate pairs) already resolved (enumerable). |
| `state` | — | getter | Internal parser state bitmask (enumerable). |
| `depth` | — | getter | Current nesting depth (enumerable). |
| `location` | — | getter | A `Location` reflecting the current input position (line/column/byte offset/filename); live, like `JsonPushParser`'s (enumerable). |
| `callback` | — | getter/setter | Per-value callback invoked while parsing. The function is called as `callback(parser, type, text)` where `type` is a `JsonValueType` integer and `text` is the token string (or `undefined`). |

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
a missing value after a key, mismatched end calls).

```js
new JsonWriter(output?, options?)   // output is a buffer or writer; options may be a number (indent) or {indent}
```

`output` may be:
- a writable buffer (`ArrayBuffer`, typed array), or
- an object exposing a `write(buf, offset, length)` method.

`options` is either a number (the indent width, default 0 = compact) or an object with an `indent` property.

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `objectStart()` | 0 | method | Writes `{` and opens a new object context. Increments the nesting level. Returns bytes written. |
| `objectEnd()` | 0 | method | Writes `}`, closing the current object. Throws if not inside an object, or if the last key has no value. Returns bytes written. |
| `arrayStart()` | 0 | method | Writes `[` and opens a new array context. Increments the nesting level. Returns bytes written. |
| `arrayEnd()` | 0 | method | Writes `]`, closing the current array. Throws if not inside an array. Returns bytes written. |
| `key(name)` | 1 | method | Writes `"name":` inside the current object. Throws if not inside an object, or if the previous key still expects a value. Returns bytes written. |
| `value(val)` | 1 | method | Writes a JSON primitive (string, number, boolean, null). Inside an object, must follow a `key()`. Inside an array, writes the next element. Returns bytes written. |
| `written` | — | getter | Total number of bytes written so far. |
| `indent` | — | getter/setter | The indentation width (number of spaces per level). Default 0 (compact, no whitespace). |

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
