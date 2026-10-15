#ifndef PATH_H
#define PATH_H

#include "char-utils.h"
//#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cutils.h>

#ifdef _WIN32
ssize_t readlink(const char*, char*, size_t);
int is_symlink(const char*);
char is_junction(const char*);
#endif

/**
 * \defgroup path path: Directory path manipulation
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
#define PATHLISTSEP_CHARS ":;"
#define path_islistsep(c) ((c) == ':' || (c) == ';')
#define path_issep(c) ((c) == '/' || (c) == '\\')
#elif defined(_WIN32)
#define PATHSEP_C '\\'
#define PATHSEP_S "\\"
#define PATHDELIM_S ";"
#define PATHLISTSEP_CHARS ";"
#define path_islistsep(c) ((c) == ';')
#define path_issep(c) ((c) == '/' || (c) == '\\')
#else
#define PATHSEP_S "/"
#define PATHSEP_C '/'
#define PATHDELIM_S ":"
#define PATHLISTSEP_CHARS ":;"
#define path_islistsep(c) ((c) == ':' || (c) == ';')
#define path_issep(c) ((c) == '/')
#endif

#define path_isabs(p) (path_issep((p)[0]) || ((p)[0] && (p)[1] == ':' && path_issep((p)[2])))
#ifdef _WIN32
#define path_isname(p) ((p)[str_chrs((p), "\\/", 2)] == '\0')
#else
#define path_isname(p) ((p)[str_chr((p), '/')] == '\0')
#endif
#define path_isdot(p) ((p)[0] == '.' && ((p)[1] == '\0' || path_issep((p)[1])))
#define path_isdotslash(p) ((p)[0] == '.' && path_issep((p)[1]))
#define path_isdotdot(p) ((p)[0] == '.' && (p)[1] == '.' && ((p)[2] == '\0' || path_issep((p)[2])))

#define path_isrelative(p) !path_isabsolute1(p)
#define path_isexplicit(p) (path_isabs(p) || path_isdot(p) || path_isdotdot(p))

/* lexical core (src/path-lib.c, shared with c-utils and shish): (pointer, length) in,
 * caller's buffer out; a writer returns the length, or what it needs when `cap` is too small */
size_t path_rootlen(const char* p, size_t n);
const char* path_basename(const char* p, size_t n, size_t* len);
size_t path_dirlen(const char* p, size_t n);
size_t path_cat(char* out, size_t cap, const char* a, size_t an, const char* b, size_t bn);
size_t path_canon(char* out, size_t cap, const char* p, size_t n);
size_t path_abs(char* out, size_t cap, const char* p, size_t n, const char* cwd, size_t cwdn);
size_t path_rel(char* out, size_t cap, const char* p, size_t pn, const char* base, size_t bn);
size_t path_common(const char* a, size_t an, const char* b, size_t bn, size_t* blen);
size_t path_extoff(const char* p, size_t n);

char* path_dup3(const char* path, size_t n, DynBuf* db);
char* path_dup2(const char* path, size_t n);
size_t path_components3(const char* p, size_t len, uint32_t n);
const char* path_at4(const char* p, size_t plen, size_t* len_ptr, int i);
const char* path_at3(const char* p, size_t* len_ptr, int i);
size_t path_length1(const char* p);
size_t path_length2(const char* p, size_t slen);
int path_slice4(const char* p, int start, int end, DynBuf* db);
int path_exists1(const char* p);
int path_exists2(const char* p, size_t len);
int path_isin4(const char* p, size_t len, const char* dir, size_t dirlen);
int path_fnmatch5(const char* pattern, size_t plen, const char* string, size_t slen, int flags);
char* path_getcwd1(DynBuf* db);
char* path_getcwd0(void);
char* path_gethome(void);
char* path_gethome1(int uid);
char* path_gethome2(const char* user, size_t userlen);
int path_isdir1(const char* p);
int path_isfile1(const char* p);
int path_ischardev1(const char* p);
int path_isblockdev1(const char* p);
int path_isfifo1(const char* p);
int path_issocket1(const char* p);
int path_issymlink1(const char* p);
int path_resolve3(const char* path, DynBuf* db, int symbolic);
int path_realpath3(const char*, size_t len, DynBuf* buf);
int path_readlink2(const char* path, DynBuf* dir);
size_t path_listlen(const char* l, size_t n);
size_t path_listnext(const char* l, size_t n, size_t pos);
size_t path_search(char* out, size_t cap, const char* l, size_t ln, const char* name, size_t nl, int (*ok)(const char*, void*), void* arg);
#define PATH_LIST_FOREACH(l, n, elem, elen) \
  for(size_t pl_pos_ = 0; pl_pos_ != (size_t)-1 && ((elem) = (l) + pl_pos_, (elen) = path_listlen((elem), (n)-pl_pos_), 1); pl_pos_ = path_listnext((l), (n), pl_pos_))
int path_diff4(const char* a, size_t la, const char* b, size_t lb);

static inline int
path_equal4(const char* a, size_t la, const char* b, size_t lb) {
  return 0 == path_diff4(a, la, b, lb);
}

static inline int
path_isabsolute2(const char* x, size_t n) {
  if(n > 0 && x[0] == PATHSEP_C)
    return 1;
#ifdef _WIN32
  if(n >= 2 && x[1] == ':')
    return 1;
#endif
  return 0;
}

static inline int
path_isabsolute1(const char* x) {
  if(x[0] && x[0] == PATHSEP_C)
    return 1;
#ifdef _WIN32
  if(x[0] && !path_issep(x[0]) && !path_isdot(x) && x[1] == ':')
    return 1;
#endif
  return 0;
}

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
path_separator2(const char* p, size_t len) {
  const char *start = p, *end = p + len;
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
  p += path_separator2(p, e - p);
  return p - s;
}

static inline size_t
path_right2(const char* s, size_t n) {
  const char* p = s + n - 1;
  while(p > s && path_issep(*p))
    --p;
  while(p > s && !path_issep(*p))
    --p;
  if(path_issep(*p))
    ++p;
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
