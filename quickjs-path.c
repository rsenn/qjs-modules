#define _GNU_SOURCE

#include "quickjs.h"
#include "cutils.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

#include "utils.h"

#define PATH_FNM_NOMATCH 1
#define PATH_FNM_PATHNAME (1 << 0)
#define PATH_FNM_NOESCAPE (1 << 1)
#define PATH_FNM_PERIOD (1 << 2)

enum path_methods {
  METHOD_ABSOLUTE = 0,
  METHOD_APPEND,
  METHOD_BASENAME,
  METHOD_CANONICAL,
  METHOD_CANONICALIZE,
  METHOD_COLLAPSE,
  METHOD_CONCAT,
  METHOD_DIRNAME,
  METHOD_EXISTS,
  METHOD_FIND,
  METHOD_FNMATCH,
  METHOD_GETCWD,
  METHOD_GETHOME,
  METHOD_GETSEP,
  METHOD_IS_ABSOLUTE,
  METHOD_IS_DIRECTORY,
  METHOD_IS_SEPARATOR,
  METHOD_LEN,
  METHOD_LEN_S,
  METHOD_NUM,
  METHOD_READLINK,
  METHOD_REALPATH,
  METHOD_RELATIVE,
  METHOD_RIGHT,
  METHOD_SKIP,
  METHOD_SKIPS,
  METHOD_SKIP_SEPARATOR,
  METHOD_SPLIT
};

#if defined(__MINGW32__) || defined(__MSYS__) || defined(__CYGWIN__)
#define PATHSEP_C '/'
#define PATHSEP_S_MIXED "\\/"
#define path_issep(c) ((c) == '/' || (c) == '\\')
#elif defined(_WIN32)
#define PATHSEP_C '\\'
#define PATHSEP_S_MIXED "\\"
#define path_issep(c) ((c) == '\\')
#else
#define PATHSEP_C '/'
#define PATHSEP_S_MIXED "/"
#define path_issep(c) ((c) == '/')
#endif

#define path_isabs(p) (path_issep((p)[0]) || ((p)[1] == ':' && path_issep((p)[2])))
#define path_isrel(p) (!path_isabs(p))
#define path_isname(p) ((p)[str_chr((p), '/')] != '\0')

static inline size_t
path_len(const char* s, size_t n) {
  const char *p = s, *e = s + n;
  while(p < e && !path_issep(*p)) ++p;
  return p - s;
}
static inline size_t
path_len_s(const char* s) {
  const char* p = s;
  while(*p && !path_issep(*p)) ++p;
  return p - s;
}
static size_t
path_skip(const char* s, size_t n) {
  const char *p = s, *e = s + n;
  while(p < e && !path_issep(*p)) ++p;
  while(p + 1 < e && path_issep(*p)) ++p;
  return p - s;
}

static int
path_absolute(const char* path, DynBuf* db) {
  int ret = 0;
  if(!path_isabs(path)) {
    dbuf_realloc(db, PATH_MAX + 1);
    if(getcwd(db->buf, PATH_MAX + 1))
      db->size = strlen(db->buf);
    else
      db->size = 0;
    if(strcmp(path, ".")) {
      dbuf_putc(db, PATHSEP_C);
      dbuf_putstr(db, path);
    }
    return 1;
  } else {
    dbuf_putstr(db, path);
  }
  return 0;
}

int
path_absolute_db(DynBuf* db) {
  int ret = 0;
  dbuf_putc(db, '\0');
  db->size--;

  if(!path_isabs(db->buf)) {
    DynBuf tmp;
    dbuf_init(&tmp);
    dbuf_put(&tmp, db->buf, db->size);

    dbuf_realloc(db, PATH_MAX + 1);
    if(getcwd(db->buf, PATH_MAX)) {
      db->size = strlen(db->buf);
    }

    dbuf_putc(db, PATHSEP_C);
    dbuf_put(db, tmp.buf, tmp.size);
    dbuf_free(&tmp);
    ret = 1;
  }
  if(db->size && path_issep(db->buf[db->size - 1]))
    --db->size;
  dbuf_putc(db, '\0');
  db->size--;
  return ret;
}

