#include "quickjs.h"
#include "cutils.h"
#include "vector.h"
#include <string.h>

JSClassID js_tree_walker_class_id;
JSValue tree_walker_proto, tree_walker_ctor, tree_walker_class;

enum tree_walker_methods {
  FIRST_CHILD = 0,
  LAST_CHILD,
  NEXT_NODE,
  NEXT_SIBLING,
  PARENT_NODE,
  PREVIOUS_NODE,
  PREVIOUS_SIBLING
};
enum tree_walker_getters { GET_ROOT = 0, GET_CURRENT, GET_LEVEL, GET_INDEX, GET_LENGTH };

typedef struct {
  JSValue object;
  JSPropertyEnum* properties;
  uint32_t nprops;
  uint32_t index;
  JSValue current;
} walker_frame;

typedef struct {
  vector frames;
} walker_context;

static walker_frame*
walker_frame_new(walker_context* wc) {
  walker_frame* fr;
  if((fr = vector_push(&wc->frames, sizeof(walker_frame)))) {
    fr->object = JS_UNDEFINED;
    fr->properties = 0;
    fr->nprops = 0;
    fr->index = 0;
    fr->current = JS_UNDEFINED;
  }
  return fr;
}

static int
walker_frame_init(walker_frame* fr, JSContext* ctx, JSValueConst object, int flags) {
  fr->object = object;
  fr->index = 0;
  if(JS_GetOwnPropertyNames(ctx, &fr->properties, &fr->nprops, object, flags)) {
    fr->nprops = 0;
    fr->properties = 0;
    return -1;
  }
  return 0;
}

static walker_frame*
walker_descend(walker_context* wc, JSContext* ctx) {
  walker_frame *parent, *new;

  parent = vector_back(&wc->frames, sizeof(walker_frame));
  new = walker_frame_new(wc);
  walker_frame_init(new,
                    ctx,
                    JS_DupValue(ctx, parent->current),
                    JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY);
  return new;
}

static JSPropertyEnum*
walker_frame_property(walker_frame* fr, int32_t index) {
  if(index < 0) {
    index %= (int32_t)fr->nprops;
    index += fr->nprops;
  } else {
    index %= (uint32_t)fr->nprops;
  }
  assert(index >= 0);
  assert(index < fr->nprops);
  return &fr->properties[(fr->index = index)];
}

static JSValue
walker_frame_current(walker_frame* fr, int32_t index, JSContext* ctx) {
  JSPropertyEnum* prop;
  if((prop = walker_frame_property(fr, index))) {
    fr->current = JS_GetProperty(ctx, fr->object, prop->atom);
    return fr->current;
  }
  return JS_EXCEPTION;
}

static JSValue
js_tree_walker_ctor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  walker_context* wc;
  walker_frame* fr = 0;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  if(!(wc = js_mallocz(ctx, sizeof(walker_context))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_tree_walker_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, wc);
  if(argc > 0 && JS_IsObject(argv[0])) {
    fr = walker_frame_new(wc);

    fr->current = JS_DupValue(ctx, argv[0]);
  }
  //    walker_frame_init(fr, ctx, argv[0], JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY);

  return obj;
fail:
  /*js_free(ctx, wc);
  JS_FreeValue(ctx, obj);*/
  return JS_EXCEPTION;
}

