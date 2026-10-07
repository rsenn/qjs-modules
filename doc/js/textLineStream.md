# textLineStream

Source: `lib/textLineStream.js` (pure JS, portable)

Deno's `TextLineStream` (`@std/streams`) as a WHATWG `TransformStream`. Results match
`jsr:@std/streams` on every case in `tests/unittests/test-textlinestream.js`, which also
runs on node, bun and deno. Needs the `TransformStream` global (under `qjsm`, `import
'globals'` first).

| Class | Writable | Readable | Behavior |
| --- | --- | --- | --- |
| `TextLineStream({ allowCR = false })` | string | string | one chunk per line, without its terminator |

- lines end at `\n` or `\r\n`; with `allowCR` a lone `\r` ends a line too
- the lines are the same however the text is split into chunks, including a `\r\n` cut in two
- a last line without a terminator is emitted when the stream ends; a trailing newline adds no empty line

```js
bytes.pipeThrough(new TextDecoderStream())
     .pipeThrough(new TextLineStream())
     .pipeThrough(new JsonParseStream());      // newline-delimited JSON
```
