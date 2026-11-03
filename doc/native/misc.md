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
| `ttyWrite(out, data)` | 2 | Writes VT/ANSI output (string or ArrayBuffer of UTF-8) to `out`, a std FILE object (through its buffer, then flushed) or an fd. A Windows console without VT processing gets the sequences carried out with console calls (see below); everything else gets the bytes as they are. Returns `true` when all was written. |
| `ttyRead(fd, buffer, offset, length)` | 4 | Like `os.read`, returns the byte count, 0 at the end, or a negative errno. On a Windows console it reads the pending input events without blocking: keys become UTF-8 and the escape sequences a VT terminal sends, other events (resize, focus, mouse) are dropped, and `-EAGAIN` means no key was pending. `length` must be at least 16 there. |
| `ttyGetWinSize(fd)` | 1 | `[columns, rows]` of the visible window, `null` when `fd` is no terminal. Unlike `os.ttyGetWinSize` on Windows, which reports the scrollback buffer (e.g. 120×9001). A console input fd reports stdout's window. |
| `ttySetRaw(fd, restore)` | 1 | Raw mode for input `fd`: no echo, no line buffering, no signal keys. `restore` puts back the settings from before the first call. On Windows it also switches both code pages to UTF-8, and for fd 0 tries VT processing on fd 1. It restores modes and code pages at exit too. Returns `true` when the output does VT itself (always on Unix), `false` when `ttyWrite` translates. Throws when `fd` is no tty/console. |
| `clearScreen(out, mode)` / `clearLine(out, mode)` | 1 | ANSI erase through `ttyWrite`: mode 0 to the end, 1 to the start, 2 all. `out` as for `ttyWrite`, default 1. |
| `setCursorPosition(out, x, y)` / `moveCursor(out, dx, dy)` | 1 | Cursor control through `ttyWrite`, 0-based; `out` as for `ttyWrite`. |
| `setTextAttribute` / `setTextColor` | 2 | ANSI text styling. |
| `setConsoleMode` / `getConsoleMode` | 1–2 | Console mode (Windows). |

What `ttyWrite` carries out on a Windows console without VT processing (`src/win32/vtconsole.c`,
parsed by `src/vtparse.c`); coordinates are relative to the visible window. Any other sequence is
dropped, never printed:

| Sequence | Effect |
| --- | --- |
| text, CR, LF, BS, HT, BEL | `WriteConsoleW` (UTF-8 converted, a character split across writes is kept) |
| `CSI n A/B/C/D`, `CSI n E/F`, `CSI n G`, `CSI n d`, `CSI r;c H`/`f` | cursor moves |
| `CSI n J`, `CSI n K`, `CSI n X` | erase in display, in line, n characters (current colors) |
| `CSI ... m` | SGR: 0 1 4 7 22 24 27, 30–37, 39, 40–47, 49, 90–97, 100–107; 38/48 with `5;n` or `2;r;g;b` map to the nearest of the 16 console colors |
| `CSI ?25 h`/`l` | cursor shown/hidden |
| `ESC 7`/`ESC 8`, `CSI s`/`CSI u` | cursor saved/restored |
| `ESC c` | reset colors, clear the window |
| OSC, DCS and other strings | consumed |

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

## Export list

Generated by `utilities/doc-api.js` from the `misc` dump of `utilities/api-surface.sh`: 423 members. Edit the text above this block, not the block.

