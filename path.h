#ifndef PATH_H
#define PATH_H

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cutils.h>
#include "utils.h"

#ifdef _WIN32
ssize_t readlink(const char*, char*, size_t);
int is_symlink(const char*);
char is_junction(const char*);
#endif

/**
 * \defgroup path Directory path manipulation
 * @{
 */
#define PATH_NOTFIRST 0x80

#define PATH_FNM_NOMATCH 1
#define PATH_FNM_PATHNAME (1 << 0)
#define PATH_FNM_NOESCAPE (1 << 1)
#define PATH_FNM_PERIOD (1 << 2)

#if defined(__MINGW32__) || defined(__MSYS__) || defined(__CYGWIN__)
#define PATHSEP_S "/"
#define PATHSEP_C '/'
#define PATHDELIM_S ";"
#define path_issep(c) ((c) == '/' || (c) == '\\')
#elif defined(_WIN32)
#define PATHSEP_C '\\'
#define PATHSEP_S "\\"
#define PATHDELIM_S ";"
#define path_issep(c) ((c) == '\\')
#else
#define PATHSEP_S "/"
#define PATHSEP_C '/'
#define PATHDELIM_S ":"
#define path_issep(c) ((c) == '/')
#endif

#define path_isabs(p) (path_issep((p)[0]) || ((p)[0] && (p)[1] == ':' && path_issep((p)[2])))
#define path_isrel(p) (!path_isabs(p))
#define path_isname(p) ((p)[str_chr((p), '/')] != '\0')

typedef struct {
  size_t sz1, sz2;
} SizePair;

int path_absolute(const char*, DynBuf*);
int path_absolute_db(DynBuf*);
void path_append(const char*, size_t, DynBuf* db);
int path_canonical(const char*, DynBuf*);
int path_canonical_buf(DynBuf*);
size_t path_collapse(char*, size_t);
SizePair path_common_prefix(const char*, size_t, const char* s2, size_t n2);
size_t path_components(const char*, size_t, uint32_t n);
void path_concat(const char*, size_t, const char* b, size_t blen, DynBuf* db);
int path_exists(const char*);
const char* path_extname(const char*);
int path_find(const char*, const char*, DynBuf* db);
int path_fnmatch(const char*, unsigned int, const char* string, unsigned int slen, int flags);
char* path_getcwd(DynBuf*);
char* path_gethome(int);
int path_is_absolute(const char*, size_t);
int path_is_directory(const char*);
int path_is_symlink(const char*);
int path_normalize(const char*, DynBuf*, int symbolic);
int path_relative(const char*, const char*, DynBuf* out);
int path_relative_b(const char*, size_t, const char* s2, size_t n2, DynBuf* out);
size_t path_root(const char*, size_t);
size_t path_skip_component(const char*, size_t, size_t pos);
size_t path_skip_separator(const char*, size_t, size_t pos);
char* path_basename(const char*);
char* __path_dirname(const char*, DynBuf*);
char* path_dirname(const char*);
int path_readlink(const char*, DynBuf*);

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

/**
 * @}
 */
#endif /* defined(PATH_H) */
