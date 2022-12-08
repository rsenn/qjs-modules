#include "getdents.h"

#if defined(_WIN32) || defined(__MSYS__)
#include <windows.h>

#define BUFFER_SIZE 1024 * 1024 * 5
#define DIRENT(d) ((WIN32_FIND_DATAW*)&(d)->fdw)

struct getdents_reader {
  HANDLE h;
  BOOL first;
  WIN32_FIND_DATAW fdw;
};

size_t
getdents_size() {
  return sizeof(struct getdents_reader);
}

void
getdents_clear(Directory* d) {
  d->h = INVALID_HANDLE_VALUE;
  d->first = FALSE;
}

ptrdiff_t
getdents_handle(Directory* d) {
  return d->h;
}

int
getdents_open(Directory* d, const char* path) {
  size_t pathlen = utf8_strlen(path);
  wchar_t wp[pathlen + 1];

  utf8_towcs(path, wp);

  if((d->h = FindFirstFileW(wp, &d->fdw)) == INVALID_HANDLE_VALUE)
    return -1;

  d->first = TRUE;

  return 0;
}

int
getdents_adopt(Directory* d, intptr_t hnd) {
  if(hnd == INVALID_HANDLE_VALUE)
    return -1;
  d->h = hnd;
  return 0;
}

DirEntry*
getdents_read(Directory* d) {
  if(d->first) {
    d->first = FALSE;
    return &d->fdw;
  }

  return 0;
}

const char*
getdents_name(const DirEntry* e) {
  WIN32_FIND_DATAW* fdw = (void*)e;
  return fdw->cFileName;
}

int
getdents_type(const DirEntry* e) {
  WIN32_FIND_DATAW* fdw = (void*)e;
  return fdw->dwFileAttributes;
}

void
getdents_close(Directory* d) {
  CloseHandle(d->h);
  d->h = INVALID_HANDLE_VALUE;
}

#else
#include <dirent.h> /* Defines DT_* constants */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#define BUFFER_SIZE 1024 * 1024 * 5
#define DIRENT(d) ((struct linux_dirent64*)&(d)->buf[(d)->bpos])

struct linux_dirent {
  long d_ino;
  off_t d_off;
  unsigned short d_reclen;
  char d_name[];
};

struct linux_dirent64 {
  ino64_t d_ino;           /* 64-bit inode number */
  off64_t d_off;           /* 64-bit offset to next structure */
  unsigned short d_reclen; /* Size of this dirent */
  unsigned char d_type;    /* File type */
  char d_name[];           /* Filename (null-terminated) */
};

struct getdents_reader {
  int fd, nread, bpos;
  char buf[BUFFER_SIZE];
};

size_t
getdents_size() {
  return sizeof(Directory);
}

void
getdents_clear(Directory* d) {
  d->fd = -1;
  d->nread = d->bpos = 0;
}

ptrdiff_t
getdents_handle(Directory* d) {
  return d->fd;
}

int
getdents_open(Directory* d, const char* path) {
  d->nread = d->bpos = 0;

  if((d->fd = open(path, O_RDONLY | O_DIRECTORY)) == -1)
    return -1;

  return 0;
}

int
getdents_adopt(Directory* d, intptr_t fd) {
  struct stat st;
  d->nread = d->bpos = 0;
  if(fstat(fd, &st) == -1)
    return -1;

  d->fd = fd;
  return 0;
}

DirEntry*
getdents_read(Directory* d) {
  for(;;) {
    if(!d->nread || d->bpos >= d->nread) {
      d->bpos = 0;
      d->nread = syscall(SYS_getdents64, d->fd, d->buf, sizeof(d->buf));
      if(d->nread <= 0)
        break;
    }
    while(d->bpos < d->nread) {
      struct linux_dirent64* e = DIRENT(d);
      char d_type = d->buf[d->bpos + e->d_reclen - 1];
      d->bpos += e->d_reclen;

      if(e->d_ino != 0 /*&& d_type == DT_REG*/)
        return (DirEntry*)e;
    }
  }

  return 0;
}

const char*
getdents_name(const DirEntry* e) {

  return ((struct linux_dirent64*)e)->d_name;
}

int
getdents_type(const DirEntry* e) {
  return ((struct linux_dirent64*)e)->d_type;
  // return ((char*)e)[e->d_reclen - 1];
}

void
getdents_close(Directory* d) {
  close(d->fd);
  d->fd = -1;
}
#endif /* defined(_WIN32) */
