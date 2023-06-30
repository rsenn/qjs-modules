#include "defines.h"
#include "quickjs-location.h"
#include "utils.h"
#include "buffer-utils.h"
#include "debug.h"

/**
 * \addtogroup quickjs-location
 * @{
 */

thread_local VISIBLE JSClassID js_location_class_id = 0;
thread_local JSValue location_proto = {{0}, JS_TAG_UNDEFINED}, location_ctor = {{0}, JS_TAG_UNDEFINED};

enum {
  LOCATION_PROP_LINE,
  LOCATION_PROP_COLUMN,
  LOCATION_PROP_FILE,
  LOCATION_PROP_CHAROFFSET,
  LOCATION_PROP_BYTEOFFSET,
};

static JSValue
js_location_create(JSContext* ctx, JSValueConst proto, Location* loc) {
  JSValue obj = JS_UNDEFINED;

  if(js_location_class_id == 0)
    js_location_init(ctx, 0);

  if(!JS_IsObject(proto))
    proto = location_proto;

  assert(JS_VALUE_GET_OBJ(location_proto) == JS_VALUE_GET_OBJ(proto));

  obj = JS_NewObjectProtoClass(ctx, proto, js_location_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, loc);

  return obj;

fail:
  location_free(loc, JS_GetRuntime(ctx));
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

VISIBLE JSValue
js_location_wrap(JSContext* ctx, Location* loc) {
  return js_location_create(ctx, location_proto, location_dup(loc));
}

static JSValue
js_location_tostring(JSContext* ctx, const Location* loc) {
  JSValue ret = JS_EXCEPTION;
  char* str;

  if((str = location_tostring(loc, ctx))) {
    ret = JS_NewString(ctx, str);
    js_free(ctx, str);
  }

  return ret;
}

VISIBLE BOOL
js_is_location(JSContext* ctx, JSValueConst obj) {
  BOOL ret;
  JSAtom line, column;
  line = JS_NewAtom(ctx, "line");
  column = JS_NewAtom(ctx, "column");
  ret = JS_IsObject(obj) && JS_HasProperty(ctx, obj, line) && JS_HasProperty(ctx, obj, column);
  JS_FreeAtom(ctx, line);
  JS_FreeAtom(ctx, column);
  if(ret)
    return ret;
  line = JS_NewAtom(ctx, "lineNumber");
  column = JS_NewAtom(ctx, "columnNumber");
  ret = JS_IsObject(obj) && JS_HasProperty(ctx, obj, line) && JS_HasProperty(ctx, obj, column);
  JS_FreeAtom(ctx, line);
  JS_FreeAtom(ctx, column);
  return ret;
}

static JSValue
js_location_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Location* loc;
  JSValue ret = JS_UNDEFINED;

  if(!(loc = js_location_data(this_val)))
    return JS_UNDEFINED;

  if(!(loc = js_location_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LOCATION_PROP_FILE: {
      if(loc->file > -1)
        ret = JS_AtomToValue(ctx, loc->file);
      break;
    }
    case LOCATION_PROP_LINE: {
      if(loc->line != -1)
        ret = JS_NewUint32(ctx, loc->line + 1);
      break;
    }
    case LOCATION_PROP_COLUMN: {
      if(loc->column != -1)
        ret = JS_NewUint32(ctx, loc->column + 1);
      break;
    }
    case LOCATION_PROP_CHAROFFSET: {
      if(loc->char_offset >= 0)
        ret = JS_NewInt64(ctx, loc->char_offset);
      break;
    }
    case LOCATION_PROP_BYTEOFFSET: {
      if(loc->byte_offset >= 0)
        ret = JS_NewInt64(ctx, loc->byte_offset);
      break;
    }
  }
  return ret;
}

static JSValue
js_location_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Location* loc;
  JSValue ret = JS_UNDEFINED;

  if(!(loc = js_location_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(loc->read_only)
    return JS_ThrowTypeError(ctx, "Location is read-only");

  if(loc->str)
    loc->str[0] = 0;

  switch(magic) {
    case LOCATION_PROP_FILE: {
      if(loc->file > -1)
        JS_FreeAtom(ctx, loc->file);
      loc->file = JS_ValueToAtom(ctx, value);
      break;
    }
    case LOCATION_PROP_LINE: {
      uint32_t n = 0;
      JS_ToUint32(ctx, &n, value);
      loc->line = n > 0 ? n - 1 : -1;
      break;
    }
    case LOCATION_PROP_COLUMN: {
      uint32_t n = 0;
      JS_ToUint32(ctx, &n, value);
      loc->column = n > 0 ? n - 1 : -1;
      break;
    }
    case LOCATION_PROP_CHAROFFSET: {
      int64_t n = 0;
      JS_ToInt64(ctx, &n, value);
      loc->char_offset = n >= 0 ? n : -1;
      break;
    }
    case LOCATION_PROP_BYTEOFFSET: {
      int64_t n = 0;
      JS_ToInt64(ctx, &n, value);
      loc->byte_offset = n >= 0 ? n : -1;
      break;
    }
  }
  return ret;
}

VISIBLE Location*
js_location_from(JSContext* ctx, JSValueConst this_val) {
  Location* loc;

  if((loc = JS_GetOpaque(this_val, js_location_class_id)))
    return location_dup(loc);

  loc = location_new(ctx);

  if(js_has_propertystr(ctx, this_val, "line"))
    loc->line = js_get_propertystr_int32(ctx, this_val, "line") - 1;
  else if(js_has_propertystr(ctx, this_val, "lineNumber"))
    loc->line = js_get_propertystr_int32(ctx, this_val, "lineNumber") - 1;
  if(js_has_propertystr(ctx, this_val, "column"))
    loc->column = js_get_propertystr_int32(ctx, this_val, "column") - 1;
  else if(js_has_propertystr(ctx, this_val, "columnNumber"))
    loc->column = js_get_propertystr_int32(ctx, this_val, "columnNumber") - 1;
  if(js_has_propertystr(ctx, this_val, "file"))
    loc->file = js_get_propertystr_atom(ctx, this_val, "file");
  else if(js_has_propertystr(ctx, this_val, "fileName"))
    loc->file = js_get_propertystr_atom(ctx, this_val, "fileName");
  if(js_has_propertystr(ctx, this_val, "charOffset"))
    loc->char_offset = js_get_propertystr_uint64(ctx, this_val, "charOffset");
  if(js_has_propertystr(ctx, this_val, "byteOffset"))
    loc->byte_offset = js_get_propertystr_uint64(ctx, this_val, "byteOffset");

  return loc;
}

JSValue
js_location_toprimitive(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Location* loc;
  const char* hint;
  JSValue ret;

  if(!(loc = js_location_data2(ctx, this_val)))
    return JS_EXCEPTION;

  hint = argc > 0 ? JS_ToCString(ctx, argv[0]) : 0;
  if(hint && !strcmp(hint, "number")) {
    ret = JS_NewInt64(ctx, loc->char_offset);
  } else {
    ret = js_location_tostring(ctx, loc);
  }

  if(hint)
    js_cstring_free(ctx, hint);
  return ret;
}

JSValue
js_location_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Location* loc = 0;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;

  /* Dup from object */
  if(argc >= 1 && JS_IsObject(argv[0])) {

    loc = js_location_from(ctx, argv[0]);

  } else {
    loc = location_new(ctx);

    /* From string */
    if(argc == 1 && js_is_input(ctx, argv[0])) {
      InputBuffer in = js_input_chars(ctx, argv[0]);
      const uint8_t *p, *begin = input_buffer_begin(&in), *end = input_buffer_end(&in);
      unsigned long v, n[2];
      size_t ni = MAX_NUM(2, str_count((const char*)begin, ':'));

      while(end >= begin) {
        for(p = end; p > begin && *(p - 1) != ':'; p--) {
        }
        if(ni > 0) {
          v = strtoul((const char*)p, (char**)&end, 10);
          if(end > p)
            n[--ni] = v;
        } else {
          loc->file = JS_NewAtomLen(ctx, (const char*)p, end - p);
          break;
        }
        end = p - 1;
      }
      if(ni == 0) {
        loc->line = n[0];
        loc->column = n[1];
      }
      loc->line--;
      loc->column--;
      /* From arguments (line,column,pos,file) */
    } else if(argc > 1) {
      int i = 0;

      loc->file = 0;

      if(i < argc && !JS_IsNumber(argv[i])) {
        loc->file = JS_IsString(argv[i]) ? JS_ValueToAtom(ctx, argv[i]) : -1;
        ++i;
      }

      if(i < argc && JS_IsNumber(argv[i]))
        JS_ToInt32(ctx, &loc->line, argv[i++]);
      if(i < argc && JS_IsNumber(argv[i]))
        JS_ToInt32(ctx, &loc->column, argv[i++]);
      if(i < argc && JS_IsNumber(argv[i]))
        JS_ToIndex(ctx, (uint64_t*)&loc->char_offset, argv[i++]);
      if(i < argc && JS_IsNumber(argv[i]))
        JS_ToIndex(ctx, (uint64_t*)&loc->byte_offset, argv[i++]);

      if(loc->file == 0 && i < argc)
        loc->file = JS_ValueToAtom(ctx, argv[i++]);

      if(loc->file == 0)
        loc->file = -1;

      loc->line--;
      loc->column--;
    }
  }

  obj = js_location_create(ctx, proto, loc);
  JS_FreeValue(ctx, proto);

  return obj;
}

enum {
  LOCATION_EQUAL = 0,
  LOCATION_TOSTRING,
};

static JSValue
js_location_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Location* loc;
  JSValue ret = JS_UNDEFINED;

  if(!(loc = js_location_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LOCATION_EQUAL: {
      Location* other;

      if(!(other = js_location_data2(ctx, argv[0])))
        return JS_EXCEPTION;

      ret = JS_NewBool(ctx, location_equal(loc, other));
      break;
    }
    case LOCATION_TOSTRING: {
      ret = js_location_tostring(ctx, loc);
      break;
    }
  }
  return ret;
}

static JSValue
js_location_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Location* loc;

  if(!(loc = js_location_data2(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectClass(ctx, js_location_class_id);

  if(loc->line != -1)
    JS_DefinePropertyValueStr(ctx, obj, "line", JS_NewUint32(ctx, loc->line + 1), JS_PROP_ENUMERABLE);
  if(loc->column != -1)
    JS_DefinePropertyValueStr(ctx, obj, "column", JS_NewUint32(ctx, loc->column + 1), JS_PROP_ENUMERABLE);
  if(loc->char_offset >= 0 && loc->char_offset <= INT64_MAX)
    JS_DefinePropertyValueStr(ctx, obj, "charOffset", JS_NewInt64(ctx, loc->char_offset), JS_PROP_ENUMERABLE);
  if(loc->byte_offset >= 0 && loc->byte_offset <= INT64_MAX)
    JS_DefinePropertyValueStr(ctx, obj, "byteOffset", JS_NewInt64(ctx, loc->byte_offset), JS_PROP_ENUMERABLE);

  if(loc->file > -1)
    JS_DefinePropertyValueStr(ctx, obj, "file", JS_AtomToValue(ctx, loc->file), JS_PROP_ENUMERABLE);

  /*  if(loc->str)
      JS_DefinePropertyValueStr(ctx, obj, "str", JS_NewString(ctx, loc->str), JS_PROP_ENUMERABLE);*/
  return obj;
}

static JSValue
js_location_clone(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  Location *loc, *other;

  if(!(other = js_location_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(loc = location_clone(other, ctx)))
    return JS_ThrowOutOfMemory(ctx);

  return js_location_create(ctx, location_proto, loc);
}

static JSValue
js_location_count(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Location* loc = 0;
  InputBuffer input;
  size_t i;
  int64_t limit = -1;

  if(!(loc = location_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  if(argc >= 2)
    JS_ToInt64(ctx, &limit, argv[1]);

  input = js_input_chars(ctx, argv[0]);
  if(limit == -1 || (size_t)limit > input.size)
    limit = input.size;

  location_zero(loc);
  location_count(loc, (const void*)input.data, limit);

  return js_location_wrap(ctx, loc);
}

void
js_location_finalizer(JSRuntime* rt, JSValue val) {
  Location* loc;

  if((loc = js_location_data(val))) {
    if(loc != (void*)-1ll)
      location_free(loc, rt);
  }
}

static JSClassDef js_location_class = {
    .class_name = "Location",
    .finalizer = js_location_finalizer,
};

static const JSCFunctionListEntry js_location_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("line", js_location_get, js_location_set, LOCATION_PROP_LINE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("column", js_location_get, js_location_set, LOCATION_PROP_COLUMN, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("charOffset", js_location_get, js_location_set, LOCATION_PROP_CHAROFFSET),
    JS_CGETSET_MAGIC_DEF("byteOffset", js_location_get, js_location_set, LOCATION_PROP_BYTEOFFSET),
    JS_CGETSET_MAGIC_FLAGS_DEF("file", js_location_get, js_location_set, LOCATION_PROP_FILE, JS_PROP_ENUMERABLE),
    JS_ALIAS_DEF("pos", "charOffset"),
    JS_CFUNC_MAGIC_DEF("equal", 1, js_location_methods, LOCATION_EQUAL),
    JS_CFUNC_DEF("[Symbol.toPrimitive]", 0, js_location_toprimitive),
    JS_CFUNC_DEF("clone", 0, js_location_clone),
    JS_CFUNC_MAGIC_DEF("toString", 0, js_location_methods, LOCATION_TOSTRING),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Location", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_location_static_funcs[] = {
    JS_CFUNC_DEF("count", 1, js_location_count),
};

VISIBLE int
js_location_init(JSContext* ctx, JSModuleDef* m) {

  if(js_location_class_id == 0) {
    JS_NewClassID(&js_location_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_location_class_id, &js_location_class);

    location_ctor = JS_NewCFunction2(ctx, js_location_constructor, "Location", 1, JS_CFUNC_constructor, 0);
    location_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, location_proto, js_location_funcs, countof(js_location_funcs));
    JS_SetPropertyFunctionList(ctx, location_ctor, js_location_static_funcs, countof(js_location_static_funcs));
    JS_SetClassProto(ctx, js_location_class_id, location_proto);

    /*js_set_inspect_method(ctx, location_proto, js_location_inspect);*/
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "Location", location_ctor);

    const char* module_name = JS_AtomToCString(ctx, m->module_name);

    if(!strcmp(module_name, "location"))
      JS_SetModuleExport(ctx, m, "default", location_ctor);

    JS_FreeCString(ctx, module_name);
  }

  return 0;
}

#ifdef JS_LOCATION_MODULE
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_location
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_location_init))) {
    JS_AddModuleExport(ctx, m, "Location");

    if(!strcmp(module_name, "location"))
      JS_AddModuleExport(ctx, m, "default");
  }

  return m;
}

/**
 * @}
 */
