#include "defines.h"
#include "quickjs-list.h"
#include "quickjs-predicate.h"
#include "buffer-utils.h"
#include "utils.h"
#include "iteration.h"
#include <list.h>
#include <string.h>
#include "debug.h"

/**
 * \addtogroup quickjs-list
 * @{
 */
typedef union Node {
  struct list_head link;
  struct {
    union Node *prev, *next;
    int ref_count;
    JSValue value;
  };
} Node;

typedef union {
  Node node;
  struct list_head header;
  struct {
    union Node *tail, *head;
    int ref_count;
    JSValue _dummy;
    size_t size;
  };
} List;

typedef enum {
  NORMAL = 0,
  REVERSE = 1,
} IteratorType;

typedef enum {
  YIELD_KEY,
  YIELD_VALUE,
  YIELD_KEY_AND_VALUE,
} IteratorYield;

typedef struct {
  /*List* list;*/
  Node *node, *header;
  IteratorType iterator_type;
  IteratorYield kind;
  int64_t index;
} ListIterator;

typedef int64_t FindCall(List*, JSValueConst, JSValueConst, Node**, JSContext*);

static JSValue js_node_wrap(JSContext*, JSValueConst, Node*);
static JSValue js_list_wrap(JSContext*, JSValueConst, List*);

VISIBLE JSClassID js_list_class_id = 0, js_list_iterator_class_id = 0, js_node_class_id = 0;
static JSValue list_proto, list_ctor, list_iterator_proto, list_iterator_ctor, node_proto, node_ctor;

static inline List*
js_list_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_list_class_id);
}

static inline List*
js_list_data(JSValueConst value) {
  return JS_GetOpaque(value, js_list_class_id);
}

static inline Node*
js_node_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_node_class_id);
}

static inline Node*
js_node_data(JSValueConst value) {
  return JS_GetOpaque(value, js_node_class_id);
}

static Node*
node_new(JSContext* ctx, JSValueConst value) {
  Node* node;

  if((node = js_malloc(ctx, sizeof(Node)))) {
    node->next = NULL;
    node->prev = NULL;
    node->value = JS_DupValue(ctx, value);
    node->ref_count = 1;
  }

  return node;
}

static void
node_free_rt(Node* node, JSRuntime* rt) {
  if(--node->ref_count == 0) {
    if(!JS_IsUninitialized(node->value)) {
      JS_FreeValueRT(rt, node->value);
      node->value = JS_UNINITIALIZED;
    }

    js_free_rt(rt, node);
  }
}

static inline void
node_free(Node* node, JSContext* ctx) {
  node_free_rt(node, JS_GetRuntime(ctx));
}

static inline Node*
node_dup(Node* node) {
  ++node->ref_count;

  return node;
}

static inline BOOL
node_linked(Node* node) {
  return !(list_empty(&node->link) || (node->next == NULL && node->prev == NULL));
}

/**
 * @brief Call a JS function for a specific list node.
 *
 * JS function will be called with arguments:
 *   fn(node, index, List)
 *
 * @return JSValue the function returned
 */
static JSValue
node_call(Node* node, JSValueConst fn, JSValueConst list_obj, int64_t i, JSContext* ctx) {
  JSValueConst args[] = {
      node->value,
      JS_NewInt64(ctx, i),
      list_obj,
  };
  JSValue ret = JS_Call(ctx, fn, JS_UNDEFINED, countof(args), args);

  JS_FreeValue(ctx, args[1]);

  return ret;
}

/**
 * @brief Call a predicate function for a specific List node.
 *
 * @return TRUE if predicate fulfilled, FALSE otherwise
 */
static BOOL
node_predicate(Node* node, JSValueConst pred, JSValueConst list_obj, int64_t i, JSContext* ctx) {
  JSValue ret = node_call(node, pred, list_obj, i, ctx);
  BOOL result = JS_ToBool(ctx, ret);

  JS_FreeValue(ctx, ret);
  return result;
}

static Node*
node_get(JSContext* ctx, JSValueConst value) {
  ListIterator* iter;
  Node* node;
  /*List* list;

  if((list = js_list_data(value))) {
    node = &list->node;
  } else*/
  if((iter = JS_GetOpaque(value, js_list_iterator_class_id))) {
    node = iter->node;
  } else if(!(node = JS_GetOpaque(value, js_node_class_id))) {
    JS_ThrowInternalError(ctx, "argument 1 must be a ListIterator or ListNode");
  }

  return node;
}

static List*
list_new(JSContext* ctx) {
  List* list;

  if((list = js_malloc(ctx, sizeof(List)))) {
    init_list_head(&list->header);

    list->ref_count = 1;
    list->size = 0;
    list->_dummy = JS_UNINITIALIZED;
  }

  return list;
}

/**
 * @brief Insert a new node after \param prev or at the head
 */
static Node*
list_insert(List* list, JSValueConst value, Node* prev, JSContext* ctx) {
  Node* node;

  if((node = node_new(ctx, value))) {
    list_add(&node->link, prev ? &prev->link : &list->header);

    ++list->size;
  }

  return node;
}

/**
 * @brief Insert a new node before \param next or at the tail
 */
static Node*
list_insert_before(List* list, JSValueConst value, Node* next, JSContext* ctx) {
  Node* node;

  if((node = node_new(ctx, value))) {
    list_add_tail(&node->link, next ? &next->link : &list->header);

    ++list->size;
  }

  return node;
}

/**
 * @brief Erase a \param node from a \param list
 */
static void
list_erase(List* list, Node* node, JSContext* ctx) {
  list_del(&node->link);
  --list->size;

  node_free(node, ctx);
}

/**
 * @brief Return value from \param node and erase it from the \param list
 *
 * @returns The value of the node
 */
static JSValue
list_remove(List* list, Node* node, JSContext* ctx) {
  JSValue ret = JS_IsUninitialized(node->value) ? JS_UNDEFINED : JS_DupValue(ctx, node->value);

  list_erase(list, node, ctx);
  return ret;
}

static inline BOOL
list_has(List* list, int64_t index) {
  int64_t size = (int64_t)list->size;
  return index >= -size && index < size;
}

