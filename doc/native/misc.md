# misc

Source: `quickjs-misc.c` — module exports a large flat list of functions plus many constants.

A grab-bag of system, process, buffer, and QuickJS-internals utilities. Grouped
below by theme.

## Filesystem & paths

| Function | Args | Description |
| --- | --- | --- |
| `tempnam()` | 0 | Generates a temporary file name. |
| `mkstemp(template)` | 1 | Creates and opens a unique temp file. |
| `fnmatch(pattern, string, flags)` | 3 | Shell-style pattern match. |
| `glob(pattern, flags)` | 2 | Filename globbing. |
| `wordexp(words, flags)` | 2 | Shell-style word expansion. |
| `watch(path)` | 1 | Watches a path for changes. |
| `unlink(path)` | 1 | Removes a file. |
| `link` / `linkat` / `symlink` / `symlinkat` | 2–3 | Create hard / symbolic links. |
| `chmod` / `fchmod` | 2 | Change file mode (by path / fd). |
| `chown` / `fchown` / `lchown` | 3 | Change ownership. |
| `fsync` / `fdatasync` | 1 | Flush file data to disk. |
| `truncate` / `ftruncate` | 2 | Set file length. |
| `readv(fd, buffers)` / `writev(fd, buffers)` | 2 | Scatter/gather I/O over an iterable of ArrayBuffer / TypedArray / DataView, returns the byte count. More than `IOV_MAX` buffers: a write goes in batches, a read fills the first `IOV_MAX` (as node). |
| `preadv(fd, buffers, offset)` / `pwritev(...)` | 3 | Same, at a file offset; the file position is not moved. |
| `preadv2(fd, buffers, offset, flags)` / `pwritev2(...)` | 4 | Same with `RWF_*` flags (Linux); offset `-1` uses the file position. |
| `utime` / `utimes` / `lutimes` / `futimes` | 2 | Set access/modification times. |
| `access(path, mode)` | 2 | Check file accessibility. |
| `fcntl(fd, cmd, ...)` | 2 | File descriptor control. `F_GETPATH` is `-1` where the OS has no such command. |
| `fstat(fd)` | 1 | Stat by descriptor. |
| `fmemopen(buf, mode)` | 2 | Open an in-memory `FILE`. |
| `_get_osfhandle` / `_open_osfhandle` | 1 | Windows fd ↔ handle conversion. |

## Processes & users

| Function | Args | Description |
| --- | --- | --- |
| `daemon(nochdir, noclose)` | 2 | Detach into the background. |
| `fork()` / `vfork()` | 0 | Fork the current process. |
| `exec(file, args)` | 2 | Replace the process image. |
| `kill(pid, sig)` | 1 | Send a signal. |
| `setsid()` | 0 | Start a new session. |
| `gettid` / `getpid` / `getppid` / `getsid` | 0 | Process/thread ids. |
| `getuid` / `getgid` / `geteuid` / `getegid` | 0 | Read user/group ids. |
| `setuid` / `setgid` / `seteuid` / `setegid` | 1 | Set user/group ids. |
| `getExecutable` / `getWorkingDirectory` / `getRootDirectory` / `getFileDescriptor` | 0 | Resolve `/proc/self/*` links. |
| `getCommandLine` / `getEnvironment` / `getProcStat` / `getProcMaps` / `getProcMounts` | 0 | Read `/proc` info. |
| `uname()` | 0 | System identification. |
| `getRelease()` | 0 | OS release string. |

## Time & randomness

| Function | Args | Description |
| --- | --- | --- |
| `getPerformanceCounter()` | 0 | High-resolution counter. |
| `hrtime()` | 0 | High-resolution time (`[s, ns]`). |
| `rand` / `randi` | 0 | Random float / integer. |
| `srand(seed)` | 1 | Seed the generator. |
| `randb(n)` | 1 | Random bytes. |

## Terminal / console

| Function | Args | Description |
| --- | --- | --- |
| `ioctl(fd, request, arg)` | 3 | Device control. |
| `getScreenSize()` | 0 | Terminal size. |
| `clearScreen` / `clearLine` | 1 | ANSI erase. |
| `setCursorPosition` / `moveCursor` | 1 | Cursor control. |
| `setTextAttribute` / `setTextColor` | 2 | ANSI text styling. |
| `setConsoleMode` / `getConsoleMode` | 1–2 | Console mode (Windows). |
| `ttySetRaw(fd)` | 1 | Put a TTY into raw mode. |

## Buffers & ArrayBuffers

