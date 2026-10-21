# node:os

Source: `lib/node/os.js` — import as `node:os` (also `require('os')`, `require('node:os')`); qjsm only.
The bare `os` stays QuickJS's own `os` module.

Node's [`os`](https://nodejs.org/api/os.html): facts about the machine, the user and the kernel. On Linux they
come from `misc.uname()`, `/proc`, `/sys` and `/etc/passwd`, read on every call.

```js
import os from 'node:os';

os.cpus().length;          // 8
os.totalmem();             // 33571848192
os.userInfo().username;    // 'roman'
`${os.hostname}`;          // 'gniltag', the getters convert as in Node
```

| Member | Result | From |
| --- | --- | --- |
| `arch()`, `platform()` | `'x64'`, `'linux'` | `process.arch`, `process.platform` |
| `type()`, `release()`, `version()`, `machine()`, `hostname()` | `uname(2)` fields | `misc.uname()` |
| `endianness()` | `'LE'` or `'BE'` | |
| `homedir()` | `$HOME`, else the user's home directory | `/etc/passwd` |
| `tmpdir()` | `$TMPDIR`, `$TMP`, `$TEMP` or `/tmp`, no trailing `/` | |
| `userInfo({ encoding })` | `{ uid, gid, username, homedir, shell }`, Buffers with `'buffer'` | `/etc/passwd` |
| `cpus()` | `{ model, speed, times: { user, nice, sys, idle, irq } }` per CPU, times in ms | `/proc/cpuinfo`, `/proc/stat`, `/sys/.../scaling_cur_freq` |
| `availableParallelism()` | CPUs in the affinity mask | `/proc/self/status` |
| `freemem()`, `totalmem()` | bytes, `MemAvailable` and `MemTotal` | `/proc/meminfo` |
| `loadavg()`, `uptime()` | `[1, 5, 15]` minute averages, seconds | `/proc/loadavg`, `/proc/uptime` |
| `getPriority(pid = 0)` | nice value | `/proc/<pid>/stat` |
| `setPriority([pid,] priority)` | throws `InternalError` after checking the arguments | |
| `networkInterfaces()` | throws `InternalError` | |
| `constants` | `signals`, `errno`, `priority`, `dlopen`, `UV_UDP_REUSEADDR` | `misc` |
| `EOL`, `devNull` | `'\n'`, `'/dev/null'` | |

Errors have Node's codes: `TypeError` `ERR_INVALID_ARG_TYPE` and `RangeError` `ERR_OUT_OF_RANGE` for a bad
`pid` or `priority`, `SystemError` `ERR_SYSTEM_ERROR` (`ESRCH` for a missing process, `ENOENT` for a user
without a passwd entry).

## Missing compared with Node

| Node | Here |
| --- | --- |
| `networkInterfaces()` | `InternalError`: needs a `getifaddrs(3)` binding |
| `setPriority()` | `InternalError`: needs a `setpriority(2)` binding |
| `userInfo()` for NSS users (LDAP, sssd) | `SystemError` `ENOENT`: only `/etc/passwd` is read |
| macOS, Windows, BSD | the `/proc` functions throw `InternalError` |
