# fs

Source: `lib/fs.js` (pure JS) — has a `default` export object bundling the API

A Node-`fs`-style filesystem module with synchronous, promise, and stream APIs,
built on QuickJS `std`/`os`.

## Standard streams & helpers

`stdin`, `stdout`, `stderr` (constants). Buffer helpers: `buffer(length)`,
`bufferFrom(chunk, offset, length)`, `bufferSize(buf)`, `bufferArgument(arg)`,
`bufferArguments(arg, offset, length)`, `stringOrBufferArguments(arg, …)`,
`bufferToString(buf, offset, length)`, `throwIfNull(error, fn, …)`.

## Classes

| Class | Description |
| --- | --- |
| `Stats` | `stat`-result object (`isFile()`, `isDirectory()`, size, times, mode, …). |
| `FileHandle` | Open-file handle with read/write/close methods. |
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
| `readdirSync(path)` | 1 | Lists a directory. |
| `statSync` / `lstatSync(path)` | 1 | File status. |
| `existsSync(path)` | 1 | Existence test. |
| `mkdirSync(path, mode)` | 1–2 | Creates a directory. |
| `renameSync(old, new)` | 2 | Renames. |
| `unlinkSync(path)` | 1 | Removes a file. |
| `copyFileSync(src, dest, flags)` | 2–3 | Copies. |
| `linkSync` / `symlinkSync` | 2 | Creates links. |
| `readlinkSync` / `realpathSync(path)` | 1 | Resolves links. |
| `accessSync(pathname, mode)` | 2 | Accessibility check. |
| `seek(fd, offset, whence)` / `tell(file)` | 1–3 | File-position control. |
| `sizeSync(file)` / `nameSync(file)` / `fileno(file)` | 1 | File metadata. |
| `getcwd()` / `chdir(path)` / `isatty(file)` | 0–1 | Process/file helpers. |
| `tmpfileSync()` / `mkstempSync(template)` / `tempnamSync(dir, pfx)` | 0–2 | Temp files. |
| `flushSync(f)` / `pipe()` | 0–1 | Flush / create a pipe. |
| `puts(fd, str)` / `gets(fd)` | 1–2 | Line write / read. |

## Async / promise API

`open`, `close`, `read`, `write`, `readFile`, `copyFile`, `exists`, `stat`,
`lstat`, `mkdir`, `link`, `symlink`, `tmpfile`, `readAll`, `readFully` — promise
variants of the corresponding sync calls.

## Streaming readers

| Function | Args | Description |
| --- | --- | --- |
| `reader(input, bufferOrBufSize=1024)` | 1–2 | Async chunk reader. |
| `readerSync(input, bufferOrBufSize=1024)` | 1–2 | Generator of chunks. |
| `readAllSync(input, bufSize)` | 1–2 | Reads everything (sync). |
| `readFullySync(input, buf, start, n)` / `readFully(...)` | 2–4 | Fills a buffer fully. |
| `createReadStream(path, options)` | 1–2 | Node-style readable stream. |
| `createWriteStream(path, options)` | 1–2 | Node-style writable stream. |

## Event helpers

`onRead(file, handler)`, `waitRead(file)`, `onWrite(file, handler)`,
`waitWrite(file)`, `watch(filename, options, callback)`.

## Constants

`constants` (object) plus individual `FD_CLOEXEC`, `F_*` (fcntl), and `O_*`
(open) flag exports.