| Function | Args | Description |
| --- | --- | --- |
| `toString(buf)` | 1 | Bytes → string. |
| `toArrayBuffer(value)` | 1 | Coerce to an `ArrayBuffer`. |
| `awaitSync(promise, timeout?)` | 1 | Wait synchronously for a promise, running jobs and the event loop; returns the value or throws the reason. Non-promises pass through. With `timeout` (ms) throws `Error('awaitSync: timed out')`; without it never returns for a promise that cannot settle. |
| `importSync(specifier, base?)` | 1 | Load an ES module synchronously and return its namespace; works with top-level await. |
| `dupArrayBuffer` / `sliceArrayBuffer` / `concatArrayBuffer` | 1 | Copy / slice / concatenate buffers. |
| `searchArrayBuffer(haystack, needle[, offset[, length]][, mask])` | 2 | Index of the first match in `haystack[offset, offset + length)`, or `null`; `needle` is an ArrayBuffer or a view. |
| `searchArrayBufferLast(...)` | 2 | The same for the last match, as `lastIndexOf`. |
| `byteSwap(view, size)` | 2 | Reverses every 2-, 4- or 8-byte word in place (Buffer `swap16`/`swap32`/`swap64`). |
| `lineStarts(input)` | 1 | `Uint32Array` of line starts, first entry 0; UTF-16 indices for a string, byte offsets for a buffer. Lines end at `\n` only. |
| `copyArrayBuffer` / `compareArrayBuffer` | 2 | Copy into / compare buffers. |
| `toPointer(value)` | 1 | Native pointer of a value. |
| `strcmp(a, b)` | 2 | C-string comparison. |
| `charCode` / `charLength` | 1 | Codepoint / UTF-8 length helpers. |

## Encoding & bit twiddling

| Function | Args | Description |
| --- | --- | --- |
| `btoa` / `stoa` | 1 | Base64 / byte encode. |
| `atob` / `atos` | 1 | Base64 decode (to bytes / string). |
| `escape` / `unescape` | 1 | Percent-style escaping. |
| `quote` / `dequote` | 1 | Add / remove quoting. |
| `not` / `xor` / `and` / `or` | 1–2 | Bitwise ops over buffers. |
| `bitfieldSet` / `bits` / `bitfieldToArray` / `arrayToBitfield` | 1 | Bitfield conversions. |

## QuickJS internals & reflection

| Function | Args | Description |
| --- | --- | --- |
| `getPrototypeChain(value)` | 0 | Walks the prototype chain. |
| `immutableClass(value)` | 1 | Marks a class immutable. |
| `writeObject` / `readObject` | 1 | Serialize / deserialize a value (bytecode form). |
| `evalBinary(buf)` | 1 | Evaluate compiled bytecode. |
| `getOpCodes()` | 0 | List VM opcodes. |
| `getByteCode(fn)` | 1 | Disassemble a function. |
| `valueType` / `typeFlag` / `typeName` / `typeString` / `valueTag` / `valuePointer` | 1 | Low-level value type info. |
| `objectClassId` / `objectRefCount` / `objectOpaque` | 1 | Object internals. |
| `classAtom` / `className` / `classId` | 1 | Class metadata. |
| `stringPointer` / `stringLength` / `stringBuffer` | 1 | String internals. |
| `atomToString` / `atomToValue` / `findAtom` / `valueToAtom` / `dupAtom` / `freeAtom` | 1 | Atom table operations. |
| `getTypeId` / `getTypeStr` / `getTypeName` | 1 | Type identification. |
| `promiseState` / `promiseResult` | 1 | Inspect a promise. |
| `error()` | 0 | Current error object. |
| `atexit(fn)` | 1 | Register an exit handler. |
| `enqueueJob(fn)` | 1 | Queue a microtask/job. |

## Type predicates (`isX(value)`, 1 arg each)

`isArray`, `isArrayBuffer`, `isBigDecimal`, `isBigFloat`, `isBigInt`, `isBool`,
`isConstructor`, `isEmptyString`, `isError`, `isException`, `isExtensible`,
`isFunction`, `isInstanceOf`, `isInteger`, `isJobPending`, `isLiveObject`,
`isNull`, `isNumber`, `isObject`, `isRegisteredClass`, `isString`, `isSymbol`,
`isUncatchableError`, `isUndefined`, `isUninitialized`.

## Constants

The module also exports a large set of `errno`, signal, `O_*`, `S_*`, `F_*`,
`AT_*`, `RWF_*` and similar numeric constants.
