#include "quickjs.h"
#include "cutils.h"
#include <string.h>

JSClassID js_tree_walker_class_id;
JSValue tree_walker_proto, tree_walker_ctor, tree_walker_class;

enum tree_walker_methods { FIRST_CHILD = 0, LAST_CHILD, NEXT_NODE, NEXT_SIBLING, PARENT_NODE, PREVIOUS_NODE, PREVIOUS_SIBLING };
enum tree_walker_getters { GET_ROOT = 0, GET_CURRENT  };

typedef struct {
  JSValue object;
  JSPropertyEnum* properties;
  uint32_t length;
  uint32_t index;
    JSValue current;

} tree_walker_frame;

typedef struct {
  tree_walker_frame* stack;
  uint32_t depth;
  uint32_t allocated;
} tree_walker_instance;

static tree_walker_frame*
js_tree_walker_push(JSContext* ctx, tree_walker_instance* tw) {
  uint32_t n = tw->depth + 1;
  if(tw->allocated < n) {
    tw->stack = tw->stack ? js_realloc(ctx, tw->stack, sizeof(tree_walker_frame) * n) : js_malloc(ctx, sizeof(tree_walker_frame) * n);
    tw->allocated = n;
  }
  if(tw->stack) {
    tree_walker_frame* r;
    r = &tw->stack[tw->depth++];
    memset(r, 0, sizeof(tree_walker_frame));
    r->object = JS_UNDEFINED;
    r->current =JS_UNDEFINED;
    return r;
  }
  return 0;
}

static tree_walker_frame*
js_tree_walker_pop(JSContext* ctx, tree_walker_instance* tw) {
  if(tw->depth > 0)
    tw->depth--;
  if(tw->depth > 0)
    return &tw->stack[tw->depth - 1];
  return 0;
}

static inline tree_walker_frame*
js_tree_walker_top( tree_walker_instance* tw) {
          return tw->depth > 0 ?  &tw->stack[tw->depth - 1] : 0;
}

static inline tree_walker_frame*
js_tree_walker_bottom( tree_walker_instance* tw) {
          return tw->depth > 0 ? &tw->stack[0] : 0;
}


static int
js_tree_walker_recurse(JSContext* ctx, JSValueConst object, tree_walker_frame* f, int flags) {
  if(JS_GetOwnPropertyNames(ctx, &f->properties, &f->length, object, flags))
    return -1;
  f->object = object;
  f->index = 0;
}

static JSValue
js_tree_walker_ctor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  tree_walker_instance* tw;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  if(!(tw = js_mallocz(ctx, sizeof(tree_walker_instance))))
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
  JS_SetOpaque(obj, tw);
  if(argc > 0) {
    tree_walker_frame* f = js_tree_walker_push(ctx, tw);
    js_tree_walker_recurse(ctx, argv[0], f, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY);
  }
  return obj;
fail:
  js_free(ctx, tw);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_tree_walker_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  tree_walker_instance* tw;

  if(!(tw = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case FIRST_CHILD: {
      break;
    }
    case LAST_CHILD: {
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
 tree_walker_instance* tw;
      tree_walker_frame* f;

  if(!(tw = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;

 switch(magic) {
    case GET_ROOT: {
      if((f = js_tree_walker_bottom(tw)))
        return f->object;
      break;
    }
    case GET_CURRENT: {
        if((f = js_tree_walker_top(tw)))
        return f->object;
      break;
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
  tree_walker_instance* tw = JS_GetOpaque(val, js_tree_walker_class_id);
  if(tw)
    js_free_rt(rt, tw);
  JS_FreeValueRT(rt, val);
}

JSClassDef js_tree_walker_class = {
    .class_name = "TreeWalker",
    .finalizer = js_tree_walker_finalizer,
};

static const JSCFunctionListEntry js_tree_walker_proto_funcs[] = {JS_CFUNC_MAGIC_DEF("firstChild", 1, js_tree_walker_method, FIRST_CHILD),
                                                                  JS_CFUNC_MAGIC_DEF("lastChild", 1, js_tree_walker_method, LAST_CHILD),
                                                                  JS_CFUNC_MAGIC_DEF("nextNode", 1, js_tree_walker_method, NEXT_NODE),
                                                                  JS_CFUNC_MAGIC_DEF("nextSibling", 1, js_tree_walker_method, NEXT_SIBLING),
                                                                  JS_CFUNC_MAGIC_DEF("parentNode", 1, js_tree_walker_method, PARENT_NODE),
                                                                  JS_CFUNC_MAGIC_DEF("previousNode", 1, js_tree_walker_method, PREVIOUS_NODE),
                                                                  JS_CFUNC_MAGIC_DEF("previousSibling", 1, js_tree_walker_method, PREVIOUS_SIBLING),
                                                                    JS_CGETSET_MAGIC_DEF("root", js_tree_walker_get, NULL, GET_ROOT),
                                                                    JS_CGETSET_MAGIC_DEF("current", js_tree_walker_get, NULL, GET_CURRENT)
};

static const JSCFunctionListEntry js_tree_walker_static_funcs[] = {JS_CFUNC_MAGIC_DEF("from", 1, js_tree_walker_funcs, 0)};

static int
js_tree_walker_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_tree_walker_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_tree_walker_class_id, &js_tree_walker_class);

  tree_walker_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, tree_walker_proto, js_tree_walker_proto_funcs, countof(js_tree_walker_proto_funcs));
  JS_SetClassProto(ctx, js_tree_walker_class_id, tree_walker_proto);

  tree_walker_class = JS_NewCFunction2(ctx, js_tree_walker_ctor, "TreeWalker", 2, JS_CFUNC_constructor, 0);

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