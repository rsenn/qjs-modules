#include "quickjs-location.h"
#include "utils.h"
#include "buffer-utils.h"

#define max(a, b) ((a) > (b) ? (a) : (b))

thread_local VISIBLE JSClassID js_location_class_id = 0;
thread_local JSValue location_proto = {JS_TAG_UNDEFINED},
                     location_ctor = {JS_TAG_UNDEFINED};

enum {
  LOCATION_PROP_LINE,
  LOCATION_PROP_COLUMN,
  LOCATION_PROP_POS,
  LOCATION_PROP_FILE
};

Location*
js_location_data(JSContext* ctx, JSValueConst value) {
  Location* loc;
  loc = JS_GetOpaque(value, js_location_class_id);
  return loc;
}

JSValue
js_location_new_proto(JSContext* ctx,
                      JSValueConst proto,
                      const Location* location) {
  JSValue obj;
  Location* loc;

  if(!(loc = js_mallocz(ctx, sizeof(Location))))
    return JS_EXCEPTION;

  *loc = location_clone(location, ctx);
  if(js_location_class_id == 0)
    js_location_init(ctx, 0);
  if(js_is_nullish(ctx, proto))
    proto = location_proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_location_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, loc);

  return obj;
fail:
  js_free(ctx, loc);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_location_new(JSContext* ctx, const Location* location) {
  if(js_location_class_id == 0)
    js_location_init(ctx, 0);

  return js_location_new_proto(ctx, location_proto, location);
}

JSValue
js_location_tostring(JSContext* ctx,
                     JSValueConst this_val,
                     int argc,
                     JSValueConst argv[]) {
  Location* loc;
  JSValue ret = JS_UNDEFINED;
  size_t len;

  if(!(loc = js_location_data(ctx, this_val)))
    return ret;

  if(!loc->str) {
    len = loc->file ? strlen(loc->file) : 0;
    len += 46;
    loc->str = js_malloc(ctx, len);
    if(loc->file)
      snprintf(loc->str,
               len,
               "%s:%" PRIi32 ":%" PRIi32 "",
               loc->file,
               loc->line + 1,
               loc->column + 1);
    else
      snprintf(loc->str,
               len,
               "%" PRIi32 ":%" PRIi32 "",
               loc->line + 1,
               loc->column + 1);
  }
  ret = JS_NewString(ctx, loc->str);
  return ret;
}

BOOL
js_is_location(JSContext* ctx, JSValueConst obj) {
  BOOL ret;
  JSAtom line, column;
  line = JS_NewAtom(ctx, "line");
  column = JS_NewAtom(ctx, "column");
  ret = JS_IsObject(obj) && JS_HasProperty(ctx, obj, line) &&
        JS_HasProperty(ctx, obj, column);
  JS_FreeAtom(ctx, line);
  JS_FreeAtom(ctx, column);
  if(ret)
    return ret;
  line = JS_NewAtom(ctx, "lineNumber");
  column = JS_NewAtom(ctx, "columnNumber");
  ret = JS_IsObject(obj) && JS_HasProperty(ctx, obj, line) &&
        JS_HasProperty(ctx, obj, column);
  JS_FreeAtom(ctx, line);
  JS_FreeAtom(ctx, column);
  return ret;
}

