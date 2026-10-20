#include "char-utils.h"
#include "buffer-utils.h"
#include "defines.h"
#include "debug.h"
#include "path.h"
#include "utils.h"

#include <quickjs.h>
#include <cutils.h>

#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* component helpers behind at(), slice(), length(), isin(), equal() and
 * friends: (p, n) views, or NUL-terminated when the name ends in _z.
 *
 *   js_path_comp / _z       length of the component at p (from pos)
 *   js_path_sep / _z        length of the separator run at p
 *   js_path_skip            component plus the separators after it
 *   js_path_right           offset of the last component of p[0..n)
 *   js_path_components      length of the first n components
 *   js_path_length / _z     number of components
 *   js_path_at / _z         component i: pointer, length in *len_ptr
 *   js_path_slice_into      components start..end appended to db
 *   js_path_diff / _equal   componentwise compare, "." components ignored
 */
static inline size_t
js_path_comp_z(const char* p) {
  const char* s = p;

  while(*s && !path_issep(*s))
    ++s;

  return s - p;
}

static inline size_t
js_path_comp(const char* p, size_t len, size_t pos) {
  const char *start = p, *end = p + len;

  if(pos > len)
    pos = len;
  p += pos;
  while(p < end && !path_issep(*p))
    ++p;
  return p - start;
}

static inline size_t
js_path_sep_z(const char* p) {
  const char* s = p;

  while(*s && path_issep(*s))
    ++s;
  return s - p;
}

static inline size_t
js_path_sep(const char* p, size_t len) {
  const char *start = p, *end = p + len;

  while(p < end && path_issep(*p))
    ++p;
  return p - start;
}

static inline size_t
js_path_skip(const char* s, size_t n) {
  const char *p = s, *e = s + n;

  p += js_path_comp(s, n, 0);
  p += js_path_sep(p, e - p);
  return p - s;
}