static void
path_append(const char* x, size_t len, DynBuf* db) {
  if(db->size > 0 && db->buf[db->size - 1] != PATHSEP_C)
    dbuf_putc(db, PATHSEP_C);

  if(len > 2 && x[0] == '.' && x[1] == PATHSEP_C) {
    x += 2;
    len -= 2;
  }
  dbuf_put(db, x, len);
}
static int
path_getsep(const char* path) {
  while(*path) {
    if(path_issep(*path))
      return *path;
    ++path;
  }
  return '\0';
}

static size_t
path_collapse(char* path, size_t n) {
  char* x = path;
  int ret = 0;
  char sep = path_getsep(path);
  char* end = x + n;
  while(x < end) {
    size_t i = byte_chr(x, end - x, sep);
    if(x + i < end) {
      i++;
      if(x + i + 2 < end) {
        if(x[i] == '.' && x[i + 1] == '.' && (x + i + 2 == end || x[i + 2] == sep)) {
          i += 3;
          memcpy(x, &x[i], n - i);
          end -= i;
          ret++;
          continue;
        }
      }
    }
    x += i;
    n -= i;
  }
  n = x - path;
  if(n > 3 && path[n - 1] == PATHSEP_C && path[n - 2] == '.' && path[n - 3] == PATHSEP_C)
    n -= 3;
  else if(n > 2 && path[n - 1] == '.' && path[n - 2] == PATHSEP_C)
    n -= 2;
  return n;
}

static int
path_canonical_buf(DynBuf* db) {
  db->size = path_collapse(db->buf, db->size);
  dbuf_putc(db, '\0');
  db->size--;
  return 1;
}

static int
path_canonical(const char* path, DynBuf* db) {
  db->size = 0;
  dbuf_putstr(db, path);
  dbuf_putc(db, '\0');
  db->size--;
  return path_canonical_buf(db);
}

static void
path_concat(const char* a, size_t alen, const char* b, size_t blen, DynBuf* db) {
  DynBuf tmp;
  const char* x;
  size_t size;
  dbuf_init(&tmp);

  path_append(a, alen, &tmp);
  path_append(b, blen, &tmp);

  x = tmp.buf;
  size = tmp.size;
  if(size > 2 && tmp.buf[0] == '.' && tmp.buf[1] == PATHSEP_C) {
    x += 2;
    size -= 2;
  }
  dbuf_put(db, x, size);
  dbuf_putc(db, '\0');
  db->size--;

  dbuf_free(&tmp);
}

int
path_find(const char* path, const char* name, DynBuf* db) {
  DIR* dir;
  struct dirent* entry;
  int ret = 0;

  if((dir = opendir(path)))
    return 0;

  while((entry = readdir(dir))) {
    const char* s = entry->d_name;
    if(!strcasecmp(s, name)) {
      dbuf_putstr(db, path);
      dbuf_putc(db, PATHSEP_C);
      dbuf_putstr(db, s);
      ret = 1;
      break;
    }
  }

  closedir(dir);
  return ret;
}

static size_t
path_num(const char* p, size_t len, int n) {
  const char *s = p, *e = p + len;
  while(s < e) {
    s += path_skip(s, e - s);
    if(--n <= 0)
      break;
  }
  return s - p;
}

#define MAX_NUM(a, b) ((a) > (b) ? (a) : (b))