static JSValue
js_tree_walker_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  walker_context* wc;
  walker_frame* fr;

  if(!(wc = JS_GetOpaque(/*ctx, */ this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;
  fr = vector_back(&wc->frames, sizeof(walker_frame));

  switch(magic) {
    case FIRST_CHILD: {
      if(JS_IsObject(fr->current)) {
        fr = walker_descend(wc, ctx);
        return walker_frame_current(fr, 0, ctx);
      }
      break;
    }
    case LAST_CHILD: {
      if(JS_IsObject(fr->current)) {
        fr = walker_descend(wc, ctx);
        return walker_frame_current(fr, -1, ctx);
      }
      break;
    }
    case NEXT_NODE: {
      break;
    }
    case NEXT_SIBLING: {
      break;
    }
    case PARENT_NODE: {
      break;
    }
    case PREVIOUS_NODE: {
      break;
    }
    case PREVIOUS_SIBLING: {
      break;
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_tree_walker_get(JSContext* ctx, JSValueConst this_val, int magic) {
  walker_context* wc;
  walker_frame* fr;

  if(!(wc = JS_GetOpaque(this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case GET_ROOT: {
      if((fr = vector_front(&wc->frames, sizeof(walker_frame))))
        return JS_DupValue(ctx, fr->current);
      break;
    }
    case GET_CURRENT: {
      if((fr = vector_back(&wc->frames, sizeof(walker_frame))))
        return JS_DupValue(ctx, fr->current);
      break;
    }
    case GET_LEVEL: {
      return JS_NewUint32(ctx, vector_size(&wc->frames, sizeof(walker_frame)));
    }
    case GET_INDEX: {
      return JS_NewUint32(ctx, fr->index);
    }
    case GET_LENGTH: {
      return JS_NewUint32(ctx, fr->nprops);
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_tree_walker_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  return JS_UNDEFINED;
}

void
js_tree_walker_finalizer(JSRuntime* rt, JSValue val) {
  walker_context* wc = JS_GetOpaque(val, js_tree_walker_class_id);
  if(wc) {
    walker_frame* fr;

    uint32_t i, n = vector_size(&wc->frames, sizeof(walker_frame));

    for(i = 0; i < n; i++) {

      fr = vector_at(&wc->frames, sizeof(walker_frame), i);
      JS_FreeValueRT(rt, fr->current);
      JS_FreeValueRT(rt, fr->object);
    }

    js_free_rt(rt, wc);
  }
  // JS_FreeValueRT(rt, val);
}

JSClassDef js_tree_walker_class = {
    .class_name = "TreeWalker",
    .finalizer = js_tree_walker_finalizer,
};

static const JSCFunctionListEntry js_tree_walker_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("firstChild", 1, js_tree_walker_method, FIRST_CHILD),
    JS_CFUNC_MAGIC_DEF("lastChild", 1, js_tree_walker_method, LAST_CHILD),
    JS_CFUNC_MAGIC_DEF("nextNode", 1, js_tree_walker_method, NEXT_NODE),
    JS_CFUNC_MAGIC_DEF("nextSibling", 1, js_tree_walker_method, NEXT_SIBLING),
    JS_CFUNC_MAGIC_DEF("parentNode", 1, js_tree_walker_method, PARENT_NODE),
    JS_CFUNC_MAGIC_DEF("previousNode", 1, js_tree_walker_method, PREVIOUS_NODE),
    JS_CFUNC_MAGIC_DEF("previousSibling", 1, js_tree_walker_method, PREVIOUS_SIBLING),
    JS_CGETSET_MAGIC_DEF("root", js_tree_walker_get, NULL, GET_ROOT),
    JS_CGETSET_MAGIC_DEF("current", js_tree_walker_get, NULL, GET_CURRENT),
    JS_CGETSET_MAGIC_DEF("level", js_tree_walker_get, NULL, GET_LEVEL),
    JS_CGETSET_MAGIC_DEF("index", js_tree_walker_get, NULL, GET_INDEX),
    JS_CGETSET_MAGIC_DEF("length", js_tree_walker_get, NULL, GET_LENGTH),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TreeWalker", JS_PROP_CONFIGURABLE)};

static const JSCFunctionListEntry js_tree_walker_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("from", 1, js_tree_walker_funcs, 0)};

static int
js_tree_walker_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_tree_walker_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_tree_walker_class_id, &js_tree_walker_class);

  tree_walker_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, tree_walker_proto, js_tree_walker_proto_funcs, countof(js_tree_walker_proto_funcs));
  JS_SetClassProto(ctx, js_tree_walker_class_id, tree_walker_proto);

  tree_walker_class = JS_NewCFunction2(ctx, js_tree_walker_ctor, "TreeWalker", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, tree_walker_class, tree_walker_proto);
  JS_SetPropertyFunctionList(ctx, tree_walker_class, js_tree_walker_static_funcs, countof(js_tree_walker_static_funcs));

  if(m)
    JS_SetModuleExport(ctx, m, "TreeWalker", tree_walker_class);

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_tree_walker
#endif

JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, &js_tree_walker_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "TreeWalker");
  return m;
}