static inline size_t
js_path_right(const char* s, size_t n) {
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
js_path_getsep(const char* path) {
  while(*path) {
    if(path_issep(*path))
      return *path;
    ++path;
  }

  return '\0';
}

static size_t
js_path_components(const char* p, size_t len, uint32_t n) {
  const char *s = p, *e = p + len;
  size_t count = 0;

  while(s < e) {
    s += js_path_sep(s, e - s);

    if(s == e)
      break;

    s += js_path_comp(s, e - s, 0);

    if(--n <= 0)
      break;

    count++;
  }

  return count;
}

static const char*
js_path_at(const char* p, size_t plen, size_t* len_ptr, int i) {
  size_t next, len = 0;
  const char* q;

  for(q = p + plen; p < q;) {
    len = js_path_comp(p, q - p, 0);
    next = len + js_path_sep(&p[len], q - p - len);

    if(i <= 0)
      break;

    p += next;
    --i;
  }

  if(len_ptr)
    *len_ptr = len;

  return p;
}

static const char*
js_path_at_z(const char* p, size_t* len_ptr, int i) {
  size_t next, len;

  for(;;) {
    len = js_path_comp_z(p);
    next = len + js_path_sep_z(&p[len]);

    if(i <= 0)
      break;

    p += next;
    --i;
  }

  if(len_ptr)
    *len_ptr = len;

  return p;
}

static size_t
js_path_length(const char* p, size_t slen) {
  int pos = 0;
  size_t next, len;
  const char* end = p + slen;

  for(; p < end; p += next) {
    len = js_path_comp_z(p);
    next = len + js_path_sep_z(&p[len]);

    if(pos && len == 0)
      break;

    ++pos;
  }

  return pos;
}

static size_t
js_path_length_z(const char* p) {
  return js_path_length(p, strlen(p));
}

static int
js_path_slice_into(const char* p, int start, int end, DynBuf* db) {
  int i;
  size_t next, len;
  size_t n = db->size;

  for(i = 0; i < end; i++) {
    len = js_path_comp_z(p);
    next = len + js_path_sep_z(&p[len]);

    if(i >= start) {
      if((db->size > n && db->buf[db->size - 1] != PATHSEP_C) || (i == start && len == 0))
        dbuf_putc(db, PATHSEP_C);

      dbuf_put(db, (const uint8_t*)p, len);
    }

    p += next;
  }

  return i;
}

static int
js_path_isin(const char* p, size_t len, const char* dir, size_t dirlen) {
  size_t i;
  size_t plen = js_path_length(p, len);
  size_t dlen = js_path_length(dir, dirlen);

  if(plen < dlen)
    return 0;

  for(i = 0; i < dlen; i++) {
    size_t pcomplen, dcomplen;
    const char *q, *pdir;
    q = js_path_at(p, len, &pcomplen, i);
    pdir = js_path_at(dir, dirlen, &dcomplen, i);

    if(!(pcomplen == dcomplen && !strncmp(q, pdir, pcomplen)))
      return 0;
  }

  return 1;
}

static int
js_path_diff(const char* a, size_t la, const char* b, size_t lb) {
  size_t aindex = 0, bindex = 0, alen = js_path_length(a, la), blen = js_path_length(b, lb);
  int ret = 0;

  while(aindex < alen && bindex < blen) {
    size_t an, bn;
    const char *p, *q;

    do
      p = js_path_at(a, la, &an, aindex++);
    while(an == 1 && *p == '.');

    do
      q = js_path_at(b, lb, &bn, bindex++);
    while(bn == 1 && *q == '.');

    if(an != bn)
      return an - bn;

    if((ret = strncmp(p, q, an)))
      return ret;
  }

  if(aindex < alen)
    return alen - aindex;
  if(bindex < blen)
    return -(blen - bindex);

  return 0;
}

static inline int
js_path_equal(const char* a, size_t la, const char* b, size_t lb) {
  return 0 == js_path_diff(a, la, b, lb);
}

/* file type bits that Windows' <sys/stat.h> lacks */
#ifndef S_IFMT
#define S_IFMT 0170000
#endif
#ifndef S_IFIFO
#define S_IFIFO 0010000
#endif
#ifndef S_IFBLK
#define S_IFBLK 0060000
#endif
#ifndef S_IFLNK
#define S_IFLNK 0120000
#endif
#ifndef S_IFSOCK
#define S_IFSOCK 0140000
#endif

/* JSCFunctionListEntry.magic is an int16_t: S_IFREG/S_IFLNK/S_IFSOCK do not fit,
 * so the dispatch value is the type nibble, `S_IFDIR >> 12 == 4` */
#define PATH_TYPE(flag) ((flag) >> 12)

/**
 * \defgroup quickjs-path quickjs-path: Directory path
 * @{
 */
// thread_local JSValue path_object;

enum {
  PATH_BASENAME,
  PATH_BASEPOS,
  PATH_BASELEN,
  PATH_DIRNAME,
  PATH_DIRLEN,
  PATH_EXISTS,
  PATH_EXTNAME,
  PATH_EXTPOS,
  PATH_EXTLEN,
  PATH_FNMATCH,
  PATH_GETCWD,
  PATH_GETHOME,
  PATH_GETSEP,
  PATH_IS_ABSOLUTE,
  PATH_IS_RELATIVE,
  PATH_LENGTH,
  PATH_COMPONENTS,
  PATH_READLINK,
  PATH_RIGHT,
  PATH_SKIP,
  PATH_SKIP_SEPARATOR,
  PATH_IS_SEPARATOR,
  PATH_ABSOLUTE,
  PATH_CANONICAL,
  PATH_NORMALIZE,
  PATH_REALPATH,
  PATH_AT,
  PATH_SEARCH,
  PATH_RELATIVE,
  PATH_ISIN,
  PATH_EQUAL,
  PATH_TOARRAY,
};

/* Node's path.dirname length (not POSIX: the separator run before the basename loses one
 * byte only), 0 for "."
 *
 *   "/tmp/x" -> 4    "/foo" -> 1    "a//b" -> 2 ("a/")    "//a" -> 2
 */
static int
js_path_exists_cb(const char* p, void* arg) {
  (void)arg;
  return path_exists_s(p);
}

static size_t
js_path_dirlen(const char* path, size_t n) {
  int hasroot, matched = 1;
  ssize_t end = -1, i;

  if(n == 0)
    return 0;

  hasroot = path_issep(path[0]);

  for(i = n - 1; i >= 1; --i) {
    if(path_issep(path[i])) {
      if(!matched) {
        end = i;
        break;
      }
    } else {
      matched = 0;
    }
  }

  if(end == -1)
    return hasroot ? 1 : 0;

  return hasroot && end == 1 ? 2 : end;
}

/* extension of a[0..n) per Node's path.extname: `pos` is the offset of the dot (n if none),
 * `len` the bytes from it to the end of the basename
 *
 *   "a/b.tar.gz" -> ".gz"    ".bashrc" -> none    "a.txt/" -> ".txt"
 */
static size_t
js_path_extpos(const char* a, size_t n) {
  return path_extoff(a, n);
}

static size_t
js_path_extlen(const char* a, size_t n) {
  size_t off = path_extoff(a, n), bl;
  const char* b = path_basename(a, n, &bl);

  return off == n ? 0 : (size_t)(b - a) + bl - off;
}

/* basename of a[0..n) as Node's path.basename sees it: `*len` bytes at the returned
 * offset; empty for "" and for a path of only separators
 *
 *   "/tmp/test/" -> pos 5 len 4    "/" -> pos 1 len 0
 */
static size_t
js_path_basename(const char* a, size_t n, size_t* len) {
  const char* b = path_basename(a, n, len);

  if(*len && path_issep(*b)) {
    *len = 0;
    return n;
  }

  return b - a;
}

/* normalizes p[0..n) into `db` the way Node's path.normalize does: "" is ".", a
 * trailing separator stays
 *
 *   "a//b/../c/" -> "a/c/"    "" -> "."
 */
static void
js_path_normalize(const char* p, size_t n, DynBuf* db) {
  size_t w;

  db->size = 0;

  if(dbuf_claim(db, n + 4))
    return;

  w = path_canon((char*)db->buf, db->allocated_size, p, n);
  db->size = w;

  if(n && path_issep(p[n - 1]) && w && !path_issep(db->buf[w - 1]))
    dbuf_putc(db, PATHSEP_C);

  dbuf_0(db);
}

/* like js_path_normalize() after joining the current directory in front of a relative `p`
 * (what path.absolute does)
 */
static void
js_path_absolute(const char* p, size_t n, DynBuf* db) {
  DynBuf tmp;

  dbuf_init2(&tmp, 0, 0);

  if(!path_isabs(p, n) && path_getcwd(&tmp) && tmp.size) {
    if(n && tmp.buf[tmp.size - 1] != PATHSEP_C)
      dbuf_putc(&tmp, PATHSEP_C);
  } else {
    tmp.size = 0;
  }

  dbuf_put(&tmp, (const uint8_t*)p, n);
  js_path_normalize((const char*)tmp.buf, tmp.size, db);
  dbuf_free(&tmp);
}

/* js_path_invalid_arg: throws Node's ERR_INVALID_ARG_TYPE for argument `v`.
 *
 * ```js
 * path.isAbsolute(5);  // TypeError [ERR_INVALID_ARG_TYPE]:
 *                      // The "path" argument must be of type string. Received type number (5)
 * ```
 *
 *   JSValueConst  v         the rejected argument
 *   const char*   name      parameter name for the message: "path", "suffix", "paths[1]", ...
 *   const char*   expected  "string" or "object"
 *
 * returns -1 with the exception pending.
 */
static int
js_path_invalid_arg(JSContext* ctx, JSValueConst v, const char* name, const char* expected) {
  JSValue err, tmp;
  const char* s;
  DynBuf db = DBUF_INIT_CTX(ctx);

  dbuf_printf(&db, "The \"%s\" argument must be of type %s. Received ", name, expected);

  if(JS_IsUndefined(v))
    dbuf_putstr(&db, "undefined");
  else if(JS_IsNull(v))
    dbuf_putstr(&db, "null");
  else if(JS_IsFunction(ctx, v)) {
    tmp = JS_GetPropertyStr(ctx, v, "name");
    s = JS_IsString(tmp) ? JS_ToCString(ctx, tmp) : 0;
    dbuf_printf(&db, "function %s", s ? s : "");
    if(s)
      JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, tmp);
  } else if(JS_IsObject(v)) {
    JSValue ctor = JS_GetPropertyStr(ctx, v, "constructor");

    tmp = JS_IsObject(ctor) ? JS_GetPropertyStr(ctx, ctor, "name") : JS_UNDEFINED;
    s = JS_IsString(tmp) ? JS_ToCString(ctx, tmp) : 0;
    dbuf_printf(&db, "an instance of %s", s && *s ? s : "Object");
    if(s)
      JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, tmp);
    JS_FreeValue(ctx, ctor);
  } else {
    /* Symbol(x) cannot go through ToString: String(v) does it */
    JSValue g = JS_GetGlobalObject(ctx), str = JS_GetPropertyStr(ctx, g, "String");

    tmp = JS_Call(ctx, str, JS_UNDEFINED, 1, &v);
    s = JS_IsString(tmp) ? JS_ToCString(ctx, tmp) : 0;
    dbuf_printf(&db, "type %s (%s%s)", JS_IsNumber(v) ? "number" : JS_IsBool(v) ? "boolean" : JS_IsBigInt(ctx, v) ? "bigint" : "symbol", s ? s : "", JS_IsBigInt(ctx, v) ? "n" : "");
    if(s)
      JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, tmp);
    JS_FreeValue(ctx, str);
    JS_FreeValue(ctx, g);
  }

  dbuf_0(&db);
  JS_ThrowTypeError(ctx, "%s", (const char*)db.buf);
  dbuf_free(&db);

  err = JS_GetException(ctx);
  JS_SetPropertyStr(ctx, err, "code", JS_NewString(ctx, "ERR_INVALID_ARG_TYPE"));
  JS_Throw(ctx, err);
  return -1;
}

