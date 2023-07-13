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
thread_local JSValue path_object = {{0}, JS_TAG_UNDEFINED};

enum {
  PATH_BASENAME,
  PATH_DIRNAME,
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
};

static JSValue
js_path_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  const char *a = 0, *b = 0;
  char buf[PATH_MAX + 1];
  size_t alen = 0, blen = 0, pos;
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
    case PATH_BASENAME: {
      size_t len;

      pos = path_basename3(a, &len, alen);

      if(blen && blen < len)
        if(!byte_diff(&a[alen - blen], blen, b))
          len -= blen;

      ret = JS_NewStringLen(ctx, a + pos, len);
      break;
    }

    case PATH_DIRNAME: {
      if((pos = path_dirlen2(a, alen)) < alen)
        ret = JS_NewStringLen(ctx, a, pos);
      else
        ret = JS_NewStringLen(ctx, ".", 1);

      break;
    }

    case PATH_READLINK: {
      ssize_t r;

      memset(buf, 0, sizeof(buf));

      if((r = readlink(a, buf, sizeof(buf)) > 0))
        ret = JS_NewString(ctx, buf);

      break;
    }

      /*#ifndef __wasi__
          case PATH_REALPATH: {
      #ifdef _WIN32
            char dst[PATH_MAX + 1];
            size_t len = GetFullPathNameA(buf, PATH_MAX + 1, dst, NULL);
            ret = JS_NewStringLen(ctx, dst, len);
      #else
            if(realpath(a, buf))
              ret = JS_NewString(ctx, buf);
      #endif
            break;
          }
      #endif*/

    case PATH_EXISTS: {
      ret = JS_NewBool(ctx, path_exists1(a));
      break;
    }

    case PATH_EXTNAME: {
      ret = JS_NewString(ctx, path_extname1(a));
      break;
    }

    case PATH_EXTPOS: {
      const char* extname = path_extname1(a);
      ret = JS_NewUint32(ctx, utf8_strlen(a, extname - a));
      break;
    }

    case PATH_EXTLEN: {
      const char* extname = path_extname1(a);
      ret = JS_NewUint32(ctx, utf8_strlen(extname, strlen(extname)));
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

    case PATH_FNMATCH: {
      int32_t flags = 0;

      if(argc > 2)
        JS_ToInt32(ctx, &flags, argv[2]);

      ret = JS_NewInt32(ctx, path_fnmatch5(a, alen, b, blen, flags));
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
      ret = JS_NewUint32(ctx, path_length2(a, alen));
      break;
    }

    case PATH_COMPONENTS: {
      uint32_t n = UINT32_MAX;

      if(argc > 1)
        JS_ToUint32(ctx, &n, argv[1]);

      ret = JS_NewUint32(ctx, path_components3(a, alen, n));
      break;
    }

    case PATH_RIGHT: {
      ret = JS_NewUint32(ctx, path_right2(a, alen));
      break;
    }

    case PATH_SKIP: {
      uint64_t n = 0;

      if(argc > 1) {
        if(JS_ToIndex(ctx, &n, argv[1]))
          n = 0;
        else if(n > alen)
          n = alen;
      }

      pos = n + path_skip2(a + n, alen - n);
      ret = JS_NewInt64(ctx, pos == alen ? -1ll : (int64_t)pos);
      break;
    }

    case PATH_SKIP_SEPARATOR:
    case PATH_IS_SEPARATOR: {
      uint64_t n = 0;

      if(argc > 1) {
        JS_ToIndex(ctx, &n, argv[1]);

        if(n > alen)
          n = alen;

        a += n;
        alen -= n;
      }

      ret = magic == PATH_SKIP_SEPARATOR ? JS_NewUint32(ctx, n + path_separator2(a, alen)) : JS_NewBool(ctx, path_separator2(a, alen) == alen);
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

    case PATH_ISIN: {
      ret = JS_NewBool(ctx, path_isin4(a, alen, b, blen));
      break;
    }

    case PATH_EQUAL: {
      ret = JS_NewBool(ctx, path_equal4(a, alen, b, blen));
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

  js_dbuf_init(ctx, &db);

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
      DynBuf db = DBUF_INIT_0();
      js_dbuf_allocator(ctx, &db);

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

      dbuf_free(&db);
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
        js_dbuf_allocator(ctx, &buf);
        from = path_getcwd1(&buf);
      } else if(path_isrelative(from)) {
        js_dbuf_allocator(ctx, &buf);
        path_absolute3(a, alen, &buf);
        dbuf_0(&buf);
        from = (const char*)buf.buf;
      }

      if(path_isrelative(to)) {
        js_dbuf_allocator(ctx, &buf2);
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
  DynBuf db = DBUF_INIT_0();
  int i;
  size_t len = 0;
  JSValue ret = JS_UNDEFINED;

  js_dbuf_init(ctx, &db);

  for(i = 0; i < argc; i++) {
    str = JS_ToCStringLen(ctx, &len, argv[i]);

    if(len > 0)
      path_append3(str, len, &db);

    JS_FreeCString(ctx, str);
  }

  len = path_normalize2((char*)db.buf, db.size);
  ret = JS_NewStringLen(ctx, (const char*)db.buf, len);

  dbuf_free(&db);
  return ret;
}

static JSValue
js_path_slice(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const char* str;
  DynBuf db = DBUF_INIT_0();
  int32_t start = 0, end = -1;
  JSValue ret = JS_UNDEFINED;

  js_dbuf_init(ctx, &db);

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
  const char *str, *ext;
  size_t basepos, baselen;
  size_t len = 0, rootlen, dirlen;
  JSValue ret = JS_UNDEFINED;

  str = JS_ToCStringLen(ctx, &len, argv[0]);
  basepos = path_basename3(str, &baselen, len);

  dirlen = basepos - 1 /*path_dirlen2(str, len)*/;
  rootlen = path_root2(str, len);
  ext = path_extname1(str);

  ret = JS_NewObject(ctx);

  js_set_propertystr_stringlen(ctx, ret, "root", str, rootlen);
  js_set_propertystr_stringlen(ctx, ret, "dir", str, dirlen);
  js_set_propertystr_stringlen(ctx, ret, "base", &str[basepos], baselen);
  js_set_propertystr_string(ctx, ret, "ext", ext);
  js_set_propertystr_stringlen(ctx, ret, "name", &str[basepos], baselen - strlen(ext));

  JS_FreeCString(ctx, str);

  return ret;
}

static JSValue
js_path_format(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValueConst obj = argv[0];
  const char *dir, *root, *base, *name, *ext;
  JSValue ret = JS_UNDEFINED;
  DynBuf db = DBUF_INIT_0();

  js_dbuf_init(ctx, &db);

  if((root = js_get_propertystr_cstring(ctx, obj, "root"))) {
    dbuf_putstr(&db, root);
    JS_FreeCString(ctx, root);
  }

  if((dir = js_get_propertystr_cstring(ctx, obj, "dir"))) {
    dbuf_putstr(&db, dir);
    JS_FreeCString(ctx, dir);
  }

  if(db.size)
    dbuf_putc(&db, PATHSEP_C);

  if((base = js_get_propertystr_cstring(ctx, obj, "base"))) {
    dbuf_putstr(&db, base);
    JS_FreeCString(ctx, base);
  } else if((name = js_get_propertystr_cstring(ctx, obj, "name"))) {
    dbuf_putstr(&db, name);
    JS_FreeCString(ctx, name);

    if((ext = js_get_propertystr_cstring(ctx, obj, "ext"))) {
      dbuf_putstr(&db, ext);
      JS_FreeCString(ctx, ext);
    }
  }

  ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);
  dbuf_free(&db);

  return ret;
}

static JSValue
js_path_resolve(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  DynBuf db = DBUF_INIT_0(), cwd = DBUF_INIT_0();
  int i;
  const char* str;
  size_t len = 0;
  JSValue ret = JS_UNDEFINED;
  BOOL absolute = FALSE;

  js_dbuf_init(ctx, &db);
  dbuf_0(&db);

  for(i = argc - 1; i >= 0; i--) {
    if(!JS_IsString(argv[i])) {
      ret = JS_ThrowTypeError(ctx, "argument #%d is not a string", i);
      goto fail;
    }

    str = JS_ToCStringLen(ctx, &len, argv[i]);

    while(len > 0 && str[len - 1] == PATHSEP_C)
      len--;

    if(dbuf_reserve_start(&db, len + 1))
      goto fail;

    if(len > 0) {
      memcpy(db.buf, str, len);
      db.buf[len] = PATHSEP_C;
    }

    JS_FreeCString(ctx, str);

    if((absolute = path_isabsolute2((const char*)db.buf, db.size)))
      break;
  }

  if(!absolute) {
    js_dbuf_init(ctx, &cwd);
    str = path_getcwd1(&cwd);
    len = cwd.size;

    if(dbuf_reserve_start(&db, len + 1))
      goto fail;

    if(len > 0) {
      memcpy(db.buf, str, len);
      db.buf[len] = PATHSEP_C;
    }

    dbuf_free(&cwd);
  }

  dbuf_0(&db);

  if(db.size) {
    db.size = path_normalize2((char*)db.buf, db.size);

    while(db.size > 0 && db.buf[db.size - 1] == PATHSEP_C)
      db.size--;

    ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);
  }

fail:
  dbuf_free(&db);
  return ret;
}

