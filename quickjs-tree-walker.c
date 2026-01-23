#include "defines.h"
#include <cutils.h>
#include <libregexp.h>
#include "property-enumeration.h"
#include <quickjs.h>
#include <string.h>
#include "debug.h"
#include "buffer-utils.h"

/**
 * \defgroup quickjs-tree-walker quickjs-tree_walker: Object tree walker
 * @{
 */
VISIBLE JSClassID js_tree_walker_class_id = 0;
static JSValue tree_walker_proto, tree_walker_ctor;
VISIBLE JSClassID js_tree_iterator_class_id = 0;
static JSValue tree_iterator_proto, tree_iterator_ctor;

enum tree_walker_filter {
  FILTER_ACCEPT = 1,
  FILTER_REJECT = 2,
  FILTER_SKIP = 3,
};

enum tree_walker_methods {
  FIRST_CHILD = 0,
  LAST_CHILD,
  NEXT_NODE,
  NEXT_SIBLING,
  PARENT_NODE,
  PREVIOUS_NODE,
  PREVIOUS_SIBLING,
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
  PROP_FLAGS,
  PROP_FILTER,
};

enum tree_iterator_return {
  RETURN_VALUE = 0,
  RETURN_PATH = 1 << 24,
  RETURN_VALUE_PATH = 2 << 24,
  RETURN_MASK = 3 << 24,
};

typedef struct {
  int ref_count;
  uint32_t tag_mask;
  Vector hier;
  JSValueConst filter, transform;
} TreeWalker;

static void
tree_walker_reset(TreeWalker* w, JSContext* ctx) {
  PropertyEnumeration* it;

  vector_foreach_t(&w->hier, it) { property_enumeration_reset(it, JS_GetRuntime(ctx)); }
  vector_clear(&w->hier);

  w->tag_mask = TYPE_ALL;
  w->filter = JS_UNDEFINED;
  w->transform = JS_UNDEFINED;
}

static void
tree_walker_free(TreeWalker* w, JSRuntime* rt) {
  if(--w->ref_count == 0) {
    PropertyEnumeration *s, *e;

    for(s = vector_begin(&w->hier), e = vector_end(&w->hier); s != e; s++)
      property_enumeration_reset(s, rt);

    vector_free(&w->hier);
    js_free_rt(rt, w);
  }
}

static TreeWalker*
tree_walker_new(JSContext* ctx) {
  TreeWalker* w;

  if((w = js_mallocz(ctx, sizeof(TreeWalker))))
    w->ref_count = 1;

  return w;
}

static PropertyEnumeration*
tree_walker_setroot(TreeWalker* w, JSContext* ctx, JSValueConst object) {
  tree_walker_reset(w, ctx);
  return property_recursion_push(&w->hier, ctx, JS_DupValue(ctx, object), PROPENUM_DEFAULT_FLAGS);
}

static void
tree_walker_dump(TreeWalker* w, JSContext* ctx, DynBuf* db) {
  dbuf_printf(db, "TreeWalker {\n  depth: %u", vector_size(&w->hier, sizeof(PropertyEnumeration)));
  dbuf_putstr(db, ",\n  hier: ");
  property_recursion_dumpall(&w->hier, ctx, db);
  dbuf_putstr(db, "\n}");
}

static JSValue
js_get_filter(JSContext* ctx, JSValueConst val) {
  JSValue ret = JS_DupValue(ctx, val);
  BOOL is_obj = JS_IsObject(val);

  if(is_obj) {
    JSValue fn = JS_GetPropertyStr(ctx, val, "acceptNode");
    if(JS_IsFunction(ctx, fn)) {
      JS_FreeValue(ctx, ret);
      ret = fn;
    }
  }

  if(!JS_IsFunction(ctx, ret)) {
    JS_FreeValue(ctx, ret);
    return JS_NULL;
  }

  return ret;
}

static BOOL
js_is_filter(JSContext* ctx, JSValueConst val) {
  JSValue fn = js_get_filter(ctx, val);
  BOOL ret = JS_IsFunction(ctx, fn);

  JS_FreeValue(ctx, fn);
  return ret;
}