/* returns 0 if v is a string, or -1 with ERR_INVALID_ARG_TYPE pending. */
static int
js_path_check_string(JSContext* ctx, JSValueConst v, const char* name) {
  return JS_IsString(v) ? 0 : js_path_invalid_arg(ctx, v, name, "string");
}

static JSValue
js_path_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  const char *a = 0, *b = 0;
  char buf[PATH_MAX + 1];
  size_t alen = 0, blen = 0;
  JSValue ret = JS_UNDEFINED;

  /* Node's functions reject a non-string; the qjsm-only helpers coerce */
  if(magic == PATH_IS_ABSOLUTE || magic == PATH_BASENAME || magic == PATH_DIRNAME || magic == PATH_EXTNAME) {
    if(js_path_check_string(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, "path"))
      return JS_EXCEPTION;

    if(magic == PATH_BASENAME && argc > 1 && !JS_IsUndefined(argv[1]) && js_path_check_string(ctx, argv[1], "suffix"))
      return JS_EXCEPTION;
  }

  if(argc > 0) {
    a = JS_ToCStringLen(ctx, &alen, argv[0]);

    if(argc > 1)
      b = JS_ToCStringLen(ctx, &blen, argv[1]);
  }

  if(magic != PATH_GETCWD && magic != PATH_GETHOME)
    if(argc == 0 || a == NULL)
      return JS_ThrowTypeError(ctx, "argument 1 must be a string");

  switch(magic) {
    case PATH_BASENAME:
    case PATH_BASEPOS:
    case PATH_BASELEN: {
      size_t len;

      if(b) {
        --argc;
        ++argv;
      }

      if(argc > 1) {
        int64_t index = INT64_MAX;
        JS_ToInt64Ext(ctx, &index, argv[1]);

        index = CLAMP_NUM(WRAP_NUM(index, (int64_t)alen), 0, (int64_t)alen);

        alen = index;
      }

      size_t pos = js_path_basename(a, alen, &len);

      if(blen > 0 && blen <= alen) {
        if(blen == alen && !byte_diff(a, blen, b))
          len = 0;
        else if(blen < len && !byte_diff(&a[pos + len - blen], blen, b))
          len -= blen;
      }

      if(magic == PATH_BASENAME)
        ret = JS_NewStringLen(ctx, a + pos, len);
      else if(magic == PATH_BASEPOS)
        ret = JS_NewUint32(ctx, utf8_strlen(a, pos));
      else if(magic == PATH_BASELEN)
        ret = JS_NewUint32(ctx, utf8_strlen(a + pos, len));

      break;
    }
    case PATH_DIRNAME:
    case PATH_DIRLEN: {
      size_t pos = js_path_dirlen(a, alen);

      if(magic == PATH_DIRNAME)
        ret = pos > 0 ? JS_NewStringLen(ctx, a, pos) : JS_NewStringLen(ctx, ".", 1);
      else if(magic == PATH_DIRLEN)
        ret = pos > 0 ? JS_NewUint32(ctx, utf8_strlen(a, pos)) : JS_NewInt32(ctx, -1);

      break;
    }
    case PATH_READLINK: {
      ssize_t r;

      memset(buf, 0, sizeof(buf));

      if((r = readlink(a, buf, sizeof(buf)) > 0))
        ret = JS_NewString(ctx, buf);

      break;
    }
    case PATH_EXISTS: {
      ret = JS_NewBool(ctx, path_exists_s(a));
      break;
    }
    case PATH_EXTNAME: {
      ret = JS_NewStringLen(ctx, a + js_path_extpos(a, alen), js_path_extlen(a, alen));
      break;
    }
    case PATH_EXTPOS: {
      ret = JS_NewUint32(ctx, utf8_strlen(a, js_path_extpos(a, alen)));
      break;
    }
    case PATH_EXTLEN: {
      ret = JS_NewUint32(ctx, utf8_strlen(a + js_path_extpos(a, alen), js_path_extlen(a, alen)));
      break;
    }
    case PATH_GETCWD: {
      if(getcwd(buf, sizeof(buf)))
        ret = JS_NewString(ctx, buf);

      break;
    }
    case PATH_IS_ABSOLUTE: {
      ret = JS_NewBool(ctx, path_isabs(a, alen));
      break;
    }
    case PATH_IS_RELATIVE: {
      ret = JS_NewBool(ctx, path_isrel_s(a));
      break;
    }

#ifndef __wasi__
    case PATH_GETHOME: {
      const char* home;
#ifdef _WIN32
      home = getenv("USERPROFILE");
#else
      home = path_gethome(getuid());
#endif
      ret = home ? JS_NewString(ctx, home) : JS_NULL;
      break;
    }
#endif

    case PATH_GETSEP: {
      char c;

      if((c = js_path_getsep(a)) != '\0')
        ret = JS_NewStringLen(ctx, &c, 1);

      break;
    }
    case PATH_LENGTH: {
      ret = JS_NewUint32(ctx, utf8_strlen(a, js_path_length(a, alen)));
      break;
    }
    case PATH_COMPONENTS: {
      uint32_t n = UINT32_MAX;

      if(argc > 1)
        JS_ToUint32(ctx, &n, argv[1]);

      ret = JS_NewUint32(ctx, utf8_strlen(a, js_path_components(a, alen, n)));
      break;
    }
    case PATH_RIGHT:
    case PATH_SKIP:
    case PATH_SKIP_SEPARATOR:
    case PATH_IS_SEPARATOR: {
      uint64_t n = 0;

      if(argc > 1) {
        if(JS_ToIndex(ctx, &n, argv[1]))
          n = 0;
        if(n > alen)
          n = alen;
      }

      switch(magic) {
        case PATH_RIGHT: {
          // PATH_RIGHT always operates on the full string, not from a position
          ret = JS_NewUint32(ctx, utf8_strlen(a, js_path_right(a, alen)));
          break;
        }
        case PATH_SKIP: {
          size_t pos = n + js_path_skip(a + n, alen - n);
          ret = JS_NewInt64(ctx, pos == alen ? -1ll : (int64_t)utf8_strlen(a, pos));
          break;
        }
        case PATH_SKIP_SEPARATOR: {
          uint32_t pos = n + js_path_sep(a + n, alen - n);
          ret = JS_NewUint32(ctx, utf8_strlen(a, pos));
          break;
        }
        default: {
          ret = JS_NewBool(ctx, js_path_sep(a + n, alen - n) == alen - n);
          break;
        }
      }

      break;
    }
    case PATH_AT: {
      int32_t idx;
      size_t len;
      const char* p;

      JS_ToInt32(ctx, &idx, argv[1]);

      if(idx < 0) {
        int32_t size = js_path_length_z(a);

        idx = MIN_NUM(size, ((idx % size) + size));
      }

      p = js_path_at_z(a, &len, idx);
      ret = JS_NewStringLen(ctx, p, len);
      break;
    }
    case PATH_FNMATCH: {
      int32_t flags = 0;

      if(argc > 2)
        JS_ToInt32(ctx, &flags, argv[2]);

      ret = JS_NewInt32(ctx, path_fnmatch(a, alen, b, blen, flags));
      break;
    }
    case PATH_ISIN: {
      ret = JS_NewBool(ctx, js_path_isin(a, alen, b, blen));
      break;
    }
    case PATH_EQUAL: {
      ret = JS_NewBool(ctx, js_path_equal(a, alen, b, blen));
      break;
    }
    case PATH_TOARRAY: {
      ret = JS_NewArray(ctx);
      uint32_t idx = 0;

      for(int i = 0;; i++) {
        size_t len;
        const char* p;

        if(!(p = js_path_at_z(a, &len, i)) || (len == 0 && i > 0))
          break;

        JS_SetPropertyUint32(ctx, ret, idx++, JS_NewStringLen(ctx, p, len));
      }

      break;
    }
  }

  if(a)
    JS_FreeCString(ctx, a);

  if(b)
    JS_FreeCString(ctx, b);

  return ret;
}

