/* getdents.c: the Directory reader behind quickjs-directory.c.
 * depends on: char-utils.h (Windows: utf8_towcs, utf8_fromwcs).
 * rule: getdents_read() hands out entries in place, no copy per entry. */
#define _GNU_SOURCE
#include "getdents.h"
#include "char-utils.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/**
 * \addtogroup getdents
 * @{
 */
#if defined(_WIN32) || (defined(__MSYS__) || defined(__CYGWIN__))
#include <windows.h>

#define DIRENT(d) ((find_data_type*)&(d)->fdw)

#if(defined(__MSYS__) || defined(__CYGWIN__))
#include <minwinbase.h>
#include <wchar.h>
#define FIND_W
#endif

#ifdef FIND_A
typedef WIN32_FIND_DATAA find_data_type;
typedef char find_char;
#elif defined(FIND_W)
typedef WIN32_FIND_DATAW find_data_type;
typedef wchar_t find_char;
#else
typedef struct _wfinddata64_t find_data_type;
typedef wchar_t find_char;
#endif

struct getdents_reader {
  union {
    HANDLE h_ptr;
    intptr_t h_int;
  };
  BOOL first;   /* fdw holds the FindFirst entry, not handed out yet */
  BOOL started; /* getdents_read() was called since open/adopt */
  find_data_type fdw;
};

#if defined(FIND_A) || defined(FIND_W)
#ifdef FIND_A
#define findfirst(path, st) FindFirstFileA(path, st)
#else
#define findfirst(path, st) FindFirstFileW(path, st)
#endif
#define findnext(hnd, dat) FindNextFile(hnd, dat)
#define findclose(hnd) FindClose(hnd)
#define h_find h_ptr
#define h_type HANDLE
#else
#define findfirst(path, st) _wfindfirst64(path, st)
#define findnext(hnd, dat) !_wfindnext64(hnd, dat)
#define findclose(hnd) _findclose(hnd)
#define cFileName name
#define dwFileAttributes attrib
#define h_find h_int
#define h_type intptr_t
#endif

size_t
getdents_size() {
  return sizeof(Directory);
}

void
getdents_clear(Directory* d) {
  d->h_ptr = INVALID_HANDLE_VALUE;
  d->first = FALSE;
  d->started = FALSE;
}

intptr_t
getdents_handle(Directory* d) {
  return (intptr_t)d->h_find;
}

int
getdents_open(Directory* d, const char* path) {
  size_t plen = strlen(path);
  char* p;

  d->started = FALSE;

  if(!(p = malloc(plen + 1 + 3 + 1)))
    return -1;

  memcpy(p, path, plen + 1);
  strcpy(&p[plen], "\\*.*");
  // plen += strlen(&p[plen]);

#ifdef FIND_A
  if((d->h_find = findfirst(p, &d->fdw)) != INVALID_HANDLE_VALUE)
    d->first = TRUE;
#else
  wchar_t* wp = utf8_towcs(p);

  if(!wp) {
    free(p);
    return -1;
  }

  if((d->h_find = findfirst(wp, &d->fdw)) != (h_type)-1)
    d->first = TRUE;

  free(wp);
#endif

  free(p);

  return d->h_ptr == INVALID_HANDLE_VALUE ? -1 : 0;
}

int
getdents_adopt(Directory* d, intptr_t hnd) {
  if(hnd == -1)
    return -1;

  d->h_int = hnd;
  d->first = FALSE;
  d->started = FALSE;
  return 0;
}

int
getdents_openat(Directory* d, intptr_t dirfd, const char* name) {
  errno = ENOSYS;
  return -1;
}

int
getdents_initialized(Directory* d) {
  return !d->started;
}

DirEntry*
getdents_read(Directory* d) {
  DirEntry* ret = (struct getdents_entry*)&d->fdw;

  d->started = TRUE;
  errno = 0;

  if(d->first)
    d->first = FALSE;
  else if(d->h_ptr == INVALID_HANDLE_VALUE || !findnext(d->h_find, (void*)&d->fdw))
    ret = 0;

  return ret;
}

const void*
getdents_cname(const DirEntry* e) {
  find_data_type* fdw = (void*)e;

  return fdw->cFileName;
}

char*
getdents_name(const DirEntry* e) {
  return utf8_fromwcs(getdents_cname(e));
}

const uint8_t*
getdents_namebuf(const DirEntry* e, size_t* len) {
  const wchar_t* s = ((find_data_type*)e)->cFileName;

  if(len)
    *len = wcslen(s) * sizeof(wchar_t);

  return (const uint8_t*)s;
}

uint64_t
getdents_ino(const DirEntry* e) {
  return 0;
}

int
getdents_isdots(const DirEntry* e) {
  const find_char* s = ((find_data_type*)e)->cFileName;

  return s[0] == '.' && (s[1] == 0 || (s[1] == '.' && s[2] == 0));
}

void
getdents_close(Directory* d) {
  if(d->h_ptr != INVALID_HANDLE_VALUE)
    findclose(d->h_find);

  d->h_ptr = INVALID_HANDLE_VALUE;
  d->first = FALSE;
}

int
getdents_isblk(const DirEntry* e) {
  return 0;
}

int
getdents_ischr(const DirEntry* e) {
  return !!(((find_data_type*)e)->dwFileAttributes & FILE_ATTRIBUTE_DEVICE);
}

int
getdents_isdir(const DirEntry* e) {
  return !!(((find_data_type*)e)->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

int
getdents_isfifo(const DirEntry* e) {
  return 0;
}

int
getdents_islnk(const DirEntry* e) {
  return !!(((find_data_type*)e)->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

int
getdents_isreg(const DirEntry* e) {
  return !getdents_isdir(e) && !getdents_ischr(e) && !getdents_islnk(e);
}

int
getdents_issock(const DirEntry* e) {
  return 0;
}

int
getdents_isunknown(const DirEntry* e) {
  return 0;
}

#else
#include <dirent.h> /* Defines DT_* constants */
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#ifdef DT_UNKNOWN
#define GETDENTS_HAVE_DTYPE 1
#else
/* no d_type here (Solaris): every entry is resolved with fstatat() */
enum { DT_UNKNOWN = 0, DT_FIFO = 1, DT_CHR = 2, DT_DIR = 4, DT_BLK = 6, DT_REG = 8, DT_LNK = 10, DT_SOCK = 12 };
#endif

/* maps the file type of `name`, found in directory `fd`, to a DT_* value.
 *
 *   int          fd    directory descriptor to resolve `name` in
 *   const char*  name  entry name, a symlink is not followed
 *
 *   returns unsigned char  DT_REG, DT_DIR, ..., or DT_UNKNOWN on failure
 */
static unsigned char
getdents_stat_type(int fd, const char* name) {
  struct stat st;

  if(fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW) == -1)
    return DT_UNKNOWN;

  switch(st.st_mode & S_IFMT) {
    case S_IFREG: return DT_REG;
    case S_IFDIR: return DT_DIR;
    case S_IFLNK: return DT_LNK;
    case S_IFBLK: return DT_BLK;
    case S_IFCHR: return DT_CHR;
    case S_IFIFO: return DT_FIFO;
    case S_IFSOCK: return DT_SOCK;
  }

  return DT_UNKNOWN;
}

/* checks that `fd` is a directory: returns 0, or -1 with errno set (ENOTDIR). */
static int
getdents_checkdir(int fd) {
  struct stat st;

  if(fstat(fd, &st) == -1)
    return -1;

  if(!S_ISDIR(st.st_mode)) {
    errno = ENOTDIR;
    return -1;
  }

  return 0;
}

#if defined(__linux__) && !defined(GETDENTS_PORTABLE)

#define GETDENTS_BUFFER_SIZE 8192

/* getdents64(2) record; the kernel ABI is the same at every pointer width */
struct linux_dirent64 {
  uint64_t d_ino;
  int64_t d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[];
};

struct getdents_reader {
  int fd;
  int nread, bpos; /* bytes held in buf, offset of the next record */
  int started;     /* getdents_read() was called since open/adopt */
  char* buf;       /* GETDENTS_BUFFER_SIZE bytes, allocated on first read */
};

#ifdef __dietlibc__
#define getdents_sys(fd, buf, size) getdents64(fd, buf, size)
#else
#define getdents_sys(fd, buf, size) syscall(SYS_getdents64, fd, buf, size)
#endif

int
getdents_initialized(Directory* d) {
  return !d->started;
}

size_t
getdents_size() {
  return sizeof(Directory);
}

void
getdents_clear(Directory* d) {
  d->fd = -1;
  d->nread = d->bpos = d->started = 0;
  d->buf = 0;
}

intptr_t
getdents_handle(Directory* d) {
  return d->fd;
}

int
getdents_open(Directory* d, const char* path) {
  int fd;

  d->nread = d->bpos = d->started = 0;

  do
    fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  while(fd == -1 && errno == EINTR);

  if(fd == -1)
    return -1;

  d->fd = fd;
  return 0;
}

int
getdents_adopt(Directory* d, intptr_t fd) {
  d->nread = d->bpos = d->started = 0;

  if(getdents_checkdir(fd) == -1)
    return -1;

  d->fd = fd;
  return 0;
}

int
getdents_openat(Directory* d, intptr_t dirfd, const char* name) {
  int fd;

  d->nread = d->bpos = d->started = 0;

  do
    fd = openat(dirfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  while(fd == -1 && errno == EINTR);

  if(fd == -1)
    return -1;

  d->fd = fd;
  return 0;
}

DirEntry*
getdents_read(Directory* d) {
  d->started = 1;

  for(;;) {
    struct linux_dirent64* e;

    if(d->bpos >= d->nread) {
      ssize_t n;

      if(d->fd < 0) {
        errno = EBADF;
        return 0;
      }

      if(!d->buf && !(d->buf = malloc(GETDENTS_BUFFER_SIZE)))
        return 0;

      d->bpos = d->nread = 0;

      do
        n = getdents_sys(d->fd, d->buf, GETDENTS_BUFFER_SIZE);
      while(n == -1 && errno == EINTR);

      if(n <= 0) {
        if(n == 0)
          errno = 0;
        return 0;
      }

      d->nread = n;
    }

    e = (struct linux_dirent64*)&d->buf[d->bpos];
    d->bpos += e->d_reclen;

    if(e->d_ino == 0)
      continue;

    /* the buffer is ours, so the resolved type is stored in the record */
    if(e->d_type == DT_UNKNOWN)
      e->d_type = getdents_stat_type(d->fd, e->d_name);

    return (DirEntry*)e;
  }
}

const void*
getdents_cname(const DirEntry* e) {
  return ((const struct linux_dirent64*)e)->d_name;
}

uint64_t
getdents_ino(const DirEntry* e) {
  return ((const struct linux_dirent64*)e)->d_ino;
}

void
getdents_close(Directory* d) {
  if(d->fd >= 0)
    close(d->fd);

  d->fd = -1;
  d->nread = d->bpos = 0;
  free(d->buf);
  d->buf = 0;
}

static int
getdents_gettype(const DirEntry* e) {
  return ((const struct linux_dirent64*)e)->d_type;
}

#else /* portable: opendir()/readdir() */

struct getdents_entry {
  struct dirent* de;
  unsigned char type; /* DT_* value, resolved if the file system gave DT_UNKNOWN */
};

struct getdents_reader {
  DIR* dirp;
  int started; /* getdents_read() was called since open/adopt */
  struct getdents_entry cur;
};

/* wraps `fd` in a DIR*: returns 0, or -1 with errno set and `fd` still open. */
static int
getdents_attach(Directory* d, int fd) {
  DIR* dirp;

  d->started = 0;

  if(!(dirp = fdopendir(fd)))
    return -1;

  d->dirp = dirp;
  return 0;
}

/* like getdents_attach() but closes `fd` on failure, keeping errno. */
static int
getdents_attach_open(Directory* d, int fd) {
  int err;

  if(fd == -1)
    return -1;

  if(getdents_attach(d, fd) == 0)
    return 0;

  err = errno;
  close(fd);
  errno = err;
  return -1;
}

int
getdents_initialized(Directory* d) {
  return !d->started;
}

size_t
getdents_size() {
  return sizeof(Directory);
}

void
getdents_clear(Directory* d) {
  d->dirp = 0;
  d->started = 0;
}

intptr_t
getdents_handle(Directory* d) {
  return d->dirp ? dirfd(d->dirp) : -1;
}

int
getdents_open(Directory* d, const char* path) {
  int fd;

  do
    fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  while(fd == -1 && errno == EINTR);

  return getdents_attach_open(d, fd);
}

int
getdents_adopt(Directory* d, intptr_t fd) {
  if(getdents_checkdir(fd) == -1)
    return -1;

  return getdents_attach(d, fd);
}

int
getdents_openat(Directory* d, intptr_t dirfd, const char* name) {
  int fd;

  do
    fd = openat(dirfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  while(fd == -1 && errno == EINTR);

  return getdents_attach_open(d, fd);
}

DirEntry*
getdents_read(Directory* d) {
  struct dirent* de;

  d->started = 1;

  if(!d->dirp) {
    errno = EBADF;
    return 0;
  }

  errno = 0;

  if(!(de = readdir(d->dirp)))
    return 0;

  d->cur.de = de;
#ifdef GETDENTS_HAVE_DTYPE
  d->cur.type = de->d_type;
#else
  d->cur.type = DT_UNKNOWN;
#endif

  if(d->cur.type == DT_UNKNOWN)
    d->cur.type = getdents_stat_type(dirfd(d->dirp), de->d_name);

  return (DirEntry*)&d->cur;
}

const void*
getdents_cname(const DirEntry* e) {
  return ((const struct getdents_entry*)e)->de->d_name;
}

uint64_t
getdents_ino(const DirEntry* e) {
  return ((const struct getdents_entry*)e)->de->d_ino;
}

void
getdents_close(Directory* d) {
  if(d->dirp)
    closedir(d->dirp);

  d->dirp = 0;
}

static int
getdents_gettype(const DirEntry* e) {
  return ((const struct getdents_entry*)e)->type;
}

#endif /* defined(__linux__) && !defined(GETDENTS_PORTABLE) */

char*
getdents_name(const DirEntry* e) {
  return strdup(getdents_cname(e));
}

const uint8_t*
getdents_namebuf(const DirEntry* e, size_t* len) {
  const char* name = getdents_cname(e);

  if(len)
    *len = strlen(name);

  return (const uint8_t*)name;
}

int
getdents_isdots(const DirEntry* e) {
  const char* s = getdents_cname(e);

  return s[0] == '.' && (s[1] == 0 || (s[1] == '.' && s[2] == 0));
}

int
getdents_isblk(const DirEntry* e) {
  return getdents_gettype(e) == DT_BLK;
}

int
getdents_ischr(const DirEntry* e) {
  return getdents_gettype(e) == DT_CHR;
}

int
getdents_isdir(const DirEntry* e) {
  return getdents_gettype(e) == DT_DIR;
}

int
getdents_isfifo(const DirEntry* e) {
  return getdents_gettype(e) == DT_FIFO;
}

int
getdents_islnk(const DirEntry* e) {
  return getdents_gettype(e) == DT_LNK;
}

int
getdents_isreg(const DirEntry* e) {
  return getdents_gettype(e) == DT_REG;
}

int
getdents_issock(const DirEntry* e) {
  return getdents_gettype(e) == DT_SOCK;
}

int
getdents_isunknown(const DirEntry* e) {
  return getdents_gettype(e) == DT_UNKNOWN;
}

#endif /* defined(_WIN32) */

size_t
getdents_namelen(const DirEntry* e) {
  size_t len;

  getdents_namebuf(e, &len);
  return len;
}

int
getdents_type(const DirEntry* e) {
  if(getdents_isblk(e))
    return TYPE_BLK;

  if(getdents_ischr(e))
    return TYPE_CHR;

  if(getdents_isdir(e))
    return TYPE_DIR;

  if(getdents_isfifo(e))
    return TYPE_FIFO;

  if(getdents_islnk(e))
    return TYPE_LNK;

  if(getdents_issock(e))
    return TYPE_SOCK;

  if(getdents_isreg(e))
    return TYPE_REG;

  return 0;
}

Directory*
getdents_new() {
  Directory* dir;

  if((dir = malloc(sizeof(Directory))))
    getdents_clear(dir);

  return dir;
}

/**
 * @}
 */
