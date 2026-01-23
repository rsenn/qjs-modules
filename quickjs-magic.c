#include "defines.h"
#include "buffer-utils.h"
#include "utils.h"
#include <errno.h>
#include <string.h>
#include <magic.h>

/**
 * \defgroup quickjs-magic quickjs-magic: libmagic MIME types
 * @{
 */
VISIBLE JSClassID js_magic_class_id = 0;
static JSValue magic_proto, magic_ctor;

enum {
  LIBMAGIC_ERROR = 0,
  LIBMAGIC_ERRNO,
  LIBMAGIC_DESCRIPTOR,
  LIBMAGIC_FILE,
  LIBMAGIC_BUFFER,
  METHOD_GETFLAGS,
  METHOD_SETFLAGS,
  METHOD_CHECK,
  METHOD_COMPILE,
  METHOD_LIST,
  METHOD_LOAD,
  METHOD_GETPARAM,
  METHOD_SETPARAM,
  METHOD_VERSION,
};

static inline magic_t
js_magic_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque(value, js_magic_class_id);
}

static int
js_magic_load(JSContext* ctx, magic_t cookie, int argc, JSValueConst argv[]) {
  int n = 1;

  if(JS_IsString(argv[0])) {
    const char* str;

    if((str = JS_ToCString(ctx, argv[0]))) {
      magic_load(cookie, str);
      JS_FreeCString(ctx, str);
    }

  } else {
    InputBuffer input = js_input_chars(ctx, argv[0]);

    if(argc > 1)
      n += js_offset_length(ctx, input.size, argc, argv, 1, &input.range);

    void* buf[] = {(void*)inputbuffer_data(&input), NULL};
    size_t siz[] = {inputbuffer_length(&input), 0};

    magic_load_buffers(cookie, buf, siz, 1);

    inputbuffer_free(&input, ctx);
  }

  return n;
}

static JSValue
js_magic_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  magic_t cookie;
  int32_t flags = 0;

  if(argc > 0 && JS_IsNumber(argv[0])) {
    JS_ToInt32(ctx, &flags, argv[0]);

    argc--;
    argv++;
  }

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

  int n;

  while(argc > 0 && (n = js_magic_load(ctx, cookie, argc, argv))) {
    argc -= n;
    argv += n;
  }

  JS_SetOpaque(obj, cookie);
  return obj;

fail:
  js_free(ctx, cookie);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_magic_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  magic_t cookie;
  const char* str = 0;

  if(!(cookie = js_magic_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LIBMAGIC_DESCRIPTOR: {
      int32_t fd = -1;

      JS_ToInt32(ctx, &fd, argv[0]);
      str = magic_descriptor(cookie, fd);

      break;
    }

    case LIBMAGIC_FILE: {
      const char* filename = JS_IsNull(argv[0]) ? NULL : JS_ToCString(ctx, argv[0]);

      str = magic_file(cookie, filename);

      if(filename)
        JS_FreeCString(ctx, filename);

      break;
    }

    case LIBMAGIC_BUFFER: {
      int n = 1;
      InputBuffer input = js_input_chars(ctx, argv[0]);

      if(argc > 1)
        n += js_offset_length(ctx, input.size, argc, argv, 1, &input.range);

      str = magic_buffer(cookie, inputbuffer_data(&input), inputbuffer_length(&input));

      inputbuffer_free(&input, ctx);
      break;
    }
  }

  return str ? JS_NewString(ctx, str) : JS_ThrowInternalError(ctx, "libmagic error: %s", magic_error(cookie));
}