static JSValue
js_path_method_dbuf(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  const char *a = 0, *b = 0;
  DynBuf db = DBUF_INIT_0();
  size_t alen = 0, blen = 0;
  JSValue ret = JS_UNDEFINED;

  /* relative(x) with one argument is qjsm's own form (x against the cwd) */
  if(magic == PATH_NORMALIZE || magic == PATH_RELATIVE) {
    if(js_path_check_string(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, magic == PATH_RELATIVE ? "from" : "path"))
      return JS_EXCEPTION;

    if(magic == PATH_RELATIVE && argc > 1 && js_path_check_string(ctx, argv[1], "to"))
      return JS_EXCEPTION;
  }

  if(argc > 0) {
    if(!JS_IsString(argv[0]))
      return JS_ThrowTypeError(ctx, "argument 1 must be a string");

    a = JS_ToCStringLen(ctx, &alen, argv[0]);

    if(argc > 1)
      b = JS_ToCStringLen(ctx, &blen, argv[1]);
  }

  dbuf_init_ctx(ctx, &db);

  switch(magic) {
    case PATH_ABSOLUTE: {
      js_path_absolute(a, alen, &db);
      break;
    }
    case PATH_NORMALIZE: {
      js_path_normalize(a, alen, &db);
      break;
    }
    case PATH_REALPATH: {
      if(!path_exists(a, alen) || !path_resolve(a, alen, &db, 0, NULL, 0))
        ret = JS_NULL;
      break;
    }
    case PATH_SEARCH: {
      if(dbuf_claim(&db, alen + blen + 2)) {
        ret = JS_EXCEPTION;
        break;
      }

      size_t n = path_search((char*)db.buf, db.allocated_size, a, alen, b, blen, js_path_exists_cb, NULL);

      ret = n ? JS_NewStringLen(ctx, (const char*)db.buf, n) : JS_NULL;
      break;
    }
    case PATH_RELATIVE: {
      DynBuf buf = DBUF_INIT_0(), buf2 = DBUF_INIT_0();
      const char *from = a, *to = b;

      if(argc == 1) {
        b = a;
        blen = alen;
        a = NULL;
        alen = 0;
        from = NULL;
        to = b;
      }

      if(from == NULL) {
        dbuf_init_ctx(ctx, &buf);
        from = path_getcwd(&buf) ? (const char*)buf.buf : NULL;
      } else if(path_isrel_s(from)) {
        dbuf_init_ctx(ctx, &buf);
        js_path_absolute(a, alen, &buf);
        dbuf_0(&buf);
        from = (const char*)buf.buf;
      }

      if(path_isrel_s(to)) {
        dbuf_init_ctx(ctx, &buf2);
        js_path_absolute(b, blen, &buf2);
        dbuf_0(&buf2);
        to = (const char*)buf2.buf;
      }

      {
        /* both sides absolute and canonical, without trailing separators, like path.resolve() */
        DynBuf fb = DBUF_INIT_0(), tb = DBUF_INIT_0();
        size_t fn, tn, need;

        dbuf_init_ctx(ctx, &fb);
        dbuf_init_ctx(ctx, &tb);
        js_path_absolute(from, strlen(from), &fb);
        js_path_absolute(to, strlen(to), &tb);

        fn = fb.size;
        tn = tb.size;

        while(fn > 1 && path_issep(fb.buf[fn - 1]))
          --fn;
        while(tn > 1 && path_issep(tb.buf[tn - 1]))
          --tn;

        need = path_rel(NULL, 0, (const char*)tb.buf, tn, (const char*)fb.buf, fn);

        if(need == (size_t)-1) {
          dbuf_put(&db, tb.buf, tn);
        } else if(!dbuf_claim(&db, need + 2)) {
          path_rel((char*)db.buf, db.allocated_size, (const char*)tb.buf, tn, (const char*)fb.buf, fn);
          /* path.relative('/a', '/a') is '' in Node */
          db.size = need == 1 && db.buf[0] == '.' ? 0 : need;
        }

        dbuf_free(&fb);
        dbuf_free(&tb);
      }

      if(to == (const char*)buf2.buf)
        dbuf_free(&buf2);

      if(from == (const char*)buf.buf)
        dbuf_free(&buf);
      from = NULL;

      break;
    }
  }

  if(a)
    JS_FreeCString(ctx, a);

  if(b)
    JS_FreeCString(ctx, b);

  return JS_IsUndefined(ret) ? dbuf_tostring_free(&db, ctx) : ret;
}

