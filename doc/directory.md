# directory

Source: `quickjs-directory.c` — module export: **`Directory`** (plus static constants)

Iterates directory entries (a wrapper over `opendir`/`readdir`). The object is
itself an iterator.

## Constructor

```js
new Directory([path])   // length 1
```

## Methods

| Method | Args | Description |
| --- | --- | --- |
| `open(path)` | 1 | Opens `path` for reading. |
| `adopt(fd)` | 1 | Wraps an already-open directory handle. |
| `close()` | 0 | Closes the directory. |
| `valueOf()` | 0 | Returns the underlying handle/descriptor. |
| `next()` | 0 | Returns the next entry (iterator protocol). |
| `return()` | 0 | Ends iteration. |
| `throw(exception)` | 1 | Injects an exception into the iteration. |
| `[Symbol.iterator]()` | 0 | Returns the directory iterator. |

## Static constants

Entry-shape flags: `NAME`, `TYPE`, `BOTH` — control whether `next()` yields the
name, the type, or both.

Entry types: `TYPE_BLK`, `TYPE_CHR`, `TYPE_DIR`, `TYPE_FIFO`, `TYPE_LNK`,
`TYPE_REG`, `TYPE_SOCK`, `TYPE_MASK`.
