#include "defines.h"
#include <cutils.h>
#include <quickjs.h>
#include "buffer-utils.h"
#include "char-utils.h"
#include "debug.h"

#include <stddef.h>
#include <sys/types.h>
#include <limits.h>
#include <string.h>

#include "path.h"
#include "utils.h"
#ifdef _WIN32
#include <windows.h>
#endif

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
  PATH_IS_DIRECTORY,
  PATH_IS_FILE,
  PATH_IS_CHARDEV,
  PATH_IS_BLOCKDEV,
  PATH_IS_FIFO,
  PATH_IS_SOCKET,
  PATH_IS_SYMLINK,
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

static JSValue
js_path_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  const char *a = 0, *b = 0;
  char buf[PATH_MAX + 1];
  size_t alen = 0, blen = 0;
  JSValue ret = JS_UNDEFINED;

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

      size_t pos = path_basename3(a, &len, alen);

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
      size_t pos = path_dirlen2(a, alen);

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
      ret = JS_NewBool(ctx, path_exists1(a));
      break;
    }
    case PATH_EXTNAME: {
      ret = JS_NewStringLen(ctx, path_extname1(a), path_extlen1(a));
      break;
    }
    case PATH_EXTPOS: {
      ret = JS_NewUint32(ctx, utf8_strlen(a, path_extpos1(a)));
      break;
    }
    case PATH_EXTLEN: {
      ret = JS_NewUint32(ctx, utf8_strlen(path_extname1(a), path_extlen1(a)));
      break;
    }
    case PATH_GETCWD: {
      if(getcwd(buf, sizeof(buf)))
        ret = JS_NewString(ctx, buf);

      break;
    }
    case PATH_IS_ABSOLUTE: {
      ret = JS_NewBool(ctx, path_isabsolute2(a, alen));
      break;
    }
    case PATH_IS_RELATIVE: {
      ret = JS_NewBool(ctx, path_isrelative(a));
      break;
    }
    case PATH_IS_DIRECTORY: {
      ret = JS_NewBool(ctx, path_isdir1(a));
      break;
    }
    case PATH_IS_FILE: {
      ret = JS_NewBool(ctx, path_isfile1(a));
      break;
    }
    case PATH_IS_CHARDEV: {
      ret = JS_NewBool(ctx, path_ischardev1(a));
      break;
    }
    case PATH_IS_BLOCKDEV: {
      ret = JS_NewBool(ctx, path_isblockdev1(a));
      break;
    }
    case PATH_IS_FIFO: {
      ret = JS_NewBool(ctx, path_isfifo1(a));
      break;
    }
    case PATH_IS_SOCKET: {
      ret = JS_NewBool(ctx, path_issocket1(a));
      break;
    }
    case PATH_IS_SYMLINK: {
      ret = JS_NewBool(ctx, path_issymlink1(a));
      break;
    }

#ifndef __wasi__
    case PATH_GETHOME: {
      const char* home;
#ifdef _WIN32
      home = getenv("USERPROFILE");
#else
      home = path_gethome1(getuid());
#endif
      ret = home ? JS_NewString(ctx, home) : JS_NULL;
      break;
    }
