#ifndef GETDENTS_H
#define GETDENTS_H

#include <stddef.h>
#include <stdint.h>

/**
 * \defgroup getdents getdents: Fast directory entry reader
 *
 * backends:
 *   - Linux/Android: raw getdents64(2) records, same ABI at any pointer width
 *   - Windows: FindFirstFile/FindNextFile
 *   - other POSIX (macOS, BSD, Solaris, WASI): opendir()/readdir(); Linux
 *     uses it too when built with -DGETDENTS_PORTABLE
 *
 * guarantees, on every backend:
 *   - each entry is returned once, in on-disk order, "." and ".." included
 *     (skip them with getdents_isdots())
 *   - POSIX skips entries whose inode number is 0 (deleted)
 *   - borrowed: a DirEntry* and the pointers taken from it are valid until
 *     the next getdents_read() or getdents_close() on that Directory
 *   - getdents_type() is exact: a DT_UNKNOWN entry is resolved inside
 *     getdents_read() with fstatat(AT_SYMLINK_NOFOLLOW), one extra system
 *     call per entry, only on file systems that report DT_UNKNOWN
 * @{
 */
typedef struct getdents_reader Directory;
typedef struct getdents_entry DirEntry;

enum {
  TYPE_REG = (1 << 0),
  TYPE_DIR = (1 << 1),
  TYPE_LNK = (1 << 2),
  TYPE_BLK = (1 << 3),
  TYPE_CHR = (1 << 4),
  TYPE_FIFO = (1 << 5),
  TYPE_SOCK = (1 << 6),
  TYPE_MASK = (TYPE_REG | TYPE_BLK | TYPE_CHR | TYPE_DIR | TYPE_FIFO | TYPE_LNK | TYPE_SOCK),
};

/* size of a Directory, for callers that allocate it themselves.
 *
 * the 8 KiB read buffer is not part of it: getdents_read() allocates it on
 * first use and getdents_close() frees it.
 */
size_t getdents_size(void);

/* underlying descriptor (POSIX fd, Windows search handle).
 *
 *   returns ptrdiff_t  the descriptor, or -1 when not open
 */
ptrdiff_t getdents_handle(Directory*);

/* allocates a cleared Directory.
 *
 *   returns Directory*  NULL with errno set on failure
 *
 * there is no free function: getdents_close() it, then free() it.
 */
Directory* getdents_new(void);

/* puts a Directory into the "not open" state.
 *
 * it closes and frees nothing, so call it only on memory that does not hold
 * an open Directory.
 */
void getdents_clear(Directory*);

/* opens the directory at `path`, close-on-exec.
 *
 *   const char*  path  directory to open; symlinks are followed
 *
 *   returns int  0, or -1 with errno set (ENOTDIR if `path` is a file)
 *
 * Windows: errno is not set.
 */
int getdents_open(Directory*, const char* path);

/* takes ownership of an already-open directory descriptor.
 *
 *   intptr_t  fd  directory descriptor; getdents_close() closes it
 *
 *   returns int   0, or -1 with errno set (ENOTDIR if `fd` is not a
 *                 directory); `fd` then still belongs to the caller
 */
int getdents_adopt(Directory*, intptr_t fd);

/* opens a directory relative to a descriptor: no PATH_MAX, no symlink race.
 *
 *   intptr_t     dirfd  directory to resolve `name` in, or AT_FDCWD
 *   const char*  name   entry to open, O_NOFOLLOW|O_CLOEXEC
 *
 *   returns int  0, or -1 with errno set (ELOOP/ENOTDIR for a symlink)
 *
 * Windows: always -1 with errno ENOSYS.
 */
int getdents_openat(Directory*, intptr_t dirfd, const char* name);

/* returns the next entry of the directory.
 *
 *   returns DirEntry*  the entry, or NULL at the end or on error
 *
 * errno is 0 after a clean end and the cause (EIO, EBADF, ...) after an
 * error; test it right after the NULL. Windows cannot tell them apart and
 * always leaves errno 0.
 */
DirEntry* getdents_read(Directory*);

/* entry name in the platform encoding: bytes on POSIX, wchar_t on Windows.
 *
 * borrowed: NUL-terminated, valid until the next getdents_read().
 */
const void* getdents_cname(const DirEntry*);

/* copy of the entry name.
 *
 *   returns char*  malloc()ed, free() it: UTF-8 on Windows, bytes on POSIX
 */
char* getdents_name(const DirEntry*);

/* raw name bytes of the entry.
 *
 *   size_t*  len  receives the length in bytes, may be NULL
 *
 *   returns const uint8_t*  name, borrowed until the next getdents_read()
 *
 * Windows: UTF-16 code units, so `*len` counts 2 bytes per unit.
 */
const uint8_t* getdents_namebuf(const DirEntry*, size_t* len);

/* name length in bytes, the value getdents_namebuf() stores in `*len`. */
size_t getdents_namelen(const DirEntry*);

/* inode number of the entry; 0 on Windows, which has none. */
uint64_t getdents_ino(const DirEntry*);

/* tells whether the entry is "." or "..": 1 if so, else 0. */
int getdents_isdots(const DirEntry*);

/* file type of the entry.
 *
 *   returns int  one TYPE_* flag, or 0 if the type is unknown
 */
int getdents_type(const DirEntry*);

/* closes the descriptor and frees the read buffer.
 *
 * safe to call twice, and on a Directory that was never opened.
 */
void getdents_close(Directory*);

/* tells whether nothing has been read yet, the same on every backend.
 *
 *   returns int  1 until the first getdents_read() after open/adopt, then 0
 */
int getdents_initialized(Directory* d);

/* type predicates, each 0 or 1.
 *
 * Windows has no fifo, socket or block-device entries; its
 * FILE_ATTRIBUTE_DEVICE entries count as chr.
 */
int getdents_isblk(const DirEntry*);
int getdents_ischr(const DirEntry*);
int getdents_isdir(const DirEntry*);
int getdents_isfifo(const DirEntry*);
int getdents_islnk(const DirEntry*);
int getdents_isreg(const DirEntry*);
int getdents_issock(const DirEntry*);
int getdents_isunknown(const DirEntry*);

/**
 * @}
 */
#endif /* defined(GETDENTS_H) */
