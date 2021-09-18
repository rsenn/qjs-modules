#include "cutils.h"
#include "libregexp.h"
#include "property-enumeration.h"
#include "quickjs.h"
#include <string.h>

thread_local VISIBLE JSClassID js_tree_walker_class_id = 0;
thread_local JSValue tree_walker_proto = {JS_TAG_UNDEFINED}, tree_walker_ctor = {JS_TAG_UNDEFINED};
thread_local VISIBLE JSClassID js_tree_iterator_class_id = 0;
thread_local JSValue tree_iterator_proto = {JS_TAG_UNDEFINED}, tree_iterator_ctor = {JS_TAG_UNDEFINED};

enum tree_walker_methods {
  FIRST_CHILD = 0,
  LAST_CHILD,
  NEXT_NODE,
  NEXT_SIBLING,
  PARENT_NODE,
  PREVIOUS_NODE,
  PREVIOUS_SIBLING
};

enum tree_walker_getters {
  PROP_ROOT = 0,
  PROP_CURRENT_NODE,
  PROP_CURRENT_KEY,
  PROP_CURRENT_PATH,
  PROP_DEPTH,
  PROP_INDEX,
  PROP_LENGTH,
  PROP_TAG_MASK,
  PROP_FLAGS
};

enum tree_iterator_return {
  RETURN_VALUE = 0,
  RETURN_PATH = 1 << 24,
  RETURN_VALUE_PATH = 2 << 24,
  RETURN_MASK = 3 << 24
};

typedef struct {
  Vector frames;
  uint32_t tag_mask;
  uint32_t ref_count;
} TreeWalker;

static void
tree_walker_reset(TreeWalker* w, JSContext* ctx) {
  PropertyEnumeration* it;

  vector_foreach_t(&w->frames, it) { property_enumeration_reset(it, JS_GetRuntime(ctx)); }
  vector_clear(&w->frames);

  w->tag_mask = TYPE_ALL;
}

static PropertyEnumeration*
tree_walker_setroot(TreeWalker* w, JSContext* ctx, JSValueConst object) {
  tree_walker_reset(w, ctx);
  return property_enumeration_push(&w->frames, ctx, JS_DupValue(ctx, object), PROPENUM_DEFAULT_FLAGS);
}

static void
tree_walker_dump(TreeWalker* w, JSContext* ctx, DynBuf* db) {
  dbuf_printf(db, "TreeWalker {\n  depth: %u", vector_size(&w->frames, sizeof(PropertyEnumeration)));

  dbuf_putstr(db, ",\n  frames: ");

  property_enumeration_dumpall(&w->frames, ctx, db);
  dbuf_putstr(db, "\n}");
}

static JSValue
js_tree_walker_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  TreeWalker* w;
  PropertyEnumeration* it = 0;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  if(!(w = js_mallocz(ctx, sizeof(TreeWalker))))
    return JS_EXCEPTION;

  w->ref_count = 1;
  tree_walker_reset(w, ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_tree_walker_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, w);

  if(argc > 0 && JS_IsObject(argv[0]))
    it = tree_walker_setroot(w, ctx, argv[0]);

  if(argc > 1)
    JS_ToUint32(ctx, &w->tag_mask, argv[1]);

  return obj;
fail:
  js_free(ctx, w);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_tree_walker_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  TreeWalker* w;
  DynBuf dbuf;
  JSValue ret;
  if(!(w = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;
  js_dbuf_init(ctx, &dbuf);
  tree_walker_dump(w, ctx, &dbuf);
  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);

  return ret;
}

static PropertyEnumeration*
js_tree_walker_next(JSContext* ctx, TreeWalker* w, JSValueConst this_arg, JSValueConst pred) {
  PropertyEnumeration* it;
  enum value_mask type, mask = w->tag_mask & TYPE_ALL;

  for(; (it = property_enumeration_recurse(&w->frames, ctx));) {
    if(mask && mask != TYPE_ALL) {
      JSValue value;
      value = property_enumeration_value(it, ctx);
      type = js_value_type(ctx, value);
      JS_FreeValue(ctx, value);
      if((mask & type) == 0)
        continue;
    }
    if(JS_IsFunction(ctx, pred)) {
      BOOL result = property_enumeration_predicate(it, ctx, pred, this_arg);
      if(!result)
        continue;
    }
    break;
  }
  return it;
}

