# syscallerror

Source: `quickjs-syscallerror.c` — module exports **`SyscallError`** plus `errno` constants.

An `Error` subclass carrying the failing syscall name and `errno`. Thrown by the
POSIX-style bindings (sockets, misc, …).

## Constructor

```js
new SyscallError([syscall, errno])   // length 1
```

## Properties (read-only)

| Property | Description |
| --- | --- |
| `syscall` | Name of the syscall that failed. |
| `errno` | Numeric error code. |
| `message` | Human-readable error message. |
| `name` | Error name. |
| `stack` | Captured stack trace. |

## Methods

| Method | Args | Description |
| --- | --- | --- |
| `toString()` | 0 | Renders the error as a string (also bound to `[Symbol.toPrimitive]`). |

## Static functions

| Function | Args | Description |
| --- | --- | --- |
| `errno()` | 0 | Returns the current global `errno`. |
| `strerror(code)` | 0 | Returns the message string for an error code. |

Plus a list of `errno` value constants (`EPERM`, `ENOENT`, …) exported by the module.