static JSValue
js_tree_walker_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  TreeWalker* w;
  JSValue proto, obj = JS_UNDEFINED;
  int argi = 1;

  if(!(w = tree_walker_new(ctx)))
    return JS_EXCEPTION;

  w->ref_count = 1;
  tree_walker_reset(w, ctx);
  vector_init(&w->hier, ctx);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_tree_walker_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  if(argc > 0 && JS_IsObject(argv[0]))
    /*it = */ tree_walker_setroot(w, ctx, argv[0]);

  if(argi < argc && JS_IsNumber(argv[argi]))
    JS_ToUint32(ctx, &w->tag_mask, argv[argi++]);

  if(argi < argc && js_is_filter(ctx, argv[argi]))
    w->filter = js_get_filter(ctx, argv[argi++]);

  if(argi < argc && JS_IsFunction(ctx, argv[argi]))
    w->transform = JS_DupValue(ctx, argv[argi++]);

  JS_SetOpaque(obj, w);

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

  dbuf_init_ctx(ctx, &dbuf);
  tree_walker_dump(w, ctx, &dbuf);
  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);

  return ret;
}

static PropertyEnumeration*
js_tree_walker_next(JSContext* ctx, TreeWalker* w, JSValueConst this_arg, JSValueConst pred) {
  PropertyEnumeration* it;
  ValueType type, mask = w->tag_mask & TYPE_ALL;

  for(; (property_recursion_next(&w->hier, ctx), it = property_recursion_top(&w->hier));) {
    if(mask && mask != TYPE_ALL) {
      JSValue value = property_enumeration_value(it, ctx);

      type = js_value_type(ctx, value);
      JS_FreeValue(ctx, value);

      if((mask & type) == 0)
        continue;
    }

    if(JS_IsFunction(ctx, pred))
      if(!property_enumeration_predicate(it, ctx, pred, this_arg))
        continue;

    break;
  }

  return it;
}

static JSValue
js_tree_walker_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  TreeWalker* w;
  PropertyEnumeration* it;
  JSValue ret = JS_UNDEFINED, predicate = JS_UNDEFINED;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;

  if(vector_empty(&w->hier))
    return JS_UNDEFINED;

  it = vector_back(&w->hier, sizeof(PropertyEnumeration));

  if(magic == PREVIOUS_NODE)
    magic = it->idx == 0 ? PARENT_NODE : PREVIOUS_SIBLING;

  if(magic == NEXT_NODE) {
    if(argc >= 1 && JS_IsFunction(ctx, argv[0]))
      predicate = argv[0];
    else if(JS_IsFunction(ctx, w->filter))
      predicate = w->filter;

    it = js_tree_walker_next(ctx, w, this_val, predicate);
  }

  switch(magic) {
    case FIRST_CHILD: {
      if((it = property_recursion_enter(&w->hier, ctx, 0, PROPENUM_DEFAULT_FLAGS)) == 0)
        return JS_UNDEFINED;

      break;
    }

    case LAST_CHILD: {
      if((it = property_recursion_enter(&w->hier, ctx, -1, PROPENUM_DEFAULT_FLAGS)) == 0)
        return JS_UNDEFINED;

      break;
    }

    case NEXT_SIBLING: {
      if(!property_enumeration_setpos(it, it->idx + 1))
        return JS_UNDEFINED;

      break;
    }

    case PARENT_NODE: {
      if((it = property_recursion_pop(&w->hier, ctx)) == 0)
        return JS_UNDEFINED;

      break;
    }

    case PREVIOUS_SIBLING: {
      if(!property_enumeration_setpos(it, it->idx - 1))
        return JS_UNDEFINED;

      break;
    }
  }

  ret = JS_UNDEFINED;

  if(it) {
    switch(w->tag_mask & RETURN_MASK) {
      case RETURN_VALUE: {
        ret = property_enumeration_value(it, ctx);
        break;
      }

      case RETURN_PATH: {
        ret = property_recursion_path(&w->hier, ctx);
        break;
      }

      case RETURN_VALUE_PATH:
      default: {
        ret = JS_NewArray(ctx);
        JS_SetPropertyUint32(ctx, ret, 0, property_enumeration_value(it, ctx));
        JS_SetPropertyUint32(ctx, ret, 1, property_recursion_path(&w->hier, ctx));
        break;
      }
    }
  }

  if(JS_IsFunction(ctx, w->transform)) {
    JSValue args[] = {
        ret,
        property_recursion_path(&w->hier, ctx),
        this_val,
    };

    ret = JS_Call(ctx, w->transform, JS_UNDEFINED, 3, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
  }

  return ret;
}

