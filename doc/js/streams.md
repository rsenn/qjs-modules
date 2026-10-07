# streams

Source: `lib/streams.js` (pure JS)

Higher-level helpers built on top of the WHATWG [`stream`](../native/stream.md)
implementation — file-backed streams, line iteration, and text transform
streams. Re-exports everything from `lib/stream.js` (the full WHATWG streams, with `pipeTo()`/`pipeThrough()`).

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| *(re-export)* | — | — | `export * from './stream.js'` — all stream classes. |
| `FileSystemReadableStream(file, bufSize)` | 1–2 | function | A `ReadableStream` reading from an open file. |
| `FileSystemReadableFileStream(path, bufSize)` | 1–2 | function | A `ReadableStream` reading from a path. |
| `FileSystemWritableFileStream(path)` | 1 | function | A `WritableStream` writing to a path. |
| `StreamReadIterator(strm)` | 1 | async generator | Yields chunks read from a stream. |
| `LineStreamIterator(strm)` | 1 | async generator | Yields lines read from a stream. |
| `ByLineStream` | — | const | A transform that splits a byte stream into lines. |
| `TextEncoderStream` | — | class | Transforms strings → bytes (`extends TransformStream`). |
| `TextDecoderStream` | — | class | Transforms bytes → strings (`extends TransformStream`). |
