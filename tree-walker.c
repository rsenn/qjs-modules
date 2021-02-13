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

enum tree_walker_methods {
  FIRST_CHILD = 0,
  LAST_CHILD,
  NEXT_NODE,
  NEXT_SIBLING,
  PARENT_NODE,
  PREVIOUS_NODE,
  PREVIOUS_SIBLING
};
enum tree_walker_getters { GET_ROOT = 0, GET_CURRENT_NODE, GET_DEPTH, GET_INDEX, GET_LENGTH, GET_KEY };

typedef struct {
  JSValue obj;
  uint32_t idx;
  uint32_t tab_atom_len;
  JSPropertyEnum* tab_atom;
} PropertyEnumeration;

typedef struct {
  JSValue current;
  vector frames;
} TreeWalker;

static PropertyEnumeration*
property_enumeration_new(TreeWalker* wc, JSValue obj) {
  PropertyEnumeration* it;

  if((it = vector_push(&wc->frames, sizeof(PropertyEnumeration)))) {
    it->obj = obj;
    it->tab_atom = 0;
    it->tab_atom_len = 0;
    it->idx = 0;
  }
  return it;
}

static void
property_enumeration_free(PropertyEnumeration* it, JSRuntime* rt) {
  uint32_t i;
  if(it->tab_atom) {
    for(i = 0; i < it->tab_atom_len; i++) JS_FreeAtomRT(rt, it->tab_atom[i].atom);
    js_free_rt(rt, it->tab_atom);
  }
}

static int
property_enumeration_init(PropertyEnumeration* it, JSContext* ctx, JSValueConst object, int flags) {
  if(JS_GetOwnPropertyNames(ctx, &it->tab_atom, &it->tab_atom_len, object, flags)) {
    it->tab_atom_len = 0;
    it->tab_atom = 0;
    return -1;
  }
  return 0;
}

static int
property_enumeration_setpos(PropertyEnumeration* it, int32_t idx) {
  if((idx < 0 ? -idx : idx) >= it->tab_atom_len)
    return 0;

  if(idx < 0)
    idx += it->tab_atom_len;
  assert(idx >= 0);
  assert(idx < it->tab_atom_len);
  it->idx = idx;
  return 1;
}

static JSValue
property_enumeration_value(PropertyEnumeration* it, JSContext* ctx) {
  return JS_GetProperty(ctx, it->obj, it->tab_atom[it->idx].atom);
}

static JSValue
property_enumeration_key(PropertyEnumeration* it, JSContext* ctx) {
  return JS_AtomToValue(ctx, it->tab_atom[it->idx].atom);
}

static void
tree_walker_reset(TreeWalker* wc, JSContext* ctx) {
  PropertyEnumeration *s = vector_begin(&wc->frames), *e = vector_end(&wc->frames);

  while(s < e) property_enumeration_free(s++, JS_GetRuntime(ctx));

  vector_clear(&wc->frames);
}

static PropertyEnumeration*
tree_walker_setroot(TreeWalker* wc, JSContext* ctx, JSValueConst object) {
  PropertyEnumeration* it;

  if(!JS_IsObject(object)) {
    JS_ThrowTypeError(ctx, "not an object");
    return 0;
  }
  wc->current = JS_DupValue(ctx, object);

  vector_clear(&wc->frames);

  if((it = property_enumeration_new(wc, wc->current)))
    property_enumeration_init(it, ctx, object, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY);
  return it;
}

static PropertyEnumeration*
tree_walker_descend(TreeWalker* wc, JSContext* ctx) {
  PropertyEnumeration* it;
  if(!JS_IsObject(wc->current)) {
    JS_ThrowTypeError(ctx, "not an object");
    return 0;
  }
  if((it = property_enumeration_new(wc, wc->current)))
    property_enumeration_init(it, ctx, wc->current, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY);
  return it;
}

static PropertyEnumeration*
tree_walker_ascend(TreeWalker* wc, JSContext* ctx) {
  PropertyEnumeration* it = vector_back(&wc->frames, sizeof(PropertyEnumeration));
  property_enumeration_free(it, JS_GetRuntime(ctx));

  vector_pop(&wc->frames, sizeof(PropertyEnumeration));
  if(vector_empty(&wc->frames))
    return 0;
  return it - 1;
}

static JSValue
js_tree_walker_ctor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  TreeWalker* wc;
  PropertyEnumeration* it = 0;
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
    it = tree_walker_setroot(wc, ctx, argv[0]);
  }

  return obj;