static JSValue
js_tree_walker_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue ret = JS_UNDEFINED;
  TreeWalker* w;
  PropertyEnumeration* it = 0;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;

  if(!vector_empty(&w->hier))
    it = vector_back(&w->hier, sizeof(PropertyEnumeration));

  switch(magic) {
    case PROP_ROOT: {
      if(!vector_empty(&w->hier)) {
        it = vector_begin(&w->hier);
        ret = JS_DupValue(ctx, it->obj);
      }

      break;
    }

    case PROP_CURRENT_NODE: {
      if(it)
        ret = property_enumeration_value(it, ctx);

      break;
    }

    case PROP_CURRENT_KEY: {
      if(it)
        ret = property_enumeration_key(it, ctx);

      break;
    }

    case PROP_CURRENT_PATH: {
      ret = property_recursion_path(&w->hier, ctx);
      break;
    }

    case PROP_DEPTH: {
      ret = JS_NewUint32(ctx, vector_size(&w->hier, sizeof(PropertyEnumeration)) - 1);
      break;
    }

    case PROP_INDEX: {
      ret = JS_NewUint32(ctx, property_enumeration_index(it));
      break;
    }

    case PROP_LENGTH: {
      ret = JS_NewUint32(ctx, property_enumeration_length(it));
      break;
    }

    case PROP_TAG_MASK: {
      ret = JS_NewUint32(ctx, w->tag_mask);
      break;
    }

    case PROP_FILTER: {
      ret = JS_DupValue(ctx, w->filter);
      break;
    }
  }

  return ret;
}

static JSValue
js_tree_walker_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  TreeWalker* w;
  PropertyEnumeration* it;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;

  if(!(it = vector_back(&w->hier, sizeof(PropertyEnumeration))))
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
  TreeWalker* w;

  if((w = JS_GetOpaque(val, js_tree_walker_class_id)))
    tree_walker_free(w, rt);
}

static JSValue
js_tree_iterator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  TreeWalker* w;
  JSValue proto, obj = JS_UNDEFINED;
  int argi = 1;

  if(!(w = tree_walker_new(ctx)))
    return JS_EXCEPTION;

  vector_init(&w->hier, ctx);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_tree_iterator_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, w);

  if(argc > 0 && JS_IsObject(argv[0]))
    /*it =*/tree_walker_setroot(w, ctx, argv[0]);

  if(argi < argc && JS_IsNumber(argv[argi]))
    JS_ToUint32(ctx, &w->tag_mask, argv[argi++]);

  if(argi < argc && JS_IsFunction(ctx, argv[argi]))
    w->filter = JS_DupValue(ctx, argv[argi++]);

  if(argi < argc && JS_IsFunction(ctx, argv[argi]))
    w->transform = JS_DupValue(ctx, argv[argi++]);

  return obj;