static JSValue
js_location_getter(JSContext* ctx, JSValueConst this_val, int magic) {
  Location* loc;
  JSValue ret = JS_UNDEFINED;

  if(!(loc = js_location_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case LOCATION_PROP_LINE: {
      if(loc->line != UINT32_MAX)
        ret = JS_NewUint32(ctx, loc->line + 1);
      break;
    }
    case LOCATION_PROP_COLUMN: {
      if(loc->column != UINT32_MAX)
        ret = JS_NewUint32(ctx, loc->column + 1);
      break;
    }
    case LOCATION_PROP_POS: {
      if(loc->pos >= 0 && loc->pos <= INT64_MAX)
        ret = JS_NewInt64(ctx, loc->pos);
      break;
    }
    case LOCATION_PROP_FILE: {
      if(loc->file)
        ret = JS_NewString(ctx, loc->file);
      break;
    }
  }
  return ret;
}

static JSValue
js_location_setter(JSContext* ctx,
                   JSValueConst this_val,
                   JSValueConst value,
                   int magic) {
  Location* loc;
  JSValue ret = JS_UNDEFINED;

  if(!(loc = js_location_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case LOCATION_PROP_FILE: {
      if(loc->file)
        js_free(ctx, loc->file);

      loc->file = js_tostring(ctx, value);
      break;
    }
  }
  return ret;
}

Location
js_location_from(JSContext* ctx, JSValueConst this_val) {
  Location loc = {0, 0, 0, -1, 0};
  if(js_has_propertystr(ctx, this_val, "line"))
    loc.line = js_get_propertystr_int32(ctx, this_val, "line") - 1;
  if(js_has_propertystr(ctx, this_val, "lineNumber"))
    loc.line = js_get_propertystr_int32(ctx, this_val, "lineNumber") - 1;
  if(js_has_propertystr(ctx, this_val, "column"))
    loc.column = js_get_propertystr_int32(ctx, this_val, "column") - 1;
  if(js_has_propertystr(ctx, this_val, "columnNumber"))
    loc.column = js_get_propertystr_int32(ctx, this_val, "columnNumber") - 1;
  if(js_has_propertystr(ctx, this_val, "file"))
    loc.file = js_get_propertystr_string(ctx, this_val, "file");
  if(js_has_propertystr(ctx, this_val, "fileName"))
    loc.file = js_get_propertystr_string(ctx, this_val, "fileName");
  if(js_has_propertystr(ctx, this_val, "pos"))
    loc.pos = js_get_propertystr_uint64(ctx, this_val, "pos");
  return loc;
}

JSValue
js_location_toprimitive(JSContext* ctx,
                        JSValueConst this_val,
                        int argc,
                        JSValueConst argv[]) {
  Location* loc;
  const char* hint;
  JSValue ret;

  if(!(loc = js_location_data(ctx, this_val)))
    return JS_EXCEPTION;

  hint = argc > 0 ? JS_ToCString(ctx, argv[0]) : 0;
  if(hint && !strcmp(hint, "number"))
    ret = JS_NewInt64(ctx, loc->pos);
  else
    ret = js_location_tostring(ctx, this_val, argc, argv);
  if(hint)
    js_cstring_free(ctx, hint);
  return ret;
}

JSValue
js_location_constructor(JSContext* ctx,
                        JSValueConst new_target,
                        int argc,
                        JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  Location loc;
  location_init(&loc);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = location_proto;

  loc.pos = -1;

  /* From string */
  if(argc == 1 && js_is_input(ctx, argv[0])) {
    InputBuffer in = js_input_chars(ctx, argv[0]);
    const uint8_t *p, *begin = input_buffer_begin(&in),
                      *end = input_buffer_end(&in);
    unsigned long v, n[2];
    size_t ni = max(2, str_count(begin, ':'));

    while(end >= begin) {
      for(p = end; p > begin && *(p - 1) != ':'; p--) {}
      if(ni > 0) {
        v = strtoul((const char*)p, (char**)&end, 10);
        if(end > p)
          n[--ni] = v;
      } else {
        loc.file = js_strndup(ctx, (const char*)p, end - p);
        break;
      }
      end = p - 1;
    }
    if(ni == 0) {
      loc.line = n[0];
      loc.column = n[1];
    }
    loc.line--;
    loc.column--;
    /* Dup from object */
  } else if(argc >= 1 && JS_IsObject(argv[0])) {
    Location* other;
    if((other = js_location_data(ctx, argv[0]))) {
      loc = location_clone(other, ctx);
    } else {
      loc = js_location_from(ctx, argv[0]);
    }
    /* From arguments (line,column,pos,file) */
  } else if(argc > 1) {
    int i = 0;
    if(i < argc && JS_IsString(argv[i]))
      loc.file = js_tostring(ctx, argv[i++]);
    if(i < argc && JS_IsNumber(argv[i]))
      JS_ToUint32(ctx, &loc.line, argv[i++]);
    if(i < argc && JS_IsNumber(argv[i]))
      JS_ToUint32(ctx, &loc.column, argv[i++]);
    if(i < argc && JS_IsNumber(argv[i]))
      JS_ToIndex(ctx, (uint64_t*)&loc.pos, argv[i++]);
    if(loc.file == 0 && i < argc && JS_IsString(argv[i]))
      loc.file = js_tostring(ctx, argv[i++]);

    loc.line--;
    loc.column--;
  }
  return js_location_new_proto(ctx, proto, &loc);

fail:
  location_free(&loc, ctx);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_location_inspect(JSContext* ctx,
                    JSValueConst this_val,
                    int argc,
                    JSValueConst argv[]) {
  Location* loc;

  if(!(loc = js_location_data(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectProto(ctx, location_proto);
  if(loc->line < UINT32_MAX)
    JS_DefinePropertyValueStr(
        ctx, obj, "line", JS_NewUint32(ctx, loc->line + 1), JS_PROP_ENUMERABLE);
  if(loc->column < UINT32_MAX)
    JS_DefinePropertyValueStr(ctx,
                              obj,
                              "column",
                              JS_NewUint32(ctx, loc->column + 1),
                              JS_PROP_ENUMERABLE);
  if(loc->pos >= 0 && loc->pos <= INT64_MAX)
    JS_DefinePropertyValueStr(
        ctx, obj, "pos", JS_NewInt64(ctx, loc->pos), JS_PROP_ENUMERABLE);
  if(loc->file)
    JS_DefinePropertyValueStr(
        ctx, obj, "file", JS_NewString(ctx, loc->file), JS_PROP_ENUMERABLE);
  if(loc->str)
    JS_DefinePropertyValueStr(
        ctx, obj, "str", JS_NewString(ctx, loc->str), JS_PROP_ENUMERABLE);
  return obj;
}

static JSValue
js_location_clone(JSContext* ctx,
                  JSValueConst this_val,
                  int argc,
                  JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  Location* loc;

  if(!(loc = js_location_data(ctx, this_val)))
    return JS_EXCEPTION;

  return js_location_new_proto(ctx, JS_GetPrototype(ctx, this_val), loc);
}

static JSValue
js_location_count(JSContext* ctx,
                  JSValueConst this_val,
                  int argc,
                  JSValueConst argv[]) {
  Location loc;
  InputBuffer input;
  size_t i;
  int64_t limit = -1;
  location_init(&loc);

  if(argc >= 2)
    JS_ToInt64(ctx, &limit, argv[1]);

  input = js_input_chars(ctx, argv[0]);
  if(limit == -1 || (size_t)limit > input.size)
    limit = input.size;
  location_count(&loc, (const char*)input.data, limit);
  return js_location_new_proto(ctx, location_proto, &loc);
}

void
js_location_finalizer(JSRuntime* rt, JSValue val) {
  Location* loc = JS_GetOpaque(val, js_location_class_id);
  if(loc) {
    location_free_rt(loc, rt);
  }
  JS_FreeValueRT(rt, val);
}

static JSClassDef js_location_class = {
    .class_name = "Location",
    .finalizer = js_location_finalizer,
};

static const JSCFunctionListEntry js_location_funcs[] = {
    JS_CGETSET_MAGIC_DEF("line", js_location_getter, 0, LOCATION_PROP_LINE),
    JS_CGETSET_MAGIC_DEF("column", js_location_getter, 0, LOCATION_PROP_COLUMN),
    JS_CGETSET_MAGIC_DEF("pos", js_location_getter, 0, LOCATION_PROP_POS),
    JS_CGETSET_MAGIC_DEF(
        "file", js_location_getter, js_location_setter, LOCATION_PROP_FILE),
    JS_CFUNC_DEF("[Symbol.toPrimitive]", 0, js_location_toprimitive),
    JS_CFUNC_DEF("clone", 0, js_location_clone),
    JS_CFUNC_DEF("toString", 0, js_location_tostring),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]",
                       "Location",
                       JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_location_static_funcs[] = {
    JS_CFUNC_DEF("count", 1, js_location_count),
};

int
js_location_init(JSContext* ctx, JSModuleDef* m) {

  if(js_location_class_id == 0) {
    JS_NewClassID(&js_location_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_location_class_id, &js_location_class);

    location_ctor = JS_NewCFunction2(
        ctx, js_location_constructor, "Location", 1, JS_CFUNC_constructor, 0);
    location_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx,
                               location_proto,
                               js_location_funcs,
                               countof(js_location_funcs));
    JS_SetPropertyFunctionList(ctx,
                               location_ctor,
                               js_location_static_funcs,
                               countof(js_location_static_funcs));
    JS_SetClassProto(ctx, js_location_class_id, location_proto);

    js_set_inspect_method(ctx, location_proto, js_location_inspect);
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
  if(!(m = JS_NewCModule(ctx, module_name, &js_location_init)))
    return m;
  JS_AddModuleExport(ctx, m, "Location");

  if(!strcmp(module_name, "location"))
    JS_AddModuleExport(ctx, m, "default");
  return m;
}
