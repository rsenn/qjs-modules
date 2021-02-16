#include "quickjs.h"
#include "cutils.h"
#include "libregexp.h"
#include "property-enumeration.h"
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
enum tree_walker_getters {
  PROP_ROOT = 0,
  PROP_CURRENT_NODE,
  PROP_CURRENT_KEY,
  PROP_CURRENT_PATH,
  PROP_DEPTH,
  PROP_INDEX,
  PROP_LENGTH,
  PROP_TAG_MASK,
  PROP_EXPR,
  PROP_FLAGS
};
enum tree_walker_types {
  TYPE_UNDEFINED = 0,
  TYPE_NULL,        // 1
  TYPE_BOOL,        // 2
  TYPE_INT,         // 3
  TYPE_OBJECT,      // 4
  TYPE_STRING,      // 5
  TYPE_SYMBOL,      // 6
  TYPE_BIG_FLOAT,   // 7
  TYPE_BIG_INT,     // 8
  TYPE_BIG_DECIMAL, // 9
  TYPE_FUNCTION = 16,
  TYPE_ARRAY = 17

};
enum tree_walker_mask {
  MASK_UNDEFINED = (1 << TYPE_UNDEFINED),
  MASK_NULL = (1 << TYPE_NULL),
  MASK_BOOL = (1 << TYPE_BOOL),
  MASK_INT = (1 << TYPE_INT),
  MASK_OBJECT = (1 << TYPE_OBJECT),
  MASK_STRING = (1 << TYPE_STRING),
  MASK_SYMBOL = (1 << TYPE_SYMBOL),
  MASK_BIG_FLOAT = (1 << TYPE_BIG_FLOAT),
  MASK_BIG_INT = (1 << TYPE_BIG_INT),
  MASK_BIG_DECIMAL = (1 << TYPE_BIG_DECIMAL),
  MASK_PRIMITIVE = (MASK_UNDEFINED | MASK_NULL | MASK_BOOL | MASK_INT | MASK_STRING | MASK_SYMBOL | MASK_BIG_FLOAT |
                    MASK_BIG_INT | MASK_BIG_DECIMAL),
  MASK_ALL = (MASK_PRIMITIVE | MASK_OBJECT),
  MASK_FUNCTION = (1 << TYPE_FUNCTION),
  MASK_ARRAY = (1 << TYPE_ARRAY),

};

enum tree_walker_flags { MATCH_KEY = 1, MATCH_VALUE = 2 };


typedef struct {
  // JSValue current;
  vector frames;
  uint32_t tag_mask;
  uint32_t flags;
  uint8_t* re_byte;
  int re_byte_len;
} TreeWalker;


static void
js_value_dump(JSContext* ctx, JSValue value, vector* vec) {
  const char* str;
  size_t len;

  if(JS_IsArray(ctx, value)) {
    vector_cats(vec, "[object Array]");
  } else {
    str = JS_ToCStringLen(ctx, &len, value);
    vector_catb(vec, str, len);
    JS_FreeCString(ctx, str);
  }
}

static int32_t
js_value_type(JSValueConst value) {
  switch(JS_VALUE_GET_TAG(value)) {
    case JS_TAG_UNDEFINED: return TYPE_UNDEFINED;
    case JS_TAG_NULL: return TYPE_NULL;
    case JS_TAG_BOOL: return TYPE_BOOL;
    case JS_TAG_INT: return TYPE_INT;
    case JS_TAG_OBJECT: return TYPE_OBJECT;
    case JS_TAG_STRING: return TYPE_STRING;
    case JS_TAG_SYMBOL: return TYPE_SYMBOL;
    case JS_TAG_BIG_FLOAT: return TYPE_BIG_FLOAT;
    case JS_TAG_BIG_INT: return TYPE_BIG_INT;
    case JS_TAG_BIG_DECIMAL: return TYPE_BIG_DECIMAL;
  }
  return -1;
}

static void
tree_walker_reset(TreeWalker* wc, JSContext* ctx) {
  PropertyEnumeration *s = vector_begin(&wc->frames), *e = vector_end(&wc->frames);

  while(s < e) property_enumeration_free(s++, JS_GetRuntime(ctx));

  vector_clear(&wc->frames);

  wc->flags = MATCH_KEY | MATCH_VALUE;
  wc->tag_mask = MASK_ALL;
  /*  JS_FreeValue(ctx, wc->current);
    wc->current = JS_UNDEFINED;*/
}