int
path_relative(const char* path, const char* relative_to, DynBuf* db) {
  size_t n1, n2, i, n;
  DynBuf rel, p, r;
  dbuf_init(&rel);
  dbuf_init(&p);
  dbuf_init(&r);
  db->size = 0;

  dbuf_putstr(&r, relative_to);
  dbuf_putstr(&p, path);

  if(r.size >= 2 && !memcmp(r.buf, "..", 2))
    path_absolute_db(&r);
  if(p.size >= 2 && !memcmp(p.buf, "..", 2))
    path_absolute_db(&p);
  if(r.size == p.size && !memcmp(r.buf, p.buf, p.size)) {
    dbuf_putstr(db, ".");
    return 1;
  }

  n1 = dbuf_count(&p, PATHSEP_C) + 1;
  n2 = dbuf_count(&r, PATHSEP_C) + 1;
  n = MAX_NUM(n1, n2);
  for(i = 0; i < n; ++i) {
    size_t l1, l2;
    char* s1 = dbuf_at_n(&p, i, &l1, PATHSEP_C);
    char* s2 = dbuf_at_n(&r, i, &l2, PATHSEP_C);
    if(l1 != l2)
      break;
    if(memcmp(s1, s2, l1))
      break;
  }

  // strlist_init(&rel, PATHSEP_C);
  while(n2-- > i) { dbuf_putstr(&rel, "..."); }
  while(i < n1) {
    char* s = dbuf_at_n(&p, i, &n, PATHSEP_C);
    dbuf_put(&rel, s, n);
    ++i;
  }

  if(rel.size == 0) {
    dbuf_putstr(db, ".");
  } else {
    db->size = 0;
    dbuf_put(db, rel.buf, rel.size);
  }
  dbuf_free(&p);
  dbuf_free(&r);
  dbuf_free(&rel);
  return 1;
}

#define NOTFIRST 0x80

static int
path_fnmatch(const char* pattern, unsigned int plen, const char* string, unsigned int slen, int flags) {
start:
  if(slen == 0) {
    while(plen && *pattern == '*') {
      pattern++;
      plen--;
    }
    return (plen ? PATH_FNM_NOMATCH : 0);
  }
  if(plen == 0)
    return PATH_FNM_NOMATCH;
  if(*string == '.' && *pattern != '.' && (flags & PATH_FNM_PERIOD)) {
    if(!(flags & NOTFIRST))
      return PATH_FNM_NOMATCH;
    if((flags & PATH_FNM_PATHNAME) && string[-1] == '/')
      return PATH_FNM_NOMATCH;
  }
  flags |= NOTFIRST;
  switch(*pattern) {
    case '[': {
      const char* start;
      int neg = 0;
      pattern++;
      plen--;
      if(*string == '/' && (flags & PATH_FNM_PATHNAME))
        return PATH_FNM_NOMATCH;
      neg = (*pattern == '!');
      pattern += neg;
      plen -= neg;
      start = pattern;
      while(plen) {
        int res = 0;
        if(*pattern == ']' && pattern != start)
          break;
        if(*pattern == '[' && pattern[1] == ':') {
        } else {
          if(plen > 1 && pattern[1] == '-' && pattern[2] != ']') {
            res = (*string >= *pattern && *string <= pattern[2]);
            pattern += 3;
            plen -= 3;
          } else {
            res = (*pattern == *string);
            pattern++;
            plen--;
          }
        }
        if((res && !neg) || ((!res && neg) && *pattern == ']')) {
          while(plen && *pattern != ']') {
            pattern++;
            plen--;
          }
          pattern += !!plen;
          plen -= !!plen;
          string++;
          slen--;
          goto start;
        } else if(res && neg)
          break;
      }
    } break;
    case '\\': {
      if(!(flags & PATH_FNM_NOESCAPE)) {
        pattern++;
        plen--;
        if(plen)
          goto match;
      } else
        goto match;
    } break;
    case '*': {
      if((*string == '/' && (flags & PATH_FNM_PATHNAME)) || path_fnmatch(pattern, plen, string + 1, slen - 1, flags)) {
        pattern++;
        plen--;
        goto start;
      }
      return 0;
    }
    case '?': {
      if(*string == '/' && (flags & PATH_FNM_PATHNAME))
        break;
      pattern++;
      plen--;
      string++;
      slen--;
    }
      goto start;
    default:
    match : {
      if(*pattern == *string) {
        pattern++;
        plen--;
        string++;
        slen--;
        goto start;
      }
    } break;
  }
  return PATH_FNM_NOMATCH;
}

