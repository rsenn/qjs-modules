#include "defines.h"
#include "getdents.h"
#include "utils.h"
#include "char-utils.h"
#include <errno.h>
#include <string.h>

/**
 * \defgroup quickjs-directory QuickJS module: directory - Directory reader
 * @{
 */
VISIBLE JSClassID js_directory_class_id = 0;
VISIBLE JSValue directory_proto = {{0}, JS_TAG_UNDEFINED}, directory_ctor = {{0}, JS_TAG_UNDEFINED};

enum { FLAG_NAME = 1, FLAG_TYPE = 2, FLAG_BOTH = FLAG_NAME | FLAG_TYPE, FLAG_BUFFER = 0x80 };
enum {
  DIRECTORY_OPEN,
  DIRECTORY_ADOPT,
  DIRECTORY_CLOSE,
  DIRECTORY_ITERATOR,
  DIRECTORY_VALUE_OF,
  DIRECTORY_NEXT,
  DIRECTORY_RETURN,
  DIRECTORY_THROW,
};

static JSValue
directory_namebuf(JSContext* ctx, DirEntry* entry) {
  JSValue ret;

  size_t len = 0;
  return JS_NewArrayBufferCopy(ctx, getdents_namebuf(entry, &len), len);
}

static JSValue
directory_namestr(JSContext* ctx, DirEntry* entry) {
#if !(defined(_WIN32) && !defined(__MSYS__))
  return JS_NewString(ctx, getdents_cname(entry));
#else
  JSValue ret = JS_UNDEFINED;
  char* str;

  if((str = getdents_name(entry))) {
    ret = JS_NewString(ctx, str);
    free(str);
  }

  return ret;
#endif
}

static JSValue
js_directory_entry(JSContext* ctx, DirEntry* entry, int dflags) {
  JSValue name;
  int type = -1;
  JSValue ret;

  if(dflags & FLAG_NAME)
    name = (dflags & FLAG_BUFFER) ? directory_namebuf(ctx, entry) : directory_namestr(ctx, entry);

  if(dflags & FLAG_TYPE)
    type = getdents_type(entry);

  switch(dflags) {
    case FLAG_NAME: {
      ret = name;
      break;
    }

    case FLAG_TYPE: {
      ret = JS_NewInt32(ctx, type);
      break;
    }

    case FLAG_BOTH: {
      ret = JS_NewArray(ctx);
      JS_SetPropertyUint32(ctx, ret, 0, name);
      JS_SetPropertyUint32(ctx, ret, 1, JS_NewInt32(ctx, type));
      break;
    }
  }
  return ret;
}

static inline Directory*
js_directory_data(JSValueConst value) {
  return JS_GetOpaque(value, js_directory_class_id);
}

static inline Directory*
js_directory_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_directory_class_id);
}

static JSValue
js_directory_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  Directory* directory;
  int32_t* opts;

  if(!(directory = js_malloc(ctx, getdents_size() + sizeof(int32_t) * 2)))
    return JS_ThrowOutOfMemory(ctx);

  getdents_clear(directory);

  opts = ((int32_t*)((char*)directory + getdents_size()));

  opts[0] = FLAG_BOTH;
  opts[1] = TYPE_MASK;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_directory_class_id);
  JS_FreeValue(ctx, proto);

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

      getdents_open(directory, dir);
      JS_FreeCString(ctx, dir);
    }
  }

  if(argc > 1)
    JS_ToInt32(ctx, &opts[0], argv[1]);

  if(argc > 2)
    JS_ToInt32(ctx, &opts[1], argv[2]);

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

  if(!(directory = js_directory_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case DIRECTORY_OPEN: {
      const char* dir;
      dir = JS_ToCString(ctx, argv[0]);

      if(getdents_open(directory, dir))
        ret = JS_ThrowInternalError(ctx, "getdents_open(%s) failed: %s", dir, strerror(errno));

      JS_FreeCString(ctx, dir);
      break;
    }
    case DIRECTORY_ADOPT: {
      int32_t fd = -1;

      JS_ToInt32(ctx, &fd, argv[0]);

      if(getdents_adopt(directory, fd))
        ret = JS_ThrowInternalError(ctx, "getdents_adopt(%d) failed: %s", fd, strerror(errno));

      break;
    }
    case DIRECTORY_ITERATOR: {
      ret = JS_DupValue(ctx, this_val);
      break;
    }
    case DIRECTORY_CLOSE: {
      getdents_close(directory);
      break;
    }
    case DIRECTORY_VALUE_OF: {
      ret = JS_NewInt64(ctx, getdents_handle(directory));
      break;
    }
    case DIRECTORY_NEXT: {
      DirEntry* entry;
      int32_t* opts = ((int32_t*)((char*)directory + getdents_size()));
      int32_t flags = opts[0], mask = opts[1];
      JSValue value = JS_UNDEFINED;
      BOOL done = FALSE;

      if(argc > 0)
        JS_ToInt32(ctx, &flags, argv[0]);
      if(argc > 1)
        JS_ToInt32(ctx, &mask, argv[1]);

      for(;;) {
        if(!(entry = getdents_read(directory)))
          break;

        if((getdents_type(entry) & mask) == 0)
          continue;

        value = js_directory_entry(ctx, entry, flags);
        break;
      }

      if(!entry) {
        getdents_close(directory);
        done = TRUE;
      }
      ret = js_iterator_result(ctx, value, done);
      JS_FreeValue(ctx, value);
      break;
    }
    case DIRECTORY_RETURN: {
      ret = js_iterator_result(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, TRUE);
      break;
    }
    case DIRECTORY_THROW: {
      ret = JS_Throw(ctx, argv[0]);
      break;
    }
  }

  return ret;
}

