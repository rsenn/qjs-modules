#ifndef GETDENTS_H
#define GETDENTS_H

#include <stddef.h>
#include <stdint.h>

typedef struct getdents_reader Directory;
typedef struct getdents_entry DirEntry;

size_t getdents_size();
void getdents_clear(Directory*);
int getdents_open(Directory*, const char* path);
int getdents_adopt(Directory*, intptr_t fd);
DirEntry* getdents_read(Directory*);
const char* getdents_name(const DirEntry*);
int getdents_type(const DirEntry*);
void getdents_close(Directory*);

#endif /* defined(GETDENTS_H) */
