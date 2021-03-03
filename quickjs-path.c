#define _GNU_SOURCE

#include "quickjs.h"
#include "cutils.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "path.h"

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

static JSValue
js_path_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  const char *a, *b;
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
      if(realpath(a, buf)) {
        ret = JS_NewString(ctx, buf);
      }
      break;
    case METHOD_EXISTS: {
      ret = JS_NewBool(ctx, path_exists(a));
      break;
    }
    case METHOD_GETCWD: {
      if(getcwd(buf, sizeof(buf))) {
        ret = JS_NewString(ctx, buf);
      }
      break;
    }
    case METHOD_IS_ABSOLUTE: {
      if(a && a[0]) {
        ret = JS_NewBool(ctx, path_isabs(a));
      }
      break;
    }
    case METHOD_IS_DIRECTORY: ret = JS_NewBool(ctx, path_is_directory(a)); break;
    case METHOD_IS_SEPARATOR:
      if(a && a[0]) {
        ret = JS_NewBool(ctx, path_issep(a[0]));
      }
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
    case METHOD_GETHOME: {
      ret = JS_NewString(ctx, path_gethome(getuid()));
      break;
    }
    case METHOD_GETSEP: {
      char c;
      if((c = path_getsep(a)) != '\0')
        ret = JS_NewStringLen(ctx, &c, 1);
      break;
    }
    case METHOD_LEN: ret = JS_NewUint32(ctx, path_len(a, alen)); break;
    case METHOD_NUM: {
      int64_t n = -1;
      JS_ToInt64(ctx, &n, argv[1]);
      ret = JS_NewUint32(ctx, path_num(a, alen, n));
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
js_path_method_dbuf(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  const char *a, *b;
  DynBuf db;
  size_t alen = 0, blen = 0, pos;
  JSValue ret = JS_UNDEFINED;

  if(argc > 0) {
    a = JS_ToCStringLen(ctx, &alen, argv[0]);
    if(argc > 1)
      b = JS_ToCStringLen(ctx, &blen, argv[1]);
  }

  dbuf_init(&db);

  switch(magic) {
    case METHOD_ABSOLUTE: path_absolute(a, &db); break;
    case METHOD_APPEND: path_append(a, alen, &db); break;
    case METHOD_CANONICAL: path_canonical(a, &db); break;
    case METHOD_CONCAT: path_concat(a, alen, b, blen, &db); break;
    case METHOD_FIND: path_find(a, b, &db); break;
    case METHOD_RELATIVE: path_relative(a, b, &db); break;
    case METHOD_CANONICALIZE: {
      BOOL symbolic = FALSE;
      if(argc > 1)
        symbolic = JS_ToBool(ctx, argv[1]);
      path_canonicalize(a, &db, symbolic);
      break;
    }
  }

  if(db.size) {
    ret = JS_NewStringLen(ctx, db.buf, db.size);
  }
  dbuf_free(&db);
  return ret;
}

static const JSCFunctionListEntry js_path_funcs[] = {
    JS_CFUNC_MAGIC_DEF("basename", 1, js_path_method, METHOD_BASENAME),
    JS_CFUNC_MAGIC_DEF("canonicalize", 1, js_path_method, METHOD_CANONICALIZE),
    JS_CFUNC_MAGIC_DEF("collapse", 1, js_path_method, METHOD_COLLAPSE),
    JS_CFUNC_MAGIC_DEF("dirname", 1, js_path_method, METHOD_DIRNAME),
    JS_CFUNC_MAGIC_DEF("exists", 1, js_path_method, METHOD_EXISTS),
    JS_CFUNC_MAGIC_DEF("fnmatch", 1, js_path_method, METHOD_FNMATCH),
    JS_CFUNC_MAGIC_DEF("getcwd", 1, js_path_method, METHOD_GETCWD),
    JS_CFUNC_MAGIC_DEF("gethome", 1, js_path_method, METHOD_GETHOME),
    JS_CFUNC_MAGIC_DEF("getsep", 1, js_path_method, METHOD_GETSEP),
    JS_CFUNC_MAGIC_DEF("isAbsolute", 1, js_path_method, METHOD_IS_ABSOLUTE),
    JS_CFUNC_MAGIC_DEF("isDirectory", 1, js_path_method, METHOD_IS_DIRECTORY),
    JS_CFUNC_MAGIC_DEF("isSeparator", 1, js_path_method, METHOD_IS_SEPARATOR),
    JS_CFUNC_MAGIC_DEF("len", 1, js_path_method, METHOD_LEN),
    JS_CFUNC_MAGIC_DEF("num", 1, js_path_method, METHOD_NUM),
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
    JS_CFUNC_MAGIC_DEF("relative", 2, js_path_method_dbuf, METHOD_RELATIVE)};

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