#endif

    case PATH_GETSEP: {
      char c;

      if((c = path_getsep1(a)) != '\0')
        ret = JS_NewStringLen(ctx, &c, 1);

      break;
    }
    case PATH_LENGTH: {
      ret = JS_NewUint32(ctx, utf8_strlen(a, path_length2(a, alen)));
      break;
    }
    case PATH_COMPONENTS: {
      uint32_t n = UINT32_MAX;

      if(argc > 1)
        JS_ToUint32(ctx, &n, argv[1]);

      ret = JS_NewUint32(ctx, utf8_strlen(a, path_components3(a, alen, n)));
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
          ret = JS_NewUint32(ctx, utf8_strlen(a, path_right2(a, alen)));
          break;
        }
        case PATH_SKIP: {
          size_t pos = n + path_skip2(a + n, alen - n);
          ret = JS_NewInt64(ctx, pos == alen ? -1ll : (int64_t)utf8_strlen(a, pos));
          break;
        }
        case PATH_SKIP_SEPARATOR: {
          uint32_t pos = n + path_separator2(a + n, alen - n);
          ret = JS_NewUint32(ctx, utf8_strlen(a, pos));
          break;
        }
        default: {
          ret = JS_NewBool(ctx, path_separator2(a + n, alen - n) == alen - n);
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
        int32_t size = path_length1(a);

        idx = MIN_NUM(size, ((idx % size) + size));
      }

      p = path_at3(a, &len, idx);
      ret = JS_NewStringLen(ctx, p, len);
      break;
    }
    case PATH_FNMATCH: {
      int32_t flags = 0;

      if(argc > 2)
        JS_ToInt32(ctx, &flags, argv[2]);

      ret = JS_NewInt32(ctx, path_fnmatch5(a, alen, b, blen, flags));
      break;
    }
    case PATH_ISIN: {
      ret = JS_NewBool(ctx, path_isin4(a, alen, b, blen));
      break;
    }
    case PATH_EQUAL: {
      ret = JS_NewBool(ctx, path_equal4(a, alen, b, blen));
      break;
    }
    case PATH_TOARRAY: {
      ret = JS_NewArray(ctx);
      uint32_t idx = 0;

      for(int i = 0;; i++) {
        size_t len;
        const char* p;

        if(!(p = path_at3(a, &len, i)) || (len == 0 && i > 0))
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
      path_absolute3(a, alen, &db);
      break;
    }
    case PATH_NORMALIZE: {
      path_normalize3(a, alen, &db);
      break;
    }
    case PATH_REALPATH: {
      if(!path_realpath3(a, alen, &db))
        ret = JS_NULL;
      break;
    }
    case PATH_SEARCH: {
      const char* pathstr = a;
      /* DynBuf db = DBUF_INIT_0();
       dbuf_init_ctx(ctx, &db);*/

      for(;;) {
        char* file;

        if(!(file = path_search(&pathstr, b, &db))) {
          ret = JS_NULL;
          break;
        }

        if(path_exists1(file)) {
          ret = JS_NewString(ctx, file);
          break;
        }
      }

      // dbuf_free(&db);
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
        from = path_getcwd1(&buf);
      } else if(path_isrelative(from)) {
        dbuf_init_ctx(ctx, &buf);
        path_absolute3(a, alen, &buf);
        dbuf_0(&buf);
        from = (const char*)buf.buf;
      }

      if(path_isrelative(to)) {
        dbuf_init_ctx(ctx, &buf2);
        path_absolute3(b, blen, &buf2);
        dbuf_0(&buf2);
        to = (const char*)buf2.buf;
      }

      path_relative3(to, from, &db);

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
    if(!(str = JS_ToCStringLen(ctx, &len, argv[i]))) {
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

  path_normalize3((const char*)db.buf, db.size, &out);
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
    int32_t len = path_length1(str);

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

    path_slice4(str, start, end, &db);
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

  if(argc < 1 || !(str = JS_ToCStringLen(ctx, &len, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a string");

  basepos = path_basename3(str, &baselen, len);
  rootlen = len > 0 && path_issep(str[0]) ? 1 : 0;

  if(baselen == 0) {
    dirlen = rootlen;
    extpos = len;
    extlen = 0;
  } else {
    dirlen = basepos == 0 ? 0 : basepos == 1 && rootlen ? 1 : basepos - 1;
    extpos = path_extpos1(str);
    extlen = path_extlen1(str);
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

  if(!JS_IsObject(obj))
    return JS_ThrowTypeError(ctx, "argument 1 must be an object");

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
    if(!JS_IsString(argv[i]))
      return JS_ThrowTypeError(ctx, "argument #%d is not a string", i);

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

  path_absolute3((const char*)joined.buf, joined.size, &out);

  while(out.size > 1 && path_issep(out.buf[out.size - 1]))
    out.size--;

  ret = JS_NewStringLen(ctx, (const char*)out.buf, out.size);

  dbuf_free(&joined);
  dbuf_free(&out);
  return ret;
}

static const JSCFunctionListEntry js_path_funcs[] = {
    JS_CFUNC_MAGIC_DEF("basename", 1, js_path_method, PATH_BASENAME),
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
    JS_CFUNC_MAGIC_DEF("isDirectory", 1, js_path_method, PATH_IS_DIRECTORY),
    JS_CFUNC_MAGIC_DEF("isFile", 1, js_path_method, PATH_IS_FILE),
    JS_CFUNC_MAGIC_DEF("isCharDev", 1, js_path_method, PATH_IS_CHARDEV),
    JS_CFUNC_MAGIC_DEF("isBlockDev", 1, js_path_method, PATH_IS_BLOCKDEV),
    JS_CFUNC_MAGIC_DEF("isFIFO", 1, js_path_method, PATH_IS_FIFO),
    JS_CFUNC_MAGIC_DEF("isSocket", 1, js_path_method, PATH_IS_SOCKET),
    JS_CFUNC_MAGIC_DEF("isSymlink", 1, js_path_method, PATH_IS_SYMLINK),
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
    JS_CFUNC_DEF("join", 1, js_path_join),
    JS_CFUNC_DEF("parse", 1, js_path_parse),
    JS_CFUNC_DEF("format", 1, js_path_format),
    JS_CFUNC_DEF("resolve", 1, js_path_resolve),
    JS_PROP_STRING_DEF("delimiter", PATHDELIM_S, JS_PROP_CONFIGURABLE),
    JS_PROP_STRING_DEF("sep", PATHSEP_S, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("FNM_NOMATCH", PATH_FNM_NOMATCH, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("FNM_NOESCAPE", PATH_FNM_NOESCAPE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("FNM_PATHNAME", PATH_FNM_PATHNAME, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("FNM_PERIOD", PATH_FNM_PERIOD, JS_PROP_CONFIGURABLE),
};

/* path.posix: Node's name for the POSIX flavour, here the same functions as
 * the module itself; also the default export (`import path from 'path'`).
 * There is no path.win32. */
static int
js_path_init(JSContext* ctx, JSModuleDef* m) {
  if(m) {
    JSValue posix = JS_NewObject(ctx);

    JS_SetModuleExportList(ctx, m, js_path_funcs, countof(js_path_funcs));
    JS_SetPropertyFunctionList(ctx, posix, js_path_funcs, countof(js_path_funcs));
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
  }

  return m;
}

/**
 * @}
 */
