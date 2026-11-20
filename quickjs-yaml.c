#include "defines.h"
#include "utils.h"
#include "buffer-utils.h"
#include "yaml/ywrite.h"
#include <quickjs.h>
#include <cutils.h>

/**
 * \defgroup quickjs-yaml quickjs-yaml: YAML writer
 * @{
 */

static JSValue
js_yaml_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t indent = 2;

  if(argc > 1 && !JS_IsUndefined(argv[1]))
    JS_ToInt32(ctx, &indent, argv[1]);

  if(indent <= 0)
    indent = 2;

  DynBuf db;
  dbuf_init_ctx(ctx, &db);

  if(ywrite_document(ctx, &db, argc > 0 ? argv[0] : JS_UNDEFINED, indent)) {
    dbuf_free(&db);
    return JS_EXCEPTION;
  }

  return dbuf_tostring_free(&db, ctx);
}

static const JSCFunctionListEntry js_yaml_funcs[] = {
    JS_CFUNC_DEF("write", 1, js_yaml_write),
};

static int
js_yaml_init(JSContext* ctx, JSModuleDef* m) {
  JS_SetModuleExportList(ctx, m, js_yaml_funcs, countof(js_yaml_funcs));
  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_yaml
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_yaml_init)))
    JS_AddModuleExportList(ctx, m, js_yaml_funcs, countof(js_yaml_funcs));

  return m;
}

/**
 * @}
 */
