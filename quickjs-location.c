#include "defines.h"
#include "quickjs-location.h"
#include "utils.h"
#include "buffer-utils.h"
#include "debug.h"

/**
 * \addtogroup quickjs-location
 * @{
 */

VISIBLE JSClassID js_location_class_id = 0;
static JSValue location_proto, location_ctor;

enum {
  PROP_LINE,
  PROP_COLUMN,
  PROP_FILE,
  PROP_CHAROFFSET,
  PROP_BYTEOFFSET,
};

static JSValue
js_location_create(JSContext* ctx, JSValueConst proto, Location* loc) {
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_location_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, loc);
  return obj;

fail:
  location_free(loc, JS_GetRuntime(ctx));
  JS_FreeValue(ctx, obj);

  return JS_EXCEPTION;
}

JSValue
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

static BOOL
js_is_location(JSContext* ctx, JSValueConst obj) {
  JSAtom line = JS_NewAtom(ctx, "line");
  JSAtom column = JS_NewAtom(ctx, "column");
  BOOL ret = JS_IsObject(obj) && JS_HasProperty(ctx, obj, line) && JS_HasProperty(ctx, obj, column);

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
    case PROP_FILE: {
      /* loc->file/loc->filename are a union; has_filename says which is live */
      if(loc->has_filename) {
        char* file;

        if((file = location_file(loc, ctx))) {
          ret = JS_NewString(ctx, file);
          js_free(ctx, file);
        }
      } else if(loc->file > -1) {
        ret = JS_AtomToValue(ctx, loc->file);
      }

      break;
    }

    case PROP_LINE: {
      if(loc->line != -1)
        ret = JS_NewUint32(ctx, loc->line + 1);

      break;
    }

    case PROP_COLUMN: {
      if(loc->column != -1)
        ret = JS_NewUint32(ctx, loc->column + 1);

      break;
    }

    case PROP_CHAROFFSET: {
      if(loc->char_offset >= 0)
        ret = JS_NewInt64(ctx, loc->char_offset);

      break;
    }

    case PROP_BYTEOFFSET: {
      if(loc->byte_offset >= 0)
        ret = JS_NewInt64(ctx, loc->byte_offset);

      break;
    }
  }

  return ret;
}

static JSValue
js_location_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Location *loc, *other = js_location_data(value);
  JSValue ret = JS_UNDEFINED;

  if(!(loc = js_location_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(loc->read_only)
    return JS_ThrowTypeError(ctx, "Location is read-only");

  switch(magic) {
    case PROP_FILE: {
      if(other)
        location_copy_file(loc, other, ctx);
      else {
        JSAtom atom = JS_ValueToAtom(ctx, value);

        if(atom == JS_ATOM_NULL)
          return JS_EXCEPTION;

        location_set_file(loc, atom, ctx);
        JS_FreeAtom(ctx, atom);
      }

      break;
    }

    case PROP_LINE: {
      loc->line = other ? other->line : js_toint32(ctx, value);
      break;
    }

    case PROP_COLUMN: {
      loc->column = other ? other->column : js_toint32(ctx, value);
      break;
    }

    case PROP_CHAROFFSET: {
      loc->char_offset = other ? other->char_offset : js_toint64(ctx, value);
      break;
    }

    case PROP_BYTEOFFSET: {
      loc->byte_offset = other ? other->byte_offset : js_toint64(ctx, value);
      break;
    }
  }

  return ret;
}

void
js_location_from2(JSContext* ctx, JSValueConst this_val, Location* loc) {
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
    loc->char_offset = js_get_propertystr_int64(ctx, this_val, "charOffset");

  if(js_has_propertystr(ctx, this_val, "byteOffset"))
    loc->byte_offset = js_get_propertystr_int64(ctx, this_val, "byteOffset");
}

Location*
js_location_from(JSContext* ctx, JSValueConst this_val) {
  Location* loc;

  if((loc = js_location_data(this_val)))
    return location_dup(loc);

  if((loc = location_new(ctx)))
    js_location_from2(ctx, this_val, loc);

  return loc;
}

Location*
js_location_copy(JSContext* ctx, JSValueConst this_val) {
  Location *loc, *other;

  if(!(loc = location_new(ctx)))
    return 0;

  if((other = js_location_data(this_val)))
    location_copy(loc, other, ctx);
  else
    js_location_from2(ctx, this_val, loc);

  return loc;
}

static JSValue
js_location_toprimitive(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Location* loc;
  const char* hint;
  JSValue ret;

  if(!(loc = js_location_data2(ctx, this_val)))
    return JS_EXCEPTION;

  hint = argc > 0 ? JS_ToCString(ctx, argv[0]) : 0;

  if(hint && !strcmp(hint, "number"))
    ret = JS_NewInt64(ctx, loc->char_offset);
  else
    ret = js_location_tostring(ctx, loc);

  if(hint)
    JS_FreeCString(ctx, hint);

  return ret;
}

static JSValue
js_location_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  Location* loc;

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;

  /* Dup from object */
  if(argc >= 1 && JS_IsObject(argv[0])) {
    loc = js_location_from(ctx, argv[0]);
  } else {
    loc = location_new(ctx);

    InputBuffer in = {0};

    if(argc == 1)
      in = js_input_chars(ctx, argv[0]);

    /* From string */
    if(in.data && JS_IsString(argv[0])) {
      const uint8_t *p, *begin = inputbuffer_begin(&in), *end = inputbuffer_end(&in);
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
    } else if(argc >= 1) {
      int i = 0;

      loc->file = 0;

      if(i < argc && !JS_IsNumber(argv[i])) {
        loc->file = JS_IsString(argv[i]) ? (int32_t)JS_ValueToAtom(ctx, argv[i]) : -1;
        ++i;
      }

      if(i < argc && JS_IsNumber(argv[i]))
        loc->line = js_toint32(ctx, argv[i++]);

      if(i < argc && JS_IsNumber(argv[i]))
        loc->column = js_toint32(ctx, argv[i++]);

      if(i < argc && JS_IsNumber(argv[i]))
        loc->char_offset = js_toint64(ctx, argv[i++]);

      if(i < argc && loc->file == 0 && !JS_IsNumber(argv[i]))
        loc->file = JS_ValueToAtom(ctx, argv[i++]);

      if(loc->file == 0)
        loc->file = -1;

      loc->line--;
      loc->column--;
    }

    inputbuffer_free(&in, ctx);
  }

  obj = js_location_create(ctx, proto, loc);
  JS_FreeValue(ctx, proto);
  return obj;
}

