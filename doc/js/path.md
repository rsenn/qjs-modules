# path

Source: `quickjs-path.c` — module exports a flat list of functions.

Filesystem path manipulation (Node `path`-like, with extra POSIX helpers). Most
functions take a path string as the first argument.

## Components & names

| Function | Args | Description |
| --- | --- | --- |
| `basename(path)` | 1 | Final path component. |
| `basepos(path)` | 1 | Character offset where the basename begins. |
| `baselen(path)` | 1 | Length of the basename. |
| `dirname(path)` | 1 | Directory portion of the path. |
| `dirlen(path)` | 1 | Length of the directory portion. |
| `extname(path)` | 1 | File extension (with dot). |
| `extpos(path)` | 1 | Offset of the extension. |
| `extlen(path)` | 1 | Length of the extension. |
| `length(path)` | 1 | Number of path components. |
| `components(path)` | 1 | Splits into an array of components. |
| `at(path, index)` | 2 | Returns the component at `index`. |
| `right(path)` | 1 | Tail portion of the path. |
| `skip(path)` | 1 | Skips the leading component. |
| `skipSeparator(path)` | 1 | Skips a leading separator. |
| `isSeparator(ch)` | 1 | Whether a character is a path separator. |

## Filesystem queries

| Function | Args | Description |
| --- | --- | --- |
| `exists(path)` | 1 | Whether the path exists. |
| `isAbsolute(path)` / `isRelative(path)` | 1 | Path kind. |
| `isDirectory` / `isFile` / `isCharDev` / `isBlockDev` / `isFIFO` / `isSocket` / `isSymlink` | 1 | `stat`-based type predicates. |
| `getcwd(buf)` | 1 | Current working directory. |
| `gethome(buf)` | 1 | Home directory. |
| `getsep(buf)` | 1 | Platform path separator. |
| `readlink(path)` | 1 | Reads a symbolic link target. |
| `fnmatch(pattern)` | 1 | Glob-style pattern match. |

## Normalization (return newly built strings)

| Function | Args | Description |
| --- | --- | --- |
| `absolute(path)` | 1 | Resolves to an absolute path. |
| `canonical(path)` | 1 | Canonical form (resolving `.`/`..`). |
| `normalize(path)` | 1 | Normalizes separators and `.`/`..`. |
| `realpath(path)` | 1 | Resolves all symlinks. |
| `search(path, list)` | 2 | Searches for a file across a path list. |
| `relative(from, to)` | 2 | Relative path from `from` to `to`. |

## Node-style helpers

| Function | Args | Description |
| --- | --- | --- |
| `slice(path, start, end)` | 0 | Sub-range of components. |
| `join(...parts)` | 1 | Joins components with the separator. |
| `parse(path)` | 1 | Returns `{root, dir, base, ext, name}`. |
| `format(obj)` | 1 | Inverse of `parse`. |
| `resolve(...parts)` | 1 | Resolves to an absolute path from segments. |

## Constants

`delimiter` (path-list separator) and `sep` (path separator) strings.
