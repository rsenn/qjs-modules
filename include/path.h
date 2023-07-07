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

#if /*defined(__MINGW32__) ||*/ defined(__MSYS__) || defined(__CYGWIN__)
#define PATHSEP_S "/"
#define PATHSEP_C '/'
#define PATHDELIM_S ";"
#define path_issep(c) ((c) == '/' || (c) == '\\')
#elif defined(_WIN32)
#define PATHSEP_C '\\'
#define PATHSEP_S "\\"
#define PATHDELIM_S ";"
#define path_issep(c) ((c) == '/' || (c) == '\\')
#else
#define PATHSEP_S "/"
#define PATHSEP_C '/'
#define PATHDELIM_S ":"
#define path_issep(c) ((c) == '/')
#endif

#define path_isabs(p) (path_issep((p)[0]) || ((p)[0] && (p)[1] == ':' && path_issep((p)[2])))
#define path_isrel(p) (!path_isabs(p))
#ifdef _WIN32
#define path_isname(p) ((p)[str_chrs((p), "\\/", 2)] == '\0')
#else
#define path_isname(p) ((p)[str_chr((p), '/')] == '\0')
#endif
#define path_isdot(p) ((p)[0] == '.' && ((p)[1] == '\0' || path_issep((p)[1])))
#define path_isdotslash(p) ((p)[0] == '.' && path_issep((p)[1]))
#define path_isdotdot(p) ((p)[0] == '.' && (p)[1] == '.' && ((p)[2] == '\0' || path_issep((p)[2])))

#define path_isrelative(p) !path_isabsolute1(p) // (path_isdotslash(p) || path_isdotdot(p))
#define path_isexplicit(p) (path_isabs(p) || path_isdot(p) || path_isdotdot(p))
#define path_isimplicit(p) (!path_isexplicit(p))

typedef struct {
  size_t sz1, sz2;
} SizePair;

char* path_dup3(const char* path, size_t n, DynBuf* db);
char* path_dup1(const char* path);
char* path_dup2(const char* path, size_t n);
int path_absolute3(const char* path, size_t len, DynBuf* db);
char* path_absolute2(const char* path, size_t len);
char* path_absolute1(const char* path);
void path_append3(const char* x, size_t len, DynBuf* db);
int path_canonical3(const char* path, size_t len, DynBuf* db);
char* path_canonical2(const char* path, size_t len);
char* path_canonical1(const char* path);
size_t path_normalize3(const char* path, size_t n, DynBuf* db);
size_t path_normalize1(char* path);
size_t path_normalize2(char* path, size_t nb);
SizePair path_common4(const char* s1, size_t n1, const char* s2, size_t n2);
size_t path_components3(const char* p, size_t len, uint32_t n);
void path_concat5(const char* a, size_t alen, const char* b, size_t blen, DynBuf* db);
char* path_concat4(const char* a, size_t alen, const char* b, size_t blen);
void path_concat3(const char* a, const char* b, DynBuf* db);
char* path_concat2(const char* a, const char* b);
const char* path_at4(const char* p, size_t plen, size_t* len_ptr, int i);
const char* path_at3(const char* p, size_t* len_ptr, int i);
size_t path_offset4(const char* p, size_t len, size_t* len_ptr, int i);
size_t path_offset3(const char* p, size_t* len_ptr, int i);
size_t path_offset2(const char* p, int i);
size_t path_size2(const char* p, int i);
const char* path_at2(const char* p, int i);
size_t path_length1(const char* p);
size_t path_length2(const char* p, size_t slen);
int path_slice4(const char* p, int start, int end, DynBuf* db);
char* path_slice3(const char* p, int start, int end);
int path_exists1(const char* p);
int path_exists2(const char* p, size_t len);
int path_isin4(const char* p, size_t len, const char* dir, size_t dirlen);
int path_isin2(const char* p, const char* dir);
int path_equal4(const char* a, size_t la, const char* b, size_t lb);
int path_equal2(const char* a, const char* b);
const char* path_extname1(const char* p);
int path_fnmatch5(const char* pattern, size_t plen, const char* string, size_t slen, int flags);
char* path_getcwd1(DynBuf* db);
char* path_getcwd0(void);
char* path_gethome(void);
char* path_gethome1(int uid);
int path_stat2(const char* p, size_t plen, struct stat* st);
int path_isabsolute2(const char* x, size_t n);
int path_isabsolute1(const char* x);
int path_isdir1(const char* p);
int path_isdir2(const char* p, size_t plen);
int path_isfile1(const char* p);
int path_isfile2(const char* p, size_t plen);
int path_ischardev1(const char* p);
int path_ischardev2(const char* p, size_t plen);
int path_isblockdev1(const char* p);
int path_isblockdev2(const char* p, size_t plen);
int path_isfifo1(const char* p);
int path_isfifo2(const char* p, size_t plen);
int path_issocket1(const char* p);
int path_issocket2(const char* p, size_t plen);
int path_issymlink1(const char* p);
int path_issymlink2(const char* p, size_t plen);
int path_resolve3(const char* path, DynBuf* db, int symbolic);
char* path_resolve2(const char* path, int symbolic);
int path_realpath3(const char*, size_t len, DynBuf* buf);
char* path_realpath2(const char*, size_t len);
char* path_realpath1(const char*);
int path_relative3(const char* path, const char* relative_to, DynBuf* out);
char* path_relative1(const char* path);
char* path_relative2(const char* path, const char* relative_to);
int path_relative5(const char* s1, size_t n1, const char* s2, size_t n2, DynBuf* out);
char* path_relative4(const char* s1, size_t n1, const char* s2, size_t n2);
size_t path_root2(const char* x, size_t n);
char* path_dirname1(const char* path);
char* path_dirname2(const char* path, size_t n);
char* path_dirname3(const char* path, size_t n, char* dest);
size_t path_dirlen1(const char* path);
size_t path_dirlen2(const char* path, size_t n);
int path_readlink2(const char* path, DynBuf* dir);
char* path_readlink1(const char* path);
int path_compare4(const char* a, size_t alen, const char* b, size_t blen);
char* path_search(const char** path_ptr, const char* name, DynBuf* db);

