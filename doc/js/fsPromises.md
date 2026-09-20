# fsPromises

Source: `lib/fsPromises.js` (pure JS)

Promise-based filesystem API mirroring Node's `fs/promises`. Every export
listed here is a real implementation (each wraps the matching `*Sync`
function from [`fs`](fs.md)) — none of them are stubs. This is also why
`fs` itself has no promise API of its own: Node/Bun's non-`Sync` names on
`fs` are callback-based, and mixing that with a promise-returning function
of the same name would make `fs` incompatible with either convention.

## Exports

All functions return promises (`watch()`/`reader()` return an async
iterable/iterator instead).

| Function | Args | Description |
| --- | --- | --- |
| `open(filename, flags='r', mode=0o644)` | 1–3 | Opens a file, resolving to a `FileHandle`-like object whose methods (`read`, `write`, `readFile`, `writeFile`, `appendFile`, `stat`, `chmod`, `chown`, `truncate`, `utimes`, `sync`, `datasync`, `close`) all return promises. |
| `readFile(file, options)` | 1–2 | Reads an entire file. |
| `writeFile(file, data, options)` | 2–3 | Writes an entire file. |
| `appendFile(path, data, options)` | 2–3 | Appends to a file, creating it if needed. |
| `access(pathname, mode)` | 2 | Checks accessibility. |
| `stat(path)` / `lstat(path, options)` | 1–2 | File status. |
| `readdir(path, options)` | 1–2 | Lists a directory (`withFileTypes`, `recursive`). |
| `opendir(path, options)` | 1–2 | Opens a directory as an iterable `Dir` (see `fs.md`). |
| `mkdir(path, options=0o777)` | 1–2 | Creates a directory; `options` is a mode number or `{recursive, mode}`. |
| `mkdtemp(prefix, options)` | 1–2 | Creates a fresh, uniquely-named directory under `prefix`. |
| `rm(path, options)` | 1–2 | Removes a file, or (with `{recursive: true}`) a directory tree; `{force: true}` ignores a missing path. |
| `rmdir(path, options)` | 1–2 | Removes an empty directory. |
| `unlink(path)` | 1 | Removes a file. |
| `rename(oldname, newname)` | 2 | Renames. |
| `truncate(path, len)` | 2 | Truncates. |
| `copyFile(src, dest, mode)` | 2–3 | Copies a file. |
| `cp(src, dest, options)` | 2–3 | Copies a file, or (with `{recursive: true}`) a directory tree. |
| `link(existingPath, newPath)` / `symlink(target, path)` | 2 | Creates links. |
| `readlink(path)` / `realpath(path)` | 1 | Resolves links. |
| `chmod(path, mode)` | 2 | Changes mode (same fd-based approach as `fs.chmodSync` — see `fs.md`). |
| `chown(path, uid, gid)` | 3 | Changes ownership (same caveat as `chmod`). |
| `utimes(path, atime, mtime)` | 3 | Sets access/modification times. |
| `watch(filename, options)` | 1–2 | Returns an `AsyncIterable<{eventType, filename}>` (see below). |

### Not implemented: `lchmod`, `lchown`, `lutimes`

`lchmod` doesn't exist here at all, matching upstream Node (removed except on
macOS). `lchown(path, uid, gid)` and `lutimes(path, atime, mtime)` exist but
always reject with "not implemented": they need the non-symlink-following
`lchown(2)`/`lutimes(2)` (or `utimensat(2)` with `AT_SYMLINK_NOFOLLOW`)
syscalls, and no native binding exposes those (only the fd-based
`fchown`/`futimes`, which — via `open()` — follow a symlink instead of
operating on it). See `BUGS`'s `fspromises-lchown-lutimes-unimplemented`.

### Non-standard: `read`, `write`, `reader`, `readAll`

These four are **not** part of Node/Bun's real `fs/promises` API, which has
no top-level raw-fd read/write functions at all (only `FileHandle` methods).
They exist here because [`lib/vfs.js`](vfs.md)'s `UnionFS.prototype` mixes
them in alongside the `*Sync` equivalents.

| Function | Args | Description |
| --- | --- | --- |
| `read(fd, buf, offset, length)` | 4 | Reads into a buffer from a raw fd. |
| `write(fd, buf, offset, length)` | 4 | Writes from a buffer to a raw fd. |
| `reader(input, bufferOrBufSize=1024)` | 1–2 | Async-iterable chunk reader over a raw fd. |
| `readAll(input, bufSize=1024)` | 1–2 | Reads a raw fd to completion, decoded as UTF-8. |

**Known issue:** none of these four can currently complete a real
read-to-EOF — the final read that's supposed to observe EOF hangs forever,
because the runtime's `os.setReadHandler()` never fires for a file
descriptor whose only reason to be readable is EOF (as opposed to actual
data arriving). See `BUGS`'s
`os-setreadhandler-never-fires-for-eof-only-readiness`. `read()`/`write()`
work fine for a bounded, known byte count (they don't need to observe EOF);
`reader()`/`readAll()` only work up until the final chunk.

### `watch(filename, options)`

Node-compatible `fsPromises.watch()`, wrapping `fs.watch()`'s `FSWatcher`
as an async iterator instead of a callback:

```js
import { watch } from 'fsPromises';

for await(const { eventType, filename } of watch('./some-dir')) {
  console.log(eventType, filename);
}
```

`options.signal` (an `AbortSignal`) ends the iteration when aborted,
closing the underlying watcher.
