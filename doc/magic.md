# magic

Source: `quickjs-magic.c` — module export: **`Magic`**

Bindings to `libmagic` (the `file(1)` library) for content-type detection. The
instance is also callable.

## Constructor

```js
new Magic([flags])   // length 1
```

## Methods

| Method | Args | Description |
| --- | --- | --- |
| `descriptor(fd)` | 1 | Identifies the content read from a file descriptor. |
| `file(path)` | 1 | Identifies the content of a file by path. |
| `buffer(data)` | 1 | Identifies the content of an in-memory buffer. |
| `getflags()` | 0 | Returns the active flags. |
| `setflags(flags)` | 1 | Sets the active flags (`Magic.MIME`, etc.). |
| `check(path)` | 1 | Checks the validity of a magic database. |
| `compile(path)` | 1 | Compiles a magic database. |
| `list(path)` | 1 | Lists entries in a magic database. |
| `load([path])` | 0 | Loads a magic database. |
| `getparam(param)` | 2 | Reads a libmagic parameter (`Magic.PARAM_*`). |
| `setparam(param, value)` | 2 | Sets a libmagic parameter. |

## Properties (read-only)

| Property | Description |
| --- | --- |
| `error` | Last error string. |
| `errno` | Last error number. |
| `version` | libmagic version. |

## Static constants

Flags: `NONE`, `DEBUG`, `SYMLINK`, `COMPRESS`, `DEVICES`, `MIME_TYPE`,
`CONTINUE`, `CHECK`, `PRESERVE_ATIME`, `RAW`, `ERROR`, `MIME_ENCODING`, `MIME`,
`APPLE`, `EXTENSION`, `COMPRESS_TRANSP`, `NODESC`, and the `NO_CHECK_*` family.

Parameters: `PARAM_INDIR_MAX`, `PARAM_NAME_MAX`, `PARAM_ELF_PHNUM_MAX`,
`PARAM_ELF_SHNUM_MAX`, `PARAM_ELF_NOTES_MAX`, `PARAM_REGEX_MAX`,
`PARAM_BYTES_MAX`. Also `VERSION` and `DEFAULT_DB`.
