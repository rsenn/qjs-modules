# wasi

Source: `lib/wasi.js` (JS over `fs` and `process`) — exports: `WASI`, `errno`; qjsm, node, bun, deno

Node's `node:wasi` class (WASI preview1), in JS, for the `wasm` module or any
`WebAssembly` global. No wasm engine's own WASI is used, so behavior is the same on
wasm3 and WAMR. It touches only `fs`, `process`, `Atomics` and `crypto` (falling back to
`/dev/urandom`), so the same file runs on node, bun and deno.

```js
import { WASI } from 'wasi';
import * as wasm from 'wasm';

const wasi = new WASI({ version: 'preview1', args: ['prog', 'x'], env: { A: '1' }, preopens: { '/sandbox': '.' } });
const instance = new wasm.Instance(new wasm.Module(bytes), wasi.getImportObject());
const code = wasi.start(instance); // runs _start; returns the exit code
```

## WASI

| Member | Meaning |
| --- | --- |
| `new WASI(options)` | `version` (`'preview1'` default, `'unstable'`), `args` (`[]`), `env` (`{}`, not inherited), `preopens` (`{ guestPath: hostPath }`), `returnOnExit` (`true`), `stdin`/`stdout`/`stderr` (host fds, default 0/1/2) |
| `getImportObject()` | `{ wasi_snapshot_preview1: wasiImport }` (`wasi_unstable` for `'unstable'`) |
| `wasiImport` | the preview1 functions; `i64` parameters arrive as `BigInt` |
| `start(instance)` | runs `_start`; returns the `proc_exit` code or 0; `ERR_WASI_ALREADY_STARTED` on a second call |
| `initialize(instance)` | runs `_initialize` of a reactor module |
| `finalizeBindings(instance, { memory }?)` | binds the guest memory without running anything |

With `returnOnExit: false`, `start()` exits the process with the guest's code.

## Behavior

| Area | Detail |
| --- | --- |
| Paths | resolved inside the preopened directory; `..` above it, an absolute path, or a symlink leading out gives `NOTCAPABLE` (76) |
| Files | `path_open`, `fd_read/write/pread/pwrite/seek/tell/sync/allocate`, `fd_filestat_*`, `fd_readdir`, rename, link, symlink, unlink, mkdir, rmdir |
| Clocks | `clock_time_get`: realtime from `Date.now()` (millisecond resolution), others from `hrtime()` |
| `random_get` | reads `/dev/urandom` |
| `poll_oneoff` | clock subscriptions sleep; fd subscriptions report ready at once |
| Not implemented | `sock_*` and `proc_raise` return `NOSYS`; `fd_fdstat_set_rights` returns `NOSYS`; rights are reported as all set and not enforced |
| Errors | Linux errno from `os`/`misc` is mapped to the WASI errno |

`errno` exports the WASI error numbers used (`errno.NOENT === 44`).
