# bun-spawn

Source: `lib/bun/spawn.js`, reached as `Bun.spawn` and `Bun.spawnSync` (lazy, loaded on first use) or `import { spawn, spawnSync } from 'bun:spawn'`

`Bun.spawn()` and `Bun.spawnSync()` on top of [child-process](../native/child-process.md) (through `node/child_process`) and the WHATWG streams of `lib/stream.js`. Names, argument shapes, results and error messages follow Bun (checked against bun 1.4.2).

```js
const p = Bun.spawn(['sh', '-c', 'echo hi; exit 3']);

await new Response(p.stdout).text(); // 'hi\n'
await p.exited; // 3

Bun.spawnSync(['echo', 'x']).stdout.toString(); // 'x\n'
```

## Options

| Option | Values |
| --- | --- |
| `cmd`, `cwd`, `env` | `Bun.spawn({ cmd, ... })` is the same as `Bun.spawn(cmd, { ... })`; `env` replaces the environment |
| `stdin` | `'ignore'` (default), `'pipe'` (a `FileSink`), `'inherit'`, an fd, a `BunFile`, a `Blob`, `Response`, `ReadableStream`, string or bytes |
| `stdout` | `'pipe'` (default), `'ignore'`, `'inherit'`, an fd, a `BunFile` |
| `stderr` | `'inherit'` (default), otherwise as `stdout` |
| `stdio` | `[stdin, stdout, stderr]` |
| `timeout`, `killSignal` | kill after `timeout` ms with `killSignal` (default `SIGTERM`) |
| `signal` | an `AbortSignal` kills the process |
| `onExit(subprocess, exitCode, signalCode, error)` | called after the exit |

`spawnSync` defaults to `stdin: 'ignore'`, `stdout: 'pipe'`, `stderr: 'pipe'`.

## Subprocess

| Member | Meaning |
| --- | --- |
| `pid`, `exitCode`, `signalCode`, `killed` | `exitCode` is `null` while running and after a signal |
| `exited` | promise of the exit code, `128 + signal` when signaled (143 for SIGTERM) |
| `stdin` | `FileSink`: `write()`, `flush()`, `end()`, `ref()`, `unref()`, never blocks |
| `stdout`, `stderr`, `readable` | `ReadableStream` of `Uint8Array`, with `text()`, `json()`, `bytes()`, `arrayBuffer()`, `blob()` on it, backpressure pauses the pipe |
| `kill(signal)` | signal name or number, default `SIGTERM` |

`spawnSync` returns `{ exitCode, stdout, stderr, success, pid }` (`stdout` and `stderr` are `Buffer`s, `signalCode` is added when signaled).

## Errors

A command not found throws `Error` with `code: 'ENOENT'`, `errno: -2` and the message `Executable not found in $PATH: "name"`. A missing `cwd` throws `ENOENT: no such file or directory, posix_spawn 'name'`. A `cmd` that is not an array or is empty throws `TypeError` (`ERR_INVALID_ARG_TYPE`).

## Differences from Bun

- no `resourceUsage()`, `ipc`, `terminal`, `disconnect()`, `send()`
- `ref()` and `unref()` do nothing
- the exit is detected by a 50 ms poll of `waitpid(WNOHANG)`
- `onExit` gets `null`, not `undefined`, as the error
