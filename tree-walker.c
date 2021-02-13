#include "quickjs.h"
#include "cutils.h"
#include "vector.h"
#include <string.h>

JSClassID js_tree_walker_class_id;
JSValue tree_walker_proto, tree_walker_ctor, tree_walker_class;

typedef struct JSForInIterator {
  JSValue obj;
  BOOL is_array;
  uint32_t array_length;
  uint32_t idx;
} JSForInIterator;

static void
js_free_prop_enum(JSContext* ctx, JSPropertyEnum* tab, uint32_t len) {
  uint32_t i;
  if(tab) {
    for(i = 0; i < len; i++) JS_FreeAtom(ctx, tab[i].atom);
    js_free(ctx, tab);
  }
}

enum tree_walker_methods {
  FIRST_CHILD = 0,
  LAST_CHILD,
  NEXT_NODE,
  NEXT_SIBLING,
  PARENT_NODE,
  PREVIOUS_NODE,
  PREVIOUS_SIBLING
};
enum tree_walker_getters { GET_ROOT = 0, GET_CURRENT_NODE, GET_LEVEL, GET_INDEX, GET_LENGTH, GET_KEY };

typedef struct {
  JSValue obj;
  BOOL is_array;
  uint32_t tab_atom_len;
  uint32_t idx;
  JSPropertyEnum* tab;
} TreeIterator;

typedef struct {
  JSValue current;
  vector frames;
} TreeWalker;

 static TreeIterator*
tree_iterator_new(TreeWalker* wc, JSValue obj) {
  TreeIterator* fr;
  if((fr = vector_push(&wc->frames, sizeof(TreeIterator)))) {
    fr->obj = obj;
    fr->tab = 0;
    fr->tab_atom_len = 0;
    fr->idx = 0;
  }
  return fr;
}
 
static int
tree_iterator_init(TreeIterator* fr, JSContext* ctx, JSValueConst object, int flags) {
    if(JS_GetOwnPropertyNames(ctx, &fr->tab, &fr->tab_atom_len, object, flags)) {
    fr->tab_atom_len = 0;
    fr->tab = 0;
    return -1;
  }
  return 0;
}

static TreeIterator*
tree_walker_push(TreeWalker* wc, JSContext* ctx, JSValueConst object) {
  TreeIterator* fr;

  if((fr = tree_iterator_new(wc,JS_DupValue(ctx, object))))
    tree_iterator_init(fr, ctx, object, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY);
  return fr;
}

static TreeIterator*
walker_descend(TreeWalker* wc,JSContext* ctx) {
TreeIterator* fr;

  if((fr = tree_iterator_new(wc, wc->current)))
    tree_iterator_init(fr, ctx,  wc->current, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY);
  return fr;
}

static JSPropertyEnum*
tree_iterator_property(TreeIterator* fr, int32_t idx) {
  if(idx < 0) {
    idx %= (int32_t)fr->tab_atom_len;
    idx += fr->tab_atom_len;
  } else {
    idx %= (uint32_t)fr->tab_atom_len;
  }
  assert(idx >= 0);
  assert(idx < fr->tab_atom_len);
  return &fr->tab[(fr->idx = idx)];
}

static JSValue
tree_iterator_setpos(TreeIterator* fr, int32_t idx, JSContext* ctx) {
  JSPropertyEnum* prop;
  if((prop = tree_iterator_property(fr, idx)))
    return JS_GetProperty(ctx, fr->obj, prop->atom);

  return JS_UNDEFINED;
}

static JSValue
tree_iterator_key(TreeIterator* fr, int32_t idx, JSContext* ctx) {
  JSPropertyEnum* prop;
  if((prop = tree_iterator_property(fr, idx))) {
    return JS_AtomToValue(ctx, prop->atom);
  }
  return JS_UNDEFINED;
}
/*static JSValue
tree_iterator_propdesc(TreeIterator* fr, int32_t idx, JSContext* ctx) {
  JSPropertyEnum* prop;
  if((prop = tree_iterator_property(fr, idx))) {
    JSPropertyDescriptor desc;

    JS_GetOwnProperty(ctx, &desc, fr->obj, prop->atom);
  }

  return JS_UNDEFINED;
}*/

static JSValue
js_tree_walker_ctor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  TreeWalker* wc;
  TreeIterator* fr = 0;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  if(!(wc = js_mallocz(ctx, sizeof(TreeWalker))))
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
    fr = tree_walker_push(wc, ctx, argv[0]);
  }

  return obj;
fail:
  /*js_free(ctx, wc);
  JS_FreeValue(ctx, obj);*/
  return JS_EXCEPTION;
}

static JSValue
js_tree_walker_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  TreeWalker* wc;
  TreeIterator* fr;

  if(!(wc = JS_GetOpaque2(ctx, /*ctx, */ this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;
  fr = vector_back(&wc->frames, sizeof(TreeIterator));

  switch(magic) {
    case FIRST_CHILD: {
      if(JS_IsObject(wc->current)) {
        fr = walker_descend(wc, ctx);
        return JS_DupValue(ctx, tree_iterator_setpos(fr, 0, ctx));
      }
      break;
    }
    case LAST_CHILD: {
      if(JS_IsObject(wc->current)) {
        fr = walker_descend(wc, ctx);
        return JS_DupValue(ctx, tree_iterator_setpos(fr, -1, ctx));
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
  TreeWalker* wc;
  TreeIterator* fr;

  if(!(wc = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;
  if(!(fr = vector_back(&wc->frames, sizeof(TreeIterator))))
    return JS_EXCEPTION;

  switch(magic) {
    case GET_ROOT: {
      if((fr = vector_front(&wc->frames, sizeof(TreeIterator))))
        return JS_DupValue(ctx, wc->current);
      break;
    }
    case GET_CURRENT_NODE: {
      return JS_DupValue(ctx, wc->current);
      break;
    }
    case GET_LEVEL: {
      return JS_NewUint32(ctx, vector_size(&wc->frames, sizeof(TreeIterator)) - 1);
    }
    case GET_INDEX: {
      return JS_NewUint32(ctx, fr->idx);
    }
    case GET_LENGTH: {
      return JS_NewUint32(ctx, fr->tab_atom_len);
    }
    case GET_KEY: {
      return tree_iterator_key(fr, fr->idx, ctx);
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
  TreeWalker* wc = JS_GetOpaque(val, js_tree_walker_class_id);
  if(wc) {
    TreeIterator* fr;

    uint32_t i, n = vector_size(&wc->frames, sizeof(TreeIterator));

    for(i = 0; i < n; i++) {

      fr = vector_at(&wc->frames, sizeof(TreeIterator), i);
      JS_FreeValueRT(rt, wc->current);
      JS_FreeValueRT(rt, fr->obj);
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
    JS_CGETSET_MAGIC_DEF("currentNode", js_tree_walker_get, NULL, GET_CURRENT_NODE),
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