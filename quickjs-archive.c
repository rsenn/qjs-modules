#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <quickjs.h>
#include <archive.h>
#include <threads.h>
#include "quickjs-archive.h"
#include "utils.h"

#define max(a, b) ((a) > (b) ? (a) : (b))

thread_local VISIBLE JSClassID js_archive_class_id = 0;
thread_local JSValue archive_proto = {.tag = JS_TAG_UNDEFINED}, archive_ctor = {.tag = JS_TAG_UNDEFINED};

enum { ARCHIVE_METHOD_READ, ARCHIVE_METHOD_WRITE, ARCHIVE_METHOD_READFILE, ARCHIVE_METHOD_WRITEFILE };
enum { ARCHIVE_PROP_FORMAT, ARCHIVE_PROP_COMPRESSION };

struct archive*
js_archive_data(JSContext* ctx, JSValueConst value) {
  struct archive* ar;
  ar = JS_GetOpaque(value, js_archive_class_id);
  return ar;
}

JSValue
js_archive_wrap_proto(JSContext* ctx, JSValueConst proto, struct archive* ar) {
  JSValue obj;

  if(js_archive_class_id == 0)
    js_archive_init(ctx, 0);
  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = JS_DupValue(ctx, archive_proto);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_archive_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ar);

  return obj;
fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_archive_wrap(JSContext* ctx, struct archive* ar) {
  return js_archive_new_proto(ctx, archive_proto, ar);
}

JSValue
js_archive_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  struct archive* ar;
  JSValue ret = JS_UNDEFINED;
  size_t len;

  if(!(ar = js_archive_data(ctx, this_val)))
    return ret;

  switch(magic) {}

  return ret;
}

static JSValue
js_archive_getter(JSContext* ctx, JSValueConst this_val, int magic) {
  struct archive* ar;
  JSValue ret = JS_UNDEFINED;

  if(!(ar = js_archive_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case ARCHIVE_PROP_FORMAT: {
      ret = JS_NewString(ctx, archive_format_name(ar));
      break;
    }
    case ARCHIVE_PROP_COMPRESSION: {
      ret = JS_NewString(ctx, archive_compression_name(ar));
      break;
    }
  }
  return ret;
}

static JSValue
js_archive_setter(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  struct archive* ar;
  JSValue ret = JS_UNDEFINED;

  if(!(ar = js_archive_data(ctx, this_val)))
    return ret;

  switch(magic) {}
  return ret;
}

JSValue
js_archive_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  return js_archive_new_proto(ctx, proto, 0);

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

void
js_archive_finalizer(JSRuntime* rt, JSValue val) {
  struct archive* ar = JS_GetOpaque(val, js_archive_class_id);
  if(ar) {
    archive_free(ar);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_archive_class = {
    .class_name = "Archive",
    .finalizer = js_archive_finalizer,
};

static const JSCFunctionListEntry js_archive_funcs[] = {
    JS_CGETSET_MAGIC_DEF("format", js_archive_getter, 0, ARCHIVE_PROP_FORMAT),
    JS_CGETSET_MAGIC_DEF("compression", js_archive_getter, 0, ARCHIVE_PROP_COMPRESSION),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Archive", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_archive_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("read", 1, js_archive_functions, ARCHIVE_METHOD_READ),
};

int
js_archive_init(JSContext* ctx, JSModuleDef* m) {

  if(js_archive_class_id == 0) {
    JS_NewClassID(&js_archive_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_archive_class_id, &js_archive_class);

    archive_ctor = JS_NewCFunction2(ctx, js_archive_constructor, "struct archive", 1, JS_CFUNC_constructor, 0);
    archive_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, archive_proto, js_archive_funcs, countof(js_archive_funcs));
    JS_SetPropertyFunctionList(ctx, archive_ctor, js_archive_static_funcs, countof(js_archive_static_funcs));
    JS_SetClassProto(ctx, js_archive_class_id, archive_proto);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "struct archive", archive_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_archive
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  if(!(m = JS_NewCModule(ctx, module_name, &js_archive_init)))
    return m;
  JS_AddModuleExport(ctx, m, "struct archive");
  return m;
}