static JSValue
js_path_join(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const char* str;
  DynBuf db = DBUF_INIT_0(), out = DBUF_INIT_0();
  int i;
  size_t len = 0;
  JSValue ret;

  dbuf_init_ctx(ctx, &db);
  dbuf_init_ctx(ctx, &out);

  for(i = 0; i < argc; i++) {
    if(js_path_check_string(ctx, argv[i], "path") || !(str = JS_ToCStringLen(ctx, &len, argv[i]))) {
      dbuf_free(&db);
      dbuf_free(&out);
      return JS_EXCEPTION;
    }

    if(len > 0) {
      if(db.size > 0)
        dbuf_putc(&db, PATHSEP_C);

      dbuf_put(&db, (const uint8_t*)str, len);
    }

    JS_FreeCString(ctx, str);
  }

  js_path_normalize((const char*)db.buf, db.size, &out);
  ret = JS_NewStringLen(ctx, (const char*)out.buf, out.size);

  dbuf_free(&db);
  dbuf_free(&out);
  return ret;
}

static JSValue
js_path_slice(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const char* str;
  DynBuf db = DBUF_INIT_0();
  int32_t start = 0, end = -1;
  JSValue ret = JS_UNDEFINED;

  dbuf_init_ctx(ctx, &db);

  if((str = JS_ToCString(ctx, argv[0]))) {
    int32_t len = js_path_length_z(str);

    if(argc > 1 && JS_IsNumber(argv[1]))
      JS_ToInt32(ctx, &start, argv[1]);

    if(start < 0)
      start = ((start % len) + len) % len;
    if(start > len)
      start = len;

    if(argc > 2 && JS_IsNumber(argv[2]))
      JS_ToInt32(ctx, &end, argv[2]);
    else
      end = len;

    if(end < 0)
      end = (end % len) + len;
    if(end > len)
      end = len;

    js_path_slice_into(str, start, end, &db);
  }

  ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);

  dbuf_free(&db);
  return ret;
}

