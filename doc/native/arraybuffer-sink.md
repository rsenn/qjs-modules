# arraybuffer-sink

Source: `quickjs-arraybuffer-sink.c` — module export: **`ArrayBufferSink`**

An incremental byte accumulator (Bun-style `ArrayBufferSink`): write chunks, then
collect everything as a single `ArrayBuffer`.

## Constructor

```js
new ArrayBufferSink([options])   // length 1
```

## Methods / properties

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `write(chunk)` | 1 | method | Appends `chunk` (string/ArrayBuffer/typed array); returns bytes written. |
| `flush()` | 0 | method | Returns the bytes accumulated so far and resets the buffer. |
| `end()` | 0 | method | Finalizes the sink and returns the complete `ArrayBuffer`. |
| `size` | — | getter | Number of bytes currently buffered. |