**Functions:** `_get_osfhandle()`, `_open_osfhandle()`, `access()`, `and()`, `arrayToBitfield()`, `atexit()`, `atob()`, `atomToString()`, `atomToValue()`, `atos()`, `awaitSync()`, `bitfieldSet()`, `bitfieldToArray()`, `bits()`, `btoa()`, `byteSwap()`, `charCode()`, `charLength()`, `chmod()`, `chown()`, `classAtom()`, `classId()`, `className()`, `clearLine()`, `clearScreen()`, `compareArrayBuffer()`, `concatArrayBuffer()`, `copyArrayBuffer()`, `daemon()`, `dequote()`, `dupArrayBuffer()`, `dupAtom()`, `enqueueJob()`, `error()`, `escape()`, `evalBinary()`, `exec()`, `fchmod()`, `fchown()`, `fcntl()`, `fdatasync()`, `findAtom()`, `fmemopen()`, `fork()`, `freeAtom()`, `fstat()`, `fsync()`, `ftruncate()`, `futimes()`, `getCommandLine()`, `getEnvironment()`, `getExecutable()`, `getFileDescriptor()`, `getPerformanceCounter()`, `getProcMaps()`, `getProcMounts()`, `getProcStat()`, `getPrototypeChain()`, `getRelease()`, `getRootDirectory()`, `getScreenSize()`, `getTypeId()`, `getTypeName()`, `getTypeStr()`, `getWorkingDirectory()`, `getegid()`, `geteuid()`, `getgid()`, `getpid()`, `getppid()`, `getsid()`, `getuid()`, `glob()`, `hrtime()`, `immutableClass()`, `importSync()`, `initgroups()`, `isArray()`, `isArrayBuffer()`, `isBigDecimal()`, `isBigFloat()`, `isBigInt()`, `isBool()`, `isConstructor()`, `isEmptyString()`, `isError()`, `isException()`, `isExtensible()`, `isFunction()`, `isInstanceOf()`, `isInteger()`, `isJobPending()`, `isLiveObject()`, `isNull()`, `isNumber()`, `isObject()`, `isRegisteredClass()`, `isString()`, `isSymbol()`, `isUncatchableError()`, `isUndefined()`, `isUninitialized()`, `kill()`, `lchown()`, `lineStarts()`, `link()`, `linkat()`, `lutimes()`, `mkstemp()`, `moveCursor()`, `not()`, `objectClassId()`, `objectOpaque()`, `objectRefCount()`, `or()`, `preadv()`, `preadv2()`, `promiseResult()`, `promiseState()`, `pwritev()`, `pwritev2()`, `quote()`, `rand()`, `randb()`, `randi()`, `readObject()`, `readv()`, `searchArrayBuffer()`, `searchArrayBufferLast()`, `setCursorPosition()`, `setTextAttribute()`, `setTextColor()`, `setegid()`, `seteuid()`, `setgid()`, `setgroups()`, `setsid()`, `setuid()`, `signame()`, `signum()`, `sliceArrayBuffer()`, `srand()`, `stoa()`, `strcmp()`, `symlink()`, `symlinkat()`, `tempnam()`, `toArrayBuffer()`, `toPointer()`, `toString()`, `truncate()`, `ttyGetWinSize()`, `ttyRead()`, `ttySetRaw()`, `ttyWrite()`, `typeFlag()`, `typeName()`, `typeString()`, `umask()`, `uname()`, `unescape()`, `unlink()`, `utime()`, `utimes()`, `valuePointer()`, `valueTag()`, `valueToAtom()`, `valueType()`, `vfork()`, `watch()`, `wordexp()`, `writeObject()`, `writev()`, `xor()`

