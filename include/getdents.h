#ifndef GETDENTS_H
#define GETDENTS_H

#include <stddef.h>
#include <stdint.h>

/**
 * \defgroup getdents getdents: Fast directory entry reader
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

size_t getdents_size();
ptrdiff_t getdents_handle(Directory*);
Directory* getdents_new();
void getdents_clear(Directory*);
int getdents_open(Directory*, const char* path);
int getdents_adopt(Directory*, intptr_t fd);
DirEntry* getdents_read(Directory*);
const void* getdents_cname(const DirEntry*);
char* getdents_name(const DirEntry*);
const uint8_t* getdents_namebuf(const DirEntry*, size_t* len);
int getdents_type(const DirEntry*);
void getdents_close(Directory*);
int getdents_initialized(Directory* d);

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