static Node*
list_at(List* list, int64_t index) {
  index = WRAP_NUM(index, (int64_t)list->size);

  if(index < (int64_t)list->size && index >= 0) {
    struct list_head* ptr;
    int64_t from_back = (list->size - 1) - index;

    if(from_back < index)
      list_for_each_prev(ptr, &list->header) {
        if(from_back-- == 0)
          return list_entry(ptr, Node, link);
      }
    else
      list_for_each(ptr, &list->header) {
        if(index-- == 0)
          return list_entry(ptr, Node, link);
      }
  }

  return NULL;
}

static void
list_clear(List* list, JSRuntime* rt) {
  struct list_head *ptr, *ptr2;

  list_for_each_safe(ptr, ptr2, &list->header) node_free_rt(list_entry(ptr, Node, link), rt);

  init_list_head(&list->header);
  list->size = 0;
}

static void
list_free(List* list, JSRuntime* rt) {
  if(--list->ref_count == 0) {
    list_clear(list, rt);
    js_free_rt(rt, list);
  }
}

static inline List*
list_dup(List* list) {
  node_dup(&list->node);

  return list;
}

static int64_t
list_find_forward(List* list, JSValueConst list_obj, JSValueConst fn, Node** nptr, JSContext* ctx) {
  struct list_head* ptr;
  int64_t i = -1;

  list_for_each(ptr, &list->header) {
    Node* node = list_entry(ptr, Node, link);

    if(node_predicate(node, fn, list_obj, ++i, ctx)) {
      if(nptr)
        *nptr = node;

      return i;
    }
  }

  return -1;
}

static int64_t
list_find_reverse(List* list, JSValueConst list_obj, JSValueConst fn, Node** nptr, JSContext* ctx) {
  struct list_head* ptr;
  int64_t i = list->size;

  list_for_each_prev(ptr, &list->header) {
    Node* node = list_entry(ptr, Node, link);

    if(node_predicate(node, fn, list_obj, --i, ctx)) {
      if(nptr)
        *nptr = node;

      return i;
    }
  }

  return -1;
}

static JSValue
list_find_value(List* list, JSValueConst list_obj, JSValueConst fn, FindCall* findcall, JSContext* ctx) {
  Node* node = 0;

  if(findcall(list, list_obj, fn, &node, ctx) != -1)
    return JS_DupValue(ctx, node->value);

  return JS_UNDEFINED;
}

static BOOL
list_append(List* list, JSValueConst iterable, JSContext* ctx) {
  Iteration iter = ITERATION_INIT();

  if(!iteration_method_symbol(&iter, ctx, iterable, "iterator")) {
    iteration_reset(&iter, ctx);
    return FALSE;
  }

  while(!iteration_next(&iter, ctx)) {
    JSValue value = iteration_value(&iter, ctx);

    list_insert_before(list, value, NULL, ctx);
  }

  iteration_reset(&iter, ctx);
  return TRUE;
}

typedef struct {
  JSContext* ctx;
  JSValueConst fn;
} SortClosure;

static int
list_sort_cmp(struct list_head* a, struct list_head* b, void* opaque) {
  SortClosure* sc = opaque;
  JSValueConst args[] = {
      ((Node*)a)->value,
      ((Node*)b)->value,
  };

  JSValue ret = JS_Call(sc->ctx, sc->fn, JS_UNDEFINED, countof(args), args);
  return JS_IsBool(ret) ? !js_tobool_free(sc->ctx, ret) : js_toint32_free(sc->ctx, ret);
}

static void
list_sort(List* list, JSValueConst pred, JSContext* ctx) {
  SortClosure sc = {ctx, pred};
  __list_sort(&list->header, list_sort_cmp, &sc);
}

static ListIterator*
list_iterator_new(Node* node, Node* header, IteratorType type, JSContext* ctx) {
  ListIterator* iter;

  if((iter = js_malloc(ctx, sizeof(ListIterator)))) {
    iter->node = node_dup(node);
    iter->header = node_dup(header);
    iter->iterator_type = type;
    iter->kind = YIELD_VALUE;
    iter->index = type == REVERSE ? -1 : 0;
  }

  return iter;
}

static JSValue
list_iterator_value(ListIterator* it, JSContext* ctx) {
  JSValue ret;

  if(it->node == it->header)
    return JS_UNDEFINED;

  assert(!JS_IsUninitialized(it->node->value));

  Node* node = it->node;

  switch(it->kind) {
    case YIELD_KEY: {
      ret = JS_NewInt64(ctx, it->index);
      break;
    }
    case YIELD_VALUE: {
      ret = JS_DupValue(ctx, node->value);
      break;
    }
    case YIELD_KEY_AND_VALUE: {
      ret = JS_NewArray(ctx);
      JS_SetPropertyUint32(ctx, ret, 0, JS_NewInt64(ctx, it->index));
      JS_SetPropertyUint32(ctx, ret, 1, JS_DupValue(ctx, node->value));

      break;
    }
  }

  return ret;
}

static BOOL
list_iterator_skip(ListIterator* it, JSContext* ctx) {
  Node* node = it->node;

  if(node == it->header)
    return TRUE;

  assert(!JS_IsUninitialized(node->value));

  if(JS_IsUninitialized(node->value))
    return TRUE;

  switch(it->iterator_type) {
    case NORMAL: {
      it->node = node_dup(node->next);
      it->index++;
      break;
    }
    case REVERSE: {
      it->node = node_dup(node->prev);
      it->index--;
      break;
    }
  }

  node_free(node, ctx);

  return FALSE;
}

static BOOL
js_call_pred(JSContext* ctx, JSValueConst pred, JSValueConst a, JSValueConst b) {
  JSValueConst args[] = {a, b};

  JSValue ret = JS_Call(ctx, pred, JS_UNDEFINED, countof(args), args);
  return js_tobool_free(ctx, ret);
}

static JSValue
js_list_iterator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  List* list;
  IteratorType iterator_type = NORMAL;
  ListIterator* it;

  if(argc < 1)
    return JS_ThrowReferenceError(ctx, "argument 1 must be a List");

  if(!(list = js_list_data2(ctx, argv[0])))
    return JS_EXCEPTION;

  if(argc > 1)
    iterator_type = JS_ToBool(ctx, argv[1]) ? REVERSE : NORMAL;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_list_iterator_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  if(!(it = list_iterator_new(iterator_type == REVERSE ? list->tail : list->head, &list->node, iterator_type, ctx)))
    goto fail;

  JS_SetOpaque(obj, it);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_list_iterator_wrap(JSContext* ctx, JSValueConst proto, ListIterator* list_iterator) {
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_list_iterator_class_id);

  JS_SetOpaque(obj, list_iterator);
  return obj;
}