char*
path_gethome(int uid) {
  FILE* fp;
  long id;

  char *line, *ret = 0;
  char buf[1024];
  static char home[PATH_MAX + 1];
  if((fp = fopen("/etc/passwd", "r")) == 0)
    return 0;
  while((line = fgets(buf, sizeof(buf) - 1, fp))) {
    size_t p, n, len = strlen(line);
    char *user, *id, *dir;
    while(len > 0 && is_whitespace_char(buf[len - 1])) buf[--len] = '\0';
    user = buf;
    user[p = str_chr(user, ':')] = '\0';
    line = buf + p + 1;
    for(n = 1; n > 0; n--) {
      p = str_chr(line, ':');
      line[p] = '\0';
      line += p + 1;
    }
    id = line;
    for(n = 3; n > 0; n--) {
      p = str_chr(line, ':');
      line[p] = '\0';
      line += p + 1;
    }
    if(atoi(id) != uid)
      continue;
    dir = line;
    n = str_chr(line, ':');
    strncpy(home, dir, n);
    home[n] = '\0';
    ret = home;
  }
  fclose(fp);
  return ret;
}

static JSValue
js_path_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  const char* str;
  struct stat st;
  char buf[PATH_MAX + 1];
  size_t len = 0, pos;
  JSValue ret = JS_UNDEFINED;
  if(argc > 0)
    str = JS_ToCStringLen(ctx, &len, argv[0]);

  switch(magic) {
    case METHOD_BASENAME: ret = JS_NewString(ctx, basename(str)); break;
    case METHOD_DIRNAME:
      pos = str_rchrs(str, "/\\", 2);
      if(pos < len)
        len = pos;
      ret = JS_NewStringLen(ctx, str, len);
      break;
    case METHOD_READLINK:
      if(readlink(str, buf, sizeof(buf)) > 0) {
        ret = JS_NewString(ctx, buf);
      }
      break;
    case METHOD_REALPATH:
      if(realpath(str, buf)) {
        ret = JS_NewString(ctx, buf);
      }
      break;
    case METHOD_EXISTS: {
      ret = JS_NewBool(ctx, access(str, 0) == 0);
      break;
    }

    case METHOD_GETCWD: {
      if(getcwd(buf, sizeof(buf))) {
        ret = JS_NewString(ctx, buf);
      }
      break;
    }
    case METHOD_IS_ABSOLUTE: {
      if(str && str[0]) {
        ret = JS_NewBool(ctx, path_isabs(str));
      }
      break;
    }
    case METHOD_IS_DIRECTORY: {
      if(lstat(str, &st) == 0) {
        ret = JS_NewBool(ctx, S_ISDIR(st.st_mode));
      }
      break;
    }
    case METHOD_IS_SEPARATOR:
      if(str && str[0]) {
        ret = JS_NewBool(ctx, path_issep(str[0]));
      }
      break;

    case METHOD_ABSOLUTE: break;
    case METHOD_APPEND: break;
    case METHOD_CANONICAL: break;
    case METHOD_CANONICALIZE: break;
    case METHOD_COLLAPSE: break;
    case METHOD_CONCAT: break;
    case METHOD_FIND: break;
    case METHOD_FNMATCH: break;
    case METHOD_GETHOME: {
      ret = JS_NewString(ctx, path_gethome(getuid()));
      break;
    }
    case METHOD_GETSEP: break;
    case METHOD_LEN: break;
    case METHOD_LEN_S: break;
    case METHOD_NUM: break;
    case METHOD_RELATIVE: break;
    case METHOD_RIGHT: break;
    case METHOD_SKIP: break;
    case METHOD_SKIPS: break;
    case METHOD_SKIP_SEPARATOR: break;
    case METHOD_SPLIT: break;
  }
  return ret;
}