static void
js_directory_finalizer(JSRuntime* rt, JSValue val) {
  Directory* directory;

  if((directory = js_directory_data(val))) {
    getdents_close(directory);
    js_free_rt(rt, directory);
  }
}

static JSClassDef js_directory_class = {
    .class_name = "Directory",
    .finalizer = js_directory_finalizer,
};

static const JSCFunctionListEntry js_directory_funcs[] = {
    JS_CFUNC_MAGIC_DEF("open", 1, js_directory_method, DIRECTORY_OPEN),
    JS_CFUNC_MAGIC_DEF("adopt", 1, js_directory_method, DIRECTORY_ADOPT),
    JS_CFUNC_MAGIC_DEF("close", 0, js_directory_method, DIRECTORY_CLOSE),
    JS_CFUNC_MAGIC_DEF("valueOf", 0, js_directory_method, DIRECTORY_VALUE_OF),
    JS_PROP_INT32_DEF("NAME", FLAG_NAME, 0),
    JS_PROP_INT32_DEF("TYPE", FLAG_TYPE, 0),
    JS_PROP_INT32_DEF("BOTH", FLAG_BOTH, 0),
    JS_PROP_INT32_DEF("TYPE_BLK", TYPE_BLK, 0),
    JS_PROP_INT32_DEF("TYPE_CHR", TYPE_CHR, 0),
    JS_PROP_INT32_DEF("TYPE_DIR", TYPE_DIR, 0),
    JS_PROP_INT32_DEF("TYPE_FIFO", TYPE_FIFO, 0),
    JS_PROP_INT32_DEF("TYPE_LNK", TYPE_LNK, 0),
    JS_PROP_INT32_DEF("TYPE_REG", TYPE_REG, 0),
    JS_PROP_INT32_DEF("TYPE_SOCK", TYPE_SOCK, 0),
    JS_PROP_INT32_DEF("TYPE_MASK", TYPE_MASK, 0),
    JS_CFUNC_MAGIC_DEF("next", 0, js_directory_method, DIRECTORY_NEXT),
    JS_CFUNC_MAGIC_DEF("return", 0, js_directory_method, DIRECTORY_RETURN),
    JS_CFUNC_MAGIC_DEF("throw", 1, js_directory_method, DIRECTORY_THROW),
    JS_CFUNC_MAGIC_DEF("[Symbol.iterator]", 0, js_directory_method, DIRECTORY_ITERATOR),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Directory", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_directory_static[] = {
    JS_PROP_INT32_DEF("NAME", FLAG_NAME, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE", FLAG_TYPE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("BOTH", FLAG_BOTH, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_BLK", TYPE_BLK, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_CHR", TYPE_CHR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_DIR", TYPE_DIR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_FIFO", TYPE_FIFO, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_LNK", TYPE_LNK, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_REG", TYPE_REG, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_SOCK", TYPE_SOCK, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_MASK", TYPE_MASK, JS_PROP_ENUMERABLE),
};

int
js_directory_init(JSContext* ctx, JSModuleDef* m) {

  if(js_directory_class_id == 0) {
    JS_NewClassID(&js_directory_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_directory_class_id, &js_directory_class);

    directory_ctor = JS_NewCFunction2(ctx, js_directory_constructor, "Directory", 1, JS_CFUNC_constructor, 0);
    JSValue generator_proto = js_generator_prototype(ctx);
    directory_proto = JS_NewObjectProto(ctx, generator_proto);
    JS_FreeValue(ctx, generator_proto);

    JS_SetPropertyFunctionList(ctx, directory_proto, js_directory_funcs, countof(js_directory_funcs));
    JS_SetPropertyFunctionList(ctx, directory_ctor, js_directory_static, countof(js_directory_static));

    JS_SetClassProto(ctx, js_directory_class_id, directory_proto);
    JS_SetConstructor(ctx, directory_ctor, directory_proto);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "Directory", directory_ctor);
    JS_SetModuleExportList(ctx, m, js_directory_static, countof(js_directory_static));

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

  if((m = JS_NewCModule(ctx, module_name, js_directory_init))) {
    JS_AddModuleExport(ctx, m, "Directory");
    JS_AddModuleExportList(ctx, m, js_directory_static, countof(js_directory_static));

    /* if(!strcmp(module_name, "directory"))
       JS_AddModuleExport(ctx, m, "default");*/
  }

  return m;
}

/**
 * @}
 */