static JSValue
js_list_iterator_new(JSContext* ctx, Node* node, Node* header, IteratorType type) {
  ListIterator* iter;

  if(!(iter = list_iterator_new(node, header, type, ctx)))
    return JS_EXCEPTION;

  return js_list_iterator_wrap(ctx, list_iterator_proto, iter);
}

enum {
  ITERATOR_EQUALS,
  ITERATOR_COPY,
  ITERATOR_ACCESSIBLE,
};

static JSValue
js_list_iterator_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  ListIterator* it;
  JSValue ret = JS_UNDEFINED;

  if(!(it = JS_GetOpaque2(ctx, this_val, js_list_iterator_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case ITERATOR_EQUALS: {
      Node* node;

      if(!(node = node_get(ctx, argv[0])))
        return JS_EXCEPTION;

      ret = JS_NewBool(ctx, node == it->node);
      break;
    }
    case ITERATOR_COPY: {
      ListIterator* iter;

      ret = JS_NewObjectProtoClass(ctx, list_iterator_proto, js_list_iterator_class_id);

      if(JS_IsException(ret) || !(iter = list_iterator_new(it->node, it->header, it->iterator_type, ctx))) {
        JS_FreeValue(ctx, ret);
        return JS_EXCEPTION;
      }

      iter->kind = it->kind;
      iter->index = it->index;

      JS_SetOpaque(ret, iter);
      break;
    }
    case ITERATOR_ACCESSIBLE: {
      ret = JS_NewBool(ctx, !JS_IsUninitialized(it->node->value));
      break;
    }
  }

  return ret;
}

enum {
  ITERATOR_TYPE,
  ITERATOR_CONTAINER,
};

static JSValue
js_list_iterator_get(JSContext* ctx, JSValueConst this_val, int magic) {
  ListIterator* it;
  JSValue ret = JS_UNDEFINED;

  if(!(it = JS_GetOpaque2(ctx, this_val, js_list_iterator_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case ITERATOR_TYPE: {
      ret = JS_NewInt32(ctx, it->iterator_type);
      break;
    }
    case ITERATOR_CONTAINER: {
      if(JS_IsUninitialized(it->header->value))
        ret = js_list_wrap(ctx, list_proto, list_dup((List*)it->header));
      break;
    }
  }

  return ret;
}

static JSValue
js_list_iterator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  ListIterator* it;
  JSValue ret = JS_UNDEFINED;

  if(!(it = JS_GetOpaque2(ctx, this_val, js_list_iterator_class_id)))
    return JS_EXCEPTION;

  ret = list_iterator_value(it, ctx);

  *pdone = list_iterator_skip(it, ctx);

  return ret;
}

/*static JSValue
js_list_iterator_fn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  BOOL done = FALSE;
  JSValue ret = JS_NewObject(ctx);
  ListIterator* it = JS_GetOpaque(this_val, js_list_iterator_class_id);

  JS_SetPropertyStr(ctx, ret, "index", JS_NewInt64(ctx, it->index));
  JS_SetPropertyStr(ctx, ret, "value", js_list_iterator_next(ctx, this_val, argc, argv, &done, 0));
  JS_SetPropertyStr(ctx, ret, "done", JS_NewBool(ctx, done));

  return ret;
}*/

static void
js_list_iterator_finalizer(JSRuntime* rt, JSValue val) {
  ListIterator* it;

  if((it = JS_GetOpaque(val, js_list_iterator_class_id))) {
    node_free_rt(it->node, rt);
    node_free_rt(it->header, rt);
    js_free_rt(rt, it);
  }
}

static const JSCFunctionListEntry js_list_iterator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_list_iterator_next, 0),
    // JS_CFUNC_DEF("next", 0, js_list_iterator_fn),
    JS_CFUNC_MAGIC_DEF("equals", 1, js_list_iterator_method, ITERATOR_EQUALS),
    JS_CFUNC_MAGIC_DEF("copy", 0, js_list_iterator_method, ITERATOR_COPY),
    JS_CFUNC_MAGIC_DEF("isAccessible", 0, js_list_iterator_method, ITERATOR_ACCESSIBLE),
    JS_CGETSET_MAGIC_DEF("container", js_list_iterator_get, 0, ITERATOR_CONTAINER),
    JS_CGETSET_MAGIC_DEF("type", js_list_iterator_get, 0, ITERATOR_TYPE),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, (void*)&JS_DupValue),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ListIterator", JS_PROP_CONFIGURABLE),
};

static JSValue
js_list_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  List* list = 0;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_list_class_id);
  if(JS_IsException(obj))
    goto fail;

  if(!(list = list_new(ctx)))
    goto fail;

  if(argc > 0) {
    if(!list_append(list, argv[0], ctx)) {
      JS_ThrowTypeError(ctx, "argument 1 not iterable");
      goto fail2;
    }
  }

  JS_SetOpaque(obj, list);
  return obj;

fail2:
  js_free(ctx, list);

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_list_wrap(JSContext* ctx, JSValueConst proto, List* list) {
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_list_class_id);

  JS_SetOpaque(obj, list);
  return obj;
}

static JSValue
js_list_wrap_species(JSContext* ctx, JSValueConst this_val, List* list) {
  JSValue species = js_object_species(ctx, this_val);
  JSValue proto = JS_IsUndefined(species) ? JS_DupValue(ctx, list_proto) : JS_GetPropertyStr(ctx, species, "prototype");
  JSValue ret = js_list_wrap(ctx, proto, list);

  JS_FreeValue(ctx, proto);
  JS_FreeValue(ctx, species);
  return ret;
}

enum {
  LIST_CLEAR,
  LIST_BEGIN,
  LIST_END,
  LIST_RBEGIN,
  LIST_REND,
  LIST_ERASE,
  LIST_INSERT_BEFORE,
  LIST_INSERT_AFTER,
  LIST_UNIQUE,
  LIST_MERGE,
};

