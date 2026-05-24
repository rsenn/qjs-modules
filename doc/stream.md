# stream

Source: `quickjs-stream.c` — WHATWG Streams implementation.

Module exports: **`ReadableStream`**, **`ReadableStreamDefaultReader`**,
**`ReadableStreamBYOBReader`**, **`ReadableStreamDefaultController`**,
**`ReadableByteStreamController`**, **`WritableStream`**,
**`WritableStreamDefaultWriter`**, **`WritableStreamDefaultController`**,
**`TransformStream`**.

## ReadableStream

```js
new ReadableStream(underlyingSource[, strategy])   // length 1
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `cancel(reason)` | 0 | method | Cancels the stream. |
| `getReader([options])` | 0 | method | Acquires a reader (default or BYOB). |
| `closed` | — | getter | Promise settling on close (enumerable). |
| `locked` | — | getter | Whether a reader holds the stream (enumerable). |

### Readers (`ReadableStreamDefaultReader`, `ReadableStreamBYOBReader`)

```js
new ReadableStreamDefaultReader(stream)   // length 1
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `read([view])` | 0 | method | Reads the next chunk; resolves `{value, done}`. |
| `cancel(reason)` | 0 | method | Cancels and releases. |
| `releaseLock()` | 0 | method | Releases the reader's lock. |
| `closed` | — | getter | Closed promise. |

### Controllers

`ReadableStreamDefaultController` / `ReadableByteStreamController` (passed to the
source): `close()`, `enqueue(chunk)`, `error(e)`, and getter `desiredSize`.
`ReadableStreamBYOBRequest`: `respond(n)`, `respondWithNewView(view)`.

## WritableStream

```js
new WritableStream(underlyingSink[, strategy])   // length 1
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `abort(reason)` | 1 | method | Aborts the stream. |
| `close()` | 0 | method | Closes the stream. |
| `getWriter()` | 0 | method | Acquires a writer. |
| `locked` | — | getter | Whether a writer holds the stream (enumerable). |

### WritableStreamDefaultWriter

```js
new WritableStreamDefaultWriter(stream)   // length 1
```

`write(chunk)`, `abort(reason)`, `close()`, `releaseLock()`; getters `closed`,
`ready`.

### WritableStreamDefaultController

`error(e)` (and the controller passed to the sink).

## TransformStream

```js
new TransformStream(transformer[, writableStrategy, readableStrategy])   // length 1
```

Getters `readable` and `writable` (both enumerable) expose the paired streams.
Its controller offers `terminate()`, `enqueue(chunk)`, `error(e)` and a
`desiredSize` getter.
