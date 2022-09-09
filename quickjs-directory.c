#include "include/defines.h"
#include "include/getdents.h"
#include "include/utils.h"
#include "include/buffer-utils.h"
#include "include/debug.h"

/**
 * \defgroup quickjs-bjson QuickJS module: directory - Directory reader
 * @{
 */
#define max(a, b) ((a) > (b) ? (a) : (b))

thread_local VISIBLE JSClassID js_directory_class_id = 0;
thread_local JSValue directory_proto = {{JS_TAG_UNDEFINED}}, directory_ctor = {{JS_TAG_UNDEFINED}};

enum {
  FLAG_NAME = 1,
  FLAG_TYPE = 2,
  FLAG_BOTH = FLAG_NAME | FLAG_TYPE,
};
enum {
  DIRECTORY_NEXT,
};

static JSValue
js_directory_value(JSContext* ctx, Directory* directory, int dflags) {
  const char* name = 0;
  int type = -1;
  JSValue ret;

  if(dflags & FLAG_NAME)
    name = getdents_name(directory);
  if(dflags & FLAG_TYPE)
    type = getdents_type(directory);

  switch(dflags) {
    case FLAG_NAME: {
      ret = JS_NewString(ctx, name);
      break;
    }
    case FLAG_TYPE: {
      ret = JS_NewInt32(ctx, type);
      break;
    }
    case FLAG_BOTH: {
      ret = JS_NewArray(ctx);
      JS_SetPropertyUint32(ctx, ret, 0, JS_NewString(ctx, name));
      JS_SetPropertyUint32(ctx, ret, 1, JS_NewInt32(ctx, type));
      break;
    }
  }
  return ret;
}

static inline Directory*
js_directory_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque(value, js_directory_class_id);
}

static JSValue
js_directory_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Directory* directory;
  JSValue ret = JS_UNDEFINED;
  if(!(directory = js_directory_data(ctx, this_val)))
    return ret;
  switch(magic) {
    /* case DIRECTORY_SIZE: {
       ret = JS_NewUint32(ctx, directory->size);
       break;
     }

     case DIRECTORY_TYPE: {
       ret = JS_NewString(ctx, directory->type);
       break;
     }*/
  }
  return ret;
}

static JSValue
js_directory_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  Directory* directory;

  if(!(directory = js_mallocz(ctx, getdents_size())))
    ;
  return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = directory_proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_directory_class_id);
  if(JS_IsException(obj))
    goto fail;

  if(argc > 0) {
    if(JS_IsNumber(argv[0])) {
      int32_t fd = -1;

      JS_ToInt32(ctx, &fd, argv[0]);
      getdents_adopt(directory, fd);
    } else {
      const char* dir;
      dir = JS_ToCString(ctx, argv[0]);

      getdents_open(dir, dir);
      JS_FreeCString(ctx, dir);
    }
  }
  JS_SetOpaque(obj, directory);

  return obj;

fail:
  js_free(ctx, directory);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_directory_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Directory* directory;
  JSValue ret = JS_UNDEFINED;

  if(!(directory = js_directory_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case DIRECTORY_NEXT: {
      DirEntry* entry;
      int32_t flags = FLAG_BOTH;
      JSValue value = JS_UNDEFINED;
      BOOL done = FALSE;

      if((entry = getdents_read(directory))) {

        if(argc > 0)
          JS_ToInt32(ctx, &flags, argv[0]);

        value = js_directory_value(ctx, directory, flags);
      } else {
        done = TRUE;
      }
      ret = js_iterator_result(ctx, value, done);
      break;
    }
  }
  return ret;
}

static void
js_directory_finalizer(JSRuntime* rt, JSValue val) {
  Directory* directory;
  if((directory = JS_GetOpaque(val, js_directory_class_id))) {
    getdents_close(directory);
    js_free_rt(rt, directory);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_directory_class = {
    .class_name = "Directory",
    .finalizer = js_directory_finalizer,
};

static const JSCFunctionListEntry js_directory_funcs[] = {
    JS_CFUNC_MAGIC_DEF("next", 0, js_directory_method, DIRECTORY_NEXT),
    /*    JS_CFUNC_MAGIC_DEF("stream", 0, js_directory_method, DIRECTORY_STREAM),
        JS_CFUNC_MAGIC_DEF("slice", 0, js_directory_method, DIRECTORY_SLICE),
        JS_CFUNC_MAGIC_DEF("text", 0, js_directory_method, DIRECTORY_TEXT),
        JS_CGETSET_MAGIC_DEF("size", js_directory_get, 0, DIRECTORY_SIZE),
        JS_CGETSET_MAGIC_DEF("type", js_directory_get, 0, DIRECTORY_TYPE),*/
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Directory", JS_PROP_CONFIGURABLE),

    JS_PROP_INT32_DEF("NAME", FLAG_NAME, 0),
    JS_PROP_INT32_DEF("TYPE", FLAG_TYPE, 0),
};

static const JSCFunctionListEntry js_directory_static[] = {
    JS_PROP_INT32_DEF("NAME", FLAG_NAME, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE", FLAG_TYPE, JS_PROP_ENUMERABLE),
};

int
js_directory_init(JSContext* ctx, JSModuleDef* m) {

  if(js_directory_class_id == 0) {
    JS_NewClassID(&js_directory_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_directory_class_id, &js_directory_class);

    directory_ctor = JS_NewCFunction2(ctx, js_directory_constructor, "Directory", 1, JS_CFUNC_constructor, 0);
    directory_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, directory_proto, js_directory_funcs, countof(js_directory_funcs));
    JS_SetPropertyFunctionList(ctx, directory_ctor, js_directory_static, countof(js_directory_static));

    JS_SetClassProto(ctx, js_directory_class_id, directory_proto);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "Directory", directory_ctor);

    const char* module_name = JS_AtomToCString(ctx, m->module_name);

    if(!strcmp(module_name, "directory"))
      JS_SetModuleExport(ctx, m, "default", directory_ctor);

    JS_FreeCString(ctx, module_name);
  }

  return 0;
}

#ifdef JS_DIRECTORY_MODULE
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_directory
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  if(!(m = JS_NewCModule(ctx, module_name, &js_directory_init)))
    return m;
  JS_AddModuleExport(ctx, m, "Directory");

  /* if(!strcmp(module_name, "directory"))
     JS_AddModuleExport(ctx, m, "default");*/

  return m;
}

/**
 * @}
 */
