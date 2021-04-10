#define _GNU_SOURCE

#include "utils.h"
#include "child-process.h"
#include "property-enumeration.h"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

extern char** environ;

VISIBLE JSClassID js_child_process_class_id = 0;
static JSValue child_process_proto, child_process_ctor;

ChildProcess*
child_process_new(JSContext* ctx) {
  return js_mallocz(ctx, sizeof(ChildProcess));
}

char**
child_process_environment(JSContext* ctx, JSValueConst object) {
  PropertyEnumeration propenum;
  Vector args;

  if(property_enumeration_init(&propenum, ctx, object, PROPENUM_DEFAULT_FLAGS))
    return 0;

  vector_init(&args, ctx);

  do {
    char* var;
    const char *name, *value;
    size_t namelen, valuelen;

    name = property_enumeration_keystrlen(&propenum, &namelen, ctx);
    value = property_enumeration_valuestrlen(&propenum, &valuelen, ctx);

    var = js_malloc(ctx, namelen + 1 + valuelen + 1);

    memcpy(var, name, namelen);
    var[namelen] = '=';
    memcpy(&var[namelen + 1], value, valuelen);
    var[namelen + 1 + valuelen] = '\0';

    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, value);

    vector_push(&args, var);

  } while(property_enumeration_next(&propenum));

  vector_emplace(&args, sizeof(char*));
  return (char**)args.data;
}

JSValue
js_child_process_wrap(JSContext* ctx, ChildProcess* cp) {
  JSValue obj;

  obj = JS_NewObjectProtoClass(ctx, child_process_proto, js_child_process_class_id);
  JS_SetOpaque(obj, cp);
  return obj;
}

static JSValue
js_child_process_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  ChildProcess* cp;
  JSValue obj = JS_UNDEFINED, proto = JS_UNDEFINED;

  if(!(cp = js_mallocz(ctx, sizeof(ChildProcess))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_child_process_class_id);
  js_value_free(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, cp);

  return obj;
fail:
  js_free(ctx, cp);
  js_value_free(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_child_process_finalizer(JSRuntime* rt, JSValue val) {
  ChildProcess* cp = JS_GetOpaque(val, js_child_process_class_id);
  if(cp) {
    // js_free_rt(rt, cp);
  }
}

static JSValue
js_child_process_exec(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;

  return ret;
}
static JSValue
js_child_process_spawn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;
  ChildProcess* cp = child_process_new(ctx);

  cp->file = js_tostring(ctx, argv[0]);

  if(argc > 1) {
    cp->args = js_array_to_argv(ctx, 0, argv[1]);
  } else {
    cp->args = js_malloc(ctx, sizeof(char*) * 2);
    cp->args[0] = js_strdup(ctx, cp->file);
    cp->args[1] = 0;
  }

  if(argc > 2 && JS_IsObject(argv[2])) {
    JSValue env, stdio;
    size_t i, len;
    int *parent_fds, *child_fds;
    env = JS_GetPropertyStr(ctx, argv[2], "env");

    if(JS_IsObject(env)) {
      cp->env = child_process_environment(ctx, env);
    } else {
      cp->env = js_argv_dup(ctx, environ);
    }
    JS_FreeValue(ctx, env);

    stdio = JS_GetPropertyStr(ctx, argv[2], "stdio");
    if(JS_IsException(stdio) || JS_IsUndefined(stdio))
      stdio = JS_NewString(ctx, "pipe");

    if(!JS_IsArray(ctx, stdio)) {
      JSValue a = JS_NewArray(ctx);
      JS_SetPropertyUint32(ctx, a, 0, JS_DupValue(ctx, stdio));
      JS_SetPropertyUint32(ctx, a, 1, JS_DupValue(ctx, stdio));
      JS_SetPropertyUint32(ctx, a, 2, JS_DupValue(ctx, stdio));
      JS_FreeValue(ctx, stdio);
      stdio = a;
    }

    len = js_array_length(ctx, stdio);
    parent_fds = js_malloc(ctx, sizeof(int) * len);
    child_fds = js_malloc(ctx, sizeof(int) * len);

    for(i = 0; i < len; i++) {
      JSValue item = JS_GetPropertyUint32(ctx, stdio, i);
      parent_fds[i] = -1;
      child_fds[i] = -1;

      if(JS_IsNumber(item)) {
        int32_t fd;
        JS_ToInt32(ctx, &fd, item);

        child_fds[i] = fd;
      } else if(JS_IsString(item)) {
        const char* s = js_get_propertyint_cstring(ctx, stdio, i);

        if(!strcmp(s, "pipe")) {
          int fds[2];

          if(pipe(fds) == -1)
            fds[0] = fds[1] = -1;

          if(i == 0) {
            child_fds[i] = fds[0];
            parent_fds[i] = fds[1];
          } else {
            child_fds[i] = fds[1];
            parent_fds[i] = fds[0];
          }

        } else if(!strcmp(s, "inherit")) {
          child_fds[i] = i;
        }

        JS_FreeCString(ctx, s);
      }
    }
    JS_FreeValue(ctx, stdio);
  }

  return ret;
}

static JSClassDef js_child_process_class = {
    .class_name = "ChildProcess",
    .finalizer = js_child_process_finalizer,
};

static const JSCFunctionListEntry js_child_process_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ChildProcess", JS_PROP_C_W_E),

};

static const JSCFunctionListEntry js_child_process_funcs[] = {
    JS_CFUNC_DEF("exec", 1, js_child_process_exec),
    JS_CFUNC_DEF("spawn", 1, js_child_process_spawn),

};

static int
js_child_process_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_child_process_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_child_process_class_id, &js_child_process_class);

  child_process_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx,
                             child_process_proto,
                             js_child_process_proto_funcs,
                             countof(js_child_process_proto_funcs));
  JS_SetClassProto(ctx, js_child_process_class_id, child_process_proto);

  child_process_ctor = JS_NewCFunction2(ctx, js_child_process_constructor, "ChildProcess", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, child_process_ctor, child_process_proto);
  JS_SetPropertyFunctionList(ctx, child_process_ctor, js_child_process_funcs, countof(js_child_process_funcs));

  if(m) {
    JS_SetModuleExportList(ctx, m, js_child_process_funcs, countof(js_child_process_funcs));
    JS_SetModuleExport(ctx, m, "ChildProcess", child_process_ctor);
    JS_SetModuleExport(ctx, m, "default", child_process_ctor);
  }
  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_child_process
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_child_process_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_child_process_funcs, countof(js_child_process_funcs));
  JS_AddModuleExport(ctx, m, "ChildProcess");
  JS_AddModuleExport(ctx, m, "default");
  return m;
}
