#define _GNU_SOURCE

#include "cutils.h"
#include "quickjs.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "path.h"
#include "utils.h"

enum path_methods {
  METHOD_ABSOLUTE = 0,
  METHOD_APPEND,
  METHOD_BASENAME,
  METHOD_CANONICAL,
  METHOD_COLLAPSE,
  METHOD_CONCAT,
  METHOD_DIRNAME,
  METHOD_EXISTS,
  METHOD_EXTNAME,
  METHOD_FIND,
  METHOD_FNMATCH,
  METHOD_GETCWD,
  METHOD_GETHOME,
  METHOD_GETSEP,
  METHOD_IS_ABSOLUTE,
  METHOD_IS_RELATIVE,
  METHOD_IS_DIRECTORY,
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
  METHOD_SPLIT
};

static JSValue
js_path_method(
    JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  const char *a = 0, *b = 0;
  struct stat st;
  char buf[PATH_MAX + 1];
  size_t alen = 0, blen = 0, pos;
  JSValue ret = JS_UNDEFINED;
  if(argc > 0) {
    a = JS_ToCStringLen(ctx, &alen, argv[0]);

    if(argc > 1)
      b = JS_ToCStringLen(ctx, &blen, argv[1]);
  }

  switch(magic) {
    case METHOD_BASENAME: ret = JS_NewString(ctx, basename(a)); break;
    case METHOD_DIRNAME:
      pos = str_rchrs(a, "/\\", 2);
      if(pos < alen)
        alen = pos;
      ret = JS_NewStringLen(ctx, a, alen);
      break;
    case METHOD_READLINK: {
      ssize_t r;
      memset(buf, 0, sizeof(buf));
      if((r = readlink(a, buf, sizeof(buf)) > 0)) {
        ret = JS_NewString(ctx, buf);
      }
      break;
    }
    case METHOD_REALPATH:
      if(realpath(a, buf))
        ret = JS_NewString(ctx, buf);
      break;
    case METHOD_EXISTS: ret = JS_NewBool(ctx, path_exists(a)); break;

    case METHOD_EXTNAME: ret = JS_NewString(ctx, path_extname(a)); break;

    case METHOD_GETCWD:
      if(getcwd(buf, sizeof(buf)))
        ret = JS_NewString(ctx, buf);
      break;

    case METHOD_IS_ABSOLUTE:
      if(a && a[0])
        ret = JS_NewBool(ctx, path_isabs(a));
      break;
    case METHOD_IS_RELATIVE:
      if(a && a[0])
        ret = JS_NewBool(ctx, !path_isabs(a));
      break;
    case METHOD_IS_DIRECTORY: ret = JS_NewBool(ctx, path_is_directory(a)); break;
    case METHOD_IS_SYMLINK: ret = JS_NewBool(ctx, path_is_symlink(a)); break;
    case METHOD_IS_SEPARATOR:
      if(a && a[0])
        ret = JS_NewBool(ctx, path_issep(a[0]));
      break;
    case METHOD_COLLAPSE: {
      char* s = str_ndup(a, alen);
      size_t newlen;
      newlen = path_collapse(s, alen);
      ret = JS_NewStringLen(ctx, s, newlen);
      free(s);
      break;
    }
    case METHOD_FNMATCH: {
      int32_t flags = 0;
      if(argc > 2)
        JS_ToInt32(ctx, &flags, argv[2]);
      ret = JS_NewInt32(ctx, path_fnmatch(a, alen, b, blen, flags));
      break;
    }
    case METHOD_GETHOME: ret = JS_NewString(ctx, path_gethome(getuid())); break;
    case METHOD_GETSEP: {
      char c;
      if((c = path_getsep(a)) != '\0')
        ret = JS_NewStringLen(ctx, &c, 1);
      break;
    }
    case METHOD_LENGTH: ret = JS_NewUint32(ctx, path_length(a, alen)); break;
    case METHOD_COMPONENTS: {
      uint32_t n = UINT32_MAX;
      if(argc > 1)
        JS_ToUint32(ctx, &n, argv[1]);
      ret = JS_NewUint32(ctx, path_components(a, alen, n));
      break;
    }
    case METHOD_RIGHT: ret = JS_NewUint32(ctx, path_right(a, alen)); break;
    case METHOD_SKIP: ret = JS_NewUint32(ctx, path_skip(a, alen)); break;
    case METHOD_SKIP_SEPARATOR: {
      int64_t n = 0;
      if(argc > 1)
        JS_ToInt64(ctx, &n, argv[1]);
      ret = JS_NewUint32(ctx, path_skip_separator(a, alen, n));
      break;
    }
  }
  return ret;
}

