#ifndef GETDENTS_H
#define GETDENTS_H

typedef struct getdents_reader Directory;
typedef struct linux_dirent64 DirEntry;

int getdents_open(Directory*, const char* path);
DirEntry* getdents_read(Directory*);
const char* getdents_name(const Directory*);
int getdents_type(const Directory*);
void getdents_close(Directory*);

#endif /* defined(GETDENTS_H) */
