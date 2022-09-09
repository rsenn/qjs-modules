#ifndef GETDENTS_H
#define GETDENTS_H

#include <stddef.h>

typedef struct getdents_reader Directory;
typedef struct linux_dirent64 DirEntry;

size_t getdents_size();
int getdents_open(Directory*, const char* path);
int getdents_adopt(Directory*, int fd);
DirEntry* getdents_read(Directory*);
const char* getdents_name(const Directory*);
int getdents_type(const Directory*);
void getdents_close(Directory*);

#endif /* defined(GETDENTS_H) */