static PropertyEnumeration*
tree_walker_setroot(TreeWalker* wc, JSContext* ctx, JSValueConst object) {
  PropertyEnumeration* it;

  if(!JS_IsObject(object)) {
    JS_ThrowTypeError(ctx, "not an object");
    return 0;
  }

  tree_walker_reset(wc, ctx);
  // wc->current = JS_DupValue(ctx, object);

  if((it = property_enumeration_push(&wc->frames)))
    property_enumeration_init(it, ctx, object, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY);
  return it;
}

static int
tree_walker_setregexp(TreeWalker* wc, JSContext* ctx, const char* str) {
  char error_msg[64];
  int re_flags = /*LRE_FLAG_GLOBAL | */ LRE_FLAG_IGNORECASE;

  wc->re_byte = lre_compile(&wc->re_byte_len, error_msg, sizeof(error_msg), str, strlen(str), re_flags, ctx);
  if(!wc->re_byte)
    return 0;

  return 1;
}

static int
tree_walker_testregexp(TreeWalker* wc, JSContext* ctx, const char* str) {
  uint8_t* capture = 0;
  int ret;
  ret = lre_exec(&capture, wc->re_byte, (const uint8_t*)str, 0, strlen(str), 0, ctx);
  return ret == 1;
}

static int
tree_walker_testregexp_value(TreeWalker* wc, JSContext* ctx, JSValue value) {
  int ret;
  size_t len;
  const char* str;
  str = JS_ToCStringLen(ctx, &len, value);
  ret = tree_walker_testregexp(wc, ctx, str);
  JS_FreeCString(ctx, str);
  return ret;
}

static PropertyEnumeration*
tree_walker_descend(TreeWalker* wc, JSContext* ctx) {
  PropertyEnumeration* it;
  JSValue value;

  assert(!vector_empty(&wc->frames));
  it = vector_back(&wc->frames, sizeof(PropertyEnumeration));
  value = property_enumeration_value(it, ctx);

  if(!JS_IsObject(value)) {
    JS_FreeValue(ctx, value);
    JS_ThrowTypeError(ctx, "not an object");
    return 0;
  }
  if((it = property_enumeration_push(&wc->frames)))
    property_enumeration_init(it, ctx, value, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY);

  /* JS_FreeValue(ctx, wc->current);
  wc->current = it->idx < it->tab_atom_len ? property_enumeration_value(it, ctx) : JS_UNDEFINED;*/

  return it;
}

static PropertyEnumeration*
tree_walker_ascend(TreeWalker* wc, JSContext* ctx) {
  return property_enumeration_pop(&wc->frames,ctx);
}

static JSValue
tree_walker_path(TreeWalker* wc, JSContext* ctx) {
  PropertyEnumeration* it;

  JSValue ret;
  size_t i = 0;

  ret = JS_NewArray(ctx);

  vector_foreach_t(&wc->frames, it) {
    JSValue key = property_enumeration_key(it, ctx);

    if(it->is_array) {
      int64_t idx;
      JS_ToInt64(ctx, &idx, key);
      JS_FreeValue(ctx, key);
      key = JS_NewInt64(ctx, idx);
    }
    JS_SetPropertyUint32(ctx, ret, i++, key);
  }

  // JS_FreeValue(ctx, ret);

  return ret;
}

static void
tree_walker_dump(TreeWalker* wc, JSContext* ctx, vector* vec) {
  PropertyEnumeration* it;
  size_t i;
  vector_cats(vec, "TreeWalker {\n  current: ");
  // js_value_dump(ctx, wc->current, vec);
  vector_printf(vec, ",\n  frames: (%u) [\n    ", vector_size(&wc->frames, sizeof(PropertyEnumeration)));

  i = 0;
  vector_foreach_t(&wc->frames, it) {
    vector_cats(vec, i > 0 ? ",\n    " : "");
    property_enumeration_dump(it, ctx, vec);
    i++;
  }
  vector_cats(vec, "\n  ]\n}");
}

