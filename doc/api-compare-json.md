# JSON: our API against Browser, Bun, Node, Deno and JS packages

What `json` (`quickjs-json.c`, `lib/jsonStreams.js`) offers, set next to what the other
runtimes and the widely used npm/jsr packages offer. Priority when they disagree is the one of
`CLAUDE.md`: WHATWG > Browser > Bun > Node > Deno. Companion documents:
[api-implement-json.md](api-implement-json.md) (how the code is split),
[api-interface-json.md](api-interface-json.md) (the interfaces between the pieces),
[native/json.md](native/json.md) (the module reference).

Method: every row marked "probed" was run on node 22.23.3, bun 1.4.2, deno 2.9.7 and the
qjsm of this tree (2026-10-17); packages were installed and run (`@streamparser/json` 0.0.26,
`json5` 2.2.3, `jsonc-parser` 3.3.1, `jsonparse` 1.3.1, `lossless-json` 4.3.2,
`stream-json` 3.7.0, `jsr:@std/json`). Anything not probed says so.

## 1. What each runtime has built in (probed)

| Capability | Browser | Bun | Node | Deno | qjsm (here) |
| --- | --- | --- | --- | --- | --- |
| `JSON.parse`, `JSON.stringify` | yes | yes | yes | yes | yes (engine) |
| `JSON.rawJSON`, `JSON.isRawJSON` | yes | yes | yes | yes | yes (engine) |
| `Response.prototype.json()`, `Request.prototype.json()` | yes | yes | yes | yes | no (sibling `../qjs-lws/`) |
| `Blob.prototype.json()` | spec (Body mixin is on Response/Request; `Blob` has `text()` only) | yes | no | no | yes (`blob` module, added) |
| `JSON5` namespace (`parse`, `stringify`) | no | `Bun.JSON5` | no | no | planned (Tier 18) |
| `JSONC` namespace (`parse`) | no | `Bun.JSONC` | no | no | planned |
| `JSONL` namespace (`parse`, `parseChunk`) | no | `Bun.JSONL` | no | no | `JSONL` export, done |
| JSON modules (`import x from './x.json' with {type: 'json'}`) | yes | yes | yes | yes | yes |
| streaming parse/stringify | `TextDecoderStream` + a library | same | same | `jsr:@std/json` streams | `lib/jsonStreams.js` (port of `@std/json`) |

Nothing in the platform parses incrementally, reports tokens, or writes JSON incrementally.
All of that is library territory, which is where our non-standard classes live.

## 2. `JSON.parse` conformance (probed, 27 inputs)

The four runtimes agree on every input: BOM, trailing commas, comments, `NaN`, single quotes,
bare keys, `1 2`, empty and whitespace-only text, `01`, `1.`, `.5`, `+1`, `0x10`, a raw tab in a
string are all `SyntaxError`; `"__proto__"` is an own property; duplicate keys keep the last;
`"\ud800"` is kept; `1e400` is `null` after `JSON.stringify`; `-0` stays `-0`.

One difference: nesting 100000 deep (`'['.repeat(1e5)`) parses on node, bun and deno and is a
`SyntaxError` ("stack overflow") on qjsm, because `JS_ParseJSON` recurses. 10000 deep is fine
everywhere. This is why `Blob.prototype.json()` is not yet 100 % Bun compatible.

## 3. Our exports against the standard counterpart

