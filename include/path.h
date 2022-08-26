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
#define path_isdot(p) ((p)[0] == '.' && ((p)[1] == '\0' || path_issep((p)[1])))
#define path_isdotslash(p) ((p)[0] == '.' && path_issep((p)[1]))
#define path_isdotdot(p) ((p)[0] == '.' && (p)[1] == '.' && ((p)[2] == '\0' || path_issep((p)[2])))

#define path_isrelative(p) !path_isabsolute1(p) // (path_isdotslash(p) || path_isdotdot(p))
#define path_isexplicit(p) (path_isabs(p) || path_isdot(p) || path_isdotdot(p))
#define path_isimplicit(p) (!path_isexplicit(p))

typedef struct {
  size_t sz1, sz2;
} SizePair;

char* path_dup3(const char*, size_t, DynBuf*);
char* path_dup1(const char*);
char* path_dup2(const char*, size_t);
int path_absolute3(const char*, size_t, DynBuf*);
char* path_absolute2(const char*, size_t);
char* path_absolute1(const char*);
void path_append3(const char*, size_t, DynBuf*);
int path_canonical3(const char*, size_t, DynBuf*);
char* path_canonical2(const char*, size_t);
char* path_canonical1(const char*);
size_t path_collapse3(const char*, size_t, DynBuf*);
char* path_collapse1(const char*);
size_t path_collapse2(char*, size_t);
SizePair path_common4(const char*, size_t, const char*, size_t n2);
size_t path_components3(const char*, size_t, uint32_t);
void path_concat5(const char*, size_t, const char*, size_t blen, DynBuf* db);
char* path_concat4(const char*, size_t, const char*, size_t blen);
void path_concat3(const char*, const char*, DynBuf*);
char* path_concat2(const char*, const char*);
const char* path_at4(const char*, size_t, size_t*, int i);
const char* path_at3(const char*, size_t*, int);
const char* path_at2(const char*, int);
size_t path_offset4(const char*, size_t, size_t*, int i);
size_t path_offset3(const char*, size_t*, int);
size_t path_offset2(const char*, int);
size_t path_size2(const char*, int);
size_t path_length1(const char*);
size_t path_length2(const char*, size_t);
int path_slice4(const char*, int, int, DynBuf* db);
char* path_slice3(const char*, int, int);
int path_exists1(const char*);
int path_exists2(const char*, size_t);
int path_isin4(const char*, size_t, const char*, size_t dirlen);
int path_isin2(const char*, const char*);
int path_equal4(const char*, size_t, const char*, size_t lb);
int path_equal2(const char*, const char*);
const char* path_extname1(const char*);
int path_find(const char*, const char*, DynBuf*);
int path_fnmatch(const char*, unsigned int, const char*, unsigned int slen, int flags);
char* path_getcwd1(DynBuf*);
char* path_getcwd0(void);
char* path_gethome1(int);
int path_isabsolute2(const char*, size_t);
int path_isabsolute1(const char*);
int path_isdir1(const char*);
int path_isfile1(const char*);
int path_ischardev1(const char*);
int path_isblockdev1(const char*);
int path_isfifo1(const char*);
int path_issocket1(const char*);
int path_issymlink1(const char*);
int path_normalize3(const char*, DynBuf*, int);
char* path_normalize2(const char*, int);
int path_relative3(const char*, const char*, DynBuf*);
int path_relative2(const char*, const char*);
int path_relative5(const char*, size_t, const char*, size_t n2, DynBuf* out);
int path_relative4(const char*, size_t, const char*, size_t n2);
size_t path_root(const char*, size_t);
size_t path_component3(const char*, size_t, size_t);
size_t path_component1(const char*);
size_t path_separator3(const char*, size_t, size_t);
size_t path_separator1(const char*);
char* __path_dirname(const char*, DynBuf*);
size_t path_dirname_len(const char*);
char* path_dirname_alloc(const char*);
char* path_dirname1(char*);
char* path_dirname2(const char*, char*);
char* path_dirname3(const char*, size_t, char*);
int path_readlink2(const char*, DynBuf*);
char* path_readlink1(const char*);
int path_compare4(const char*, size_t, const char*, size_t blen);

/*static inline size_t
path_length_s(const char* s) {
  return path_component3(s, strlen(s), 0);
}

static inline size_t
path_skip_s(const char* s) {
  const char* p = s;
  while(*p && path_issep(*p)) ++p;
  while(*p && !path_issep(*p)) ++p;
  return p - s;
}
*/
static inline size_t
path_skip1(const char* s) {
  const char* p = s;
  while(*p && !path_issep(*p)) ++p;
  while(*p && path_issep(*p)) ++p;
  return p - s;
}

/*static inline size_t
path_skip2(const char* s, size_t n) {
  const char *p = s, *e = s + n;
  p += path_separator3(s, n, 0);
  p += path_component3(p, e - p, 0);
  return p - s;
}*/

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

static inline const char*
path_trim_dotslash(const char* s) {
  while(*s && path_isdotslash(s)) s += path_skip1(s);

  return s;
}

static inline size_t
path_skip_dotslash1(const char* s) {
  size_t i = 0;
  for(i = 0; path_isdotslash(&s[i]);) i += path_skip1(&s[i]);
  return i;
}

static inline size_t
path_skip_dotslash2(const char* s, size_t n) {
  size_t i = 0;
  while(i < n && path_isdotslash(&s[i])) i += path_skip2(&s[i], n - i);
  return i;
}

static inline int
path_compare2(const char* a, const char* b) {
  a += path_skip_dotslash1(a);
  b += path_skip_dotslash1(b);

  return strcmp(a, b);
}

/**
 * @}
 */
#endif /* defined(PATH_H) */