static const JSCFunctionListEntry js_path_funcs[] = {
    JS_CFUNC_MAGIC_DEF("absolute", 1, js_path_method, METHOD_ABSOLUTE),
    JS_CFUNC_MAGIC_DEF("append", 1, js_path_method, METHOD_APPEND),
    JS_CFUNC_MAGIC_DEF("basename", 1, js_path_method, METHOD_BASENAME),
    JS_CFUNC_MAGIC_DEF("canonical", 1, js_path_method, METHOD_CANONICAL),
    JS_CFUNC_MAGIC_DEF("canonicalize", 1, js_path_method, METHOD_CANONICALIZE),
    JS_CFUNC_MAGIC_DEF("collapse", 1, js_path_method, METHOD_COLLAPSE),
    JS_CFUNC_MAGIC_DEF("concat", 1, js_path_method, METHOD_CONCAT),
    JS_CFUNC_MAGIC_DEF("dirname", 1, js_path_method, METHOD_DIRNAME),
    JS_CFUNC_MAGIC_DEF("exists", 1, js_path_method, METHOD_EXISTS),
    JS_CFUNC_MAGIC_DEF("find", 1, js_path_method, METHOD_FIND),
    JS_CFUNC_MAGIC_DEF("fnmatch", 1, js_path_method, METHOD_FNMATCH),
    JS_CFUNC_MAGIC_DEF("getcwd", 1, js_path_method, METHOD_GETCWD),
    JS_CFUNC_MAGIC_DEF("gethome", 1, js_path_method, METHOD_GETHOME),
    JS_CFUNC_MAGIC_DEF("getsep", 1, js_path_method, METHOD_GETSEP),
    JS_CFUNC_MAGIC_DEF("is_absolute", 1, js_path_method, METHOD_IS_ABSOLUTE),
    JS_CFUNC_MAGIC_DEF("is_directory", 1, js_path_method, METHOD_IS_DIRECTORY),
    JS_CFUNC_MAGIC_DEF("is_separator", 1, js_path_method, METHOD_IS_SEPARATOR),
    JS_CFUNC_MAGIC_DEF("len", 1, js_path_method, METHOD_LEN),
    JS_CFUNC_MAGIC_DEF("len_s", 1, js_path_method, METHOD_LEN_S),
    JS_CFUNC_MAGIC_DEF("num", 1, js_path_method, METHOD_NUM),
    JS_CFUNC_MAGIC_DEF("readlink", 1, js_path_method, METHOD_READLINK),
    JS_CFUNC_MAGIC_DEF("realpath", 1, js_path_method, METHOD_REALPATH),
    JS_CFUNC_MAGIC_DEF("relative", 1, js_path_method, METHOD_RELATIVE),
    JS_CFUNC_MAGIC_DEF("right", 1, js_path_method, METHOD_RIGHT),
    JS_CFUNC_MAGIC_DEF("skip", 1, js_path_method, METHOD_SKIP),
    JS_CFUNC_MAGIC_DEF("skips", 1, js_path_method, METHOD_SKIPS),
    JS_CFUNC_MAGIC_DEF("skip_separator", 1, js_path_method, METHOD_SKIP_SEPARATOR),
    JS_CFUNC_MAGIC_DEF("split", 1, js_path_method, METHOD_SPLIT),
};

static int
js_path_init(JSContext* ctx, JSModuleDef* m) {
  return JS_SetModuleExportList(ctx, m, js_path_funcs, countof(js_path_funcs));
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_path
#endif

JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_path_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_path_funcs, countof(js_path_funcs));
  return m;
}