static JSValue
js_path_parse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const char* str;
  size_t len = 0, basepos, baselen, rootlen, dirlen, extpos, extlen;
  JSValue ret;

  if(js_path_check_string(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, "path") || !(str = JS_ToCStringLen(ctx, &len, argv[0])))
    return JS_EXCEPTION;

  basepos = js_path_basename(str, len, &baselen);
  rootlen = len > 0 && path_issep(str[0]) ? 1 : 0;

  if(baselen == 0) {
    dirlen = rootlen;
    extpos = len;
    extlen = 0;
  } else {
    dirlen = basepos == 0 ? 0 : basepos == 1 && rootlen ? 1 : basepos - 1;
    extpos = js_path_extpos(str, len);
    extlen = js_path_extlen(str, len);
  }

  ret = JS_NewObject(ctx);

  js_set_propertystr_stringlen(ctx, ret, "root", str, rootlen);
  js_set_propertystr_stringlen(ctx, ret, "dir", str, dirlen);
  js_set_propertystr_stringlen(ctx, ret, "base", &str[basepos], baselen);
  js_set_propertystr_stringlen(ctx, ret, "ext", &str[extpos], extlen);
  js_set_propertystr_stringlen(ctx, ret, "name", &str[basepos], extlen ? extpos - basepos : baselen);

  JS_FreeCString(ctx, str);

  return ret;
}

static const char*
js_path_getstr(JSContext* ctx, JSValueConst obj, const char* name) {
  const char* s = js_get_propertystr_cstring(ctx, obj, name);

  if(s && !*s) {
    JS_FreeCString(ctx, s);
    s = 0;
  }

  return s;
}

/* like Node.js: dir || root, base || name + ext (dot added); the separator is
 * left out when the dir is the root */
static JSValue
js_path_format(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValueConst obj = argc > 0 ? argv[0] : JS_UNDEFINED;
  const char *dir, *root, *base, *name, *ext, *d;
  JSValue ret;
  DynBuf db = DBUF_INIT_0(), bb = DBUF_INIT_0();

  if(!JS_IsObject(obj) || JS_IsFunction(ctx, obj)) {
    js_path_invalid_arg(ctx, obj, "pathObject", "object");
    return JS_EXCEPTION;
  }

  dbuf_init_ctx(ctx, &db);
  dbuf_init_ctx(ctx, &bb);

  root = js_path_getstr(ctx, obj, "root");
  dir = js_path_getstr(ctx, obj, "dir");
  base = js_path_getstr(ctx, obj, "base");

  if(base) {
    dbuf_putstr(&bb, base);
  } else {
    name = js_path_getstr(ctx, obj, "name");
    ext = js_path_getstr(ctx, obj, "ext");

    if(name)
      dbuf_putstr(&bb, name);

    if(ext) {
      if(ext[0] != '.')
        dbuf_putc(&bb, '.');

      dbuf_putstr(&bb, ext);
    }

    if(name)
      JS_FreeCString(ctx, name);
    if(ext)
      JS_FreeCString(ctx, ext);
  }

  if((d = dir ? dir : root)) {
    dbuf_putstr(&db, d);

    if(!(root && !strcmp(d, root)))
      dbuf_putc(&db, PATHSEP_C);
  }

  dbuf_put(&db, bb.buf, bb.size);
  ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);

  if(root)
    JS_FreeCString(ctx, root);
  if(dir)
    JS_FreeCString(ctx, dir);
  if(base)
    JS_FreeCString(ctx, base);

  dbuf_free(&db);
  dbuf_free(&bb);

  return ret;
}

