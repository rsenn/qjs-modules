#include "defines.h"
#include "buffer-utils.h"
#include "utils.h"
#include <errno.h>
#include <string.h>
#include <magic.h>

/**
 * \defgroup quickjs-magic QuickJS module: magic - libmagic MIME types
 * @{
 */
thread_local VISIBLE JSClassID js_magic_class_id = 0;
thread_local JSValue magic_proto = {{JS_TAG_UNDEFINED}}, magic_ctor = {{JS_TAG_UNDEFINED}};

enum {
  LIBMAGIC_ERROR = 0,
  LIBMAGIC_ERRNO,
  LIBMAGIC_DESCRIPTOR,
  LIBMAGIC_FILE,
  LIBMAGIC_BUFFER,
  LIBMAGIC_GETFLAGS,
  LIBMAGIC_SETFLAGS,
  LIBMAGIC_CHECK,
  LIBMAGIC_COMPILE,
  LIBMAGIC_LIST,
  LIBMAGIC_LOAD,
  LIBMAGIC_LOAD_BUFFERS,
  LIBMAGIC_GETPARAM,
  LIBMAGIC_SETPARAM,
  LIBMAGIC_VERSION,
};

static inline magic_t
js_magic_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque(value, js_magic_class_id);
}

static JSValue
js_magic_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  magic_t cookie;
  int32_t flags = 0;

  if(argc > 0)
    JS_ToInt32(ctx, &flags, argv[0]);

  cookie = magic_open(flags);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = magic_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_magic_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, cookie);

  return obj;

fail:
  js_free(ctx, cookie);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_magic_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  magic_t cookie;
  JSValue ret = JS_UNDEFINED;

  if(!(cookie = js_magic_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LIBMAGIC_DESCRIPTOR: {
      int32_t fd = -1;
      JS_ToInt32(ctx, &fd, argv[0]);
      const char* str = magic_descriptor(cookie, fd);
      ret = str ? JS_NewString(ctx, str) : JS_ThrowInternalError(ctx, "libmagic error: %s", magic_error(cookie));

      break;
    }
    case LIBMAGIC_FILE: {
      const char* filename = JS_IsNull(argv[0]) ? NULL : JS_ToCString(ctx, argv[0]);
      const char* str = magic_file(cookie, filename);
      ret = str ? JS_NewString(ctx, str) : JS_ThrowInternalError(ctx, "libmagic error: %s", magic_error(cookie));

      if(filename)
        JS_FreeCString(ctx, filename);
      break;
    }
    case LIBMAGIC_BUFFER: {
      InputBuffer buf = js_input_chars(ctx, argv[0]);
      const char* str = magic_buffer(cookie, buf.data, buf.size);
      ret = str ? JS_NewString(ctx, str) : JS_ThrowInternalError(ctx, "libmagic error: %s", magic_error(cookie));

      input_buffer_free(&buf, ctx);
      break;
    }
    case LIBMAGIC_GETFLAGS: {
      ret = JS_NewInt32(ctx, magic_getflags(cookie));
      break;
    }
    case LIBMAGIC_SETFLAGS: {
      int32_t flags = -1;
      JS_ToInt32(ctx, &flags, argv[0]);
      ret = JS_NewInt32(ctx, magic_setflags(cookie, flags));
      break;
    }
    case LIBMAGIC_CHECK: {
      const char* filename = JS_IsNull(argv[0]) ? NULL : JS_ToCString(ctx, argv[0]);

      if(magic_check(cookie, filename))
        ret = JS_ThrowInternalError(ctx, "libmagic error: %s", magic_error(cookie));

      if(filename)
        JS_FreeCString(ctx, filename);
      break;
    }
    case LIBMAGIC_COMPILE: {
      const char* filename = JS_IsNull(argv[0]) ? NULL : JS_ToCString(ctx, argv[0]);

      if(magic_compile(cookie, filename))
        ret = JS_ThrowInternalError(ctx, "libmagic error: %s", magic_error(cookie));

      if(filename)
        JS_FreeCString(ctx, filename);
      break;
    }
    case LIBMAGIC_LIST: {
      const char* filename = JS_IsNull(argv[0]) ? NULL : JS_ToCString(ctx, argv[0]);

      if(magic_list(cookie, filename))
        ret = JS_ThrowInternalError(ctx, "libmagic error: %s", magic_error(cookie));

      if(filename)
        JS_FreeCString(ctx, filename);
      break;
    }
    case LIBMAGIC_LOAD: {
      const char* filename = argc < 1 || JS_IsNull(argv[0]) ? NULL : JS_ToCString(ctx, argv[0]);

      if(magic_load(cookie, filename))
        ret = JS_ThrowInternalError(ctx, "libmagic error: %s", magic_error(cookie));

      if(filename)
        JS_FreeCString(ctx, filename);
      break;
    }
    case LIBMAGIC_LOAD_BUFFERS: {
      InputBuffer buf[argc];
      void* pointers[argc];
      size_t sizes[argc];

      for(int i = 0; i < argc; i++) {
        buf[i] = js_input_chars(ctx, argv[i]);
        pointers[i] = buf[i].data;
        sizes[i] = buf[i].size;
      }

      if(magic_load_buffers(cookie, pointers, sizes, argc))
        ret = JS_ThrowInternalError(ctx, "libmagic error: %s", magic_error(cookie));

      for(int i = 0; i < argc; i++) { input_buffer_free(&buf[i], ctx); }

      break;
    }
    case LIBMAGIC_GETPARAM: {
      int32_t param = -1;
      size_t value = 0;
      JS_ToInt32(ctx, &param, argv[0]);

      if(!magic_getparam(cookie, param, &value))
        ret = JS_NewInt64(ctx, (int64_t)value);
      break;
    }
    case LIBMAGIC_SETPARAM: {
      int32_t param = -1;
      int64_t value = -1;
      size_t sz;
      JS_ToInt32(ctx, &param, argv[0]);
      JS_ToInt64(ctx, &value, argv[1]);
      sz = value;

      magic_setparam(cookie, param, &sz);
      break;
    }
  }
  return ret;
}

