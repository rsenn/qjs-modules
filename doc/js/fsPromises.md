# fsPromises

Source: `lib/fsPromises.js` (pure JS)

Promise-based filesystem API mirroring Node's `fs/promises`. (Several entries are
thin stubs delegating to the synchronous [`fs`](fs.md) module.)

## Exports

All functions return promises.

| Function | Args | Description |
| --- | --- | --- |
| `open(filename, flags='r', mode=0o644)` | 1–3 | Opens a file, resolving to a handle. |
| `read(fd, buf, offset, length)` | 4 | Reads into a buffer. |
| `write(fd, buf, offset, length)` | 4 | Writes from a buffer. |
| `readFile(file, options)` | 1–2 | Reads an entire file. |
| `writeFile(file, data, overwrite=true)` | 2–3 | Writes an entire file. |
| `appendFile(path, data)` | 2 | Appends to a file. |
| `readAll(input, bufSize=1024)` | 1–2 | Reads a stream to completion. |
| `access(pathname, mode)` | 2 | Checks accessibility. |
| `stat(path)` / `lstat(path, options)` | 1–2 | File status. |
| `readdir(path)` | 1 | Lists a directory. |
| `opendir(path, options)` | 1–2 | Opens a directory. |
| `mkdir(path, mode=0o777)` | 1–2 | Creates a directory. |
| `mkdtemp(prefix, options)` | 1–2 | Creates a temp directory. |
| `rm(path, options)` / `rmdir(path, options)` | 1–2 | Removes a file / directory. |
| `unlink(path)` | 1 | Removes a file. |
| `rename(oldname, newname)` | 2 | Renames. |
| `truncate(path, len)` | 2 | Truncates. |
| `copyFile(src, dest, mode)` / `cp(src, dest, mode)` | 2–3 | Copies. |
| `link(existingPath, newPath)` / `symlink(target, path)` | 2 | Creates links. |
| `readlink(path)` / `realpath(path)` | 1 | Resolves links. |
| `chmod(path, mode)` / `lchmod(path, mode)` | 2 | Changes mode. |
| `chown(path, uid, gid)` / `lchown(path, uid, gid)` | 3 | Changes ownership. |
| `utimes(path, atime, mtime)` / `lutimes(path, atime, mtime)` | 3 | Sets timestamps. |
| `watch(filename, options, callback)` | 1–3 | Watches a path for changes. |
