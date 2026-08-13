# archive

Source: `quickjs-archive.c` — module exports **`Archive`**, **`ArchiveEntry`**, **`ArchiveMatch`**

Reading and writing archive files (wraps `libarchive`): tar, zip, cpio, and the
compression filters libarchive supports. An `Archive` is iterable over its
entries.

## Archive

```js
new Archive()   // length 1
```

### Methods

| Method | Args | Description |
| --- | --- | --- |
| `open(filename)` | 1 | Opens an archive for reading. |
| `read(buffer)` | 1 | Reads archive data from a buffer. |
| `write(filename)` | 1 | Opens an archive for writing. |
| `next()` | 0 | Advances to the next entry; returns an `ArchiveEntry`. |
| `skip()` | 0 | Skips the current entry's data. |
| `seek(offset, whence)` | 2 | Seeks within the archive. |
| `extract(flags)` | 1 | Extracts the current entry to disk. |
| `filterBytes(data)` | 1 | Runs data through the archive's filters. |
| `close()` | 0 | Closes the archive. |
| `[Symbol.iterator]()` | 0 | Iterates entries. |

### Properties

| Property | Kind | Description |
| --- | --- | --- |
| `errno` / `error` | getter | Last libarchive error code / string. |
| `format` | getter/setter | Archive format. |
| `compression` | getter | Compression method. |
| `filters` | getter | Active filter chain. |
| `position` | getter | Current byte position. |
| `readHeaderPosition` | getter | Header read position. |
| `hasEncryptedEntries` | getter | Whether encrypted entries are present. |
| `blockSize` | getter/setter | I/O block size. |
| `fileCount` | getter | Number of entries. |

### Static members

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `read(filename)` | 1 | function | Opens an archive for reading in one call. |
| `write(filename)` | 1 | function | Opens an archive for writing in one call. |
| `version` | — | getter | libarchive version. |

## ArchiveEntry

```js
new ArchiveEntry()   // length 1
```

`clone()` plus read/write metadata accessors: `atime`, `ctime`, `mtime`,
`birthtime`, `dev`, `devmajor`, `devminor`, `rdev`, `rdevmajor`, `rdevminor`,
`filetype`, `type`, `fflags`, `uid`, `gid`, `ino`, `nlink`, `pathname`
(enumerable), `uname`, `gname`, `mode`, `perm`, `size` (enumerable), `symlink`,
`hardlink`, `link`, `isDataEncrypted`, `isMetadataEncrypted`, `isEncrypted`.

## ArchiveMatch

```js
new ArchiveMatch()   // length 1
```

`include(pattern)` and `exclude(pattern)` build path-matching filters for
selective extraction.