static JSValue
js_tree_walker_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  TreeWalker* w;
  PropertyEnumeration* it;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;

  if(vector_empty(&w->frames))
    return JS_UNDEFINED;

  it = vector_back(&w->frames, sizeof(PropertyEnumeration));

  if(magic == PREVIOUS_NODE) {
    magic = it->idx == 0 ? PARENT_NODE : PREVIOUS_SIBLING;
  }

  if(magic == NEXT_NODE) {
    it = js_tree_walker_next(ctx, w, this_val, argc > 0 ? argv[0] : JS_UNDEFINED);
  }

  switch(magic) {
    case FIRST_CHILD: {
      if((it = property_enumeration_enter(&w->frames, ctx, 0, PROPENUM_DEFAULT_FLAGS)) == 0 /*||
         !property_enumeration_setpos(it, 0)*/)
        return JS_UNDEFINED;
      break;
    }

    case LAST_CHILD: {
      if((it = property_enumeration_enter(&w->frames, ctx, -1, PROPENUM_DEFAULT_FLAGS)) == 0 /*||
         !property_enumeration_setpos(it, -1)*/)
        return JS_UNDEFINED;
      break;
    }

    case NEXT_SIBLING: {
      if(!property_enumeration_setpos(it, it->idx + 1))
        return JS_UNDEFINED;
      break;
    }

    case PARENT_NODE: {
      if((it = property_enumeration_pop(&w->frames, ctx)) == 0)
        return JS_UNDEFINED;
      break;
    }

    case PREVIOUS_SIBLING: {
      if(!property_enumeration_setpos(it, it->idx - 1))
        return JS_UNDEFINED;
      break;
    }
  }
  return it ? property_enumeration_value(it, ctx) : JS_UNDEFINED;
}