static JSValue
js_path_resolve(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  DynBuf joined = DBUF_INIT_0(), out = DBUF_INIT_0();
  const char* str;
  size_t len;
  int i, start = 0;
  JSValue ret;

  for(i = 0; i < argc; i++)
    if(!JS_IsString(argv[i])) {
      char name[32];

      snprintf(name, sizeof(name), "paths[%d]", i);
      js_path_invalid_arg(ctx, argv[i], name, "string");
      return JS_EXCEPTION;
    }

  /* the last absolute argument wins */
  for(i = argc - 1; i >= 0; i--) {
    int abs;

    if(!(str = JS_ToCStringLen(ctx, &len, argv[i])))
      return JS_EXCEPTION;

    abs = len > 0 && path_issep(str[0]);
    JS_FreeCString(ctx, str);

    if(abs) {
      start = i;
      break;
    }
  }

  dbuf_init_ctx(ctx, &joined);
  dbuf_init_ctx(ctx, &out);

  for(i = start; i < argc; i++) {
    if(!(str = JS_ToCStringLen(ctx, &len, argv[i]))) {
      dbuf_free(&joined);
      dbuf_free(&out);
      return JS_EXCEPTION;
    }

    if(len > 0) {
      if(joined.size > 0)
        dbuf_putc(&joined, PATHSEP_C);

      dbuf_put(&joined, (const uint8_t*)str, len);
    }

    JS_FreeCString(ctx, str);
  }

  js_path_absolute((const char*)joined.buf, joined.size, &out);

  while(out.size > 1 && path_issep(out.buf[out.size - 1]))
    out.size--;

  ret = JS_NewStringLen(ctx, (const char*)out.buf, out.size);

  dbuf_free(&joined);
  dbuf_free(&out);
  return ret;
}

/* path type tests: one function behind isDirectory, isFile, isCharDev, ...
 *
 * ```js
 * path.isDirectory('/tmp');      // true; a symlink to a directory too
 * path.isSymlink('/tmp/link');   // true; the link itself, not its target
 * ```
 *
 *   string  path  file name to test
 *
 *   returns  true if the entry exists and has the type, else false
 *   throws   TypeError if `path` is not a string
 *
 * wired in with JS_CFUNC_MAGIC_DEF(name, 1, js_path_istype, PATH_TYPE(S_IFx)).
 *
 *   magic     JS name      S_IF*      call
 *   4         isDirectory  S_IFDIR    stat
 *   8         isFile       S_IFREG    stat
 *   2         isCharDev    S_IFCHR    stat
 *   6         isBlockDev   S_IFBLK    stat
 *   1         isFIFO       S_IFIFO    stat
 *   12        isSocket     S_IFSOCK   stat
 *   10        isSymlink    S_IFLNK    lstat
 *
 * every test follows symlinks (as fs.statSync, Deno.stat, pathlib) except
 * isSymlink, which must not.
 */
