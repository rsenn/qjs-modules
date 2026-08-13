# io

Source: `lib/io.js` (pure JS) — default export: `io`

File-descriptor read/write handler registration and multiplexing (an event-loop
I/O dispatcher).

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `setHandler(fd, m, callback)` | 3 | function | Registers a read/write (`m` = `READ`/`WRITE`) handler for `fd`. |
| `setReadHandler(fd, callback)` | 2 | function | Registers a read handler (or clears it with `null`). |
| `setWriteHandler(fd, callback)` | 2 | function | Registers a write handler (or clears it with `null`). |
| `checkFileDescriptor(fd)` | 1 | function | Validates a descriptor. |
| `Multiplexer` | — | class | Dispatches registered handlers as descriptors become ready. |
| `DescriptorMap` | — | class | `Array` subtype mapping fds → handler entries. |
| `HandlerEntry` | — | class | `Array` subtype holding the handlers for one fd. |
| `IOReadDecorator`, `IOWriteDecorator` | — | const | Mixins adding read/write helpers to streams. |
| `READ`, `WRITE` | — | const | Handler-direction constants (0 / 1). |
| `io` | — | object | The default I/O dispatcher. **(default export)** |