static JSValue
js_tree_walker_get(JSContext* ctx, JSValueConst this_val, int magic) {
  TreeWalker* w;
  PropertyEnumeration* it;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;

  it = vector_back(&w->frames, sizeof(PropertyEnumeration));

  switch(magic) {
    case PROP_ROOT: {
      if(!vector_empty(&w->frames)) {
        it = vector_begin(&w->frames);
        return JS_DupValue(ctx, it->obj);
      }
      break;
    }

    case PROP_CURRENT_NODE: {
      if(it)
        return property_enumeration_value(it, ctx);
      break;
    }

    case PROP_CURRENT_KEY: {
      if(it)
        return property_enumeration_key(it, ctx);
      break;
    }

    case PROP_CURRENT_PATH: {
      return property_enumeration_path(&w->frames, ctx);
    }

    case PROP_DEPTH: {
      return JS_NewUint32(ctx, vector_size(&w->frames, sizeof(PropertyEnumeration)) - 1);
    }

    case PROP_INDEX: {
      return JS_NewUint32(ctx, property_enumeration_index(it));
    }

    case PROP_LENGTH: {
      return JS_NewUint32(ctx, property_enumeration_length(it));
    }

    case PROP_TAG_MASK: {
      return JS_NewUint32(ctx, w->tag_mask);
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_tree_walker_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  TreeWalker* w;
  PropertyEnumeration* it;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;
  if(!(it = vector_back(&w->frames, sizeof(PropertyEnumeration))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_INDEX: {
      int64_t index = 0;
      JS_ToInt64(ctx, &index, value);
      if(index < 0)
        index = (index % property_enumeration_length(it)) + property_enumeration_length(it);
      it->idx = index;
      break;
    }

    case PROP_TAG_MASK: {
      uint32_t tag_mask = 0;
      JS_ToUint32(ctx, &tag_mask, value);
      w->tag_mask = tag_mask;
      break;
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_tree_walker_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  TreeWalker* w;
  JSValue obj;

  if(!(w = JS_GetOpaque(this_val, js_tree_walker_class_id)))
    w = JS_GetOpaque(this_val, js_tree_iterator_class_id);

  obj = JS_NewObjectProtoClass(ctx, tree_iterator_proto, js_tree_iterator_class_id);

  w->ref_count++;

  JS_SetOpaque(obj, w);

  return obj;
}

static void
js_tree_walker_finalizer(JSRuntime* rt, JSValue val) {
  TreeWalker* w = JS_GetOpaque(val, js_tree_walker_class_id);
  if(w) {
    PropertyEnumeration *s, *e;

    if(--w->ref_count == 0) {
      for(s = vector_begin(&w->frames), e = vector_end(&w->frames); s != e; s++) { property_enumeration_reset(s, rt); }
      vector_free(&w->frames);
      js_free_rt(rt, w);
    }
  }
  JS_FreeValueRT(rt, val);
}

static JSValue
js_tree_iterator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  TreeWalker* w;
  PropertyEnumeration* it = 0;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  if(!(w = js_mallocz(ctx, sizeof(TreeWalker))))
    return JS_EXCEPTION;

  vector_init(&w->frames, ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_tree_iterator_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, w);

  if(argc > 0 && JS_IsObject(argv[0]))
    it = tree_walker_setroot(w, ctx, argv[0]);

  if(argc > 1)
    JS_ToUint32(ctx, &w->tag_mask, argv[1]);

  return obj;
fail:
  js_free(ctx, w);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_tree_iterator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  PropertyEnumeration* it;
  TreeWalker* w;
  enum tree_iterator_return r;
  enum value_mask t;
  JSValue ret = JS_UNDEFINED;

  w = JS_GetOpaque(this_val, js_tree_iterator_class_id);

  r = w->tag_mask & RETURN_MASK;
  t = w->tag_mask & 0xffffff;

  for(;;) {
    if((it = js_tree_walker_next(ctx, w, this_val, argc > 0 ? argv[0] : JS_UNDEFINED))) {
      enum value_mask vtype;
      *pdone = FALSE;

      /* if(t) {
         vtype = property_enumeration_type(it, ctx);

         if((vtype & t) == 0)
           continue;
       }*/

      switch(r) {
        case RETURN_VALUE: ret = property_enumeration_value(it, ctx); break;
        case RETURN_PATH: ret = property_enumeration_path(&w->frames, ctx); break;
        case RETURN_VALUE_PATH:
        default: {
          ret = JS_NewArray(ctx);
          JS_SetPropertyUint32(ctx, ret, 0, property_enumeration_value(it, ctx));
          JS_SetPropertyUint32(ctx, ret, 1, property_enumeration_path(&w->frames, ctx));
          break;
        }
      }

    } else {
      *pdone = TRUE;
      ret = JS_UNDEFINED;
    }

    break;
  }
  return ret;
}

static void
js_tree_iterator_finalizer(JSRuntime* rt, JSValue val) {
  TreeWalker* w = JS_GetOpaque(val, js_tree_iterator_class_id);
  if(w) {
    PropertyEnumeration *s, *e;

    if(--w->ref_count == 0) {
      for(s = vector_begin(&w->frames), e = vector_end(&w->frames); s != e; s++) { property_enumeration_reset(s, rt); }
      vector_free(&w->frames);
      js_free_rt(rt, w);
    }
  }
  JS_FreeValueRT(rt, val);
}

static JSClassDef js_tree_walker_class = {
    .class_name = "TreeWalker",
    .finalizer = js_tree_walker_finalizer,
};

static JSClassDef js_tree_iterator_class = {
    .class_name = "TreeIterator",
    .finalizer = js_tree_iterator_finalizer,
};

static const JSCFunctionListEntry js_tree_walker_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("firstChild", 0, js_tree_walker_method, FIRST_CHILD),
    JS_CFUNC_MAGIC_DEF("lastChild", 0, js_tree_walker_method, LAST_CHILD),
    JS_CFUNC_MAGIC_DEF("nextNode", 0, js_tree_walker_method, NEXT_NODE),
    JS_CFUNC_MAGIC_DEF("nextSibling", 0, js_tree_walker_method, NEXT_SIBLING),
    JS_CFUNC_MAGIC_DEF("parentNode", 0, js_tree_walker_method, PARENT_NODE),
    JS_CFUNC_MAGIC_DEF("previousNode", 0, js_tree_walker_method, PREVIOUS_NODE),
    JS_CFUNC_MAGIC_DEF("previousSibling", 0, js_tree_walker_method, PREVIOUS_SIBLING),
    JS_CGETSET_MAGIC_DEF("root", js_tree_walker_get, NULL, PROP_ROOT),
    JS_CGETSET_MAGIC_DEF("currentNode", js_tree_walker_get, NULL, PROP_CURRENT_NODE),
    JS_CGETSET_MAGIC_DEF("currentKey", js_tree_walker_get, NULL, PROP_CURRENT_KEY),
    JS_CGETSET_MAGIC_DEF("currentPath", js_tree_walker_get, NULL, PROP_CURRENT_PATH),
    JS_CGETSET_MAGIC_DEF("depth", js_tree_walker_get, NULL, PROP_DEPTH),
    JS_CGETSET_MAGIC_DEF("index", js_tree_walker_get, js_tree_walker_set, PROP_INDEX),
    JS_CGETSET_MAGIC_DEF("length", js_tree_walker_get, NULL, PROP_LENGTH),
    JS_CGETSET_MAGIC_DEF("tagMask", js_tree_walker_get, js_tree_walker_set, PROP_TAG_MASK),
    JS_CGETSET_MAGIC_DEF("flags", js_tree_walker_get, js_tree_walker_set, PROP_FLAGS),
    JS_CFUNC_DEF("toString", 0, js_tree_walker_tostring),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TreeWalker", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_tree_walker_static_funcs[] = {
    JS_PROP_INT32_DEF("TYPE_UNDEFINED", TYPE_UNDEFINED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_NULL", TYPE_NULL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_BOOL", TYPE_BOOL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_INT", TYPE_INT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_OBJECT", TYPE_OBJECT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_STRING", TYPE_STRING, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_SYMBOL", TYPE_SYMBOL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_BIG_FLOAT", TYPE_BIG_FLOAT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_BIG_INT", TYPE_BIG_INT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_BIG_DECIMAL", TYPE_BIG_DECIMAL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ALL", TYPE_ALL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_PRIMITIVE", TYPE_PRIMITIVE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("RETURN_VALUE", RETURN_VALUE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("RETURN_PATH", RETURN_PATH, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("RETURN_VALUE_PATH", RETURN_VALUE_PATH, JS_PROP_ENUMERABLE),
};

static const JSCFunctionListEntry js_tree_iterator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_tree_iterator_next, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TreeIterator", JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_tree_walker_iterator),
};

static int
js_tree_walker_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_tree_walker_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_tree_walker_class_id, &js_tree_walker_class);

  tree_walker_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, tree_walker_proto, js_tree_walker_proto_funcs, countof(js_tree_walker_proto_funcs));
  JS_SetClassProto(ctx, js_tree_walker_class_id, tree_walker_proto);

  tree_walker_ctor = JS_NewCFunction2(ctx, js_tree_walker_constructor, "TreeWalker", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, tree_walker_ctor, tree_walker_proto);
  JS_SetPropertyFunctionList(ctx, tree_walker_ctor, js_tree_walker_static_funcs, countof(js_tree_walker_static_funcs));

  JS_NewClassID(&js_tree_iterator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_tree_iterator_class_id, &js_tree_iterator_class);

  tree_iterator_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx,
                             tree_iterator_proto,
                             js_tree_iterator_proto_funcs,
                             countof(js_tree_iterator_proto_funcs));
  JS_SetClassProto(ctx, js_tree_iterator_class_id, tree_iterator_proto);

  tree_iterator_ctor = JS_NewCFunction2(ctx, js_tree_iterator_constructor, "TreeIterator", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, tree_iterator_ctor, tree_iterator_proto);
  JS_SetPropertyFunctionList(ctx,
                             tree_iterator_ctor,
                             js_tree_walker_static_funcs,
                             countof(js_tree_walker_static_funcs));

  if(m) {
    JS_SetModuleExport(ctx, m, "TreeWalker", tree_walker_ctor);
    JS_SetModuleExport(ctx, m, "TreeIterator", tree_iterator_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_tree_walker
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, &js_tree_walker_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "TreeWalker");
  JS_AddModuleExport(ctx, m, "TreeIterator");
  return m;
}
