#ifndef PATH_H
#define PATH_H

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cutils.h"
#include "utils.h"

#define PATH_NOTFIRST 0x80

#define PATH_FNM_NOMATCH 1
#define PATH_FNM_PATHNAME (1 << 0)
#define PATH_FNM_NOESCAPE (1 << 1)
#define PATH_FNM_PERIOD (1 << 2)

#if defined(__MINGW32__) || defined(__MSYS__) || defined(__CYGWIN__)
#define WINDOWS 1
#define PATHSEP_S "/"
#define PATHSEP_C '/'
#define PATHSEP_S_MIXED "\\/"
#define PATHDELIM_S ":"
#define path_issep(c) ((c) == '/' || (c) == '\\')
#elif defined(_WIN32)
#define WINDOWS 1
#define WINDOWS_NATIVE 1
#define PATHSEP_C '\\'
#define PATHSEP_S "\\"
#define PATHSEP_S_MIXED "\\"
#define PATHDELIM_S ";"
#define path_issep(c) ((c) == '\\')
#else
#define PATHSEP_S "/"
#define PATHSEP_C '/'
#define PATHSEP_S_MIXED "/"
#define PATHDELIM_S ":"
#define path_issep(c) ((c) == '/')
#endif

#define path_isabs(p) (path_issep((p)[0]) || ((p)[1] == ':' && path_issep((p)[2])))
#define path_isrel(p) (!path_isabs(p))
#define path_isname(p) ((p)[str_chr((p), '/')] != '\0')

typedef struct {
  size_t sz1, sz2;
} SizePair;

int path_absolute_db(DynBuf*);
int path_absolute(const char*, DynBuf* db);
int path_normalize(const char*, DynBuf* db, int symbolic);
size_t path_collapse(char*, size_t n);
void path_concat(const char*, size_t alen, const char* b, size_t blen, DynBuf* db);
int path_find(const char*, const char* name, DynBuf* db);
int path_fnmatch(const char*, unsigned int plen, const char* string, unsigned int slen, int flags);
char* path_gethome(int);
char* path_getcwd(DynBuf*);
SizePair path_common_prefix(const char* s1, size_t n1, const char* s2, size_t n2);
int path_relative_b(const char* s1, size_t n1, const char* s2, size_t n2, DynBuf* out);
int path_relative(const char* path, const char* relative_to, DynBuf* out);
size_t path_skip_separator(const char* p, size_t len, size_t pos);
size_t path_skip_component(const char* p, size_t len, size_t pos);
void path_append(const char* x, size_t len, DynBuf* db);
int path_exists(const char* p);
int path_is_absolute(const char* x, size_t n);
size_t path_root(const char* x, size_t n);
int path_is_directory(const char* p);
int path_is_symlink(const char* p);
int path_canonical_buf(DynBuf* db);
int path_canonical(const char* path, DynBuf* db);
size_t path_components(const char* p, size_t len, uint32_t n);
const char* path_extname(const char* p);

static inline size_t
path_length(const char* s, size_t n) {
  return path_skip_component(s, n, 0);
}

static inline size_t
path_length_s(const char* s) {
  return path_skip_component(s, strlen(s), 0);
}

static inline size_t
path_skip(const char* s, size_t n) {
  const char *p = s, *e = s + n;
  p += path_skip_separator(s, n, 0);
  p += path_skip_component(p, e - p, 0);
  return p - s;
}

static inline size_t
path_right(const char* s, size_t n) {
  const char* p = s + n - 1;
  while(p >= s && path_issep(*p)) --p;
  while(p >= s && !path_issep(*p)) --p;
  return p - s;
}


static inline int
path_getsep(const char* path) {
  while(*path) {
    if(path_issep(*path))
      return *path;
    ++path;
  }
  return '\0';
}


#endif /* defined(PATH_H) */
