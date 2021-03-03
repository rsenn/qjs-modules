#define _GNU_SOURCE

#include <quickjs.h>
#include <cutils.h>

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
  return JS_UNDEFINED;
}


static const JSCFunctionListEntry js_path_funcs[] = {  JS_CFUNC_MAGIC_DEF("absolute", 1, js_path_method, METHOD_ABSOLUTE),
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