static JSValue
js_path_istype(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  const char* a;
  int ret = 0;

  if(argc == 0 || !(a = JS_ToCString(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a string");

#ifdef _WIN32
  if(magic == PATH_TYPE(S_IFLNK))
    ret = path_issymlink_s(a);
  else
#endif
  {
    struct stat st;

    if((magic == PATH_TYPE(S_IFLNK) ? lstat(a, &st) : stat(a, &st)) == 0)
      ret = PATH_TYPE(st.st_mode & S_IFMT) == magic;
  }

  JS_FreeCString(ctx, a);
  return JS_NewBool(ctx, ret);
}

static const JSCFunctionListEntry js_path_funcs[] = {
    JS_CFUNC_MAGIC_DEF("basename", 2, js_path_method, PATH_BASENAME),
    JS_CFUNC_MAGIC_DEF("basepos", 1, js_path_method, PATH_BASEPOS),
    JS_CFUNC_MAGIC_DEF("baselen", 1, js_path_method, PATH_BASELEN),
    JS_CFUNC_MAGIC_DEF("dirname", 1, js_path_method, PATH_DIRNAME),
    JS_CFUNC_MAGIC_DEF("dirlen", 1, js_path_method, PATH_DIRLEN),
    JS_CFUNC_MAGIC_DEF("exists", 1, js_path_method, PATH_EXISTS),
    JS_CFUNC_MAGIC_DEF("extname", 1, js_path_method, PATH_EXTNAME),
    JS_CFUNC_MAGIC_DEF("extpos", 1, js_path_method, PATH_EXTPOS),
    JS_CFUNC_MAGIC_DEF("extlen", 1, js_path_method, PATH_EXTLEN),
    JS_CFUNC_MAGIC_DEF("fnmatch", 1, js_path_method, PATH_FNMATCH),
    JS_CFUNC_MAGIC_DEF("getcwd", 1, js_path_method, PATH_GETCWD),
#ifndef __wasi__
    JS_CFUNC_MAGIC_DEF("gethome", 1, js_path_method, PATH_GETHOME),
#endif
    JS_CFUNC_MAGIC_DEF("getsep", 1, js_path_method, PATH_GETSEP),
    JS_CFUNC_MAGIC_DEF("isAbsolute", 1, js_path_method, PATH_IS_ABSOLUTE),
    JS_CFUNC_MAGIC_DEF("isRelative", 1, js_path_method, PATH_IS_RELATIVE),
    JS_CFUNC_MAGIC_DEF("isDirectory", 1, js_path_istype, PATH_TYPE(S_IFDIR)),
    JS_CFUNC_MAGIC_DEF("isFile", 1, js_path_istype, PATH_TYPE(S_IFREG)),
    JS_CFUNC_MAGIC_DEF("isCharDev", 1, js_path_istype, PATH_TYPE(S_IFCHR)),
    JS_CFUNC_MAGIC_DEF("isBlockDev", 1, js_path_istype, PATH_TYPE(S_IFBLK)),
    JS_CFUNC_MAGIC_DEF("isFIFO", 1, js_path_istype, PATH_TYPE(S_IFIFO)),
    JS_CFUNC_MAGIC_DEF("isSocket", 1, js_path_istype, PATH_TYPE(S_IFSOCK)),
    JS_CFUNC_MAGIC_DEF("isSymlink", 1, js_path_istype, PATH_TYPE(S_IFLNK)),
    JS_CFUNC_MAGIC_DEF("length", 1, js_path_method, PATH_LENGTH),
    JS_CFUNC_MAGIC_DEF("components", 1, js_path_method, PATH_COMPONENTS),
    JS_CFUNC_MAGIC_DEF("readlink", 1, js_path_method, PATH_READLINK),
    JS_CFUNC_MAGIC_DEF("right", 1, js_path_method, PATH_RIGHT),
    JS_CFUNC_MAGIC_DEF("skip", 1, js_path_method, PATH_SKIP),
    JS_CFUNC_MAGIC_DEF("skipSeparator", 1, js_path_method, PATH_SKIP_SEPARATOR),
    JS_CFUNC_MAGIC_DEF("isSeparator", 1, js_path_method, PATH_IS_SEPARATOR),
    JS_CFUNC_MAGIC_DEF("absolute", 1, js_path_method_dbuf, PATH_ABSOLUTE),
    JS_CFUNC_MAGIC_DEF("canonical", 1, js_path_method_dbuf, PATH_CANONICAL),
    JS_CFUNC_MAGIC_DEF("normalize", 1, js_path_method_dbuf, PATH_NORMALIZE),
    JS_CFUNC_MAGIC_DEF("realpath", 1, js_path_method_dbuf, PATH_REALPATH),
    JS_CFUNC_MAGIC_DEF("at", 2, js_path_method, PATH_AT),
    JS_CFUNC_MAGIC_DEF("search", 2, js_path_method_dbuf, PATH_SEARCH),
    JS_CFUNC_MAGIC_DEF("relative", 2, js_path_method_dbuf, PATH_RELATIVE),
    JS_CFUNC_MAGIC_DEF("isin", 2, js_path_method, PATH_ISIN),
    JS_CFUNC_MAGIC_DEF("equal", 2, js_path_method, PATH_EQUAL),
    JS_CFUNC_MAGIC_DEF("toArray", 1, js_path_method, PATH_TOARRAY),
    JS_CFUNC_DEF("slice", 0, js_path_slice),
    JS_CFUNC_DEF("join", 0, js_path_join),
    JS_CFUNC_DEF("parse", 1, js_path_parse),
    JS_CFUNC_DEF("format", 1, js_path_format),
    JS_CFUNC_DEF("resolve", 0, js_path_resolve),
    JS_PROP_STRING_DEF("delimiter", PATHDELIM_S, JS_PROP_CONFIGURABLE),
    JS_PROP_STRING_DEF("sep", PATHSEP_S, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("FNM_NOMATCH", PATH_FNM_NOMATCH, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("FNM_NOESCAPE", PATH_FNM_NOESCAPE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("FNM_PATHNAME", PATH_FNM_PATHNAME, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("FNM_PERIOD", PATH_FNM_PERIOD, JS_PROP_CONFIGURABLE),
};

/* path.toNamespacedPath(path): Node's Windows long-path helper; on POSIX it returns `path`
 * unchanged, even a non-string. `_makeLong` is the same function object.
 *
 * ```js
 * path.toNamespacedPath('/a/b');   // '/a/b'
 * path._makeLong === path.toNamespacedPath;   // true
 * ```
 */
static JSValue
js_path_tonamespaced(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return argc > 0 ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;
}

/* path.posix: Node's name for the POSIX flavour, here the same functions as
 * the module itself; also the default export (`import path from 'path'`).
 * There is no path.win32. */
static int
js_path_init(JSContext* ctx, JSModuleDef* m) {
  if(m) {
    JSValue posix = JS_NewObject(ctx);

    JSValue ns = JS_NewCFunction(ctx, js_path_tonamespaced, "toNamespacedPath", 1);

    JS_SetModuleExportList(ctx, m, js_path_funcs, countof(js_path_funcs));
    JS_SetPropertyFunctionList(ctx, posix, js_path_funcs, countof(js_path_funcs));
    JS_SetModuleExport(ctx, m, "toNamespacedPath", JS_DupValue(ctx, ns));
    JS_SetModuleExport(ctx, m, "_makeLong", JS_DupValue(ctx, ns));
    JS_SetPropertyStr(ctx, posix, "toNamespacedPath", JS_DupValue(ctx, ns));
    JS_SetPropertyStr(ctx, posix, "_makeLong", ns);
    JS_SetPropertyStr(ctx, posix, "posix", JS_DupValue(ctx, posix)); // path.posix === path
    JS_SetModuleExport(ctx, m, "default", JS_DupValue(ctx, posix));
    JS_SetModuleExport(ctx, m, "posix", posix);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_path
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_path_init))) {
    JS_AddModuleExportList(ctx, m, js_path_funcs, countof(js_path_funcs));
    JS_AddModuleExport(ctx, m, "default");
    JS_AddModuleExport(ctx, m, "posix");
    JS_AddModuleExport(ctx, m, "toNamespacedPath");
    JS_AddModuleExport(ctx, m, "_makeLong");
  }

  return m;
}

/**
 * @}
 */
