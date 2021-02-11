#include <quickjs.h>
#include <cutils.h>

typedef struct {
  int colors : 1;
  int show_hidden : 1;
  int custom_inspect : 1;
  int show_proxy : 1;
  int getters : 1;
  int32_t depth;
  int32_t max_array_length;
  int32_t max_string_length;
  int32_t break_length;
  int32_t compact;

} inspect_options_t;

static JSValue
js_inspect_print(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts) {

  int tag = JS_VALUE_GET_TAG(value);

  switch(tag) {
    case JS_TAG_BIG_DECIMAL: {
      break;
    }
    case JS_TAG_BIG_INT: {
      break;
    }
    case JS_TAG_BIG_FLOAT: {
      break;
    }
    case JS_TAG_SYMBOL: {
      break;
    }
    case JS_TAG_STRING: {
      const char* str;
      size_t len;
      str = JS_ToCStringLen(ctx, &len, value);
      dbuf_putc(buf, '"');
      dbuf_put(buf, (const uint8_t*)str, len);
      dbuf_putc(buf, '"');
      break;
    }
    case JS_TAG_MODULE: {
      break;
    }
    case JS_TAG_FUNCTION_BYTECODE: {
      break;
    }
    case JS_TAG_OBJECT: {
      break;
    }
    case JS_TAG_INT: {
      int i = JS_VALUE_GET_INT(value);
      if(i < 0) {
        dbuf_putc(buf, '-');
        i = -i;
      }
      dbuf_put_u32(buf, i);
      break;
    }
    case JS_TAG_BOOL: {
      dbuf_putstr(buf, JS_VALUE_GET_BOOL(value) ? "true" : "false");
      break;
    }
    case JS_TAG_NULL: {
      dbuf_putstr(buf, "null");
      break;
    }
    case JS_TAG_UNDEFINED: {
      dbuf_putstr(buf, "undefined");
      break;
    }
    case JS_TAG_UNINITIALIZED: {
      break;
    }
    case JS_TAG_CATCH_OFFSET: {
      break;
    }
    case JS_TAG_EXCEPTION: {
      break;
    }
    case JS_TAG_FLOAT64: {
      break;
    }
  }
}

static void
js_inspect_default_options(inspect_options_t* opts) {
  opts->colors = TRUE;
  opts->show_hidden = FALSE;
  opts->custom_inspect = TRUE;
  opts->show_proxy = FALSE;
  opts->getters = FALSE;
  opts->depth = 2;
  opts->max_array_length = 100;
  opts->max_string_length = -1;
  opts->break_length = 0;
  opts->compact = 0;
}

static void
js_inspect_get_options(JSContext* ctx, JSValueConst object, inspect_options_t* opts) {
  JSValue value;
  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "colors"))))
    opts->colors = JS_ToBool(ctx, value);

  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "showHidden"))))
    opts->show_hidden = JS_ToBool(ctx, value);

  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "customInspect"))))
    opts->custom_inspect = JS_ToBool(ctx, value);

  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "showProxy"))))
    opts->show_proxy = JS_ToBool(ctx, value);

  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "getters"))))
    opts->getters = JS_ToBool(ctx, value);

  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "depth"))))
    JS_ToInt32(ctx, &opts->depth, value);

  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "maxArrayLength"))))
    JS_ToInt32(ctx, &opts->max_array_length, value);

  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "maxStringLength"))))
    JS_ToInt32(ctx, &opts->max_string_length, value);

  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "breakLength"))))
    JS_ToInt32(ctx, &opts->break_length, value);

  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "compact"))))
    JS_ToInt32(ctx, &opts->compact, value);
}

static JSValue
js_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  DynBuf dbuf;
  inspect_options_t options;

  dbuf_init(&dbuf);

  js_inspect_default_options(&options);
  if(argc > 1)
    js_inspect_get_options(ctx, argv[1], &options);
  js_inspect_print(ctx, &dbuf, argv[0], &options);

  return JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
}

static const JSCFunctionListEntry js_inspect_funcs[] = {JS_CFUNC_DEF("inspect", 1, js_inspect)};

static int
js_inspect_init(JSContext* ctx, JSModuleDef* m) {
  return JS_SetModuleExportList(ctx, m, js_inspect_funcs, countof(js_inspect_funcs));
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_inspect
#endif

JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_inspect_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_inspect_funcs, countof(js_inspect_funcs));
  return m;
}
