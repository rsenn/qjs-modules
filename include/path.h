#ifndef PATH_H
#define PATH_H

#include "char-utils.h"
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

#define path_isdot(p) ((p)[0] == '.' && ((p)[1] == '\0' || path_issep((p)[1])))
#define path_isdotslash(p) ((p)[0] == '.' && path_issep((p)[1]))
#define path_isdotdot(p) ((p)[0] == '.' && (p)[1] == '.' && ((p)[2] == '\0' || path_issep((p)[2])))

/* NUL-terminated string forms, as shish's path_basename_s */
#define path_isabs_s(s) path_isabs((s), strlen(s))
#define path_isrel_s(s) path_isrel((s), strlen(s))
#define path_isname_s(s) path_isname((s), strlen(s))
#define path_isexplicit(p) (path_isabs_s(p) || path_isdot(p) || path_isdotdot(p))

/* lexical core (src/path-lib.c, shared with c-utils and shish): (pointer, length) in,
 * caller's buffer out; a writer returns the length, or what it needs when `cap` is too small */
size_t path_rootlen(const char* p, size_t n);
int path_isabs(const char* p, size_t n);
int path_isrel(const char* p, size_t n);
int path_isname(const char* p, size_t n);
size_t path_next(const char* p, size_t n, size_t pos);
size_t path_parentlen(const char* p, size_t n);
const char* path_basename(const char* p, size_t n, size_t* len);
size_t path_dirlen(const char* p, size_t n);
size_t path_cat(char* out, size_t cap, const char* a, size_t an, const char* b, size_t bn);
size_t path_canon(char* out, size_t cap, const char* p, size_t n);
size_t path_abs(char* out, size_t cap, const char* p, size_t n, const char* cwd, size_t cwdn);
size_t path_rel(char* out, size_t cap, const char* p, size_t pn, const char* base, size_t bn);
size_t path_common(const char* a, size_t an, const char* b, size_t bn, size_t* blen);
size_t path_extoff(const char* p, size_t n);

int path_exists(const char* p, size_t len);
int path_fnmatch(const char* pattern, size_t plen, const char* string, size_t slen, int flags);
int path_getcwd(DynBuf* db);
char* path_gethome(int uid);
char* path_homeof(const char* user, size_t userlen);
int path_isfile(const char* p, size_t len);
int path_issymlink(const char* p, size_t len);
#define path_exists_s(s) path_exists((s), strlen(s))
#define path_isfile_s(s) path_isfile((s), strlen(s))
#define path_issymlink_s(s) path_issymlink((s), strlen(s))
int path_resolve(const char* p, size_t n, DynBuf* db, int symbolic, const char* cwd, size_t cwdn);
int path_readlink(const char* path, DynBuf* dir);
size_t path_listlen(const char* l, size_t n);
size_t path_listnext(const char* l, size_t n, size_t pos);
size_t path_search(char* out, size_t cap, const char* l, size_t ln, const char* name, size_t nl, int (*ok)(const char*, void*), void* arg);
#define PATH_LIST_FOREACH(l, n, elem, elen) \
  for(size_t pl_pos_ = 0; pl_pos_ != (size_t)-1 && ((elem) = (l) + pl_pos_, (elen) = path_listlen((elem), (n) - pl_pos_), 1); \
      pl_pos_ = path_listnext((l), (n), pl_pos_))

/**
 * @}
 */
#endif /* defined(PATH_H) */
