#include "defines.h"
#include <cutils.h>
#include <quickjs.h>
#include "buffer-utils.h"
#include "char-utils.h"
#include "debug.h"

#include <limits.h>
#include <string.h>

#include "path.h"
#include "utils.h"
#ifdef _WIN32
#include <windows.h>
#endif

/**
 * \defgroup quickjs-path QuickJS module: path - Directory path
 * @{
 */
thread_local JSValue path_object = {{JS_TAG_UNDEFINED}};

enum path_methods {
  METHOD_ABSOLUTE = 0,
  METHOD_APPEND,
  METHOD_BASENAME,
  METHOD_BASEPOS,
  METHOD_BASELEN,
  METHOD_CANONICAL,
  METHOD_COLLAPSE,
  METHOD_CONCAT,
  METHOD_DIRNAME,
  METHOD_DIRLEN,
  METHOD_EXISTS,
  METHOD_EXTNAME,
  METHOD_EXTPOS,
  METHOD_EXTLEN,
  METHOD_FIND,
  METHOD_FNMATCH,
  METHOD_GETCWD,
  METHOD_GETHOME,
  METHOD_GETSEP,
  METHOD_IS_ABSOLUTE,
  METHOD_IS_RELATIVE,
  METHOD_IS_DIRECTORY,
  METHOD_IS_FILE,
  METHOD_IS_CHARDEV,
  METHOD_IS_BLOCKDEV,
  METHOD_IS_FIFO,
  METHOD_IS_SOCKET,
  METHOD_IS_SYMLINK,
  METHOD_IS_SEPARATOR,
  METHOD_LENGTH,
  METHOD_LEN_S,
  METHOD_NORMALIZE,
  METHOD_COMPONENTS,
  METHOD_READLINK,
  METHOD_REALPATH,
  METHOD_RELATIVE,
  METHOD_RIGHT,
  METHOD_SKIP,
  METHOD_SKIPS,
  METHOD_SKIP_SEPARATOR,
  METHOD_SPLIT,
  METHOD_AT,
  METHOD_OBJECT,
  METHOD_FROMOBJ,
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

