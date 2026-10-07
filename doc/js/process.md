# process

Source: `lib/process.js` (pure JS) — default export: `process`

A Node-style `process` object.

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `process` | object | `argv`, `env`, `pid`, `cwd()`, `exit()`, `stdout`/`stderr`/`stdin`, `platform`, etc. **(default export)** |

## Node additions

`process` inherits `EventEmitter.prototype` (`process.on('exit', code => ...)`; `exit` is
emitted by `process.exit()`, not at normal termination). Also: `nextTick(fn, ...args)` (a
promise job), `exitCode`, `memoryUsage()` (`rss` from `/proc/self/statm`, the heap fields
are 0), `uptime()`, `title`, `emitWarning()`. `version` and `versions` are not provided.
