# mmap

Source: `quickjs-mmap.c` — module exports a flat list of functions.

Thin bindings around the POSIX memory-mapping syscalls. Mapped regions are
returned as `ArrayBuffer`s.

## Functions

| Function | Args | Description |
| --- | --- | --- |
| `mmap(length, prot, flags, fd, offset)` | 2 | Maps memory and returns it as an `ArrayBuffer`. Uses `PROT_*` and `MAP_*` flags. |
| `munmap(buffer)` | 1 | Unmaps a previously mapped region. |
| `msync(buffer, flags, ...)` | 3 | Flushes changes in a mapping back to the backing store (`MS_*` flags). |
| `mprotect(buffer, prot, ...)` | 3 | Changes the protection (`PROT_*`) of a mapping. |
| `filename(buffer)` | 1 | Returns the file name backing a mapping, if any. |
| `toString(buffer)` | 1 | Renders a description of a mapping. |

## Constants

Protection: `PROT_READ`, `PROT_WRITE`, `PROT_EXEC`, `PROT_NONE`,
`PROT_GROWSDOWN`, `PROT_GROWSUP`.

Mapping flags: `MAP_SHARED`, `MAP_PRIVATE`, `MAP_TYPE`, `MAP_FIXED`,
`MAP_ANONYMOUS`, `MAP_GROWSDOWN`, `MAP_DENYWRITE`, `MAP_EXECUTABLE`,
`MAP_LOCKED`, `MAP_NORESERVE`, `MAP_POPULATE`, `MAP_NONBLOCK`, `MAP_STACK`,
`MAP_HUGETLB`, `MAP_FAILED`.

Sync flags: `MS_ASYNC`, `MS_INVALIDATE`, `MS_SYNC`. Plus the `errno` values
`EBUSY`, `EFAULT`, `EINVAL`, `ENOMEM` where available.