static JSValue
js_tree_walker_ctor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  TreeWalker* wc;
  PropertyEnumeration* it = 0;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  if(!(wc = js_mallocz(ctx, sizeof(TreeWalker))))
    return JS_EXCEPTION;

  tree_walker_reset(wc, ctx);

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
js_tree_walker_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  TreeWalker* wc;
  vector v = VECTOR_INIT();
  JSValue ret;
  if(!(wc = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;

  // dbuf_init2(&dbuf, ctx, (DynBufReallocFunc*)&js_realloc);
  tree_walker_dump(wc, ctx, &v);
  vector_cat0(&v);
  ret = JS_NewStringLen(ctx, v.data, v.size - 1);
  vector_free(&v);

  return ret;
}

static PropertyEnumeration*
js_tree_walker_next(JSContext* ctx, TreeWalker* wc) {
  PropertyEnumeration* it;
  JSValue value = JS_UNDEFINED;
  int32_t type;
  it = vector_back(&wc->frames, sizeof(PropertyEnumeration));

  for(;;) {
    value = property_enumeration_value(it, ctx);
    type = JS_VALUE_GET_TAG(value);
    JS_FreeValue(ctx, value);

    if(type == JS_TAG_OBJECT) {
      it = tree_walker_descend(wc, ctx);
      if(it && property_enumeration_setpos(it, 0))
        goto end;
    } else {
      if(property_enumeration_setpos(it, it->idx + 1))
        goto end;
    }

    for(;;) {
      if((it = tree_walker_ascend(wc, ctx)) == 0)
        goto end;
      if(property_enumeration_setpos(it, it->idx + 1))
        goto end;
    }

  end:
    if(!it)
      break;

    value = property_enumeration_value(it, ctx);
    type = js_value_type(value);

    if(wc->tag_mask && 0 == (wc->tag_mask & (1 << type))) {
      JS_FreeValue(ctx, value);
      continue;
    }

    if(wc->flags && wc->re_byte) {
      int ret = 0;

      if(type != TYPE_OBJECT && (wc->flags & MATCH_VALUE))
        ret = tree_walker_testregexp_value(wc, ctx, value);

      if(!ret && (wc->flags & MATCH_KEY) && it->idx < it->tab_atom_len) {
        const char* str = JS_AtomToCString(ctx, property_enumeration_atom(it));
        ret = tree_walker_testregexp(wc, ctx, str);
        /*fprintf(stderr, "atom: %s match: %i\n", str, ret);
        fflush(stderr);*/
        JS_FreeCString(ctx, str);
      }

      if(wc->flags && ret == 0) {
        JS_FreeValue(ctx, value);
        continue;
      }
    }
    JS_FreeValue(ctx, value);
    break;
  }
  return it;
}

static JSValue
js_tree_walker_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  TreeWalker* wc;
  PropertyEnumeration* it;

  if(!(wc = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;

  if(vector_empty(&wc->frames))
    return JS_UNDEFINED;

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

    it = js_tree_walker_next(ctx, wc);

    /*    JSValue value = property_enumeration_value(it, ctx);

        if(JS_IsObject(value)) {
          it = tree_walker_descend(wc, ctx);
          if(it && property_enumeration_setpos(it, 0))
            goto end;
        } else {
          if(property_enumeration_setpos(it, it->idx + 1))
            goto end;
        }
        for(;;) {
          if((it = tree_walker_ascend(wc, ctx)) == 0)
            goto end;
          if(property_enumeration_setpos(it, it->idx + 1))
            break;
        }
      end:
        JS_FreeValue(ctx, value);*/
  }
  switch(magic) {
    case FIRST_CHILD: {
      if((it = tree_walker_descend(wc, ctx)) == 0 || !property_enumeration_setpos(it, 0))
        return JS_UNDEFINED;
      break;
    }
    case LAST_CHILD: {
      if((it = tree_walker_descend(wc, ctx)) == 0 || !property_enumeration_setpos(it, -1))
        return JS_UNDEFINED;
      break;
    }
    case NEXT_SIBLING: {
      if(!property_enumeration_setpos(it, it->idx + 1))
        return JS_UNDEFINED;
      break;
    }
    case PARENT_NODE: {
      if((it = tree_walker_ascend(wc, ctx)) == 0)
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
  TreeWalker* wc;
  PropertyEnumeration* it;

  if(!(wc = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;
  if(!(it = vector_back(&wc->frames, sizeof(PropertyEnumeration))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_ROOT: {
      if(!vector_empty(&wc->frames)) {
        it = vector_begin(&wc->frames);
        return JS_DupValue(ctx, it->obj);
      }
      break;
    }
    case PROP_CURRENT_NODE: {
      return property_enumeration_value(it, ctx);
      /* return JS_DupValue(ctx, wc->current);
         break;*/
    }
    case PROP_CURRENT_KEY: {
      return property_enumeration_key(it, ctx);
    }
    case PROP_CURRENT_PATH: {
      return tree_walker_path(wc, ctx);
    }
    case PROP_DEPTH: {
      return JS_NewUint32(ctx, vector_size(&wc->frames, sizeof(PropertyEnumeration)) - 1);
    }
    case PROP_INDEX: {
      return JS_NewUint32(ctx, it->idx);
    }
    case PROP_LENGTH: {
      return JS_NewUint32(ctx, it->tab_atom_len);
    }
    case PROP_TAG_MASK: {
      return JS_NewUint32(ctx, wc->tag_mask);
    }
    case PROP_FLAGS: {
      return JS_NewUint32(ctx, wc->flags);
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_tree_walker_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  TreeWalker* wc;
  PropertyEnumeration* it;

  if(!(wc = JS_GetOpaque2(ctx, this_val, js_tree_walker_class_id)))
    return JS_EXCEPTION;
  if(!(it = vector_back(&wc->frames, sizeof(PropertyEnumeration))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_TAG_MASK: {
      uint32_t tag_mask = 0;
      JS_ToUint32(ctx, &tag_mask, value);
      wc->tag_mask = tag_mask;
      break;
    }
    case PROP_FLAGS: {
      uint32_t flags = 0;
      JS_ToUint32(ctx, &flags, value);
      wc->flags = flags;
      break;
    }
    case PROP_EXPR: {
      const char* str = JS_ToCString(ctx, value);
      tree_walker_setregexp(wc, ctx, str);
      JS_FreeCString(ctx, str);
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
  TreeWalker* wc = JS_GetOpaque(val, js_tree_walker_class_id);
  if(wc) {
    PropertyEnumeration* it;

    uint32_t i, n = vector_size(&wc->frames, sizeof(PropertyEnumeration));

    //    JS_FreeValueRT(rt, wc->current);

    for(i = 0; i < n; i++) {
      it = vector_at(&wc->frames, sizeof(PropertyEnumeration), i);
      __JS_FreeValueRT(rt, it->obj);
      property_enumeration_free(it, rt);
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
    JS_CGETSET_MAGIC_DEF("index", js_tree_walker_get, NULL, PROP_INDEX),
    JS_CGETSET_MAGIC_DEF("length", js_tree_walker_get, NULL, PROP_LENGTH),
    JS_CGETSET_MAGIC_DEF("tagMask", js_tree_walker_get, js_tree_walker_set, PROP_TAG_MASK),
    JS_CGETSET_MAGIC_DEF("expr", js_tree_walker_get, js_tree_walker_set, PROP_EXPR),
    JS_CGETSET_MAGIC_DEF("flags", js_tree_walker_get, js_tree_walker_set, PROP_FLAGS),
    JS_CFUNC_DEF("toString", 0, js_tree_walker_tostring),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TreeWalker", JS_PROP_CONFIGURABLE)};

static const JSCFunctionListEntry js_tree_walker_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("from", 1, js_tree_walker_funcs, 0),
    JS_PROP_INT32_DEF("MASK_UNDEFINED", MASK_UNDEFINED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_NULL", MASK_NULL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_BOOL", MASK_BOOL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_INT", MASK_INT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_OBJECT", MASK_OBJECT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_STRING", MASK_STRING, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_SYMBOL", MASK_SYMBOL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_BIG_FLOAT", MASK_BIG_FLOAT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_BIG_INT", MASK_BIG_INT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_BIG_DECIMAL", MASK_BIG_DECIMAL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_ALL", MASK_ALL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_PRIMITIVE", MASK_PRIMITIVE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MATCH_KEY", MATCH_KEY, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MATCH_VALUE", MATCH_VALUE, JS_PROP_ENUMERABLE)};

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