static JSValue
js_magic_get(JSContext* ctx, JSValueConst this_val, int magic) {
  magic_t cookie;
  JSValue ret = JS_UNDEFINED;

  if(!(cookie = js_magic_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case LIBMAGIC_ERROR: {
      ret = JS_NewString(ctx, magic_error(cookie));
      break;
    }
    case LIBMAGIC_ERRNO: {
      ret = JS_NewInt32(ctx, magic_errno(cookie));
      break;
    }
    case LIBMAGIC_VERSION: {
      ret = JS_NewInt32(ctx, magic_version());
      break;
    }
  }
  return ret;
}

static void
js_magic_finalizer(JSRuntime* rt, JSValue val) {
  magic_t cookie;

  if((cookie = JS_GetOpaque(val, js_magic_class_id))) {
    magic_close(cookie);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_magic_class = {
    .class_name = "Magic",
    .finalizer = js_magic_finalizer,
};

static const JSCFunctionListEntry js_magic_funcs[] = {
    JS_CGETSET_MAGIC_DEF("error", js_magic_get, 0, LIBMAGIC_ERROR),
    JS_CGETSET_MAGIC_DEF("errno", js_magic_get, 0, LIBMAGIC_ERRNO),
    JS_CFUNC_MAGIC_DEF("descriptor", 1, js_magic_method, LIBMAGIC_DESCRIPTOR),
    JS_CFUNC_MAGIC_DEF("file", 1, js_magic_method, LIBMAGIC_FILE),
    JS_CFUNC_MAGIC_DEF("buffer", 1, js_magic_method, LIBMAGIC_BUFFER),
    JS_CFUNC_MAGIC_DEF("getflags", 0, js_magic_method, LIBMAGIC_GETFLAGS),
    JS_CFUNC_MAGIC_DEF("setflags", 1, js_magic_method, LIBMAGIC_SETFLAGS),
    JS_CFUNC_MAGIC_DEF("check", 1, js_magic_method, LIBMAGIC_CHECK),
    JS_CFUNC_MAGIC_DEF("compile", 1, js_magic_method, LIBMAGIC_COMPILE),
    JS_CFUNC_MAGIC_DEF("list", 1, js_magic_method, LIBMAGIC_LIST),
    JS_CFUNC_MAGIC_DEF("load", 0, js_magic_method, LIBMAGIC_LOAD),
    JS_CFUNC_MAGIC_DEF("loadBuffers", 1, js_magic_method, LIBMAGIC_LOAD_BUFFERS),
    JS_CFUNC_MAGIC_DEF("getparam", 2, js_magic_method, LIBMAGIC_GETPARAM),
    JS_CFUNC_MAGIC_DEF("setparam", 2, js_magic_method, LIBMAGIC_SETPARAM),
    JS_CGETSET_MAGIC_DEF("version", js_magic_get, 0, LIBMAGIC_VERSION),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Magic", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_magic_static[] = {
    JS_CONSTANT(MAGIC_NONE),
    JS_CONSTANT(MAGIC_DEBUG),
    JS_CONSTANT(MAGIC_SYMLINK),
    JS_CONSTANT(MAGIC_COMPRESS),
    JS_CONSTANT(MAGIC_DEVICES),
    JS_CONSTANT(MAGIC_MIME_TYPE),
    JS_CONSTANT(MAGIC_CONTINUE),
    JS_CONSTANT(MAGIC_CHECK),
    JS_CONSTANT(MAGIC_PRESERVE_ATIME),
    JS_CONSTANT(MAGIC_RAW),
    JS_CONSTANT(MAGIC_ERROR),
    JS_CONSTANT(MAGIC_MIME_ENCODING),
    JS_CONSTANT(MAGIC_MIME),
    JS_CONSTANT(MAGIC_APPLE),
    JS_CONSTANT(MAGIC_EXTENSION),
    JS_CONSTANT(MAGIC_COMPRESS_TRANSP),
    JS_CONSTANT(MAGIC_NODESC),
    JS_CONSTANT(MAGIC_NO_CHECK_COMPRESS),
    JS_CONSTANT(MAGIC_NO_CHECK_TAR),
    JS_CONSTANT(MAGIC_NO_CHECK_SOFT),
    JS_CONSTANT(MAGIC_NO_CHECK_APPTYPE),
    JS_CONSTANT(MAGIC_NO_CHECK_ELF),
    JS_CONSTANT(MAGIC_NO_CHECK_TEXT),
    JS_CONSTANT(MAGIC_NO_CHECK_CDF),
    JS_CONSTANT(MAGIC_NO_CHECK_CSV),
    JS_CONSTANT(MAGIC_NO_CHECK_TOKENS),
    JS_CONSTANT(MAGIC_NO_CHECK_ENCODING),
    JS_CONSTANT(MAGIC_NO_CHECK_JSON),
    JS_CONSTANT(MAGIC_NO_CHECK_BUILTIN),
    JS_CONSTANT(MAGIC_NO_CHECK_ASCII),
    JS_CONSTANT(MAGIC_NO_CHECK_FORTRAN),
    JS_CONSTANT(MAGIC_NO_CHECK_TROFF),
    JS_CONSTANT(MAGIC_VERSION),
    JS_CONSTANT(MAGIC_PARAM_INDIR_MAX),
    JS_CONSTANT(MAGIC_PARAM_NAME_MAX),
    JS_CONSTANT(MAGIC_PARAM_ELF_PHNUM_MAX),
    JS_CONSTANT(MAGIC_PARAM_ELF_SHNUM_MAX),
    JS_CONSTANT(MAGIC_PARAM_ELF_NOTES_MAX),
    JS_CONSTANT(MAGIC_PARAM_REGEX_MAX),
    JS_CONSTANT(MAGIC_PARAM_BYTES_MAX),
};

int
js_magic_init(JSContext* ctx, JSModuleDef* m) {

  if(js_magic_class_id == 0) {
    JS_NewClassID(&js_magic_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_magic_class_id, &js_magic_class);

    magic_ctor = JS_NewCFunction2(ctx, js_magic_constructor, "Magic", 1, JS_CFUNC_constructor, 0);
    magic_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, magic_proto, js_magic_funcs, countof(js_magic_funcs));
    JS_SetPropertyFunctionList(ctx, magic_ctor, js_magic_static, countof(js_magic_static));

    JS_SetClassProto(ctx, js_magic_class_id, magic_proto);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "Magic", magic_ctor);

    const char* module_name = JS_AtomToCString(ctx, m->module_name);

    if(!strcmp(module_name, "cookie"))
      JS_SetModuleExport(ctx, m, "default", magic_ctor);

    JS_FreeCString(ctx, module_name);
  }

  return 0;
}

#ifdef JS_MAGIC_MODULE
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_magic
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  if(!(m = JS_NewCModule(ctx, module_name, &js_magic_init)))
    return m;
  JS_AddModuleExport(ctx, m, "Magic");

  /* if(!strcmp(module_name, "cookie"))
     JS_AddModuleExport(ctx, m, "default");*/

  return m;
}

/**
 * @}
 */