static JSValue
js_list_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int64_t index;
  List* list;
  JSValue ret = JS_UNDEFINED;

  if(!(list = js_list_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LIST_CLEAR: {
      list_clear(list, JS_GetRuntime(ctx));
      break;
    }
    case LIST_BEGIN: {
      ret = js_list_iterator_new(ctx, list->head, &list->node, NORMAL);
      break;
    }
    case LIST_END: {
      ret = js_list_iterator_new(ctx, &list->node, &list->node, NORMAL);
      break;
    }
    case LIST_RBEGIN: {
      ret = js_list_iterator_new(ctx, list->tail, &list->node, REVERSE);
      break;
    }
    case LIST_REND: {
      ret = js_list_iterator_new(ctx, &list->node, &list->node, REVERSE);
      break;
    }

    case LIST_ERASE: {
      Node *q, *p;

      if(!(p = node_get(ctx, argv[0])))
        return JS_EXCEPTION;

      assert(!JS_IsUninitialized(p->value));
      assert(p != &list->node);

      if(argc > 1) {
        Node* end;

        if(!(end = node_get(ctx, argv[1])))
          return JS_EXCEPTION;

        for(; (q = p->next, (p != &list->node && p != end)); p = q) {
          ;

          list_erase(list, p, ctx);
        }

      } else {
        q = p->next;

        list_erase(list, p, ctx);
      }

      ret = js_list_iterator_new(ctx, q, &list->node, NORMAL);
      break;
    }
    case LIST_INSERT_BEFORE:
    case LIST_INSERT_AFTER: {
      Node *node, *tmp;

      if(js_is_null_or_undefined(argv[0]))
        node = &list->node;
      else if(!(node = node_get(ctx, argv[0])))
        return JS_EXCEPTION;

      if(magic == LIST_INSERT_BEFORE) {
        tmp = node->prev;

        for(int i = 1; i < argc; i++)
          list_insert_before(list, argv[i], node, ctx);

        ret = js_list_iterator_new(ctx, tmp->next, &list->node, REVERSE);
      } else {
        tmp = node->next;
        for(int i = argc - 1; i >= 1; i++)
          list_insert(list, argv[i], node, ctx);

        ret = js_list_iterator_new(ctx, tmp->prev, &list->node, NORMAL);
      }

      break;
    }

    case LIST_UNIQUE: {
      for(Node* node = list->head; node != &list->node; node = node->next) {
        while(node->next != &list->node) {
          if(argc > 0) {
            JSValueConst args[] = {node->value, node->next->value};
            JSValue ret = JS_Call(ctx, argv[0], JS_UNDEFINED, countof(args), args);

            if(js_toint32_free(ctx, ret))
              break;
          } else {
            if(0 >= js_value_equals(ctx, node->value, node->next->value, FALSE))
              break;
          }

          list_remove(list, node->next, ctx);
        }
      }

      ret = JS_NewUint32(ctx, list->size);
      break;
    }

    case LIST_MERGE: {
      Node *node = list->head, *el;
      List* other;

      if(!(other = js_list_data2(ctx, argv[0])))
        return JS_EXCEPTION;

      JSValue pred = argc > 1 ? JS_DupValue(ctx, argv[0])
                              : JS_Eval(ctx, "(a, b) => a <= b", sizeof("(a, b) => a <= b") - 1, "-", 0);

      list_for_each(el, &other->node) {
        while(node != &list->node && js_call_pred(ctx, pred, node->value, el->value)) {
          node = node->next;
        }

        list_insert_before(list, el->value, node, ctx);
      }

      JS_FreeValue(ctx, pred);
      ret = JS_DupValue(ctx, this_val);
      break;
    }
  }
  return ret;
}

enum {
  LIST_PUSH,
  LIST_POP,
  LIST_UNSHIFT,
  LIST_SHIFT,
  LIST_AT,
  LIST_INDEX_OF,
  LIST_INCLUDES,
  LIST_LAST_INDEX_OF,
  LIST_FIND_INDEX,
  LIST_FIND_LAST_INDEX,
  LIST_FIND,
  LIST_FIND_LAST,
  LIST_CONCAT,
  LIST_SLICE,
  LIST_FILL,
  LIST_ROTATE,
  LIST_REVERSE,
  LIST_TOREVERSED,
  LIST_SPLICE,
  LIST_EVERY,
  LIST_FILTER,
  LIST_FOREACH,
  LIST_MAP,
  LIST_REDUCE,
  LIST_REDUCE_RIGHT,
  LIST_SOME,
  LIST_SORT,
};

