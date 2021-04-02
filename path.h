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

static inline size_t
path_length(const char* s, size_t n) {
  const char *p = s, *e = s + n;
  while(p < e && !path_issep(*p)) ++p;
  return p - s;
}

static inline size_t
path_length_s(const char* s) {
  const char* p = s;
  while(*p && !path_issep(*p)) ++p;
  return p - s;
}

static inline size_t
path_skip(const char* s, size_t n) {
  const char *p = s, *e = s + n;
  while(p < e && !path_issep(*p)) ++p;
  while(p + 1 < e && path_issep(*p)) ++p;
  return p - s;
}

static inline size_t
path_skip_separator(const char* p, size_t len, size_t pos) {
  const char *start = p, *end = p + len;
  if(pos > len)
    pos = len;
  p += pos;
  while(p < end && path_issep(*p)) ++p;
  return p - start;
}

static inline size_t
path_right(const char* s, size_t n) {
  const char* p = s + n - 1;
  while(p >= s && path_issep(*p)) --p;
  while(p >= s && !path_issep(*p)) --p;
  return p - s;
}

static inline void
path_append(const char* x, size_t len, DynBuf* db) {
  if(db->size > 0 && db->buf[db->size - 1] != PATHSEP_C)
    dbuf_putc(db, PATHSEP_C);

  if(len > 2 && x[0] == '.' && x[1] == PATHSEP_C) {
    x += 2;
    len -= 2;
  }
  dbuf_append(db, (const uint8_t*)x, len);
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

static inline int
path_exists(const char* p) {
  struct stat st;
  int r;
#ifdef WINDOWS_NATIVE
  if(access(p, 0) == 0)
    return 1;
#endif
  return ((r = lstat(p, &st)) == 0);
}

static inline int
path_is_absolute(const char* x, size_t n) {
  if(n > 0 && x[0] == PATHSEP_C)
    return 1;
#ifdef WINDOWS
  if(n >= 3 && isalnum(x[0]) && x[1] == ':' && path_issep(x[2]))
    return 1;
#endif
  return 0;
}

static inline size_t
path_root(const char* x, size_t n) {
  if(n > 0 && x[0] == PATHSEP_C)
    return 1;
#if 1 // def WINDOWS
  if(n >= 3 && isalnum(x[0]) && x[1] == ':' && path_issep(x[2]))
    return 3;
#endif
  return 0;
}

static inline int
path_is_directory(const char* p) {
  struct stat st;
  int r;
  if((r = lstat(p, &st) == 0)) {
    if(S_ISDIR(st.st_mode))
      return 1;
  }
  return 0;
}

static inline int
path_is_symlink(const char* p) {
  struct stat st;
  int r;
  if((r = lstat(p, &st) == 0)) {
    if(S_ISLNK(st.st_mode))
      return 1;
  }
  return 0;
}

static inline int
path_canonical_buf(DynBuf* db) {
  db->size = path_collapse((char*)db->buf, db->size);
  dbuf_putc(db, '\0');
  db->size--;
  return 1;
}

static inline int
path_canonical(const char* path, DynBuf* db) {
  db->size = 0;
  dbuf_putstr(db, path);
  dbuf_putc(db, '\0');
  db->size--;
  return path_canonical_buf(db);
}

static inline size_t
path_components(const char* p, size_t len, uint32_t n) {
  const char *s = p, *e = p + len;
  size_t count = 0;
  while(s < e) {
    s += path_skip_separator(s, e - s, 0);
    if(s == e)
      break;
    s += path_length(s, e - s);
    if(--n <= 0)
      break;
    count++;
  }
  return count;
}

static inline const char*
path_extname(const char* p) {
  size_t pos;
  char* q;
  if((q = strrchr(p, PATHSEP_C)))
    p = q + 1;
  pos = str_rchr(p, '.');
  p += pos;
  return p;
}

#endif /* defined(PATH_H) */