  switch(magic) {
    case METHOD_BASENAME: {
      const char* o = basename(a);
      size_t len = strlen(o);
      if(b && str_ends(o, b))
        len -= strlen(b);
      ret = JS_NewStringLen(ctx, o, len);
      break;
    }
    case METHOD_BASEPOS: {
      const char* o = basename(a);
      ret = JS_NewUint32(ctx, utf8_strlen(a, o - a));
      break;
    }
    case METHOD_BASELEN: {
      const char* o = basename(a);
      size_t len = strlen(o);
      if(b && str_ends(o, b))
        len -= strlen(b);
      ret = JS_NewUint32(ctx, utf8_strlen(o, len));
      break;
    }

    case METHOD_DIRNAME: {
      if((pos = str_rchrs(a, "/\\", 2)) < alen)
        ret = JS_NewStringLen(ctx, a, pos);
      else if(alen == 1 && a[0] == '.')
        ret = JS_NULL;
      else
        ret = JS_NewStringLen(ctx, ".", 1);
      break;
    }

    case METHOD_DIRLEN: {
      pos = str_rchrs(a, "/\\", 2);
      ret = JS_NewUint32(ctx, utf8_strlen(a, pos));
      break;
    }

    case METHOD_READLINK: {
      ssize_t r;
      memset(buf, 0, sizeof(buf));
      if((r = readlink(a, buf, sizeof(buf)) > 0)) {
        ret = JS_NewString(ctx, buf);
      }
      break;
    }

      /*#ifndef __wasi__
          case METHOD_REALPATH: {
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

    case METHOD_EXISTS: {
      ret = JS_NewBool(ctx, path_exists1(a));
      break;
    }

    case METHOD_EXTNAME: {
      ret = JS_NewString(ctx, path_extname1(a));
      break;
    }

    case METHOD_EXTPOS: {
      const char* extname = path_extname1(a);
      ret = JS_NewUint32(ctx, utf8_strlen(a, extname - a));
      break;
    }

    case METHOD_EXTLEN: {
      const char* extname = path_extname1(a);
      ret = JS_NewUint32(ctx, utf8_strlen(extname, strlen(extname)));
      break;
    }

    case METHOD_GETCWD: {
      if(getcwd(buf, sizeof(buf)))
        ret = JS_NewString(ctx, buf);
      break;
    }

    case METHOD_IS_ABSOLUTE: {
      if(a && a[0])
        ret = JS_NewBool(ctx, path_isabs(a));
      break;
    }

    case METHOD_IS_RELATIVE: {
      if(a && a[0])
        ret = JS_NewBool(ctx, !path_isabs(a));
      break;
    }

    case METHOD_IS_DIRECTORY: {
      ret = JS_NewBool(ctx, path_isdir1(a));
      break;
    }

    case METHOD_IS_FILE: {
      ret = JS_NewBool(ctx, path_isfile1(a));
      break;
    }

    case METHOD_IS_CHARDEV: {
      ret = JS_NewBool(ctx, path_ischardev1(a));
      break;
    }

    case METHOD_IS_BLOCKDEV: {
      ret = JS_NewBool(ctx, path_isblockdev1(a));
      break;
    }

    case METHOD_IS_FIFO: {
      ret = JS_NewBool(ctx, path_isfifo1(a));
      break;
    }

    case METHOD_IS_SOCKET: {
      ret = JS_NewBool(ctx, path_issocket1(a));
      break;
    }

    case METHOD_IS_SYMLINK: {
      ret = JS_NewBool(ctx, path_issymlink1(a));
      break;
    }

    case METHOD_IS_SEPARATOR: {
      if(a && a[0])
        ret = JS_NewBool(ctx, path_issep(a[0]));
      break;
    }

    case METHOD_COLLAPSE: {
      char* s = malloc(alen + 1);

      memcpy(s, a, alen);
      s[alen] = '\0';
      size_t newlen;

      newlen = path_collapse2(s, alen);
      ret = JS_NewStringLen(ctx, s, newlen);
      free(s);
      break;
    }

    case METHOD_FNMATCH: {
      int32_t flags = 0;
      if(argc > 2)
        JS_ToInt32(ctx, &flags, argv[2]);
      ret = JS_NewInt32(ctx, path_fnmatch5(a, alen, b, blen, flags));
      break;
    }

#ifndef __wasi__
    case METHOD_GETHOME: {
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

    case METHOD_GETSEP: {
      char c;
      if((c = path_getsep1(a)) != '\0')
        ret = JS_NewStringLen(ctx, &c, 1);
      break;
    }

    case METHOD_LENGTH: {
      ret = JS_NewUint32(ctx, path_length2(a, alen));
      break;
    }

    case METHOD_COMPONENTS: {
      uint32_t n = UINT32_MAX;
      if(argc > 1)
        JS_ToUint32(ctx, &n, argv[1]);
      ret = JS_NewUint32(ctx, path_components3(a, alen, n));
      break;
    }

    case METHOD_RIGHT: {
      ret = JS_NewUint32(ctx, path_right2(a, alen));
      break;
    }

    case METHOD_SKIP: {
      ret = JS_NewUint32(ctx, path_skip2(a, alen));
      break;
    }

    case METHOD_SKIP_SEPARATOR: {
      int64_t n = 0;
      if(argc > 1)
        JS_ToInt64(ctx, &n, argv[1]);
      ret = JS_NewUint32(ctx, path_separator3(a, alen, n));
      break;
    }
    case METHOD_AT: {
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
    case METHOD_OBJECT: {
      const char* ext = path_extname1(a);
      const char* base = basename(a);

      ret = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, ret, "dir", JS_NewStringLen(ctx, a, path_dirlen2(a, alen)));
      JS_SetPropertyStr(ctx, ret, "base", JS_NewStringLen(ctx, base, ext ? ext - base : strlen(base)));
      if(ext)
        JS_SetPropertyStr(ctx, ret, "ext", JS_NewString(ctx, ext));

      break;
    }
  }

  if(a)
    js_cstring_free(ctx, a);
  if(b)
    js_cstring_free(ctx, b);

  return ret;
}

static JSValue
js_path_method_dbuf(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  const char *a = 0, *b = 0;
  DynBuf db;
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
    case METHOD_ABSOLUTE: {
      path_absolute3(a, alen, &db);
      break;
    }

    case METHOD_APPEND: {
      path_append3(a, alen, &db);
      break;
    }

    case METHOD_CANONICAL: {
      path_canonical3(a, alen, &db);
      break;
    }

    case METHOD_REALPATH: {
      if(!path_realpath3(a, alen, &db))
        ret = JS_NULL;
      break;
    }

    case METHOD_CONCAT: {
      path_concat5(a, alen, b, blen, &db);
      break;
    }

    case METHOD_FIND: {
      // path_find(a, b, &db);
      break;
    }

    case METHOD_RELATIVE: {
      DynBuf cwd = {0, 0, 0, 0, 0, 0};

      if(b == NULL) {
        dbuf_init2(&cwd, JS_GetRuntime(ctx), (DynBufReallocFunc*)js_realloc_rt);
        b = path_getcwd1(&cwd);
      }
      path_relative3(a, b, &db);
      if(b == (const char*)cwd.buf) {
        dbuf_free(&db);
        b = NULL;
      }
      break;
    }

    case METHOD_NORMALIZE: {
      BOOL symbolic = FALSE;
      if(argc > 1)
        symbolic = JS_ToBool(ctx, argv[1]);
      path_normalize3(a, &db, symbolic);
      break;
    }
  }

  if(a)
    js_cstring_free(ctx, a);
  if(b)
    js_cstring_free(ctx, b);

  return JS_IsUndefined(ret) ? dbuf_tostring_free(&db, ctx) : ret;
}

static JSValue
js_path_join(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const char* str;
  DynBuf db;
  int i;
  size_t len = 0;
  JSValue ret = JS_UNDEFINED;
  js_dbuf_init(ctx, &db);
  js_dbuf_init(ctx, &db);
  for(i = 0; i < argc; i++) {
    str = JS_ToCStringLen(ctx, &len, argv[i]);
    path_append3(str, len, &db);
    js_cstring_free(ctx, str);
  }
  if(db.size) {
    ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);
  }
  dbuf_free(&db);
  return ret;
}

static JSValue
js_path_slice(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const char* str;
  DynBuf db;
  int32_t i, start = 0, end = -1;
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
  const char *str, *base, *ext;
  size_t len = 0, rootlen, dirlen;
  JSValue ret = JS_UNDEFINED;

  str = JS_ToCStringLen(ctx, &len, argv[0]);

  base = basename(str);
  dirlen = base - str - 1;
  rootlen = path_root2(str, len);
  ext = path_extname1(str);

  ret = JS_NewObject(ctx);

  js_set_propertystr_stringlen(ctx, ret, "root", str, rootlen);
  js_set_propertystr_stringlen(ctx, ret, "dir", str, dirlen);
  js_set_propertystr_stringlen(ctx, ret, "base", base, strlen(base));
  js_set_propertystr_stringlen(ctx, ret, "ext", ext, strlen(ext));
  js_set_propertystr_stringlen(ctx, ret, "name", base, strlen(base) - strlen(ext));

  js_cstring_free(ctx, str);

  return ret;
}

static JSValue
js_path_format(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValueConst obj = argv[0];
  const char *dir, *root, *base, *name, *ext;
  JSValue ret = JS_UNDEFINED;
  DynBuf db;

  js_dbuf_init(ctx, &db);

  if((dir = js_get_propertystr_cstring(ctx, obj, "dir"))) {
    dbuf_putstr(&db, dir);
    js_cstring_free(ctx, dir);
  } else if((root = js_get_propertystr_cstring(ctx, obj, "root"))) {
    dbuf_putstr(&db, root);
    js_cstring_free(ctx, root);
  }

  if(db.size)
    dbuf_putc(&db, PATHSEP_C);

  if((base = js_get_propertystr_cstring(ctx, obj, "base"))) {
    dbuf_putstr(&db, base);
    js_cstring_free(ctx, base);
  } else if((name = js_get_propertystr_cstring(ctx, obj, "name"))) {
    dbuf_putstr(&db, name);
    js_cstring_free(ctx, name);
    if((ext = js_get_propertystr_cstring(ctx, obj, "ext"))) {
      dbuf_putstr(&db, ext);
      js_cstring_free(ctx, ext);
    }
  }

  ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);
  dbuf_free(&db);

  return ret;
}

static JSValue
js_path_fromobj(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {

  const char *dir, *base, *ext;
  DynBuf db;
  JSValue ret;

  base = js_get_propertystr_cstring(ctx, argv[0], "base");
  ext = js_get_propertystr_cstring(ctx, argv[0], "ext");

  js_dbuf_init(ctx, &db);

  if((dir = js_get_propertystr_cstring(ctx, argv[0], "dir"))) {
    dbuf_putstr(&db, dir);

    if(db.buf[db.size - 1] != PATHSEP_C)
      dbuf_putc(&db, PATHSEP_C);
  }

  if((base = js_get_propertystr_cstring(ctx, argv[0], "base"))) {
    dbuf_putstr(&db, base);
  }
  if((ext = js_get_propertystr_cstring(ctx, argv[0], "ext"))) {
    dbuf_putstr(&db, ext);
  }

  ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);
  dbuf_free(&db);

  return ret;
}

static JSValue
js_path_resolve(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const char* str;
  DynBuf db, cwd;
  ssize_t i;
  size_t len = 0;
  JSValue ret = JS_UNDEFINED;
  js_dbuf_init(ctx, &db);
  dbuf_0(&db);

  for(i = argc - 1; i >= 0; i--) {
    if(!JS_IsString(argv[i])) {
      ret = JS_ThrowTypeError(ctx, "argument #%zx is not a string", i);
      goto fail;
    }
    str = JS_ToCStringLen(ctx, &len, argv[i]);
    while(len > 0 && str[len - 1] == PATHSEP_C) len--;
    if(dbuf_reserve_start(&db, len + 1))
      goto fail;
    if(len > 0) {
      memcpy(db.buf, str, len);
      db.buf[len] = PATHSEP_C;
    }
    js_cstring_free(ctx, str);
  }

  if(!path_isabsolute2((const char*)db.buf, db.size)) {
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
    db.size = path_collapse2((char*)db.buf, db.size);
    while(db.size > 0 && db.buf[db.size - 1] == PATHSEP_C) db.size--;
    ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);
  }
fail:
  dbuf_free(&db);
  return ret;
}

static JSValue
js_path_isin(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  const char *a, *b;
  BOOL ret;
  a = JS_ToCString(ctx, argv[0]);
  b = JS_ToCString(ctx, argv[1]);

  switch(magic) {
    case 0: ret = path_isin2(a, b); break;
    case 1: ret = path_equal2(a, b); break;
  }

  JS_FreeCString(ctx, a);
  JS_FreeCString(ctx, b);
  return JS_NewBool(ctx, ret);
}

static JSValue
js_path_toarray(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  const char* str;
  int32_t i, len, clen;
  JSValue ret = JS_NewArray(ctx);

  if((str = JS_ToCString(ctx, argv[0]))) {
    IndexRange ir = {0, -1};
    JSValue value;

    len = path_length1(str);

    if(argc > 1) {
      js_index_range(ctx, len, argc - 1, argv + 1, &ir);
    }

    // printf("IndexRange { %" PRId64 ", %" PRId64 " }\n", ir.start, ir.end);

    if(argc < 3)
      ir.end = len;
    else if(ir.end < 0)
      ir.end = ((ir.end % len) + len) % len;

    for(i = ir.start; i < ir.end; i++) {
      size_t clen;
      const char* x = path_at3(str, &clen, i);

      if(magic == 0) {
        value = JS_NewStringLen(ctx, x, clen);
      } else if(magic >= 3) {

        value = JS_NewArray(ctx);
        JS_SetPropertyUint32(ctx, value, 0, JS_NewUint32(ctx, x - str));
        JS_SetPropertyUint32(ctx, value, 1, JS_NewUint32(ctx, clen));

      } else {
        value = JS_NewUint32(ctx, magic == 2 ? clen : x - str);
      }
      JS_SetPropertyUint32(ctx, ret, i - ir.start, value);
    }
  }
  return ret;
}

static JSValue
js_path_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {

  JSValue ret, arr, fn;
  int32_t magic = 0;

  if(argc > 3)
    JS_ToInt32(ctx, &magic, argv[3]);

  arr = js_path_toarray(ctx, this_val, argc, argv, magic);

  fn = js_iterator_method(ctx, arr);

  ret = JS_Call(ctx, fn, arr, 0, 0);
  JS_FreeValue(ctx, fn);
  JS_FreeValue(ctx, arr);

  return ret;
}

static const JSCFunctionListEntry js_path_funcs[] = {
    JS_CFUNC_MAGIC_DEF("basename", 1, js_path_method, METHOD_BASENAME),
    JS_CFUNC_MAGIC_DEF("basepos", 1, js_path_method, METHOD_BASEPOS),
    JS_CFUNC_MAGIC_DEF("baselen", 1, js_path_method, METHOD_BASELEN),
    JS_CFUNC_MAGIC_DEF("collapse", 1, js_path_method, METHOD_COLLAPSE),
    JS_CFUNC_MAGIC_DEF("dirname", 1, js_path_method, METHOD_DIRNAME),
    JS_CFUNC_MAGIC_DEF("dirlen", 1, js_path_method, METHOD_DIRLEN),
    JS_CFUNC_MAGIC_DEF("exists", 1, js_path_method, METHOD_EXISTS),
    JS_CFUNC_MAGIC_DEF("extname", 1, js_path_method, METHOD_EXTNAME),
    JS_CFUNC_MAGIC_DEF("extpos", 1, js_path_method, METHOD_EXTPOS),
    JS_CFUNC_MAGIC_DEF("extlen", 1, js_path_method, METHOD_EXTLEN),
    JS_CFUNC_MAGIC_DEF("fnmatch", 1, js_path_method, METHOD_FNMATCH),
    JS_CFUNC_MAGIC_DEF("getcwd", 1, js_path_method, METHOD_GETCWD),
#ifndef __wasi__
    JS_CFUNC_MAGIC_DEF("gethome", 1, js_path_method, METHOD_GETHOME),
#endif
    JS_CFUNC_MAGIC_DEF("getsep", 1, js_path_method, METHOD_GETSEP),
    JS_CFUNC_MAGIC_DEF("isAbsolute", 1, js_path_method, METHOD_IS_ABSOLUTE),
    JS_CFUNC_MAGIC_DEF("isRelative", 1, js_path_method, METHOD_IS_RELATIVE),
    JS_CFUNC_MAGIC_DEF("isDirectory", 1, js_path_method, METHOD_IS_DIRECTORY),
    JS_CFUNC_MAGIC_DEF("isFile", 1, js_path_method, METHOD_IS_FILE),
    JS_CFUNC_MAGIC_DEF("isCharDev", 1, js_path_method, METHOD_IS_CHARDEV),
    JS_CFUNC_MAGIC_DEF("isBlockDev", 1, js_path_method, METHOD_IS_BLOCKDEV),
    JS_CFUNC_MAGIC_DEF("isFIFO", 1, js_path_method, METHOD_IS_FIFO),
    JS_CFUNC_MAGIC_DEF("isSocket", 1, js_path_method, METHOD_IS_SOCKET),
    JS_CFUNC_MAGIC_DEF("isSymlink", 1, js_path_method, METHOD_IS_SYMLINK),
    JS_CFUNC_MAGIC_DEF("isSeparator", 1, js_path_method, METHOD_IS_SEPARATOR),
    JS_CFUNC_MAGIC_DEF("length", 1, js_path_method, METHOD_LENGTH),
    JS_CFUNC_MAGIC_DEF("components", 1, js_path_method, METHOD_COMPONENTS),
    JS_CFUNC_MAGIC_DEF("readlink", 1, js_path_method, METHOD_READLINK),
    /*#ifndef __wasi__
        JS_CFUNC_MAGIC_DEF("realpath", 1, js_path_method, METHOD_REALPATH),
    #endif*/
    JS_CFUNC_MAGIC_DEF("right", 1, js_path_method, METHOD_RIGHT),
    JS_CFUNC_MAGIC_DEF("skip", 1, js_path_method, METHOD_SKIP),
    JS_CFUNC_MAGIC_DEF("skipSeparator", 1, js_path_method, METHOD_SKIP_SEPARATOR),
    JS_CFUNC_MAGIC_DEF("absolute", 1, js_path_method_dbuf, METHOD_ABSOLUTE),
    JS_CFUNC_MAGIC_DEF("append", 1, js_path_method_dbuf, METHOD_APPEND),
    JS_CFUNC_MAGIC_DEF("canonical", 1, js_path_method_dbuf, METHOD_CANONICAL),
    JS_CFUNC_MAGIC_DEF("realpath", 1, js_path_method_dbuf, METHOD_REALPATH),
    JS_CFUNC_MAGIC_DEF("concat", 2, js_path_method_dbuf, METHOD_CONCAT),
    JS_CFUNC_MAGIC_DEF("at", 2, js_path_method, METHOD_AT),
    JS_CFUNC_MAGIC_DEF("find", 2, js_path_method_dbuf, METHOD_FIND),
    JS_CFUNC_MAGIC_DEF("normalize", 1, js_path_method_dbuf, METHOD_NORMALIZE),
    JS_CFUNC_MAGIC_DEF("relative", 2, js_path_method_dbuf, METHOD_RELATIVE),
    JS_CFUNC_DEF("slice", 0, js_path_slice),
    JS_CFUNC_DEF("join", 1, js_path_join),
    JS_CFUNC_DEF("parse", 1, js_path_parse),
    JS_CFUNC_DEF("format", 1, js_path_format),
    JS_CFUNC_DEF("resolve", 1, js_path_resolve),
    JS_CFUNC_MAGIC_DEF("isin", 2, js_path_isin, 0),
    JS_CFUNC_MAGIC_DEF("equal", 2, js_path_isin, 1),
    JS_CFUNC_MAGIC_DEF("toArray", 0, js_path_toarray, 0),
    JS_CFUNC_MAGIC_DEF("toObject", 1, js_path_method, METHOD_OBJECT),
    JS_CFUNC_DEF("fromObject", 1, js_path_fromobj),
    JS_CFUNC_MAGIC_DEF("offsets", 0, js_path_toarray, 1),
    JS_CFUNC_MAGIC_DEF("lengths", 0, js_path_toarray, 2),
    JS_CFUNC_MAGIC_DEF("ranges", 0, js_path_toarray, 3),
    JS_CFUNC_DEF("iterator", 0, js_path_iterator),
    JS_PROP_STRING_DEF("delimiter", PATHDELIM_S, JS_PROP_CONFIGURABLE),
    JS_PROP_STRING_DEF("sep", PATHSEP_S, JS_PROP_CONFIGURABLE),
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
  m = JS_NewCModule(ctx, module_name, js_path_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_path_funcs, countof(js_path_funcs));
  JS_AddModuleExport(ctx, m, "default");
  return m;
}

/**
 * @}
 */