enum {
  METHOD_EQUAL = 0,
  METHOD_CLONE,
  METHOD_COPY,
  METHOD_NEXTCHAR,
  METHOD_TOSTRING,
};

static JSValue
js_location_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Location* loc;
  JSValue ret = JS_UNDEFINED;

  if(!(loc = js_location_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_EQUAL: {
      Location* other;

      if(!(other = js_location_data2(ctx, argv[0])))
        return JS_EXCEPTION;

      ret = JS_NewBool(ctx, location_equal(loc, other));
      break;
    }

    case METHOD_CLONE: {
      ret = js_location_wrap(ctx, location_clone(loc, ctx));
      break;
    }

    case METHOD_COPY: {
      Location* other;

      if(!(other = js_location_data2(ctx, argv[0])))
        return JS_EXCEPTION;

      location_copy(loc, other, ctx);
      break;
    }

    case METHOD_NEXTCHAR: {
      int32_t code = -1;

      if(JS_IsNumber(argv[0])) {
        code = js_toint32(ctx, argv[0]);
      } else {
        InputBuffer buf = js_input_args(ctx, argc, argv);

        if(buf.size) {
          const uint8_t *end, *pos = (uint8_t*)inputbuffer_data(&buf);

          code = unicode_from_utf8(pos, inputbuffer_length(&buf), &end);
        }

        inputbuffer_free(&buf, ctx);
      }

      if(code != -1)
        ret = JS_NewUint32(ctx, location_nextchar(loc, code));
      else
        ret = JS_ThrowTypeError(ctx, "argument 1 must be Number | ArrayBuffer | TypedArray | string");

      break;
    }

    case METHOD_TOSTRING: {
      ret = js_location_tostring(ctx, loc);
      break;
    }
  }

  return ret;
}

static JSValue
js_location_count(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Location* loc = 0;
  InputBuffer input = js_input_args(ctx, argc, argv);

  if(!(loc = location_new(ctx)))
    return JS_EXCEPTION;

  location_zero(loc);
  location_count(loc, inputbuffer_data(&input), inputbuffer_length(&input));

  inputbuffer_free(&input, ctx);

  return js_location_create(ctx, location_proto, loc);
}

static void
js_location_finalizer(JSRuntime* rt, JSValue val) {
  Location* loc;

  if((loc = js_location_data(val)))
    if(loc != (void*)-1ll)
      location_free(loc, rt);
}

static JSClassDef js_location_class = {
    .class_name = "Location",
    .finalizer = js_location_finalizer,
};

static const JSCFunctionListEntry js_location_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("line", js_location_get, 0, PROP_LINE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("column", js_location_get, 0, PROP_COLUMN, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("charOffset", js_location_get, 0, PROP_CHAROFFSET, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("byteOffset", js_location_get, 0, PROP_BYTEOFFSET, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("file", js_location_get, js_location_set, PROP_FILE, JS_PROP_ENUMERABLE),
    JS_CFUNC_MAGIC_DEF("equal", 1, js_location_method, METHOD_EQUAL),
    JS_CFUNC_MAGIC_DEF("clone", 0, js_location_method, METHOD_CLONE),
    JS_CFUNC_MAGIC_DEF("copy", 1, js_location_method, METHOD_COPY),
    JS_CFUNC_MAGIC_DEF("nextChar", 1, js_location_method, METHOD_NEXTCHAR),
    JS_CFUNC_MAGIC_DEF("toString", 0, js_location_method, METHOD_TOSTRING),
    JS_CFUNC_DEF("[Symbol.toPrimitive]", 0, js_location_toprimitive),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Location", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_location_static_funcs[] = {
    JS_CFUNC_DEF("count", 1, js_location_count),
};

int
js_location_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_location_class_id);

  JS_NewClass(JS_GetRuntime(ctx), js_location_class_id, &js_location_class);

  location_ctor = JS_NewCFunction2(ctx, js_location_constructor, "Location", 1, JS_CFUNC_constructor, 0);
  location_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, location_proto, js_location_funcs, countof(js_location_funcs));
  JS_SetPropertyFunctionList(ctx, location_ctor, js_location_static_funcs, countof(js_location_static_funcs));
  JS_SetClassProto(ctx, js_location_class_id, location_proto);
  JS_SetConstructor(ctx, location_ctor, location_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "Location", location_ctor);

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_location
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_location_init)))
    JS_AddModuleExport(ctx, m, "Location");

  return m;
}

/**
 * @}
 */