**Values:** `AT_EMPTY_PATH = 4096`, `AT_SYMLINK_FOLLOW = 1024`, `BACKGROUND_BLUE = 16`, `BACKGROUND_GREEN = 32`, `BACKGROUND_INTENSITY = 128`, `BACKGROUND_RED = 64`, `CLOCK_BOOTTIME = 7`, `CLOCK_BOOTTIME_ALARM = 9`, `CLOCK_MONOTONIC = 1`, `CLOCK_MONOTONIC_COARSE = 6`, `CLOCK_MONOTONIC_RAW = 4`, `CLOCK_PROCESS_CPUTIME_ID = 2`, `CLOCK_REALTIME = 0`, `CLOCK_REALTIME_ALARM = 8`, `CLOCK_REALTIME_COARSE = 5`, `CLOCK_TAI = 11`, `CLOCK_THREAD_CPUTIME_ID = 3`, `COMMON_LVB_REVERSE_VIDEO = 16384`, `E2BIG = 7`, `EACCES = 13`, `EADDRINUSE = 98`, `EADDRNOTAVAIL = 99`, `EAFNOSUPPORT = 97`, `EAGAIN = 11`, `EALREADY = 114`, `EBADF = 9`, `EBADMSG = 74`, `EBUSY = 16`, `ECANCELED = 125`, `ECHILD = 10`, `ECONNABORTED = 103`, `ECONNREFUSED = 111`, `ECONNRESET = 104`, `EDEADLK = 35`, `EDESTADDRREQ = 89`, `EDOM = 33`, `EDQUOT = 122`, `EEXIST = 17`, `EFAULT = 14`, `EFBIG = 27`, `EHOSTUNREACH = 113`, `EIDRM = 43`, `EILSEQ = 84`, `EINPROGRESS = 115`, `EINTR = 4`, `EINVAL = 22`, `EIO = 5`, `EISCONN = 106`, `EISDIR = 21`, `ELOOP = 40`, `EMFILE = 24`, `EMLINK = 31`, `EMSGSIZE = 90`, `EMULTIHOP = 72`, `ENAMETOOLONG = 36`, `ENETDOWN = 100`, `ENETRESET = 102`, `ENETUNREACH = 101`, `ENFILE = 23`, `ENOBUFS = 105`, `ENODATA = 61`, `ENODEV = 19`, `ENOENT = 2`, `ENOEXEC = 8`, `ENOLCK = 37`, `ENOLINK = 67`, `ENOMEM = 12`, `ENOMSG = 42`, `ENOPROTOOPT = 92`, `ENOSPC = 28`, `ENOSR = 63`, `ENOSTR = 60`, `ENOSYS = 38`, `ENOTCONN = 107`, `ENOTDIR = 20`, `ENOTEMPTY = 39`, `ENOTSOCK = 88`, `ENOTSUP = 95`, `ENOTTY = 25`, `ENXIO = 6`, `EOPNOTSUPP = 95`, `EOVERFLOW = 75`, `EPERM = 1`, `EPIPE = 32`, `EPROTO = 71`, `EPROTONOSUPPORT = 93`, `EPROTOTYPE = 91`, `ERANGE = 34`, `EROFS = 30`, `ESPIPE = 29`, `ESRCH = 3`, `ESTALE = 116`, `ETIME = 62`, `ETIMEDOUT = 110`, `ETXTBSY = 26`, `EWOULDBLOCK = 11`, `EXDEV = 18`, `FD_CLOEXEC = 1`, `FOREGROUND_BLUE = 1`, `FOREGROUND_GREEN = 2`, `FOREGROUND_INTENSITY = 8`, `FOREGROUND_RED = 4`, `F_DUPFD = 0`, `F_DUPFD_CLOEXEC = 1030`, `F_GETFD = 1`, `F_GETFL = 3`, `F_GETLK = 5`, `F_GETPATH = -1`, `F_OK = 0`, `F_RDLCK = 0`, `F_SETFD = 2`, `F_SETFL = 4`, `F_SETLK = 6`, `F_SETLKW = 7`, `F_UNLCK = 2`, `F_WRLCK = 1`, `GLOB_ABORTED = -2`, `GLOB_ALTDIRFUNC = 64`, `GLOB_APPEND = 1`, `GLOB_BRACE = 128`, `GLOB_DOOFFS = 2`, `GLOB_ERR = 4`, `GLOB_MAGCHAR = 256`, `GLOB_MARK = 8`, `GLOB_NOCHECK = 16`, `GLOB_NOESCAPE = 4096`, `GLOB_NOMAGIC = 512`, `GLOB_NOMATCH = -3`, `GLOB_NOSORT = 32`, `GLOB_NOSPACE = -1`, `GLOB_NOSYS = -4`, `GLOB_QUOTE = 1024`, `GLOB_TILDE = 2048`, `IN_ACCESS = 1`, `IN_ALL_EVENTS = 4095`, `IN_ATTRIB = 4`, `IN_CLOEXEC = 524288`, `IN_CLOSE = 24`, `IN_CLOSE_NOWRITE = 16`, `IN_CLOSE_WRITE = 8`, `IN_CREATE = 256`, `IN_DELETE = 512`, `IN_DELETE_SELF = 1024`, `IN_DONT_FOLLOW = 33554432`, `IN_EXCL_UNLINK = 67108864`, `IN_IGNORED = 32768`, `IN_ISDIR = 1073741824`, `IN_MASK_ADD = 536870912`, `IN_MODIFY = 2`, `IN_MOVE = 192`, `IN_MOVED_FROM = 64`, `IN_MOVED_TO = 128`, `IN_MOVE_SELF = 2048`, `IN_NONBLOCK = 2048`, `IN_ONESHOT = -2147483648`, `IN_ONLYDIR = 16777216`, `IN_OPEN = 32`, `IN_Q_OVERFLOW = 16384`, `IN_UNMOUNT = 8192`, `JS_EVAL_FLAG_BACKTRACE_BARRIER = 64`, `JS_EVAL_FLAG_COMPILE_ONLY = 32`, `JS_EVAL_FLAG_MASK = 232`, `JS_EVAL_FLAG_STRICT = 8`, `JS_EVAL_IS_MAIN = 4`, `JS_EVAL_TYPE_DIRECT = 2`, `JS_EVAL_TYPE_GLOBAL = 0`, `JS_EVAL_TYPE_INDIRECT = 3`, `JS_EVAL_TYPE_MASK = 3`, `JS_EVAL_TYPE_MODULE = 1`, `JS_PROMISE_FULFILLED = 1`, `JS_PROMISE_PENDING = 0`, `JS_PROMISE_REJECTED = 2`, `O_APPEND = 1024`, `O_CLOEXEC = 524288`, `O_CREAT = 64`, `O_DIRECT = 16384`, `O_DSYNC = 4096`, `O_EXCL = 128`, `O_NOATIME = 262144`, `O_NOCTTY = 256`, `O_NONBLOCK = 2048`, `O_RDONLY = 0`, `O_RDWR = 2`, `O_SYNC = 1052672`, `O_TRUNC = 512`, `O_WRONLY = 1`, `RWF_APPEND = 16`, `RWF_DSYNC = 2`, `RWF_HIPRI = 1`, `RWF_NOWAIT = 8`, `RWF_SYNC = 4`, `R_OK = 4`, `SIGABRT = 6`, `SIGALRM = 14`, `SIGBUS = 7`, `SIGCHLD = 17`, `SIGCONT = 18`, `SIGFPE = 8`, `SIGHUP = 1`, `SIGILL = 4`, `SIGINT = 2`, `SIGIO = 29`, `SIGKILL = 9`, `SIGPIPE = 13`, `SIGPOLL = 29`, `SIGPROF = 27`, `SIGPWR = 30`, `SIGQUIT = 3`, `SIGSEGV = 11`, `SIGSTKFLT = 16`, `SIGSTOP = 19`, `SIGSYS = 31`, `SIGTERM = 15`, `SIGTRAP = 5`, `SIGTSTP = 20`, `SIGTTIN = 21`, `SIGTTOU = 22`, `SIGURG = 23`, `SIGUSR1 = 10`, `SIGUSR2 = 12`, `SIGVTALRM = 26`, `SIGWINCH = 28`, `SIGXCPU = 24`, `SIGXFSZ = 25`, `TIOCCBRK = 21544`, `TIOCGPGRP = 21519`, `TIOCGWINSZ = 21523`, `TIOCINQ = 21531`, `TIOCLINUX = 21532`, `TIOCMBIC = 21527`, `TIOCMBIS = 21526`, `TIOCMGET = 21525`, `TIOCMSET = 21528`, `TIOCPKT = 21536`, `TIOCSBRK = 21543`, `TIOCSCTTY = 21518`, `TIOCSPGRP = 21520`, `TIOCSWINSZ = 21524`, `WRDE_BADCHAR = 2`, `WRDE_BADVAL = 3`, `WRDE_CMDSUB = 4`, `WRDE_NOCMD = 4`, `WRDE_NOSPACE = 1`, `WRDE_SHOWERR = 16`, `WRDE_SYNTAX = 5`, `WRDE_UNDEF = 32`, `W_OK = 2`, `X_OK = 1`, `inotify_event_size = 16`