static inline size_t
path_component1(const char* p) {
  const char* s = p;

  while(*s && !path_issep(*s))
    ++s;

  return s - p;
}

static inline size_t
path_component3(const char* p, size_t len, size_t pos) {
  const char *start = p, *end = p + len;

  if(pos > len)
    pos = len;

  p += pos;

  while(p < end && !path_issep(*p))
    ++p;

  return p - start;
}

static inline size_t
path_separator1(const char* p) {
  const char* s = p;

  while(*s && path_issep(*s))
    ++s;

  return s - p;
}

static inline size_t
path_separator3(const char* p, size_t len, size_t pos) {
  const char *start = p, *end = p + len;

  if(pos > len)
    pos = len;

  p += pos;

  while(p < end && path_issep(*p))
    ++p;

  return p - start;
}

static inline size_t
path_skip1(const char* s) {
  const char* p = s;
  while(*p && !path_issep(*p))
    ++p;
  while(*p && path_issep(*p))
    ++p;
  return p - s;
}

static inline size_t
path_skip2(const char* s, size_t n) {
  const char *p = s, *e = s + n;
  p += path_component3(s, n, 0);
  p += path_separator3(p, e - p, 0);
  return p - s;
}

static inline size_t
path_skip3(const char* s, size_t* len, size_t n) {
  const char *p = s, *e = s + n;

  p += path_component3(s, e - p, 0);

  if(len)
    *len = p - s;

  p += path_separator3(p, e - p, 0);

  return p - s;
}

static inline size_t
path_right2(const char* s, size_t n) {
  const char* p = s + n - 1;
  while(p > s && path_issep(*p))
    --p;
  while(p > s && !path_issep(*p))
    --p;
  return p - s;
}

static inline int
path_getsep1(const char* path) {
  while(*path) {
    if(path_issep(*path))
      return *path;
    ++path;
  }
  return '\0';
}

static inline int
path_getsep2(const char* path, size_t len) {
  const char* q = path + len;
  while(path < q) {
    if(path_issep(*path))
      return *path;
    ++path;
  }
  return '\0';
}

static inline const char*
path_trimdotslash1(const char* s) {
  while(*s && path_isdotslash(s))
    s += path_skip1(s);

  return s;
}

static inline size_t
path_skipdotslash1(const char* s) {
  size_t i = 0;
  for(i = 0; path_isdotslash(&s[i]);)
    i += path_skip1(&s[i]);
  return i;
}

static inline size_t
path_skipdotslash2(const char* s, size_t n) {
  size_t i = 0;
  while(i < n && path_isdotslash(&s[i]))
    i += path_skip2(&s[i], n - i);
  return i;
}

static inline int
path_compare2(const char* a, const char* b) {
  a += path_skipdotslash1(a);
  b += path_skipdotslash1(b);

  return strcmp(a, b);
}

/**
 * @}
 */
#endif /* defined(PATH_H) */
