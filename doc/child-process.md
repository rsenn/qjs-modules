# child-process

Source: `quickjs-child-process.c` — module exports **`ChildProcess`**, a function list, and `default`.

Spawning and managing child processes (Node `child_process`-flavored).

## Module functions

| Function | Args | Description |
| --- | --- | --- |
| `exec(command, options)` | 1 | Runs a command via the shell, asynchronously. |
| `execSync(command, options)` | 1 | Synchronous variant of `exec`. |
| `spawn(file, args, options)` | 1 | Spawns a process without a shell, asynchronously. |
| `spawnSync(file, args, options)` | 1 | Synchronous variant of `spawn`. |
| `kill(pid, signal)` | 1 | Sends a signal to a process. |

## ChildProcess

```js
new ChildProcess([options])   // length 1
```

### Methods

| Method | Args | Description |
| --- | --- | --- |
| `wait()` | 0 | Waits for the process to change state; returns status. |
| `kill([signal])` | 0 | Sends a signal to this process. |
| `[Symbol.toPrimitive]()` | 0 | Primitive coercion (the pid). |

### Properties (read-only, mostly enumerable)

| Property | Description |
| --- | --- |
| `file` | Executable path. |
| `cwd` | Working directory. |
| `args` | Argument vector. |
| `env` | Environment map. |
| `stdio` | Array of stdio streams/fds. |
| `pid` | Process id. |
| `exitcode` | Exit code once exited. |
| `termsig` | Signal that terminated the process. |
| `exited` | Whether it has exited. |
| `signaled` | Whether it was terminated by a signal. |
| `stopped` | Whether it is stopped. |
| `continued` | Whether it was continued. |