fail:
  js_free(ctx, w);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_tree_iterator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  PropertyEnumeration* it;
  JSValue ret = JS_UNDEFINED;
  TreeWalker* w = JS_GetOpaque(this_val, js_tree_iterator_class_id);

  for(;;) {
    if((it = js_tree_walker_next(ctx, w, this_val, argc > 0 ? argv[0] : JS_UNDEFINED))) {
      *pdone = FALSE;

      switch(w->tag_mask & RETURN_MASK) {
        case RETURN_VALUE: ret = property_enumeration_value(it, ctx); break;
        case RETURN_PATH: ret = property_recursion_path(&w->hier, ctx); break;
        case RETURN_VALUE_PATH:
        default: {
          ret = JS_NewArray(ctx);
          JS_SetPropertyUint32(ctx, ret, 0, property_enumeration_value(it, ctx));
          JS_SetPropertyUint32(ctx, ret, 1, property_recursion_path(&w->hier, ctx));
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
  TreeWalker* w;

  if((w = JS_GetOpaque(val, js_tree_iterator_class_id)))
    tree_walker_free(w, rt);
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
    JS_CGETSET_MAGIC_DEF("root", js_tree_walker_get, 0, PROP_ROOT),
    JS_CGETSET_MAGIC_DEF("currentNode", js_tree_walker_get, 0, PROP_CURRENT_NODE),
    JS_CGETSET_MAGIC_DEF("currentKey", js_tree_walker_get, 0, PROP_CURRENT_KEY),
    JS_CGETSET_MAGIC_DEF("currentPath", js_tree_walker_get, 0, PROP_CURRENT_PATH),
    JS_CGETSET_MAGIC_DEF("depth", js_tree_walker_get, 0, PROP_DEPTH),
    JS_CGETSET_MAGIC_DEF("index", js_tree_walker_get, js_tree_walker_set, PROP_INDEX),
    JS_CGETSET_MAGIC_DEF("length", js_tree_walker_get, 0, PROP_LENGTH),
    JS_CGETSET_MAGIC_DEF("tagMask", js_tree_walker_get, js_tree_walker_set, PROP_TAG_MASK),
    JS_CGETSET_MAGIC_FLAGS_DEF("filter", js_tree_walker_get, 0, PROP_FILTER, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("flags", js_tree_walker_get, js_tree_walker_set, PROP_FLAGS),
    JS_CFUNC_DEF("toString", 0, js_tree_walker_tostring),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TreeWalker", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_tree_walker_static_funcs[] = {
    JS_CONSTANT(TYPE_UNDEFINED),
    JS_CONSTANT(TYPE_NULL),
    JS_CONSTANT(TYPE_BOOL),
    JS_CONSTANT(TYPE_INT),
    JS_CONSTANT(TYPE_OBJECT),
    JS_CONSTANT(TYPE_STRING),
    JS_CONSTANT(TYPE_SYMBOL),
#ifdef CONFIG_BIGNUM
    JS_CONSTANT(TYPE_BIG_FLOAT),
    JS_CONSTANT(TYPE_BIG_DECIMAL),
#endif
    JS_CONSTANT(TYPE_BIG_INT),
    JS_CONSTANT(TYPE_ALL),
    JS_CONSTANT(TYPE_PRIMITIVE),
    JS_CONSTANT(RETURN_VALUE),
    JS_CONSTANT(RETURN_PATH),
    JS_CONSTANT(RETURN_VALUE_PATH),
    JS_CONSTANT(FILTER_ACCEPT),
    JS_CONSTANT(FILTER_REJECT),
    JS_CONSTANT(FILTER_SKIP),
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
  JS_SetPropertyFunctionList(ctx, tree_iterator_proto, js_tree_iterator_proto_funcs, countof(js_tree_iterator_proto_funcs));
  JS_SetClassProto(ctx, js_tree_iterator_class_id, tree_iterator_proto);

  tree_iterator_ctor = JS_NewCFunction2(ctx, js_tree_iterator_constructor, "TreeIterator", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, tree_iterator_ctor, tree_iterator_proto);
  JS_SetPropertyFunctionList(ctx, tree_iterator_ctor, js_tree_walker_static_funcs, countof(js_tree_walker_static_funcs));

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

  if((m = JS_NewCModule(ctx, module_name, js_tree_walker_init))) {
    JS_AddModuleExport(ctx, m, "TreeWalker");
    JS_AddModuleExport(ctx, m, "TreeIterator");
  }

  return m;
}

/**
 * @}
 */
