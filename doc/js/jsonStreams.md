# jsonStreams

Source: `lib/jsonStreams.js` (pure JS, portable)

Deno's `@std/json` streams as WHATWG `TransformStream`s. Results match `jsr:@std/json`
on every case in `tests/unittests/test-jsonstreams.js`, which also runs on node, bun
and deno.

Needs the `TransformStream` global. Under `qjsm` the global streams from `globals` have
no `pipeTo`/`pipeThrough`; use the classes of `lib/stream.js`.

## Exports

| Class | Writable | Readable | Behavior |
| --- | --- | --- | --- |
| `JsonParseStream` | string | any | one JSON value per chunk (a line); empty chunks skipped; `SyntaxError` on a bad chunk |
| `ConcatenatedJsonParseStream` | string | any | back-to-back values (`{}{}`, `[]{}`, `1 2`), any chunking; `SyntaxError` on bad or truncated input |
| `JsonStringifyStream` | any | string | `prefix + JSON.stringify(value) + suffix` |

Every constructor takes `{ writableStrategy, readableStrategy }`; `JsonStringifyStream` also
`{ prefix = '', suffix = '\n' }`.

```js
values.pipeThrough(new JsonStringifyStream());                       // '{"a":1}\n'
values.pipeThrough(new JsonStringifyStream({ prefix: '\x1e' }));     // RFC 7464
chunks.pipeThrough(new ConcatenatedJsonParseStream());               // {a: 1}, [2], ...
```

A number or literal in `ConcatenatedJsonParseStream` ends at whitespace, `{`, `[`, `"` or the
end of the stream. `JsonParseStream` expects complete values per chunk: split text into
lines first (Deno uses `TextLineStream` from `@std/streams`; there is none here), or use
`JSONL.parseChunk` from `json` for newline-delimited input with a carried tail.
