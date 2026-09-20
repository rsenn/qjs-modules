# fs

Source: `lib/fs.js` (pure JS) — has a `default` export object bundling the API

A Node-`fs`-style filesystem module with synchronous and stream APIs, built on
QuickJS `std`/`os`. All promise-returning functions live in the separate
[`fsPromises`](fsPromises.md) module (matching Node/Bun, where non-`Sync`
names on `fs` are callback-based, not promise-based) — `fs` itself has no
promise API of its own.

## Standard streams & helpers

`stdin`, `stdout`, `stderr` (constants). Buffer helpers: `buffer(length)`,
`bufferFrom(chunk, offset, length)`, `bufferSize(buf)`, `bufferArgument(arg)`,
`bufferArguments(arg, offset, length)`, `stringOrBufferArguments(arg, …)`,
`bufferToString(buf, offset, length)`, `throwIfNull(error, fn, …)`.

## Classes

| Class | Description |
| --- | --- |
| `Stats` | `stat`-result object (`isFile()`, `isDirectory()`, size, times, mode, …). |
| `Dirent` | `readdir`/`opendir`-entry object (`isFile()`, `isDirectory()`, `name`, …). |
| `FileHandle` | Open-file handle with read/write/close methods (sync here; see `fsPromises`'s `open()` for the promise-returning wrapper). |
| `Dir` | Directory handle returned by `opendirSync()` — iterable, plus `readSync()`/`closeSync()`. |
| `inotify_event` | `ArrayBuffer` subclass decoding an inotify event. |

## Synchronous API

| Function | Args | Description |
| --- | --- | --- |
| `openSync(filename, flags='r', mode=0o644)` | 1–3 | Opens a file → fd. |
| `fopenSync(filename, flags, mode)` / `fdopenSync(fd, flags)` | 1–3 | Open as a `FILE`. |
| `closeSync(fd)` | 1 | Closes an fd. |
| `readSync(fd, buffer, offset, length)` | 4 | Reads into a buffer. |
| `writeSync(fd, buffer, offset, length)` | 4 | Writes from a buffer. |
| `readFileSync(file, options)` | 1–2 | Reads a whole file. |
| `writeFileSync(file, data, options)` | 2–3 | Writes a whole file. |
| `appendFileSync(path, data, options)` | 2–3 | Appends to a file, creating it if needed. |
| `readdirSync(path, options)` | 1–2 | Lists a directory (`withFileTypes`, `recursive`). |
| `opendirSync(path)` | 1 | Opens a directory as an iterable `Dir`. |
| `statSync` / `lstatSync(path)` | 1 | File status. |
| `existsSync(path)` | 1 | Existence test. |
| `mkdirSync(path, options=0o777)` | 1–2 | Creates a directory; `options` is a mode number or `{recursive, mode}`. |
| `mkdtempSync(prefix)` | 1 | Creates a fresh, uniquely-named directory under `prefix`. |
| `renameSync(old, new)` | 2 | Renames. |
| `unlinkSync(path)` | 1 | Removes a file. |
| `rmdirSync(path)` | 1 | Removes an empty directory. |
| `rmSync(path, options)` | 1–2 | Removes a file or (with `{recursive: true}`) a directory tree; `{force: true}` ignores a missing path. |
| `copyFileSync(src, dest, flags)` | 2–3 | Copies a file. |
| `cpSync(src, dest, options)` | 2–3 | Copies a file, or (with `{recursive: true}`) a directory tree. |
| `linkSync` / `symlinkSync` | 2 | Creates links. |
| `readlinkSync` / `realpathSync(path)` | 1 | Resolves links. |
| `accessSync(pathname, mode)` | 2 | Accessibility check. |
| `chmodSync(path, mode)` / `chownSync(path, uid, gid)` | 2–3 | Changes mode/ownership (opens the path and uses the fd-based syscall — no path-based binding is exposed). |
| `truncateSync(path, len=0)` | 1–2 | Truncates a file (same fd-based approach as `chmodSync`/`chownSync`). |
| `utimesSync(path, atime, mtime)` | 3 | Sets access/modification times (`Date` or seconds-since-epoch). |
| `seek(fd, offset, whence)` / `tell(file)` | 1–3 | File-position control. |
| `sizeSync(file)` / `nameSync(file)` / `fileno(file)` | 1 | File metadata. |
| `getcwd()` / `chdir(path)` / `isatty(file)` | 0–1 | Process/file helpers. |
| `tmpfileSync()` / `mkstempSync(template)` / `tempnamSync(dir, pfx)` | 0–2 | Temp files. |
| `flushSync(f)` / `pipe()` | 0–1 | Flush / create a pipe. |
| `puts(fd, str)` / `gets(fd)` | 1–2 | Line write / read. |

There's no path-based `chmod(2)`/`chown(2)`/`truncate(2)` binding available (only
the fd-based `fchmod`/`fchown`/`ftruncate` used by `FileHandle`), so the `*Sync`
versions above open the path (read-only for `chmodSync`/`chownSync`, write-only
for `truncateSync`) and use those instead — this doesn't work for a symlink's
own metadata (that needs the non-following `lchmod`/`lchown`/`lutimes` syscalls,
which aren't implemented; see `BUGS`).

## Streaming readers

| Function | Args | Description |
| --- | --- | --- |
| `readerSync(input, bufferOrBufSize=1024)` | 1–2 | Generator of chunks. |
| `readAllSync(input, bufSize)` | 1–2 | Reads everything (sync). |
| `readFullySync(input, buf, start, n)` | 2–4 | Fills a buffer fully. |
| `createReadStream(path, options)` | 1–2 | Node-style readable stream. |
| `createWriteStream(path, options)` | 1–2 | Node-style writable stream. |

The promise-based `reader()`/`readAll()` (async-iterator/fd equivalents of the
two above) live in [`fsPromises`](fsPromises.md), along with the non-standard
raw-fd `read()`/`write()` — see that module's doc for why they're there
despite not being part of Node/Bun's `fs/promises`.

## Event helpers

`onRead(file, handler)`, `waitRead(file)`, `onWrite(file, handler)`,
`waitWrite(file)`.

### `watch(filename, options, callback)`

Node-compatible `fs.watch()`, backed by inotify (via the native `misc`
module). Watches a single path (not recursive - matching Node's own
Linux behavior) and returns an `FSWatcher` (`EventEmitter`) that emits
`'change'`/`'rename'` with the affected entry's name, mirroring Node's
`(eventType, filename)` callback shape:

```js
import { watch } from 'fs';

const w = watch('./some-dir', {}, (eventType, filename) => {
  console.log(eventType, filename); // 'rename' | 'change', e.g. "foo.txt"
});

w.close(); // stop watching
```

- `options.mask` — raw inotify event mask (default: all events).
- `options.signal` — an `AbortSignal` (see the `abort` module); aborting
  it closes the watcher, same as calling `.close()`.
- The watcher emits `'error'` if the underlying inotify read fails, and
  `'close'` once, whether closed explicitly, via `options.signal`, or
  after an error.
- An invalid path throws synchronously (matching Node), rather than
  returning an error value.

See also `fsPromises.watch()` for the async-iterator form.

## Constants

`constants` (object) plus individual `FD_CLOEXEC`, `F_*` (fcntl), and `O_*`
(open) flag exports.
