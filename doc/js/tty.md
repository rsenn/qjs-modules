# tty

Source: `lib/tty.js` (pure JS)

Node-style TTY stream wrappers.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `ReadStream(fd)` | 1 | function | Wraps a TTY input descriptor (raw mode, etc.). |
| `WriteStream(fd)` | 1 | function | Wraps a TTY output descriptor (columns/rows, color support). |
| `isatty` | — | re-export | Re-exported from `os`; tests whether a descriptor is a TTY. |
