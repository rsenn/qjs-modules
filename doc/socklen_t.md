# socklen_t

Source: `lib/socklen_t.js` (pure JS) — default export: `socklen_t`

A tiny `ArrayBuffer` subclass representing a C `socklen_t` value, for use with
the socket `getsockopt`/`accept` style APIs that need an in/out length.

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `socklen_t` | class | `ArrayBuffer` subclass holding a single `socklen_t`. **(default export)** |