static JSValue
js_path_method_dbuf(
    JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  const char *a = 0, *b = 0;
  DynBuf db;
  size_t alen = 0, blen = 0, pos;
  JSValue ret = JS_UNDEFINED;

  if(argc > 0) {
    a = JS_ToCStringLen(ctx, &alen, argv[0]);
    if(argc > 1)
      b = JS_ToCStringLen(ctx, &blen, argv[1]);
  }

  dbuf_init2(&db, JS_GetRuntime(ctx), (DynBufReallocFunc*)js_realloc_rt);

  switch(magic) {
    case METHOD_ABSOLUTE: path_absolute(a, &db); break;
    case METHOD_APPEND: path_append(a, alen, &db); break;
    case METHOD_CANONICAL: path_canonical(a, &db); break;
    case METHOD_CONCAT: path_concat(a, alen, b, blen, &db); break;
    case METHOD_FIND: path_find(a, b, &db); break;
    case METHOD_RELATIVE: path_relative(a, b, &db); break;
    case METHOD_NORMALIZE: {
      BOOL symbolic = FALSE;
      if(argc > 1)
        symbolic = JS_ToBool(ctx, argv[1]);
      path_normalize(a, &db, symbolic);
      break;
    }
  }

  if(db.size) {
    ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);
  }
  dbuf_free(&db);
  return ret;
}

static JSValue
js_path_join(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  const char* str;
  DynBuf db;
  size_t i, len = 0, pos;
  JSValue ret = JS_UNDEFINED;
  dbuf_init2(&db, JS_GetRuntime(ctx), (DynBufReallocFunc*)js_realloc_rt);
  dbuf_init2(&db, JS_GetRuntime(ctx), (DynBufReallocFunc*)js_realloc_rt);
  for(i = 0; i < argc; i++) {
    str = JS_ToCStringLen(ctx, &len, argv[i]);
    path_append(str, len, &db);
  }
  if(db.size) {
    ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);
  }
  dbuf_free(&db);
  return ret;
}

static JSValue
js_path_parse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  const char *str, *base, *ext;
  size_t i, len = 0, rootlen, dirlen;
  JSValue ret = JS_UNDEFINED;

  str = JS_ToCStringLen(ctx, &len, argv[0]);

  base = basename(str);
  dirlen = base - str - 1;
  rootlen = path_root(str, len);
  ext = path_extname(str);

  ret = JS_NewObject(ctx);

  js_object_propertystr_setstr(ctx, ret, "root", str, rootlen);
  js_object_propertystr_setstr(ctx, ret, "dir", str, dirlen);
  js_object_propertystr_setstr(ctx, ret, "base", base, strlen(base));
  js_object_propertystr_setstr(ctx, ret, "ext", ext, strlen(ext));
  js_object_propertystr_setstr(ctx, ret, "name", base, strlen(base) - strlen(ext));

  return ret;
}

static JSValue
js_path_format(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValueConst obj = argv[0];
  const char *dir, *root, *base, *name, *ext;
  JSValue ret = JS_UNDEFINED;
  DynBuf db;

  dbuf_init2(&db, JS_GetRuntime(ctx), (DynBufReallocFunc*)js_realloc_rt);

  if((dir = js_object_propertystr_getstr(ctx, obj, "dir"))) {
    dbuf_putstr(&db, dir);
    JS_FreeCString(ctx, dir);
  } else if((root = js_object_propertystr_getstr(ctx, obj, "root"))) {
    dbuf_putstr(&db, root);
    JS_FreeCString(ctx, root);
  }

  if(db.size)
    dbuf_putc(&db, PATHSEP_C);

  if((base = js_object_propertystr_getstr(ctx, obj, "base"))) {
    dbuf_putstr(&db, base);
    JS_FreeCString(ctx, base);
  } else if((name = js_object_propertystr_getstr(ctx, obj, "name"))) {
    dbuf_putstr(&db, name);
    JS_FreeCString(ctx, name);
    if((ext = js_object_propertystr_getstr(ctx, obj, "ext"))) {
      dbuf_putstr(&db, ext);
      JS_FreeCString(ctx, ext);
    }
  }

  ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);
  dbuf_free(&db);

  return ret;
}