static JSValue
js_list_method2(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int64_t index;
  List* list;
  JSValue ret = JS_UNDEFINED;

  if(!(list = js_list_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LIST_PUSH: {
      for(int i = 0; i < argc; i++)
        if(!list_insert_before(list, argv[i], NULL, ctx))
          return JS_EXCEPTION;

      ret = JS_NewInt64(ctx, list->size);
      break;
    }
    case LIST_POP: {
      Node* node = 0;

      if((node = list_last(&list->header, Node, link)))
        ret = list_remove(list, node, ctx);

      break;
    }
    case LIST_UNSHIFT: {
      for(int i = argc - 1; i >= 0; i--)
        if(!list_insert(list, argv[i], NULL, ctx))
          return JS_EXCEPTION;

      ret = JS_NewInt64(ctx, list->size);
      break;
    }
    case LIST_SHIFT: {
      Node* node = 0;

      if((node = list_first(&list->header, Node, link)))
        ret = list_remove(list, node, ctx);

      break;
    }
    /*case LIST_AT: {
      if((index = js_toindex_name(ctx, argv[0], list->size, "argument 1")) == -1)
        return JS_EXCEPTION;

      if((node = list_at(list, index)))
        ret = JS_DupValue(ctx, node->value);

      break;
    }*/
    case LIST_INCLUDES: {
      Node *n1, *n2;
      BOOL result = FALSE;

      for(n1 = list->head, n2 = list->tail; n1 != &list->node && n2 != &list->node; n1 = n1->next, n2 = n2->prev) {
        if(js_value_equals(ctx, n1->value, argv[0], FALSE) > 0 ||
           (n1 != n2 && js_value_equals(ctx, n2->value, argv[0], FALSE) > 0)) {
          result = TRUE;
          break;
        }

        if(n1 == n2)
          break;

        if(n1->next == n2 || n2->prev == n1)
          break;
      }

      ret = JS_NewBool(ctx, result);
      break;
    }
    case LIST_INDEX_OF: {
      Node* n;

      list_for_each(n, &list->node) if(0 < js_value_equals(ctx, n->value, argv[0], FALSE)) break;

      ret = js_list_iterator_new(ctx, n, &list->node, NORMAL);
      break;
    }
    case LIST_LAST_INDEX_OF: {
      Node* n;

      list_for_each_prev(n, &list->node) if(0 < js_value_equals(ctx, n->value, argv[0], FALSE)) break;

      ret = js_list_iterator_new(ctx, n, &list->node, REVERSE);
      break;
    }
    case LIST_CONCAT: {
      List* other;
      Node* node;

      if(!(other = list_new(ctx)))
        return JS_EXCEPTION;

      list_for_each(node, &list->node) list_insert_before(other, node->value, NULL, ctx);

      for(int i = 0; i < argc; i++) {
        if(!list_append(other, argv[i], ctx)) {
          list_free(other, JS_GetRuntime(ctx));

          return JS_ThrowTypeError(ctx, "argument %d not iterable", i);
        }
      }

      ret = js_list_wrap_species(ctx, this_val, other);
      break;
    }
    case LIST_SLICE: {
      List* other;
      Node *start, *end, *node;

      if(!(start = node_get(ctx, argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be ListNode");

      if(!(end = node_get(ctx, argv[1])))
        return JS_ThrowTypeError(ctx, "argument 2 must be ListNode");

      if(!(other = list_new(ctx)))
        return JS_EXCEPTION;

      for(node = start; node != end && node != &list->node; node = node->next) {
        list_insert_before(other, node->value, NULL, ctx);
      }

      ret = js_list_wrap_species(ctx, this_val, other);
      break;
    }
    case LIST_SPLICE: {
      List* other;
      Node *start, *end;

      if(!(start = node_get(ctx, argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be ListNode");

      if(!(end = node_get(ctx, argv[1])))
        return JS_ThrowTypeError(ctx, "argument 2 must be ListNode");

      if(!(other = list_new(ctx)))
        return JS_EXCEPTION;

      struct list_head removed = list_unlink(&start->link, &end->link);

      list_splice(&removed, &other->header);

      list->size -= other->size = list_size(&other->header);

      for(int i = 2; i < argc; i++)
        list_insert_before(list, argv[i], end, ctx);

      ret = js_list_wrap_species(ctx, this_val, other);
      break;
    }
    case LIST_FILL: {
      List* other;
      Node *node, *start, *end;

      if(!(start = node_get(ctx, argv[1])))
        return JS_ThrowTypeError(ctx, "argument 2 must be ListNode");

      if(!(end = node_get(ctx, argv[2])))
        return JS_ThrowTypeError(ctx, "argument 3 must be ListNode");

      if(!(other = list_new(ctx)))
        return JS_EXCEPTION;

      for(node = start; node != end && node != &list->node; node = node->next) {
        JS_FreeValue(ctx, node->value);
        node->value = JS_DupValue(ctx, argv[0]);
      }

      ret = JS_DupValue(ctx, this_val);
      break;
    }
    case LIST_ROTATE: {
      Node* node;

      if(!(node = node_get(ctx, argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be ListNode");

      if(JS_IsUninitialized(node->value))
        return JS_ThrowTypeError(ctx, "ListNode must not be a sentinel");

      list_del(&list->header);
      __list_add(&list->header, node->link.prev, &node->link);

      ret = JS_DupValue(ctx, this_val);
      break;
    }
    case LIST_REVERSE: {
      __list_reverse(&list->header);

      ret = JS_DupValue(ctx, this_val);
      break;
    }
    case LIST_TOREVERSED: {
      List* other;
      struct list_head* ptr;

      if(!(other = list_new(ctx)))
        return JS_EXCEPTION;

      list_for_each_prev(ptr, &list->header) list_insert_before(other, list_entry(ptr, Node, link)->value, NULL, ctx);

      ret = js_list_wrap_species(ctx, this_val, other);
      break;
    }
  }

  return ret;
}

static JSValue
js_list_predicate(JSContext* ctx, int argc, JSValueConst argv[]) {
  return argc > 1 ? js_function_bind_this(ctx, argv[0], argv[1]) : JS_DupValue(ctx, argv[0]);
}

static JSValue
js_list_search(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue pred, ret = JS_UNDEFINED;
  List* list;

  if(!(list = js_list_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!JS_IsFunction(ctx, argv[0]))
    return JS_ThrowTypeError(ctx, "argument 1 must be a function");

  pred = js_list_predicate(ctx, argc, argv);

  switch(magic) {
    case LIST_FIND_INDEX: {
      Node* node = 0;

      if(list_find_forward(list, this_val, pred, &node, ctx) != -1)
        ret = js_list_iterator_new(ctx, node, &list->node, NORMAL);
      //        ret = js_node_wrap(ctx, node_proto, node_dup(node));
      break;
    }
    case LIST_FIND_LAST_INDEX: {
      Node* node = 0;

      if(list_find_reverse(list, this_val, pred, &node, ctx) != -1)
        ret = js_list_iterator_new(ctx, node, &list->node, REVERSE);
      break;
    }
    case LIST_FIND: {
      ret = list_find_value(list, this_val, pred, list_find_forward, ctx);
      break;
    }
    case LIST_FIND_LAST: {
      ret = list_find_value(list, this_val, pred, list_find_reverse, ctx);
      break;
    }
  }

  JS_FreeValue(ctx, pred);
  return ret;
}

static JSValue
js_list_functional(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  List* list;
  JSValue pred, ret = JS_UNDEFINED;
  struct list_head* ptr;
  int64_t i = 0;

  if(!(list = js_list_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(argc < 1 || !JS_IsFunction(ctx, argv[0])) {
    return JS_ThrowTypeError(ctx, "argument 1 must be a function");
  }

  pred = js_list_predicate(ctx, argc, argv);

  switch(magic) {
    case LIST_EVERY: {
      ret = JS_TRUE;

      list_for_each(ptr, &list->header) {
        Node* node = list_entry(ptr, Node, link);

        if(!node_predicate(node, pred, this_val, i++, ctx)) {
          ret = JS_FALSE;
          break;
        }
      }

      break;
    }
    case LIST_SOME: {
      ret = JS_FALSE;

      list_for_each(ptr, &list->header) {
        Node* node = list_entry(ptr, Node, link);

        if(node_predicate(node, pred, this_val, i++, ctx)) {
          ret = JS_TRUE;
          break;
        }
      }

      break;
    }
    case LIST_FILTER: {
      List* other;

      if(!(other = list_new(ctx))) {
        ret = JS_EXCEPTION;
        break;
      }

      list_for_each(ptr, &list->header) {
        Node* node = list_entry(ptr, Node, link);

        if(node_predicate(node, pred, this_val, i++, ctx))
          list_insert_before(other, node->value, NULL, ctx);
      }

      ret = js_list_wrap_species(ctx, this_val, other);
      break;
    }
    case LIST_FOREACH: {
      list_for_each(ptr, &list->header) {
        Node* node = list_entry(ptr, Node, link);

        node_predicate(node, pred, this_val, i++, ctx);
      }

      break;
    }
    case LIST_MAP: {
      List* other;

      if(!(other = list_new(ctx))) {
        ret = JS_EXCEPTION;
        break;
      }

      list_for_each(ptr, &list->header) {
        Node* node = list_entry(ptr, Node, link);
        JSValue value = node_call(node, pred, this_val, i++, ctx);

        list_insert_before(other, value, NULL, ctx);
      }

      ret = js_list_wrap_species(ctx, this_val, other);
      break;
    }
    case LIST_REDUCE: {
      List* other;

      ret = JS_DupValue(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);

      if(!(other = list_new(ctx))) {
        ret = JS_EXCEPTION;
        break;
      }

      list_for_each(ptr, &list->header) {
        Node* node = list_entry(ptr, Node, link);
        JSValueConst args[] = {
            ret,
            node->value,
            JS_NewInt64(ctx, i++),
            this_val,
        };

        ret = JS_Call(ctx, argv[0], JS_UNDEFINED, countof(args), args);

        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[2]);
      }

      break;
    }
    case LIST_REDUCE_RIGHT: {
      List* other;

      ret = JS_DupValue(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);

      if(!(other = list_new(ctx))) {
        ret = JS_EXCEPTION;
        break;
      }

      i = list->size;

      list_for_each_prev(ptr, &list->header) {
        Node* node = list_entry(ptr, Node, link);
        JSValueConst args[] = {
            ret,
            node->value,
            JS_NewInt64(ctx, --i),
            this_val,
        };

        ret = JS_Call(ctx, argv[0], JS_UNDEFINED, countof(args), args);

        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[2]);
      }

      break;
    }
    case LIST_SORT: {
      JSValue pred =
          argc > 0 ? JS_DupValue(ctx, argv[0]) : JS_Eval(ctx, "(a, b) => a - b", sizeof("(a, b) => a - b") - 1, "-", 0);

      list_sort(list, pred, ctx);

      JS_FreeValue(ctx, pred);
      ret = JS_DupValue(ctx, this_val);
      break;
    }
  }

  JS_FreeValue(ctx, pred);
  return ret;
}

enum {
  LIST_LENGTH,
  LIST_ADDRESS,
};

static JSValue
js_list_get(JSContext* ctx, JSValueConst this_val, int magic) {
  List* list;
  JSValue ret = JS_UNDEFINED;

  if(!(list = js_list_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LIST_LENGTH: {
      ret = JS_NewInt64(ctx, list->size);
      break;
    }
    case LIST_ADDRESS: {
      char buf[32];

      snprintf(buf, sizeof(buf), "%p", list);
      ret = JS_NewString(ctx, buf);
      break;
    }
  }

  return ret;
}

static JSValue
js_list_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  List* list;
  ListIterator* it;
  JSValue ret = JS_UNDEFINED;

  if(!(list = js_list_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(it = list_iterator_new(list->head, &list->node, NORMAL, ctx)))
    return JS_EXCEPTION;

  it->kind = magic;

  return js_list_iterator_wrap(ctx, list_iterator_proto, it);
}

enum {
  STATIC_FROM = 0,
  STATIC_OF,
  STATIC_IS_LIST,
};

static JSValue
js_list_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  List* list;

  switch(magic) {
    case STATIC_FROM: {
      if(!(list = list_new(ctx)))
        return JS_EXCEPTION;

      if(!list_append(list, argv[0], ctx)) {
        list_free(list, JS_GetRuntime(ctx));
        return JS_ThrowTypeError(ctx, "argument 1 not iterable");
      }

      ret = js_list_wrap(ctx, list_proto, list);
      break;
    }
    case STATIC_OF: {
      if(!(list = list_new(ctx)))
        return JS_EXCEPTION;

      for(int i = 0; i < argc; i++)
        list_insert_before(list, argv[i], NULL, ctx);

      ret = js_list_wrap(ctx, list_proto, list);
      break;
    }
    case STATIC_IS_LIST: {
      ret = JS_NewBool(ctx, js_list_data(argv[0]) != NULL);
      break;
    }
  }

  return ret;
}

static void
js_list_finalizer(JSRuntime* rt, JSValue val) {
  List* list;

  if((list = js_list_data(val)))
    list_free(list, rt);
}

static int
js_list_get_own_property(JSContext* ctx, JSPropertyDescriptor* pdesc, JSValueConst obj, JSAtom prop) {
  List* list;
  int64_t index;

  if(!(list = js_list_data2(ctx, obj)))
    return FALSE;

  if(js_atom_is_index(ctx, &index, prop)) {
    Node* node;

    if((node = list_at(list, index))) {
      if(pdesc) {
        pdesc->flags = JS_PROP_ENUMERABLE;
        pdesc->value = JS_DupValue(ctx, node->value);
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
      }

      return TRUE;
    }
  }

  return FALSE;
}

static int
js_list_has_property(JSContext* ctx, JSValueConst obj, JSAtom prop) {
  List* list;
  int64_t index;

  if(!(list = js_list_data2(ctx, obj)))
    return FALSE;

  if(js_atom_is_index(ctx, &index, prop))
    return list_has(list, index);

  return FALSE;
}

static int
js_list_set_property(
    JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst value, JSValueConst receiver, int flags) {
  List* list;
  int64_t index, size;

  if(!(list = js_list_data2(ctx, obj)))
    return FALSE;

  size = (int64_t)list->size;

  if(js_atom_is_index(ctx, &index, prop)) {
    if(index >= size) {
      for(int64_t i = size; i < index; i++)
        list_insert_before(list, JS_UNDEFINED, NULL, ctx);

      list_insert_before(list, value, NULL, ctx);
    } else if(index < -size) {
      for(int64_t i = index; i < -(size + 1); i++)
        list_insert(list, JS_UNDEFINED, NULL, ctx);

      list_insert(list, value, NULL, ctx);
    } else {
      Node* node = list_at(list, index);

      JS_FreeValue(ctx, node->value);
      node->value = JS_DupValue(ctx, value);
    }

    return TRUE;
  }

  return FALSE;
}

static JSClassExoticMethods js_list_exotic_methods = {
    .get_own_property = js_list_get_own_property,
    .has_property = js_list_has_property,
    .set_property = js_list_set_property,
};

static JSClassDef js_list_class = {
    .class_name = "List",
    .finalizer = js_list_finalizer,
    .exotic = &js_list_exotic_methods,
};

static JSClassDef js_list_iterator_class = {
    .class_name = "ListIterator",
    .finalizer = js_list_iterator_finalizer,
};

static const JSCFunctionListEntry js_list_methods[] = {
    JS_CFUNC_MAGIC_DEF("clear", 0, js_list_method, LIST_CLEAR),
    JS_CFUNC_MAGIC_DEF("begin", 0, js_list_method, LIST_BEGIN),
    JS_CFUNC_MAGIC_DEF("end", 0, js_list_method, LIST_END),
    JS_CFUNC_MAGIC_DEF("rbegin", 0, js_list_method, LIST_RBEGIN),
    JS_CFUNC_MAGIC_DEF("rend", 0, js_list_method, LIST_REND),
    JS_CFUNC_MAGIC_DEF("erase", 1, js_list_method, LIST_ERASE),
    JS_CFUNC_MAGIC_DEF("insert", 1, js_list_method, LIST_INSERT_AFTER),
    JS_CFUNC_MAGIC_DEF("insertBefore", 1, js_list_method, LIST_INSERT_BEFORE),
    JS_CFUNC_MAGIC_DEF("unique", 0, js_list_method, LIST_UNIQUE),
    JS_CFUNC_MAGIC_DEF("merge", 1, js_list_method, LIST_MERGE),

    JS_CFUNC_MAGIC_DEF("push", 1, js_list_method2, LIST_PUSH),
    JS_CFUNC_MAGIC_DEF("pop", 0, js_list_method2, LIST_POP),
    JS_CFUNC_MAGIC_DEF("unshift", 1, js_list_method2, LIST_UNSHIFT),
    JS_CFUNC_MAGIC_DEF("shift", 0, js_list_method2, LIST_SHIFT),
    JS_CFUNC_MAGIC_DEF("at", 1, js_list_method2, LIST_AT),
    JS_CFUNC_MAGIC_DEF("includes", 1, js_list_method2, LIST_INCLUDES),
    JS_CFUNC_MAGIC_DEF("indexOf", 1, js_list_method2, LIST_INDEX_OF),
    JS_CFUNC_MAGIC_DEF("lastIndexOf", 1, js_list_method2, LIST_LAST_INDEX_OF),
    JS_CFUNC_MAGIC_DEF("findIndex", 1, js_list_search, LIST_FIND_INDEX),
    JS_CFUNC_MAGIC_DEF("findLastIndex", 1, js_list_search, LIST_FIND_LAST_INDEX),
    JS_CFUNC_MAGIC_DEF("find", 1, js_list_search, LIST_FIND),
    JS_CFUNC_MAGIC_DEF("findLast", 1, js_list_search, LIST_FIND_LAST),
    JS_CFUNC_MAGIC_DEF("concat", 1, js_list_method2, LIST_CONCAT),
    JS_CFUNC_MAGIC_DEF("slice", 0, js_list_method2, LIST_SLICE),
    JS_CFUNC_MAGIC_DEF("reverse", 0, js_list_method2, LIST_REVERSE),
    JS_CFUNC_MAGIC_DEF("toReversed", 0, js_list_method2, LIST_TOREVERSED),
    JS_CFUNC_MAGIC_DEF("splice", 0, js_list_method2, LIST_SPLICE),
    JS_CFUNC_MAGIC_DEF("fill", 1, js_list_method2, LIST_FILL),
    JS_CFUNC_MAGIC_DEF("rotate", 1, js_list_method2, LIST_ROTATE),
    JS_CFUNC_MAGIC_DEF("every", 1, js_list_functional, LIST_EVERY),
    JS_CFUNC_MAGIC_DEF("some", 1, js_list_functional, LIST_SOME),
    JS_CFUNC_MAGIC_DEF("filter", 1, js_list_functional, LIST_FILTER),
    JS_CFUNC_MAGIC_DEF("forEach", 1, js_list_functional, LIST_FOREACH),
    JS_CFUNC_MAGIC_DEF("map", 1, js_list_functional, LIST_MAP),
    JS_CFUNC_MAGIC_DEF("reduce", 1, js_list_functional, LIST_REDUCE),
    JS_CFUNC_MAGIC_DEF("reduceRight", 1, js_list_functional, LIST_REDUCE_RIGHT),
    JS_CFUNC_MAGIC_DEF("sort", 0, js_list_functional, LIST_SORT),
    JS_CFUNC_MAGIC_DEF("values", 0, js_list_iterator, YIELD_VALUE),
    JS_CFUNC_MAGIC_DEF("keys", 0, js_list_iterator, YIELD_KEY),
    JS_CFUNC_MAGIC_DEF("entries", 0, js_list_iterator, YIELD_KEY_AND_VALUE),
    JS_ALIAS_DEF("[Symbol.iterator]", "values"),
    JS_CGETSET_MAGIC_FLAGS_DEF("length", js_list_get, 0, LIST_LENGTH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("address", js_list_get, 0, LIST_ADDRESS),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "List", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_list_functions[] = {
    JS_CFUNC_MAGIC_DEF("from", 1, js_list_function, STATIC_FROM),
    JS_CFUNC_MAGIC_DEF("of", 0, js_list_function, STATIC_OF),
    JS_CFUNC_MAGIC_DEF("isList", 1, js_list_function, STATIC_IS_LIST),
    JS_CONSTANT(NORMAL),
    JS_CONSTANT(REVERSE),
};

static JSValue
js_node_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Node* node;

  if(!(node = node_get(ctx, argv[0])))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_node_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, node);
  return obj;

fail:
  js_free(ctx, node);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_node_wrap(JSContext* ctx, JSValueConst proto, Node* node) {
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_node_class_id);

  JS_SetOpaque(obj, node);
  return obj;
}

enum {
  NODE_EQUALS,
  NODE_VALUEOF,
};

static JSValue
js_node_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Node* node;
  JSValue ret = JS_UNDEFINED;

  if(!(node = js_node_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case NODE_EQUALS: {
      Node* other;

      if(!(other = node_get(ctx, argv[0])))
        return JS_EXCEPTION;

      ret = JS_NewBool(ctx, other == node);
      break;
    }
    case NODE_VALUEOF: {
      if(JS_IsUninitialized(node->value))
        ret = js_list_wrap(ctx, list_proto, list_dup((List*)node));
      else
        ret = JS_DupValue(ctx, node->value);

      break;
    }
  }

  return ret;
}

enum {
  NODE_VALUE,
  NODE_PREV,
  NODE_NEXT,
  NODE_LINKED,
  NODE_ADDRESS,
  NODE_SENTINEL,
};

static JSValue
js_node_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Node* node;
  JSValue ret = JS_UNDEFINED;

  if(!(node = js_node_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case NODE_VALUE: {
      ret = JS_IsUninitialized(node->value) ? JS_UNINITIALIZED : JS_DupValue(ctx, node->value);
      break;
    }
    case NODE_PREV: {
      ret = node_linked(node) ? js_node_wrap(ctx, node_proto, node_dup(node->prev)) : JS_NULL;
      break;
    }
    case NODE_NEXT: {
      ret = node_linked(node) ? js_node_wrap(ctx, node_proto, node_dup(node->next)) : JS_NULL;
      break;
    }
    case NODE_LINKED: {
      ret = JS_NewBool(ctx, node_linked(node));
      break;
    }
    case NODE_SENTINEL: {
      ret = JS_NewBool(ctx, JS_IsUninitialized(node->value));
      break;
    }
    case NODE_ADDRESS: {
      char buf[32];

      snprintf(buf, sizeof(buf), "%p", node);
      ret = JS_NewString(ctx, buf);
      break;
    }
  }

  return ret;
}

static JSValue
js_node_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Node* node;
  JSValue ret = JS_UNDEFINED;

  if(!(node = js_node_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case NODE_VALUE: {
      if(!JS_IsUninitialized(node->value)) {
        JS_FreeValue(ctx, node->value);
        node->value = JS_DupValue(ctx, value);
      }
      break;
    }
  }

  return ret;
}

static void
js_node_finalizer(JSRuntime* rt, JSValue val) {
  Node* node;

  if((node = js_node_data(val))) {
    node_free_rt(node, rt);
  }
}

static const JSCFunctionListEntry js_node_methods[] = {
    JS_CFUNC_MAGIC_DEF("equals", 1, js_node_method, NODE_EQUALS),
    JS_CFUNC_MAGIC_DEF("valueOf", 0, js_node_method, NODE_VALUEOF),
    JS_CGETSET_MAGIC_DEF("prev", js_node_get, 0, NODE_PREV),
    JS_CGETSET_MAGIC_DEF("next", js_node_get, 0, NODE_NEXT),
    JS_CGETSET_MAGIC_DEF("linked", js_node_get, 0, NODE_LINKED),
    JS_CGETSET_MAGIC_DEF("sentinel", js_node_get, 0, NODE_SENTINEL),
    JS_CGETSET_MAGIC_DEF("value", js_node_get, js_node_set, NODE_VALUE),
    JS_CGETSET_MAGIC_DEF("address", js_node_get, 0, NODE_ADDRESS),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Node", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_node_class = {
    .class_name = "Node",
    .finalizer = js_node_finalizer,
};

static int
js_list_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_list_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_list_class_id, &js_list_class);

  list_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, list_proto, js_list_methods, countof(js_list_methods));

  JS_SetClassProto(ctx, js_list_class_id, list_proto);

  list_ctor = JS_NewCFunction2(ctx, js_list_constructor, "List", 1, JS_CFUNC_constructor, 0);

  /* JSAtom species_atom = js_symbol_static_atom(ctx, "species");
  JS_SetProperty(ctx, list_ctor, species_atom, list_ctor);
  JS_FreeAtom(ctx, species_atom);*/

  JS_SetConstructor(ctx, list_ctor, list_proto);
  JS_SetPropertyFunctionList(ctx, list_ctor, js_list_functions, countof(js_list_functions));

  JSValue array_proto = js_global_prototype(ctx, "Array");

  if(JS_IsObject(array_proto)) {
    JS_DefinePropertyValueStr(
        ctx, list_proto, "join", JS_GetPropertyStr(ctx, array_proto, "join"), JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(
        ctx, list_proto, "toString", JS_GetPropertyStr(ctx, array_proto, "toString"), JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(
        ctx, list_proto, "toLocaleString", JS_GetPropertyStr(ctx, array_proto, "toLocaleString"), JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(
        ctx, list_proto, "flat", JS_GetPropertyStr(ctx, array_proto, "flat"), JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(
        ctx, list_proto, "flatMap", JS_GetPropertyStr(ctx, array_proto, "flatMap"), JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(
        ctx, list_proto, "copyWithin", JS_GetPropertyStr(ctx, array_proto, "copyWithin"), JS_PROP_CONFIGURABLE);
  }

  JS_FreeValue(ctx, array_proto);

  JS_NewClassID(&js_list_iterator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_list_iterator_class_id, &js_list_iterator_class);

  list_iterator_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx,
                             list_iterator_proto,
                             js_list_iterator_proto_funcs,
                             countof(js_list_iterator_proto_funcs));

  JS_SetClassProto(ctx, js_list_iterator_class_id, list_iterator_proto);

  list_iterator_ctor = JS_NewCFunction2(ctx, js_list_iterator_constructor, "ListIterator", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, list_iterator_ctor, list_iterator_proto);

  JS_NewClassID(&js_node_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_node_class_id, &js_node_class);

  node_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, node_proto, js_node_methods, countof(js_node_methods));

  JS_SetClassProto(ctx, js_node_class_id, node_proto);

  node_ctor = JS_NewCFunction2(ctx, js_node_constructor, "Node", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, node_ctor, node_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "List", list_ctor);
    JS_SetModuleExport(ctx, m, "ListIterator", list_iterator_ctor);
    JS_SetModuleExport(ctx, m, "ListNode", node_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_list
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_list_init))) {
    JS_AddModuleExport(ctx, m, "List");
    JS_AddModuleExport(ctx, m, "ListIterator");
    JS_AddModuleExport(ctx, m, "ListNode");
  }

  return m;
}

/**
 * @}
 */
