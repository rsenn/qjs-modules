# path

Source: `quickjs-path.c` — module exports a flat list of functions.

Filesystem path manipulation (Node `path`-like, with extra POSIX helpers). Most
functions take a path string as the first argument.

The Node functions (`basename`, `dirname`, `extname`, `format`, `isAbsolute`, `join`, `normalize`, `parse`,
`relative`, `resolve`) throw `TypeError` with `code: 'ERR_INVALID_ARG_TYPE'` and Node's message for a
non-string argument (`basename` also for a non-string `suffix`, `relative` also for a missing `to`).
The other functions coerce their arguments to strings.

`toNamespacedPath(path)` returns `path` unchanged (POSIX); `_makeLong` is the same function. `path/posix`
and `node:path/posix` import this module.

## Components & names

| Function | Args | Description |
| --- | --- | --- |
| `basename(path)` | 1 | Final path component. |
| `dirname(path)` | 1 | Directory portion of the path. |
| `extname(path)` | 1 | File extension (with dot). |
| `skip(path, pos?)` | 1 | Offset after the component at `pos` and its separators, `-1` at the end. |

## Filesystem queries

| Function | Args | Description |
| --- | --- | --- |
| `exists(path)` | 1 | Whether the path exists. Follows symlinks like `fs.existsSync`: a dangling link is `false`. |
| `isAbsolute(path)` | 1 | Whether the path starts at the root; `!isAbsolute(p)` for a relative one. |
| `isDirectory` / `isFile` / `isSymlink` | 1 | Type predicates on the `S_IF*` bits. `isDirectory` and `isFile` follow symlinks (`stat`, like `fs.statSync`), `isSymlink` tests the link itself (`lstat`). `false` if the entry is missing. Other types: `fs.statSync(p).isFIFO()` and the like. |
| `getcwd(buf)` | 1 | Current working directory. |
| `gethome(buf)` | 1 | Home directory. |
| `fnmatch(pattern)` | 1 | Glob-style pattern match. |

## Normalization (return newly built strings)

| Function | Args | Description |
| --- | --- | --- |
| `normalize(path)` | 1 | Normalizes separators and `.`/`..`. |
| `realpath(path)` | 1 | Resolves all symlinks. |
| `relative(from, to)` | 2 | Relative path from `from` to `to`. |

## Node-style helpers

| Function | Args | Description |
| --- | --- | --- |
| `join(...parts)` | 1 | Joins components with the separator. |
| `parse(path)` | 1 | Returns `{root, dir, base, ext, name}`. |
| `format(obj)` | 1 | Inverse of `parse`. |
| `resolve(...parts)` | 1 | Resolves to an absolute path from segments. |

## Constants

`delimiter` (path-list separator) and `sep` (path separator) strings.

## posix and default export

`path.posix` and the default export are the module's own functions (`import path from
'path'`; `path.posix === path`). There is no `path.win32`. `join()` does not normalize
(`join('a', '..', 'b')` is `'a/../b'`, Node gives `'b'`) and an absolute argument restarts
the result.

## Removed

These qjsm-only functions had no user and no standard counterpart, or one that does the same:

| Removed | Use instead |
| --- | --- |
| `absolute(p)` | `resolve(p)` |
| `isRelative(p)` | `!isAbsolute(p)` |
| `readlink(p)` | `fs.readlinkSync(p)` |
| `isCharDev` `isBlockDev` `isFIFO` `isSocket` | `fs.statSync(p).isCharacterDevice()` and the like |
| `toArray(p)`, `slice(p, a, b)`, `length(p)` | `p.split('/')` and array methods |
| `at` `components` `right` `skipSeparator` `isSeparator` `getsep` `basepos` `baselen` `dirlen` `extpos` `extlen` `canonical` `isin` `equal` `search` | none |