static JSValue
js_path_resolve(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  const char* str;
  DynBuf db, cwd;
  ssize_t i;
  size_t len = 0, pos;
  JSValue ret = JS_UNDEFINED;
  dbuf_init2(&db, JS_GetRuntime(ctx), (DynBufReallocFunc*)js_realloc_rt);
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
  }

  if(!path_is_absolute((const char*)db.buf, db.size)) {
    dbuf_init2(&cwd, JS_GetRuntime(ctx), (DynBufReallocFunc*)js_realloc_rt);
    str = path_getcwd(&cwd);
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
    db.size = path_collapse((char*)db.buf, db.size);
    while(db.size > 0 && db.buf[db.size - 1] == PATHSEP_C) db.size--;
    ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);
  }
fail:
  dbuf_free(&db);
  return ret;
}

static const JSCFunctionListEntry js_path_funcs[] = {
    JS_CFUNC_MAGIC_DEF("basename", 1, js_path_method, METHOD_BASENAME),
    JS_CFUNC_MAGIC_DEF("collapse", 1, js_path_method, METHOD_COLLAPSE),
    JS_CFUNC_MAGIC_DEF("dirname", 1, js_path_method, METHOD_DIRNAME),
    JS_CFUNC_MAGIC_DEF("exists", 1, js_path_method, METHOD_EXISTS),
    JS_CFUNC_MAGIC_DEF("extname", 1, js_path_method, METHOD_EXTNAME),
    JS_CFUNC_MAGIC_DEF("fnmatch", 1, js_path_method, METHOD_FNMATCH),
    JS_CFUNC_MAGIC_DEF("getcwd", 1, js_path_method, METHOD_GETCWD),
    JS_CFUNC_MAGIC_DEF("gethome", 1, js_path_method, METHOD_GETHOME),
    JS_CFUNC_MAGIC_DEF("getsep", 1, js_path_method, METHOD_GETSEP),
    JS_CFUNC_MAGIC_DEF("isAbsolute", 1, js_path_method, METHOD_IS_ABSOLUTE),
    JS_CFUNC_MAGIC_DEF("isRelative", 1, js_path_method, METHOD_IS_RELATIVE),
    JS_CFUNC_MAGIC_DEF("isDirectory", 1, js_path_method, METHOD_IS_DIRECTORY),
    JS_CFUNC_MAGIC_DEF("isSymlink", 1, js_path_method, METHOD_IS_SYMLINK),
    JS_CFUNC_MAGIC_DEF("isSeparator", 1, js_path_method, METHOD_IS_SEPARATOR),
    JS_CFUNC_MAGIC_DEF("length", 1, js_path_method, METHOD_LENGTH),
    JS_CFUNC_MAGIC_DEF("components", 1, js_path_method, METHOD_COMPONENTS),
    JS_CFUNC_MAGIC_DEF("readlink", 1, js_path_method, METHOD_READLINK),
    JS_CFUNC_MAGIC_DEF("realpath", 1, js_path_method, METHOD_REALPATH),
    JS_CFUNC_MAGIC_DEF("right", 1, js_path_method, METHOD_RIGHT),
    JS_CFUNC_MAGIC_DEF("skip", 1, js_path_method, METHOD_SKIP),
    JS_CFUNC_MAGIC_DEF("skipSeparator", 1, js_path_method, METHOD_SKIP_SEPARATOR),
    JS_CFUNC_MAGIC_DEF("absolute", 1, js_path_method_dbuf, METHOD_ABSOLUTE),
    JS_CFUNC_MAGIC_DEF("append", 1, js_path_method_dbuf, METHOD_APPEND),
    JS_CFUNC_MAGIC_DEF("canonical", 1, js_path_method_dbuf, METHOD_CANONICAL),
    JS_CFUNC_MAGIC_DEF("concat", 2, js_path_method_dbuf, METHOD_CONCAT),
    JS_CFUNC_MAGIC_DEF("find", 2, js_path_method_dbuf, METHOD_FIND),
    JS_CFUNC_MAGIC_DEF("normalize", 1, js_path_method_dbuf, METHOD_NORMALIZE),
    JS_CFUNC_MAGIC_DEF("relative", 2, js_path_method_dbuf, METHOD_RELATIVE),
    JS_CFUNC_DEF("join", 1, js_path_join),
    JS_CFUNC_DEF("parse", 1, js_path_parse),
    JS_CFUNC_DEF("format", 1, js_path_format),
    JS_CFUNC_DEF("resolve", 1, js_path_resolve),
    JS_PROP_STRING_DEF("delimiter", PATHDELIM_S, JS_PROP_CONFIGURABLE),
    JS_PROP_STRING_DEF("sep", PATHSEP_S, JS_PROP_CONFIGURABLE)};

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