fail:
  js_free(ctx, wc);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_tree_walker_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  TreeWalker* wc;
  PropertyEnumeration* it;

  if(!(wc = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;
  it = vector_back(&wc->frames, sizeof(PropertyEnumeration));

  /*  if(!JS_IsObject(wc->current))
      return JS_EXCEPTION;*/

  if(magic == PREVIOUS_NODE) {
    if(it->idx == 0)
      magic = PARENT_NODE;
    else
      magic = PREVIOUS_SIBLING;
  }

  if(magic == NEXT_NODE) {
    if(JS_IsObject(wc->current))
      magic = FIRST_CHILD;
    else
      magic = NEXT_SIBLING;
  }

  switch(magic) {
    case FIRST_CHILD: {
      if((it = tree_walker_descend(wc, ctx)))
        if(property_enumeration_setpos(it, 0))
          return wc->current = property_enumeration_value(it, ctx);
      break;
    }
    case LAST_CHILD: {
      if((it = tree_walker_descend(wc, ctx)))
        if(property_enumeration_setpos(it, -1))
          return wc->current = property_enumeration_value(it, ctx);
      break;
    }
    case NEXT_SIBLING: {
      if(property_enumeration_setpos(it, it->idx + 1))
        return wc->current = property_enumeration_value(it, ctx);
      break;
    }
    case PARENT_NODE: {
      if((it = tree_walker_ascend(wc, ctx)))
        return wc->current = property_enumeration_value(it, ctx);
      break;
    }
    case PREVIOUS_SIBLING: {
      if(property_enumeration_setpos(it, it->idx - 1))
        return wc->current = property_enumeration_value(it, ctx);
      break;
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_tree_walker_get(JSContext* ctx, JSValueConst this_val, int magic) {
  TreeWalker* wc;
  PropertyEnumeration* it;

  if(!(wc = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;
  if(!(it = vector_back(&wc->frames, sizeof(PropertyEnumeration))))
    return JS_EXCEPTION;

  switch(magic) {
    case GET_ROOT: {
      if((it = vector_front(&wc->frames, sizeof(PropertyEnumeration))))
        return JS_DupValue(ctx, wc->current);
      break;
    }
    case GET_CURRENT_NODE: {
      return JS_DupValue(ctx, wc->current);
      break;
    }
    case GET_DEPTH: {
      return JS_NewUint32(ctx, vector_size(&wc->frames, sizeof(PropertyEnumeration)) - 1);
    }
    case GET_INDEX: {
      return JS_NewUint32(ctx, it->idx);
    }
    case GET_LENGTH: {
      return JS_NewUint32(ctx, it->tab_atom_len);
    }
    case GET_KEY: {
      return property_enumeration_key(it, ctx);
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
    PropertyEnumeration* it;

    uint32_t i, n = vector_size(&wc->frames, sizeof(PropertyEnumeration));

    for(i = 0; i < n; i++) {
      it = vector_at(&wc->frames, sizeof(PropertyEnumeration), i);
      JS_FreeValueRT(rt, it->obj);
      property_enumeration_free(it, rt);
    }

    JS_FreeValueRT(rt, wc->current);
    js_free_rt(rt, wc);
  }
  // JS_FreeValueRT(rt, val);
}

JSClassDef js_tree_walker_class = {
    .class_name = "TreeWalker",
    .finalizer = js_tree_walker_finalizer,
};

static const JSCFunctionListEntry js_tree_walker_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("firstChild", 0, js_tree_walker_method, FIRST_CHILD),
    JS_CFUNC_MAGIC_DEF("lastChild", 0, js_tree_walker_method, LAST_CHILD),
    JS_CFUNC_MAGIC_DEF("nextNode", 0, js_tree_walker_method, NEXT_NODE),
    JS_CFUNC_MAGIC_DEF("nextSibling", 0, js_tree_walker_method, NEXT_SIBLING),
    JS_CFUNC_MAGIC_DEF("parentNode", 0, js_tree_walker_method, PARENT_NODE),
    JS_CFUNC_MAGIC_DEF("previousNode", 0, js_tree_walker_method, PREVIOUS_NODE),
    JS_CFUNC_MAGIC_DEF("previousSibling", 0, js_tree_walker_method, PREVIOUS_SIBLING),
    JS_CGETSET_MAGIC_DEF("root", js_tree_walker_get, NULL, GET_ROOT),
    JS_CGETSET_MAGIC_DEF("currentNode", js_tree_walker_get, NULL, GET_CURRENT_NODE),
    JS_CGETSET_MAGIC_DEF("depth", js_tree_walker_get, NULL, GET_DEPTH),
    JS_CGETSET_MAGIC_DEF("index", js_tree_walker_get, NULL, GET_INDEX),
    JS_CGETSET_MAGIC_DEF("length", js_tree_walker_get, NULL, GET_LENGTH),
    JS_CGETSET_MAGIC_DEF("key", js_tree_walker_get, NULL, GET_KEY),
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