static const JSCFunctionListEntry js_path_funcs[] = {
    JS_CFUNC_MAGIC_DEF("basename", 1, js_path_method, PATH_BASENAME),
    JS_CFUNC_MAGIC_DEF("dirname", 1, js_path_method, PATH_DIRNAME),
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
    JS_CFUNC_DEF("slice", 0, js_path_slice),
    JS_CFUNC_DEF("join", 1, js_path_join),
    JS_CFUNC_DEF("parse", 1, js_path_parse),
    JS_CFUNC_DEF("format", 1, js_path_format),
    JS_CFUNC_DEF("resolve", 1, js_path_resolve),
    JS_PROP_STRING_DEF("delimiter", PATHDELIM_S, JS_PROP_CONFIGURABLE),
    JS_PROP_STRING_DEF("sep", PATHSEP_S, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("FNM_NOMATCH", PATH_FNM_NOMATCH, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("FNM_PATHNAME", PATH_FNM_PATHNAME, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("FNM_NOESCAPE", PATH_FNM_NOESCAPE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("FNM_PERIOD", PATH_FNM_PERIOD, JS_PROP_CONFIGURABLE),
};

static int
js_path_init(JSContext* ctx, JSModuleDef* m) {

  path_object = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, path_object, js_path_funcs, countof(js_path_funcs));

  if(m) {
    JS_SetModuleExportList(ctx, m, js_path_funcs, countof(js_path_funcs));
    JS_SetModuleExport(ctx, m, "default", path_object);
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
  }

  return m;
}

/**
 * @}
 */
