#include "defines.h"
#include <cutils.h>
#include <libregexp.h>
#include "property-enumeration.h"
#include <quickjs.h>
#include <string.h>
#include "debug.h"
#include "buffer-utils.h"
#include "utils.h"

/**
 * \defgroup quickjs-tree-walker quickjs-arraybuffer_sink: Object tree walker
 * @{
 */
VISIBLE JSClassID js_arraybuffer_sink_class_id = 0;
static JSValue arraybuffer_sink_proto, arraybuffer_sink_ctor;

static JSValue
js_arraybuffer_sink_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  DynBuf* s;
  JSValue proto, obj = JS_UNDEFINED;
  int argi = 1;

  if(!(s = js_mallocz(ctx, sizeof(DynBuf))))
    return JS_EXCEPTION;

  dbuf_init_ctx(ctx, s);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_arraybuffer_sink_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, s);

  return obj;

fail:
  js_free(ctx, s);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_WRITE,
  METHOD_FLUSH,
  METHOD_END,
};

static void
js_arraybuffer_sink_free(JSRuntime* rt, void* opaque, void* ptr) {
  /* the DynBuf grows via js_realloc_rt */
  js_free_rt(rt, ptr);
}

static JSValue
js_arraybuffer_sink_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  DynBuf* s;
  JSValue ret = JS_UNDEFINED;

  if(!(s = JS_GetOpaque2(ctx, this_val, js_arraybuffer_sink_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_WRITE: {
      if(!s->realloc_func) {
        return JS_ThrowInternalError(ctx, "ArrayBufferSink has ended");
      }

      InputBuffer buf = js_input_args(ctx, argc, argv);

      if(buf.data && buf.size) {
        if(dbuf_put(s, inputbuffer_data(&buf), inputbuffer_length(&buf))) {
          inputbuffer_free(&buf, ctx);
          return JS_ThrowInternalError(ctx, "Unable to write to ArrayBufferSink");
        }

        ret = JS_NewInt32(ctx, inputbuffer_length(&buf));
      }

      inputbuffer_free(&buf, ctx);
      break;
    }
    case METHOD_FLUSH:
    case METHOD_END: {
      if(s->buf && s->size)
        ret = dbuf_toarraybuffer(s, ctx);
      /* ended: write() checks realloc_func; dbuf_free() skips a NULL buf */
      if(magic == METHOD_END)
        s->realloc_func = NULL;
      break;
    }
  }

  return ret;
}

enum {
  PROP_SIZE,
};

static JSValue
js_arraybuffer_sink_get(JSContext* ctx, JSValueConst this_val, int magic) {
  DynBuf* s;
  JSValue ret = JS_UNDEFINED;

  if(!(s = JS_GetOpaque2(ctx, this_val, js_arraybuffer_sink_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_SIZE: {
      ret = JS_NewUint32(ctx, s->size);
      break;
    }
  }

  return ret;
}

static void
js_arraybuffer_sink_finalizer(JSRuntime* rt, JSValue val) {
  DynBuf* s;

  if((s = JS_GetOpaque(val, js_arraybuffer_sink_class_id))) {
    dbuf_free(s);
    js_free_rt(rt, s);
  }
}

/* [Symbol.for('qjs.dynbuf')](): address of the sink's DynBuf, for C callers.
 *
 * ```js
 * sink[Symbol.for('qjs.dynbuf')](); // e.g. 94283746591232
 * ```
 *
 * stream-utils (writer_from_jsarraybuffersink) calls it once per Writer, then
 * writes in C. borrowed: the address is valid while `sink` is referenced.
 *
 *   returns number   the DynBuf* as an integer
 *   throws  TypeError if `this` is not an ArrayBufferSink
 */
static JSValue
js_arraybuffer_sink_dynbuf(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  DynBuf* s;

  if(!(s = JS_GetOpaque2(ctx, this_val, js_arraybuffer_sink_class_id)))
    return JS_EXCEPTION;

  return JS_NewInt64(ctx, (int64_t)(intptr_t)s);
}

static JSClassDef js_arraybuffer_sink_class = {
    .class_name = "ArrayBufferSink",
    .finalizer = js_arraybuffer_sink_finalizer,
};

static const JSCFunctionListEntry js_arraybuffer_sink_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("write", 1, js_arraybuffer_sink_method, METHOD_WRITE),
    JS_CFUNC_MAGIC_DEF("flush", 0, js_arraybuffer_sink_method, METHOD_FLUSH),
    JS_CFUNC_MAGIC_DEF("end", 0, js_arraybuffer_sink_method, METHOD_END),
    JS_CGETSET_MAGIC_DEF("size", js_arraybuffer_sink_get, 0, PROP_SIZE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ArrayBufferSink", JS_PROP_CONFIGURABLE),
};

static int
js_arraybuffer_sink_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_arraybuffer_sink_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_arraybuffer_sink_class_id, &js_arraybuffer_sink_class);

  arraybuffer_sink_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, arraybuffer_sink_proto, js_arraybuffer_sink_proto_funcs, countof(js_arraybuffer_sink_proto_funcs));
  JS_SetClassProto(ctx, js_arraybuffer_sink_class_id, arraybuffer_sink_proto);

  {
    JSAtom atom = js_symbol_for_atom(ctx, "qjs.dynbuf");
    JSValue fn = JS_NewCFunction(ctx, js_arraybuffer_sink_dynbuf, "dynbuf", 0);

    JS_DefinePropertyValue(ctx, arraybuffer_sink_proto, atom, fn, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE); // takes `fn`
    JS_FreeAtom(ctx, atom);
  }

  arraybuffer_sink_ctor = JS_NewCFunction2(ctx, js_arraybuffer_sink_constructor, "ArrayBufferSink", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, arraybuffer_sink_ctor, arraybuffer_sink_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "ArrayBufferSink", arraybuffer_sink_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_arraybuffer_sink
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_arraybuffer_sink_init))) {
    JS_AddModuleExport(ctx, m, "ArrayBufferSink");
  }

  return m;
}

/**
 * @}
 */
