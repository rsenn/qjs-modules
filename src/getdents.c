#define _GNU_SOURCE
#include <dirent.h> /* Defines DT_* constants */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include "getdents.h"

#define BUFFER_SIZE 1024 * 1024 * 5
#define DIRENT(dir) ((struct linux_dirent*)&(dir)->buf[(dir)->bpos])

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

int
getdents_open(Directory* dir, const char* path) {
  dir->nread = dir->bpos = 0;

  if((dir->fd = open(path, O_RDONLY | O_DIRECTORY)) == -1)
    return -1;

  return 0;
}

int
getdents_adopt(Directory* dir, int fd) {
  struct stat st;
  dir->nread = dir->bpos = 0;
  if(fstat(fd, &st) == -1)
    return -1;

  dir->fd = fd;
  return 0;
}

DirEntry*
getdents_read(Directory* dir) {
  for(;;) {
    if(!dir->nread || dir->bpos >= dir->nread) {
      dir->bpos = 0;
      dir->nread = syscall(SYS_getdents64, dir->fd, dir->buf, sizeof(dir->buf));
      if(dir->nread == -1)
        break;
    }
    while(dir->bpos < dir->nread) {
      DirEntry* d = DIRENT(dir);
      char d_type = dir->buf[dir->bpos + d->d_reclen - 1];
      dir->bpos += d->d_reclen;

      if(d->d_ino != 0 /*&& d_type == DT_REG*/)
        return d;
    }
  }

  return 0;
}

const char*
getdents_name(const Directory* dir) {
  if(dir->bpos < dir->nread) {
    DirEntry* d = DIRENT(dir);

    return d->d_name;
  }
  return 0;
}

int
getdents_type(const Directory* dir) {
  if(dir->bpos < dir->nread) {
    DirEntry* d = DIRENT(dir);
    return dir->buf[dir->bpos + d->d_reclen - 1];
  }
  return -1;
}

void
getdents_close(Directory* dir) {
  close(dir->fd);
  dir->fd = -1;
}