static JSValue
js_magic_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  magic_t cookie;
  JSValue ret = JS_UNDEFINED;

  if(!(cookie = js_magic_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {

    case METHOD_GETFLAGS: {
      ret = JS_NewInt32(ctx, magic_getflags(cookie));
      break;
    }

    case METHOD_SETFLAGS: {
      int32_t flags = -1;

      JS_ToInt32(ctx, &flags, argv[0]);
      ret = JS_NewInt32(ctx, magic_setflags(cookie, flags));
      break;
    }

    case METHOD_CHECK: {
      const char* filename = JS_IsNull(argv[0]) ? NULL : JS_ToCString(ctx, argv[0]);

      if(magic_check(cookie, filename))
        ret = JS_ThrowInternalError(ctx, "libmagic error: %s", magic_error(cookie));

      if(filename)
        JS_FreeCString(ctx, filename);

      break;
    }

    case METHOD_COMPILE: {
      const char* filename = JS_IsNull(argv[0]) ? NULL : JS_ToCString(ctx, argv[0]);

      if(magic_compile(cookie, filename))
        ret = JS_ThrowInternalError(ctx, "libmagic error: %s", magic_error(cookie));

      if(filename)
        JS_FreeCString(ctx, filename);

      break;
    }

    case METHOD_LIST: {
      const char* filename = JS_IsNull(argv[0]) ? NULL : JS_ToCString(ctx, argv[0]);

      if(magic_list(cookie, filename))
        ret = JS_ThrowInternalError(ctx, "libmagic error: %s", magic_error(cookie));

      if(filename)
        JS_FreeCString(ctx, filename);

      break;
    }

    case METHOD_LOAD: {
      int i = 0;

      while(argc > 0) {
        int n;

        if((n = js_magic_load(ctx, cookie, argc, argv)) == 0)
          break;

        argc -= n;
        argv += n;
        i++;
      }

      ret = JS_NewInt32(ctx, i);
      break;
    }

    case METHOD_GETPARAM: {
      int32_t param = -1;
      size_t value = 0;

      JS_ToInt32(ctx, &param, argv[0]);

      if(!magic_getparam(cookie, param, &value))
        ret = JS_NewInt64(ctx, (int64_t)value);

      break;
    }

    case METHOD_SETPARAM: {
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

    case METHOD_VERSION: {
      ret = JS_NewInt32(ctx, magic_version());
      break;
    }
  }

  return ret;
}

static JSValue
js_magic_exec(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  magic_t cookie;

  if(!(cookie = js_magic_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(argc == 0)
    return JS_ThrowInternalError(ctx, "Magic(arg)");

  return js_magic_function(ctx, this_val, argc, argv, JS_IsNumber(argv[0]) ? LIBMAGIC_DESCRIPTOR : JS_IsString(argv[0]) ? LIBMAGIC_FILE : LIBMAGIC_BUFFER);
}

static JSValue
js_magic_call(JSContext* ctx, JSValueConst func_obj, JSValueConst this_val, int argc, JSValueConst argv[], int flags) {
  return js_magic_exec(ctx, func_obj, argc, argv);
}

static void
js_magic_finalizer(JSRuntime* rt, JSValue val) {
  magic_t cookie;

  if((cookie = JS_GetOpaque(val, js_magic_class_id))) {
    JS_SetOpaque(val, NULL);
    magic_close(cookie);
  }
}

static JSClassDef js_magic_class = {.class_name = "Magic", .finalizer = js_magic_finalizer, .call = js_magic_call};

static const JSCFunctionListEntry js_magic_funcs[] = {
    JS_CGETSET_MAGIC_DEF("error", js_magic_get, 0, LIBMAGIC_ERROR),
    JS_CGETSET_MAGIC_DEF("errno", js_magic_get, 0, LIBMAGIC_ERRNO),
    JS_CFUNC_MAGIC_DEF("descriptor", 1, js_magic_function, LIBMAGIC_DESCRIPTOR),
    JS_CFUNC_MAGIC_DEF("file", 1, js_magic_function, LIBMAGIC_FILE),
    JS_CFUNC_MAGIC_DEF("buffer", 1, js_magic_function, LIBMAGIC_BUFFER),
    JS_CFUNC_MAGIC_DEF("getflags", 0, js_magic_method, METHOD_GETFLAGS),
    JS_CFUNC_MAGIC_DEF("setflags", 1, js_magic_method, METHOD_SETFLAGS),
    JS_CFUNC_MAGIC_DEF("check", 1, js_magic_method, METHOD_CHECK),
    JS_CFUNC_MAGIC_DEF("compile", 1, js_magic_method, METHOD_COMPILE),
    JS_CFUNC_MAGIC_DEF("list", 1, js_magic_method, METHOD_LIST),
    JS_CFUNC_MAGIC_DEF("load", 0, js_magic_method, METHOD_LOAD),
    JS_CFUNC_MAGIC_DEF("getparam", 2, js_magic_method, METHOD_GETPARAM),
    JS_CFUNC_MAGIC_DEF("setparam", 2, js_magic_method, METHOD_SETPARAM),
    JS_CGETSET_MAGIC_DEF("version", js_magic_get, 0, METHOD_VERSION),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Magic", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_magic_static[] = {
    JS_PROP_INT32_DEF("NONE", MAGIC_NONE, 0),
    JS_PROP_INT32_DEF("DEBUG", MAGIC_DEBUG, 0),
    JS_PROP_INT32_DEF("SYMLINK", MAGIC_SYMLINK, 0),
    JS_PROP_INT32_DEF("COMPRESS", MAGIC_COMPRESS, 0),
    JS_PROP_INT32_DEF("DEVICES", MAGIC_DEVICES, 0),
    JS_PROP_INT32_DEF("MIME_TYPE", MAGIC_MIME_TYPE, 0),
    JS_PROP_INT32_DEF("CONTINUE", MAGIC_CONTINUE, 0),
    JS_PROP_INT32_DEF("CHECK", MAGIC_CHECK, 0),
    JS_PROP_INT32_DEF("PRESERVE_ATIME", MAGIC_PRESERVE_ATIME, 0),
    JS_PROP_INT32_DEF("RAW", MAGIC_RAW, 0),
    JS_PROP_INT32_DEF("ERROR", MAGIC_ERROR, 0),
    JS_PROP_INT32_DEF("MIME_ENCODING", MAGIC_MIME_ENCODING, 0),
    JS_PROP_INT32_DEF("MIME", MAGIC_MIME, 0),
    JS_PROP_INT32_DEF("APPLE", MAGIC_APPLE, 0),
    JS_PROP_INT32_DEF("EXTENSION", MAGIC_EXTENSION, 0),
    JS_PROP_INT32_DEF("COMPRESS_TRANSP", MAGIC_COMPRESS_TRANSP, 0),
    JS_PROP_INT32_DEF("NODESC", MAGIC_NODESC, 0),
    JS_PROP_INT32_DEF("NO_CHECK_COMPRESS", MAGIC_NO_CHECK_COMPRESS, 0),
    JS_PROP_INT32_DEF("NO_CHECK_TAR", MAGIC_NO_CHECK_TAR, 0),
    JS_PROP_INT32_DEF("NO_CHECK_SOFT", MAGIC_NO_CHECK_SOFT, 0),
    JS_PROP_INT32_DEF("NO_CHECK_APPTYPE", MAGIC_NO_CHECK_APPTYPE, 0),
    JS_PROP_INT32_DEF("NO_CHECK_ELF", MAGIC_NO_CHECK_ELF, 0),
    JS_PROP_INT32_DEF("NO_CHECK_TEXT", MAGIC_NO_CHECK_TEXT, 0),
    JS_PROP_INT32_DEF("NO_CHECK_CDF", MAGIC_NO_CHECK_CDF, 0),
    JS_PROP_INT32_DEF("NO_CHECK_CSV", MAGIC_NO_CHECK_CSV, 0),
    JS_PROP_INT32_DEF("NO_CHECK_TOKENS", MAGIC_NO_CHECK_TOKENS, 0),
    JS_PROP_INT32_DEF("NO_CHECK_ENCODING", MAGIC_NO_CHECK_ENCODING, 0),
    JS_PROP_INT32_DEF("NO_CHECK_JSON", MAGIC_NO_CHECK_JSON, 0),
    JS_PROP_INT32_DEF("NO_CHECK_BUILTIN", MAGIC_NO_CHECK_BUILTIN, 0),
    JS_PROP_INT32_DEF("NO_CHECK_ASCII", MAGIC_NO_CHECK_ASCII, 0),
    JS_PROP_INT32_DEF("NO_CHECK_FORTRAN", MAGIC_NO_CHECK_FORTRAN, 0),
    JS_PROP_INT32_DEF("NO_CHECK_TROFF", MAGIC_NO_CHECK_TROFF, 0),
    JS_PROP_INT32_DEF("VERSION", MAGIC_VERSION, 0),
    JS_PROP_INT32_DEF("PARAM_INDIR_MAX", MAGIC_PARAM_INDIR_MAX, 0),
    JS_PROP_INT32_DEF("PARAM_NAME_MAX", MAGIC_PARAM_NAME_MAX, 0),
    JS_PROP_INT32_DEF("PARAM_ELF_PHNUM_MAX", MAGIC_PARAM_ELF_PHNUM_MAX, 0),
    JS_PROP_INT32_DEF("PARAM_ELF_SHNUM_MAX", MAGIC_PARAM_ELF_SHNUM_MAX, 0),
    JS_PROP_INT32_DEF("PARAM_ELF_NOTES_MAX", MAGIC_PARAM_ELF_NOTES_MAX, 0),
    JS_PROP_INT32_DEF("PARAM_REGEX_MAX", MAGIC_PARAM_REGEX_MAX, 0),
    JS_PROP_INT32_DEF("PARAM_BYTES_MAX", MAGIC_PARAM_BYTES_MAX, 0),
    JS_PROP_STRING_DEF("DEFAULT_DB", LIBMAGIC_DB, JS_PROP_C_W_E),
};

int
js_magic_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_magic_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_magic_class_id, &js_magic_class);

  magic_ctor = JS_NewCFunction2(ctx, js_magic_constructor, "Magic", 1, JS_CFUNC_constructor, 0);
  magic_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, magic_proto, js_magic_funcs, countof(js_magic_funcs));
  JS_SetPropertyFunctionList(ctx, magic_ctor, js_magic_static, countof(js_magic_static));

  JS_SetClassProto(ctx, js_magic_class_id, magic_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "Magic", magic_ctor);

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

  if((m = JS_NewCModule(ctx, module_name, js_magic_init)))
    JS_AddModuleExport(ctx, m, "Magic");

  return m;
}

/**
 * @}
 */
