# queue

Source: `quickjs-queue.c` — module export: **`Queue`**

A chunked byte queue (FIFO buffer of binary data), iterable over its chunks.

## Constructor

```js
new Queue()   // length 1
```

## Methods

| Method | Args | Description |
| --- | --- | --- |
| `write(data)` | 1 | Appends `data` (string/ArrayBuffer/typed array) to the tail. |
| `read(size)` | 1 | Removes and returns up to `size` bytes from the head. |
| `peek(size)` | 1 | Returns up to `size` bytes from the head without consuming them. |
| `skip(size)` | 1 | Discards up to `size` bytes from the head. |
| `clear()` | 0 | Empties the queue. |
| `next()` | 0 | Returns the next chunk (iterator step). |
| `chunk(index)` | 1 | Returns a specific buffered chunk. |
| `at(index)` | 1 | Returns the byte/chunk at `index`. |
| `[Symbol.iterator]()` | 0 | Iterates over the queued chunks. |

## Properties (read-only)

| Property | Description |
| --- | --- |
| `size` | Total number of buffered bytes. |
| `empty` | Whether the queue holds no data. |
| `head` | The head chunk. |
| `tail` | The tail chunk. |
| `chunks` | The list of buffered chunks. |

## QueueIterator

Returned by `[Symbol.iterator]`. Exposes `next()` and tags itself
`"QueueIterator"`.