| Export | Counterpart | Same? | Notes |
| --- | --- | --- | --- |
| `read(text)` | `JSON.parse` | **no** | probed: accepts trailing commas, `01`, `1.`, a raw tab in a string; builds null-prototype objects; does not decode string escapes at all (`"A"` stays six characters; `BUGS`). Backed by `sj.h`, the least conformant of the three parsers in the tree |
| `write(v, replacer, space)` | `JSON.stringify` | yes | differential over 420 cases, 0 differences; also takes `write(v, 2)` as shorthand |
| `JSONL.parse`, `JSONL.parseChunk` | `Bun.JSONL` | yes | `parseChunk` returns `{values, read, done, error}`; units are UTF-16 for strings, bytes for `Uint8Array` |
| `Blob.prototype.json()` (module `blob`) | Bun `Blob.json()` | yes except two | nesting depth (above) and property flags (ours: writable, non-enumerable, configurable; Bun's: enumerable, non-configurable) |
| `JsonParser` | none standard; closest `jsonparse` `Parser`, `stream-json` `parser()` | n/a | pull tokenizer: `next()`/`token`/`state`/`depth`/`location`, iterable; tolerates `//` and `/* */` comments, reports them with `comments` |
| `JsonPushParser` | `@streamparser/json` `JSONParser` | partly | `write(chunk)`, `close()`, `root`, `path`, `location`; `TYPE_*` constants. No `onValue`, no `paths` filter, no `end()` alias; resyncs after a bad value |
| `JsonSerializer` | `stream-json` `Stringer`, `@std/json` `JsonStringifyStream` | partly | pull: `read(n)` / `read(buffer, offset, length)` / `next()`, `finished`, `root`, `path`; takes `(value, replacer, space)` like `JSON.stringify` |
| `JsonWriter` | none | n/a | event writer (`objectStart`, `key`, `value`...), writes into any `Writer` target including `ArrayBufferSink` |
| `JsonParseStream`, `ConcatenatedJsonParseStream`, `JsonStringifyStream` (`lib/jsonStreams.js`) | `jsr:@std/json` | yes | diffed against the jsr package |

`JSON.parse`'s reviver, with `context.source`, and `JSON.rawJSON` are engine features; our `write`
honours `rawJSON` through the `raw` walk callback.

## 4. The three namespaces only Bun has

| API | Bun shape (probed on 1.4.2) | Us |
| --- | --- | --- |
| `JSON5.parse(text)` | one argument; reviver ignored; trailing data is a `SyntaxError` | planned on `jread` + `jbuilder` |
| `JSON5.stringify(v, replacer, space)` | `length` 3; bare keys, single quotes, `NaN`/`Infinity`; rejects replacer functions and BigInt; `undefined` for a value with no JSON form | planned on `jwalk` + `jwrite` options (`bare_keys`, `single_quotes`, `hex_numbers` already exist in `JsonWriteOptions`) |
| `JSONC.parse(text)` | comments and trailing commas allowed, otherwise JSON | planned |
| `JSONL.parse(input, start?, end?)`, `JSONL.parseChunk(...)` | see table above | done |
| no `JSONL.stringify`, no `JSONC.stringify` | none anywhere (also none in node/deno) | none, deliberately |

npm `json5` (`parse(text, reviver)`, `stringify(value, replacer, space | options)`) and
`jsonc-parser` (`parse`, `parseTree`, `visit`, `modify`, `applyEdits`, `printParseErrorCode`) are
the packages behind the same formats. Their reviver and edit APIs are not in Bun's namespaces and
are not planned; the namespaces are the target.

## 5. Streaming and event libraries

| Package | Shape | Closest of ours | Gap |
| --- | --- | --- | --- |
| `@streamparser/json` | `new JSONParser({paths, keepStack, stringBufferSize})`, `onValue({value, key, parent, stack})`, `onToken`, `write`, `end` | `JsonPushParser` | `onValue`, `paths`, `end()`. Planned (Tier 18 step 4) |
| `jsonparse` | `new Parser()`, `onValue`, `onToken`, `write`, `push/pop` stack | `JsonParser`/`JsonPushParser` | none needed |
| `stream-json` | Node streams (`parser()`, `Assembler`, `Stringer`, `pick`/`filter`) | `JsonSerializer`, `lib/jsonStreams.js` | **Node Streams are out of scope** (`CLAUDE.md`); the WHATWG forms of `@std/json` are the target |
| `jsr:@std/json` | `JsonParseStream`, `ConcatenatedJsonParseStream`, `JsonStringifyStream` (WHATWG TransformStreams) | `lib/jsonStreams.js` | done |
| `lossless-json` | `parse` with `LosslessNumber`, `stringify` | `JSON.rawJSON`, `JSONL` | engine `rawJSON` plus reviver `context.source` cover the use; no wrapper planned |

## 6. Findings and what they decide

1. `read()` and `write()` duplicate `JSON.parse`/`JSON.stringify`; `read` is wrong in four ways (section 3). Plan: deprecate both after
   the Bun namespaces exist, migrate `plot-cv/yt-playlist.js` and `plot-cv/clang-ast.js`, then remove (Tier 18 step 5). Until then
   `read` should decode escapes (`BUGS`).
2. The Bun-compatible work is `JSON5`, `JSONC` and finishing `Blob.json()`; all three need one strict, non-recursive reader
   that builds ordinary-prototype objects. `jread` + `jbuilder` can be that reader once given a strict mode and a prototype option.
3. `JsonPushParser` should grow the `@streamparser/json` surface (`onValue`, `paths`, `end()`), the only widely used shape for it.
4. Our `Json*` classes have no standard to follow; they stay as the engines under the standard surface (`CLAUDE.md`: no new
   custom public API). `JsonWriter` and `JsonParser` remain because `lib/jsonStreams.js` and `JsonSerializer` need them.
5. Keep `write(undefined)` returning `undefined` and the `JSON.stringify` rule set exactly; the differential test is the guard.

## 7. Parse errors and locations (probed 2026-10-17)

How each runtime and package reports a parse error and where the position goes. Input: `{"a":` newline `  [1, x]}`
(the bad token is at line 2, column 7).

| API | Exception | Message | Position for the client |
| --- | --- | --- | --- |
| node, deno: `JSON.parse`, `Response.json`, `Request.json` | `SyntaxError` | `Unexpected token 'x', "{"a":\n  [1, x]}" is not valid JSON`; EOF: `Unexpected end of JSON input` | none: a snippet in the message, no property |
| node: `import './bad.json' with {type:'json'}` | `SyntaxError` | the V8 text with the file path prefixed | path only |
| bun: `JSON.parse`, `Response/Request/Blob.json`, `Bun.JSON5/JSONC/XML/TOML/YAML.parse` | `SyntaxError` | `JSON Parse error: Unexpected identifier "x"`; `JSON5 Parse error: ...` and so on | own `line`, `column`: **the throw site in the calling script**, identical for every input (checked with errors at 1:6, 2:7, 5:1); not the position in the parsed text |
| bun: bad JSON module, `new RegExp`, `new URL` | `SyntaxError` / `TypeError` | | additionally `originalLine`, `originalColumn`, `sourceURL` (again the importer or caller) |
| bun: `Bun.Transpiler`, bundler messages | `BuildMessage`, `ResolveMessage` (custom classes) | | **`position`: `{lineText, file, namespace, line, column, length, offset}`**, `level`, `notes`; `Bun.build` rejects with `AggregateError` (`errors`, `cause`) |
| bun: `Bun.JSONL.parseChunk` | `error` field is a `SyntaxError` | | no position; `read` says where the good values end |
| Firefox, Safari (docs, not probed) | `SyntaxError` | SpiderMonkey: `JSON.parse: unexpected character at line 2 column 7 of the JSON data`; JSC: `JSON Parse error: Unrecognized token 'x'` | SpiderMonkey `fileName`, `lineNumber`, `columnNumber` are the throw site, not the JSON position |
| `json5` 2.2.3 | `SyntaxError` | `JSON5: invalid character 'x' at 2:7` | own **`lineNumber`, `columnNumber`** (1-based, correct) |
| `jsonc-parser` 3.3.1 | **no exception**; `parse(text, errors)` fills an array | | `{error: code, offset, length}` with `printParseErrorCode(code)` (`InvalidSymbol`, `ValueExpected`, ...) |
| `lossless-json` 4.3.2 | `SyntaxError` | `Array item expected but got 'x' at position 12` | message only |
| `@streamparser/json` 0.0.26 | custom `TokenizerError`, `TokenParserError` (extend `Error`) | `Unexpected "," at chunk position "10" (absolute position "10") in state ERROR` | message only; errors delivered through `onError`, or thrown when there is no handler |
| `jsonparse` 1.3.1 | plain `Error` through `onError` | `Unexpected "x" at position 12 in state STOP` | message only |
| `jsr:@std/jsonc` | `SyntaxError` | `Cannot parse JSONC: unexpected token "x" in JSONC at position 12` | message only |
| `jsr:@std/json` `JsonParseStream` | `SyntaxError` | the V8 text plus `(parsing: '...')` | none |
| **ours now** | see section 8 | | |

### 7.1 What this shows

- No runtime exposes the position of a JSON parse error as a structured, correct value. The only correct ones are npm `json5`
  (`lineNumber`/`columnNumber`), `jsonc-parser` (`offset`/`length`/code) and Bun's *bundler* classes (`position`).
- Bun's `line`/`column` on `SyntaxError` look like the answer and are not: they locate the `JSON.parse` call, as JSC does for every
  error. Do not copy them; do not alias them.
- Custom exception classes appear for tools, not for `JSON.parse`: `BuildMessage`, `ResolveMessage`, `AggregateError` (Bun),
  `TokenizerError`/`TokenParserError` (`@streamparser/json`), `ParseError` (`@xmldom/xmldom`, see the XML document). The platform
  keeps `SyntaxError` for parsing.
- Streaming parsers (`@streamparser/json`, `jsonparse`, `sax`, `parse5`) report through a callback and keep going; whole-text
  parsers throw at the first error; `jsonc-parser` is the only one that collects.
- Message conventions differ in every runtime; only the class (`SyntaxError`) is common.

## 8. Our parse errors today (probed)

| API | Exception | Message | Position |
| --- | --- | --- | --- |
| `read(text, file?)` | **`InternalError`** | `error: 2:7: unknown token` | in the message only; not `SyntaxError`, and the file name is dropped |
| `JsonParser.parse()` | `SyntaxError` | `f.json:2:8: expected a value` | in the message; column 8 where `JSON.parse` and `json5` say 7: it is the position **after** the offending token; `.location` is live but nothing on the error |
| `JsonPushParser.write()` / `close()` | `SyntaxError` | `parse error`, `unexpected end of input` | none, although the parser has `.location` |
| `JSONL.parse` | `TypeError` for a bad input; `SyntaxError` | `JSONL: expected newline after value` | none |
| `JSONL.parseChunk` | `error` field: a `SyntaxError` | | none (`read` marks the end of the good values) |
| `Blob.prototype.json()` | rejects with `SyntaxError` | the engine's text | none |
| bad argument type (`read(5)`) | **`ReferenceError`** | `json.read(): expecting buffer or string` | n/a; should be `TypeError` |
| `JsonWriter` ordering mistakes | `TypeError`, `InternalError` for a write failure | `JsonWriter: expected key` | n/a |

## 9. Target: exceptions and locations

The contract all JSON parsers follow (the same text is in [api-interface-json.md](api-interface-json.md) section 11):

- parse failure is a `SyntaxError` with an own, non-enumerable **`loc`**: a `Location` (`file`, `line`, `column`, `charOffset`,
  `byteOffset`) at the **start** of the offending token (1-based, as `JSON.parse` tools and `json5` count), plus `length` (characters
  of the offending span, 0 at end of input) and a stable `code` string (`unexpected-token`, `unexpected-eof`, `invalid-escape`,
  `invalid-number`, `unterminated-string`, `trailing-comma`, `unexpected-comment`, `trailing-data`, ...);
- bad argument type is a `TypeError`, never `ReferenceError`; `InternalError` is never used for input problems;
- a failing `Reader` or user callback keeps its own exception; ours is not wrapped over it;
- streaming parsers deliver each error to `onError(err)` with the same `SyntaxError`, and recover; collecting parsers throw an
  `AggregateError` whose `errors` are those `SyntaxError`s (Bun's `Bun.build` shape);
- promise APIs (`Blob.json()`) reject with the same `SyntaxError`.

