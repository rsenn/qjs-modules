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

typedef struct Node {
  struct list_head link;
  JSValue value;
} Node;

//  typedef Node* SkipCall(Node*, List*);

typedef enum Direction {
  FWD = 0,
  REV = 1,
} Direction;

typedef enum ListIteratorKind {
  ITERATOR_KIND_KEY,
  ITERATOR_KIND_VALUE,
  ITERATOR_KIND_KEY_AND_VALUE,
} ListIteratorKind;

typedef struct ListIterator {
  List* list;
  struct list_head* link;
  int64_t index;
  ListIteratorKind kind;
  Direction dir;
} ListIterator;

typedef int64_t FindCall(List*, JSValueConst, JSValueConst, Node**, JSContext*);

VISIBLE JSClassID js_list_class_id = 0, js_list_iterator_class_id = 0;
VISIBLE JSValue list_proto = {{0}, JS_TAG_UNDEFINED}, list_ctor = {{0}, JS_TAG_UNDEFINED}, list_iterator_proto = {{0}, JS_TAG_UNDEFINED},
                list_iterator_ctor = {{0}, JS_TAG_UNDEFINED};

enum {
  METHOD_PUSH = 0,
  METHOD_POP,
  METHOD_UNSHIFT,
  METHOD_SHIFT,
  METHOD_AT,
  METHOD_INDEX_OF,
  METHOD_INCLUDES,
  METHOD_LAST_INDEX_OF,
  METHOD_FIND_INDEX,
  METHOD_FIND_LAST_INDEX,
  METHOD_FIND,
  METHOD_FIND_LAST,
  METHOD_CONCAT,
  METHOD_SLICE,
  METHOD_FILL,
  METHOD_ROTATE,
  METHOD_REVERSE,
  METHOD_SPLICE,
  METHOD_EVERY,
  METHOD_FILTER,
  METHOD_FOREACH,
  METHOD_MAP,
  METHOD_REDUCE,
  METHOD_REDUCE_RIGHT,
  METHOD_SOME,
  METHOD_SORT,
  METHOD_INSERT,
};

enum {
  STATIC_FROM = 0,
  STATIC_OF,
  STATIC_IS_LIST,
};

enum {
  PROP_LENGTH = 0,
};

static Node*
node_new(JSContext* ctx, JSValueConst value) {
  Node* node;

  if((node = js_malloc(ctx, sizeof(Node)))) {
    node->link.next = NULL;
    node->link.prev = NULL;
    node->value = JS_DupValue(ctx, value);
  }

  return node;
}

static void
node_free(Node* node, JSRuntime* rt) {
  JS_FreeValueRT(rt, node->value);
  node->value = JS_UNINITIALIZED;

  js_free_rt(rt, node);
}

static inline JSValue
node_remove(Node* node, List* list, JSContext* ctx) {
  JSValue ret = node->value;

  list_del(&node->link);
  --list->size;

  js_free(ctx, node);

  return ret;
}

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

static BOOL
node_predicate(Node* node, JSValueConst fn, JSValueConst list_obj, int64_t i, JSContext* ctx) {
  JSValue ret = node_call(node, fn, list_obj, i, ctx);
  BOOL result = JS_ToBool(ctx, ret);

  JS_FreeValue(ctx, ret);

  return result;
}

static List*
list_new(JSContext* ctx) {
  List* list;

  if((list = js_malloc(ctx, sizeof(List)))) {
    init_list_head(&list->head);

    list->ref_count = 1;
    list->size = 0;
  }

  return list;
}

static Node*
list_push(List* list, JSValueConst value, JSContext* ctx) {
  Node* node;

  if((node = node_new(ctx, value))) {
    list_add_tail(&node->link, &list->head);

    ++list->size;
  }

  return node;
}

static Node*
list_unshift(List* list, JSValueConst value, JSContext* ctx) {
  Node* node;

  if((node = node_new(ctx, value))) {
    list_add(&node->link, &list->head);

    ++list->size;
  }

  return node;
}

static struct list_head*
list_insert(List* list, struct list_head* prev, JSValueConst value, JSContext* ctx) {
  Node* node;

  if((node = node_new(ctx, value))) {
    if(prev)
      __list_add(&node->link, prev, prev->next);
    else
      list_add(&node->link, &list->head);

    ++list->size;
  }

  return (struct list_head*)node;
}

static Node*
list_at_forward(List* list, int64_t index) {
  struct list_head* ptr;
  int64_t i = 0;

  list_for_each(ptr, &list->head) {
    if(i++ == index)
      return list_entry(ptr, Node, link);
  }

  return NULL;
}

static Node*
list_at_reverse(List* list, int64_t index) {
  struct list_head* ptr;
  int64_t i = 0;

  list_for_each_prev(ptr, &list->head) {
    if(i++ == index)
      return list_entry(ptr, Node, link);
  }

  return NULL;
}

static inline BOOL
list_has(List* list, int64_t index) {
  if(index < 0)
    return index >= -(int64_t)list->size;

  return index < (int64_t)list->size;
}

static inline int64_t
list_index(List* list, int64_t index) {
  if(index < 0)
    index += list->size;

  return index;
}

static Node*
list_at(List* list, int64_t index) {
  if(index < 0)
    index += list->size;
  int64_t from_back = (list->size - 1) - index;

  if(index < (int64_t)list->size) {
    if(from_back < index)
      return list_at_reverse(list, from_back);

    return list_at_forward(list, index);
  }

  return NULL;
}

static inline JSValue
list_value(List* list, int64_t index) {
  Node* node;

  if((node = list_at(list, index)))
    return node->value;

  return JS_UNINITIALIZED;
}

static void
list_free_rt(List* list, JSRuntime* rt) {
  if(--list->ref_count == 0) {
    struct list_head *ptr, *ptr2;

    list_for_each_safe(ptr, ptr2, &list->head) {
      Node* node = list_entry(ptr, Node, link);

      node_free(node, rt);
    }

    js_free_rt(rt, list);
  }
}

static inline void
list_free(List* list, JSContext* ctx) {
  list_free_rt(list, JS_GetRuntime(ctx));
}

static void
list_unref(List** list_p, JSRuntime* rt) {
  List* list = *list_p;

  *list_p = NULL;

  list_free_rt(list, rt);
}

static inline List*
list_dup(List* list) {
  ++list->ref_count;

  return list;
}

static int64_t
list_indexof_forward(List* list, JSValueConst value, JSContext* ctx) {
  struct list_head* ptr;
  int64_t i = 0;

  list_for_each(ptr, &list->head) {
    Node* node = list_entry(ptr, Node, link);

    if(js_value_equals(ctx, value, node->value))
      return i;

    i++;
  }

  return -1;
}

static int64_t
list_indexof_reverse(List* list, JSValueConst value, JSContext* ctx) {
  struct list_head* ptr;
  int64_t i = 0;

  list_for_each_prev(ptr, &list->head) {
    Node* node = list_entry(ptr, Node, link);

    if(js_value_equals(ctx, value, node->value))
      return i;

    i++;
  }

  return -1;
}

static int64_t
list_find_forward(List* list, JSValueConst list_obj, JSValueConst fn, Node** nptr, JSContext* ctx) {
  struct list_head* ptr;
  int64_t i = 0;

  list_for_each(ptr, &list->head) {
    Node* node = list_entry(ptr, Node, link);

    if(node_predicate(node, fn, list_obj, i++, ctx)) {
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
  int64_t i = list->size - 1;

  list_for_each_prev(ptr, &list->head) {
    Node* node = list_entry(ptr, Node, link);

    if(node_predicate(node, fn, list_obj, i--, ctx)) {
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

  findcall(list, list_obj, fn, &node, ctx);

  return node ? JS_DupValue(ctx, node->value) : JS_UNDEFINED;
}

static BOOL
list_append(List* list, JSValueConst iterable, JSContext* ctx) {
  Iteration iter = {0};

  if(!iteration_method_symbol(&iter, ctx, iterable, "iterator")) {
    iteration_reset(&iter, ctx);
    return FALSE;
  }

  while(!iteration_next(&iter, ctx)) {
    JSValue value = iteration_value(&iter, ctx);

    list_push(list, value, ctx);
  }

  iteration_reset(&iter, ctx);

  return TRUE;
}

static void
list_iterator_free(ListIterator* it, JSRuntime* rt) {
  if(it->list)
    list_unref(&it->list, rt);

  js_free_rt(rt, it);
}

static JSValue
list_iterator_value(ListIterator* it, JSContext* ctx) {
  JSValue ret;
  Node* node;

  if(it->link == &it->list->head)
    return JS_UNDEFINED;

  node = (Node*)it->link;

  switch(it->kind) {
    case ITERATOR_KIND_KEY: {
      ret = JS_NewInt64(ctx, it->index + it->list->size);
      break;
    }

    case ITERATOR_KIND_VALUE: {
      ret = JS_DupValue(ctx, node->value);
      break;
    }

    case ITERATOR_KIND_KEY_AND_VALUE: {
      ret = JS_NewArray(ctx);
      JS_SetPropertyUint32(ctx, ret, 0, JS_NewInt64(ctx, it->index + it->list->size));
      JS_SetPropertyUint32(ctx, ret, 1, JS_DupValue(ctx, node->value));

      break;
    }
  }

  return ret;
}

static BOOL
list_iterator_skip(ListIterator* it, JSContext* ctx) {
  struct list_head* link = it->link;

  if(link == &it->list->head)
    return TRUE;

  switch(it->dir) {
    case FWD: {
      it->link = link->next;
      it->index++;
      break;
    }
    
    case REV: {
      it->link = link->prev;
      it->index--;
      break;
    }
  }

  return FALSE;
}

static JSValue
list_iterator_next(ListIterator* it, BOOL* pdone, JSContext* ctx) {
  JSValue ret = list_iterator_value(it, ctx);

  *pdone = list_iterator_skip(it, ctx);

  return ret;
}

static JSValue
js_list_iterator_new(JSContext* ctx, JSValueConst proto, List* list, ListIteratorKind kind, Direction dir) {
  ListIterator* it;
  JSValue obj = JS_UNDEFINED;

  if(!(it = js_malloc(ctx, sizeof(ListIterator))))
    return JS_EXCEPTION;

  it->list = list_dup(list);
  it->index = dir == REV ? -1 : 0;
  it->link = dir == REV ? list->head.prev : list->head.next;
  it->kind = kind;
  it->dir = dir;

  obj = JS_NewObjectProtoClass(ctx, proto, js_list_iterator_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, it);

  return obj;

fail:
  js_free(ctx, it);
  JS_FreeValue(ctx, obj);

  return JS_EXCEPTION;
}

static JSValue
js_list_iterator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto;
  List* list;
  Direction dir = FWD;

  if(argc < 1)
    return JS_ThrowReferenceError(ctx, "argument 1 must be a List");

  if(!(list = js_list_data2(ctx, argv[0])))
    return JS_EXCEPTION;

  if(argc > 1) {
    uint32_t n = 0;
    JS_ToUint32(ctx, &n, argv[1]);
    dir = (n & 1) ? REV : FWD;
  }

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;

  return js_list_iterator_new(ctx, proto, list, ITERATOR_KIND_VALUE, dir);
}

static JSValue
js_list_iterator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  ListIterator* it;
  JSValue ret = JS_UNDEFINED;

  if(!(it = JS_GetOpaque2(ctx, this_val, js_list_iterator_class_id)))
    return JS_EXCEPTION;

  ret = list_iterator_next(it, pdone, ctx);

  return ret;
}

static JSValue
js_list_iterator_fn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  BOOL done = FALSE;
  JSValue ret = JS_NewObject(ctx);
  ListIterator* it = JS_GetOpaque(this_val, js_list_iterator_class_id);

  JS_SetPropertyStr(ctx, ret, "index", JS_NewInt64(ctx, it->index));
  JS_SetPropertyStr(ctx, ret, "value", js_list_iterator_next(ctx, this_val, argc, argv, &done, 0));
  JS_SetPropertyStr(ctx, ret, "done", JS_NewBool(ctx, done));

  return ret;
}

static JSValue
js_list_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret;
  ListIterator* it;
  JSValueConst args[] = {
      this_val,
      argc > 0 ? JS_DupValue(ctx, argv[0]) : JS_NewInt32(ctx, 0),
  };

  ret = js_list_iterator_constructor(ctx, list_iterator_ctor, countof(args), args);

  JS_FreeValue(ctx, args[1]);

  it = JS_GetOpaque(ret, js_list_iterator_class_id);
  it->kind = magic;

  return ret;
}

VISIBLE JSValue
js_list_new(JSContext* ctx, JSValueConst proto) {
  List* list;
  JSValue obj = JS_UNDEFINED;

  if(!(list = list_new(ctx)))
    return JS_EXCEPTION;

  obj = JS_NewObjectProtoClass(ctx, proto, js_list_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, list);

  return obj;

fail:
  js_free(ctx, list);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

VISIBLE JSValue
js_list_wrap(JSContext* ctx, JSValueConst proto, List* list) {
  JSValue obj;

  obj = JS_NewObjectProtoClass(ctx, proto, js_list_class_id);

  JS_SetOpaque(obj, list);

  return obj;
}

VISIBLE JSValue
js_list_wrap_species(JSContext* ctx, JSValueConst this_val, List* list) {
  JSValue species = js_object_species(ctx, this_val);
  JSValue proto = JS_IsUndefined(species) ? JS_DupValue(ctx, list_proto) : JS_GetPropertyStr(ctx, species, "prototype");

  JSValue ret = js_list_wrap(ctx, proto, list);

  JS_FreeValue(ctx, proto);
  JS_FreeValue(ctx, species);

  return ret;
}

static JSValue
js_list_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  List* list;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, list_proto);

  obj = js_list_new(ctx, proto);

  if(!(list = js_list_data2(ctx, obj)))
    return JS_EXCEPTION;

  if(argc > 0) {
    if(!list_append(list, argv[0], ctx)) {
      JS_FreeValue(ctx, obj);

      return JS_ThrowTypeError(ctx, "argument 1 not iterable");
    }
  }

  return obj;
}

static JSValue
js_list_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int64_t index;
  List* list;
  Node* node = 0;
  JSValue ret = JS_UNDEFINED;

  if(!(list = js_list_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_PUSH: {
      for(int i = 0; i < argc; i++) 
        if(!list_push(list, argv[i], ctx))
          return JS_EXCEPTION;

      ret = JS_NewInt64(ctx, list->size);
      break;
    }

    case METHOD_POP: {
      if(list->head.prev != &list->head) {
        node = list_entry(list->head.prev, Node, link);
        ret = node_remove(node, list, ctx);
      }

      break;
    }

    case METHOD_UNSHIFT: {
      for(int i = argc - 1; i >= 0; i--) 
        if(!list_unshift(list, argv[i], ctx))
          return JS_EXCEPTION;
      
      ret = JS_NewInt64(ctx, list->size);
      break;
    }

    case METHOD_SHIFT: {
      if(list->head.next != &list->head) {
        node = list_entry(list->head.next, Node, link);
        ret = node_remove(node, list, ctx);
      }

      break;
    }

    case METHOD_AT: {
      if(JS_ToInt64(ctx, &index, argv[0]))
        return JS_ThrowRangeError(ctx, "argument 1 must be numeric index");

      if((node = list_at(list, index)))
        ret = JS_DupValue(ctx, node->value);
      else
        ret = JS_ThrowRangeError(ctx, "Index %ld is out of range", (long)index);

      break;
    }

    case METHOD_INCLUDES: {
      index = list_indexof_forward(list, argv[0], ctx);

      ret = JS_NewBool(ctx, index != -1);
      break;
    }

    case METHOD_INDEX_OF: {
      index = list_indexof_forward(list, argv[0], ctx);

      ret = JS_NewInt64(ctx, index);
      break;
    }

    case METHOD_LAST_INDEX_OF: {
      index = list_indexof_reverse(list, argv[0], ctx);

      ret = JS_NewInt64(ctx, index == -1 ? -1 : (int64_t)(list->size - 1) - index);
      break;
    }

    case METHOD_CONCAT: {
      List* other;
      struct list_head* ptr;

      if(!(other = list_new(ctx)))
        return JS_EXCEPTION;

      list_for_each(ptr, &list->head) {
        node = list_entry(ptr, Node, link);

        list_push(other, node->value, ctx);
      }

      for(int i = 0; i < argc; i++) {
        if(!list_append(other, argv[i], ctx)) {
          list_free(other, ctx);

          return JS_ThrowTypeError(ctx, "argument %d not iterable", i);
        }
      }

      ret = js_list_wrap_species(ctx, this_val, other);
      break;
    }

    case METHOD_SLICE: {
      List* other;
      struct list_head* ptr;
      int64_t i = 0, start = 0, end = list->size;

      if(argc > 0 && JS_ToInt64(ctx, &start, argv[0]))
        return JS_ThrowRangeError(ctx, "argument 1 must be numeric index");

      if(argc > 1 && JS_ToInt64(ctx, &end, argv[1]))
        return JS_ThrowRangeError(ctx, "argument 2 must be numeric index");

      start = list_index(list, start);
      end = list_index(list, end);

      if(!(other = list_new(ctx)))
        return JS_EXCEPTION;

      list_for_each(ptr, &list->head) {
        node = list_entry(ptr, Node, link);

        if(i >= start && i < end)
          list_push(other, node->value, ctx);

        i++;
      }

      ret = js_list_wrap_species(ctx, this_val, other);
      break;
    }

    case METHOD_SPLICE: {
      List* other;
      struct list_head *ptr, *ptr2;
      int64_t i = 0, start = 0, end = list->size;

      if(argc > 0 && JS_ToInt64(ctx, &start, argv[0]))
        return JS_ThrowRangeError(ctx, "argument 1 must be numeric index");

      if(argc > 1 && JS_ToInt64(ctx, &end, argv[1]))
        return JS_ThrowRangeError(ctx, "argument 2 must be numeric count");

      start = list_index(list, start);
      end = list_index(list, start + end);

      if(!(other = list_new(ctx)))
        return JS_EXCEPTION;

      list_for_each_safe(ptr, ptr2, &list->head) {
        node = list_entry(ptr, Node, link);

        if(i == end)
          break;

        if(i >= start) {
          list_del(&node->link);
          list_add_tail(&node->link, &other->head);
        }

        i++;
      }

      ptr2 = ptr->prev;

      for(i = 2; i < argc; i++)
        ptr2 = list_insert(list, ptr2, argv[i], ctx);

      ret = js_list_wrap_species(ctx, this_val, other);
      break;
    }

    case METHOD_FILL: {
      List* other;
      struct list_head* ptr;
      int64_t i = 0, start = 0, end = list->size;

      if(argc > 1 && JS_ToInt64(ctx, &start, argv[1]))
        return JS_ThrowRangeError(ctx, "argument 2 must be numeric index");

      if(argc > 2 && JS_ToInt64(ctx, &end, argv[2]))
        return JS_ThrowRangeError(ctx, "argument 3 must be numeric index");

      start = list_index(list, start);
      end = list_index(list, end);

      if(!(other = list_new(ctx)))
        return JS_EXCEPTION;

      list_for_each(ptr, &list->head) {
        node = list_entry(ptr, Node, link);
        JSValue value = node->value;

        if(i >= start && i < end)
          value = argv[0];

        list_push(other, value, ctx);

        i++;
      }

      ret = js_list_wrap_species(ctx, this_val, other);
      break;
    }

    case METHOD_ROTATE: {
      if(JS_ToInt64(ctx, &index, argv[0]))
        return JS_ThrowRangeError(ctx, "argument 1 must be numeric index");

      index %= (int64_t)list->size;

      while(index > 0 && list->head.prev != &list->head) {
        node = list_entry(list->head.prev, Node, link);
        list_del(&node->link);
        list_add(&node->link, &list->head);
        index--;
      }

      while(index < 0 && list->head.next != &list->head) {
        node = list_entry(list->head.next, Node, link);
        list_del(&node->link);
        list_add_tail(&node->link, &list->head);
        index++;
      }

      ret = JS_DupValue(ctx, this_val);
      break;
    }

    case METHOD_REVERSE: {
      List* other;
      struct list_head* ptr;

      if(!(other = list_new(ctx)))
        return JS_EXCEPTION;

      list_for_each_prev(ptr, &list->head) {
        node = list_entry(ptr, Node, link);

        list_push(other, node->value, ctx);
      }

      ret = js_list_wrap_species(ctx, this_val, other);
      break;
    }

    case METHOD_INSERT: {
      ListIterator* iter;
      struct list_head* ptr = NULL;

      iter = JS_GetOpaque(argv[0], js_list_iterator_class_id);
      /*if(!(iter = JS_GetOpaque2(ctx, argv[0], js_list_iterator_class_id)))
        return JS_EXCEPTION;*/

      if(iter) {
        if(iter->list != list)
          return JS_ThrowReferenceError(ctx, "Iterator not from this list");

        ptr = iter->link;
      }

      for(int i = 1; i < argc; i++)
        ptr = list_insert(list, ptr, argv[i], ctx);

      break;
    }
  }

  return ret;
}

static JSValue
js_list_predicate(JSContext* ctx, int argc, JSValueConst argv[]) {
  JSValueConst func_obj;

  if(argc > 1)
    func_obj = js_function_bind_this(ctx, argv[0], argv[1]);
  else
    func_obj = JS_DupValue(ctx, argv[0]);

  return func_obj;
}

static JSValue
js_list_search(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  List* list;
  JSValue pred, ret = JS_UNDEFINED;

  if(!(list = js_list_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!JS_IsFunction(ctx, argv[0]))
    return JS_ThrowTypeError(ctx, "argument 1 must be a function");

  pred = js_list_predicate(ctx, argc, argv);

  switch(magic) {
    case METHOD_FIND_INDEX: {
      ret = JS_NewInt64(ctx, list_find_forward(list, this_val, pred, NULL, ctx));
      break;
    }

    case METHOD_FIND_LAST_INDEX: {
      ret = JS_NewInt64(ctx, list_find_reverse(list, this_val, pred, NULL, ctx));
      break;
    }

    case METHOD_FIND: {
      ret = list_find_value(list, this_val, pred, list_find_forward, ctx);
      break;
    }

    case METHOD_FIND_LAST: {
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

  if(argc < 1 && magic == METHOD_SORT) {
    const char* fn = "(a, b) => a - b";
    argv[0] = JS_Eval(ctx, fn, strlen(fn), "-", 0);
    argc = 1;
  } else if(argc < 1 || !JS_IsFunction(ctx, argv[0])) {
    return JS_ThrowTypeError(ctx, "argument 1 must be a function");
  }

  pred = js_list_predicate(ctx, argc, argv);

  switch(magic) {
    case METHOD_EVERY: {
      ret = JS_TRUE;
      list_for_each(ptr, &list->head) {
        Node* node = list_entry(ptr, Node, link);
        if(!node_predicate(node, pred, this_val, i++, ctx)) {
          JS_FreeValue(ctx, pred);
          ret = JS_FALSE;
          break;
        }
      }
      break;
    }

    case METHOD_SOME: {
      ret = JS_FALSE;
      list_for_each(ptr, &list->head) {
        Node* node = list_entry(ptr, Node, link);
        if(node_predicate(node, pred, this_val, i++, ctx)) {
          JS_FreeValue(ctx, pred);
          ret = JS_TRUE;
          break;
        }
      }
      break;
    }

    case METHOD_FILTER: {
      List* other;

      if(!(other = list_new(ctx))) {
        ret = JS_EXCEPTION;
        break;
      }

      list_for_each(ptr, &list->head) {
        Node* node = list_entry(ptr, Node, link);
        if(node_predicate(node, pred, this_val, i++, ctx))
          list_push(other, node->value, ctx);
      }

      ret = js_list_wrap_species(ctx, this_val, other);
      break;
    }

    case METHOD_FOREACH: {
      list_for_each(ptr, &list->head) {
        Node* node = list_entry(ptr, Node, link);

        node_predicate(node, pred, this_val, i++, ctx);
      }
      break;
    }

    case METHOD_MAP: {
      List* other;

      if(!(other = list_new(ctx))) {
        ret = JS_EXCEPTION;
        break;
      }

      list_for_each(ptr, &list->head) {
        Node* node = list_entry(ptr, Node, link);
        JSValue value = node_call(node, pred, this_val, i++, ctx);

        list_push(other, value, ctx);
      }

      ret = js_list_wrap_species(ctx, this_val, other);
      break;
    }

    case METHOD_REDUCE: {
      List* other;
      ret = JS_DupValue(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);

      if(!(other = list_new(ctx))) {
        ret = JS_EXCEPTION;
        break;
      }

      list_for_each(ptr, &list->head) {
        Node* node = list_entry(ptr, Node, link);
        JSValueConst args[] = {
            ret,
            node->value,
            JS_NewInt64(ctx, i++),
            this_val,
        };

        ret = JS_Call(ctx, argv[0], JS_UNDEFINED, countof(args), args);

        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
      }

      break;
    }

    case METHOD_REDUCE_RIGHT: {
      List* other;
      ret = JS_DupValue(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);

      if(!(other = list_new(ctx))) {
        ret = JS_EXCEPTION;
        break;
      }

      i = list->size;

      list_for_each_prev(ptr, &list->head) {
        Node* node = list_entry(ptr, Node, link);
        JSValueConst args[] = {
            ret,
            node->value,
            JS_NewInt64(ctx, --i),
            this_val,
        };

        ret = JS_Call(ctx, argv[0], JS_UNDEFINED, countof(args), args);

        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
      }
      break;
    }

    case METHOD_SORT: {
      List* other;
      struct list_head* ptr2;

      if(!(other = list_new(ctx))) {
        ret = JS_EXCEPTION;
        break;
      }

      list_for_each(ptr, &list->head) {
        Node* node = list_entry(ptr, Node, link);

        list_for_each(ptr2, &other->head) {
          JSValueConst args[] = {
              node->value,
              list_entry(ptr2, Node, link)->value,
          };

          JSValue value = JS_Call(ctx, pred, JS_UNDEFINED, countof(args), args);
          int32_t retval;
          JS_ToInt32(ctx, &retval, value);
          JS_FreeValue(ctx, value);

          if(retval <= 0)
            break;
        }

        ptr2 = ptr2->prev;
        list_insert(other, ptr2, node->value, ctx);
      }

      ret = js_list_wrap_species(ctx, this_val, other);
      break;
    }
  }

  JS_FreeValue(ctx, pred);

  return ret;
}

static JSValue
js_list_get(JSContext* ctx, JSValueConst this_val, int magic) {
  List* list;
  JSValue ret = JS_UNDEFINED;

  if(!(list = js_list_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_LENGTH: {
      ret = JS_NewInt64(ctx, list->size);
      break;
    }
  }
  return ret;
}

static JSValue
js_list_static(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  List* list;

  switch(magic) {
    case STATIC_FROM: {
      if(!(list = list_new(ctx)))
        return JS_EXCEPTION;

      if(!list_append(list, argv[0], ctx)) {
        list_free(list, ctx);
        return JS_ThrowTypeError(ctx, "argument 1 not iterable");
      }

      ret = js_list_wrap(ctx, list_proto, list);
      break;
    }

    case STATIC_OF: {
      if(!(list = list_new(ctx)))
        return JS_EXCEPTION;

      for(int i = 0; i < argc; i++)
        list_push(list, argv[i], ctx);

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
    list_free_rt(list, rt);
}

static void
js_list_iterator_finalizer(JSRuntime* rt, JSValue val) {
  ListIterator* it;

  if((it = JS_GetOpaque(val, js_list_iterator_class_id)))
    list_iterator_free(it, rt);
}

static const JSCFunctionListEntry js_list_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("push", 1, js_list_functions, METHOD_PUSH),
    JS_CFUNC_MAGIC_DEF("pop", 0, js_list_functions, METHOD_POP),
    JS_CFUNC_MAGIC_DEF("unshift", 1, js_list_functions, METHOD_UNSHIFT),
    JS_CFUNC_MAGIC_DEF("shift", 0, js_list_functions, METHOD_SHIFT),
    JS_CFUNC_MAGIC_DEF("at", 1, js_list_functions, METHOD_AT),
    JS_CFUNC_MAGIC_DEF("includes", 1, js_list_functions, METHOD_INCLUDES),
    JS_CFUNC_MAGIC_DEF("indexOf", 1, js_list_functions, METHOD_INDEX_OF),
    JS_CFUNC_MAGIC_DEF("lastIndexOf", 1, js_list_functions, METHOD_LAST_INDEX_OF),
    JS_CFUNC_MAGIC_DEF("findIndex", 1, js_list_search, METHOD_FIND_INDEX),
    JS_CFUNC_MAGIC_DEF("findLastIndex", 1, js_list_search, METHOD_FIND_LAST_INDEX),
    JS_CFUNC_MAGIC_DEF("find", 1, js_list_search, METHOD_FIND),
    JS_CFUNC_MAGIC_DEF("findLast", 1, js_list_search, METHOD_FIND_LAST),
    JS_CFUNC_MAGIC_DEF("concat", 1, js_list_functions, METHOD_CONCAT),
    JS_CFUNC_MAGIC_DEF("slice", 0, js_list_functions, METHOD_SLICE),
    JS_CFUNC_MAGIC_DEF("reverse", 0, js_list_functions, METHOD_REVERSE),
    JS_CFUNC_MAGIC_DEF("insert", 1, js_list_functions, METHOD_INSERT),
    JS_CFUNC_MAGIC_DEF("splice", 0, js_list_functions, METHOD_SPLICE),
    JS_CFUNC_MAGIC_DEF("fill", 1, js_list_functions, METHOD_FILL),
    JS_CFUNC_MAGIC_DEF("rotate", 1, js_list_functions, METHOD_ROTATE),
    JS_CFUNC_MAGIC_DEF("every", 1, js_list_functional, METHOD_EVERY),
    JS_CFUNC_MAGIC_DEF("some", 1, js_list_functional, METHOD_SOME),
    JS_CFUNC_MAGIC_DEF("filter", 1, js_list_functional, METHOD_FILTER),
    JS_CFUNC_MAGIC_DEF("forEach", 1, js_list_functional, METHOD_FOREACH),
    JS_CFUNC_MAGIC_DEF("map", 1, js_list_functional, METHOD_MAP),
    JS_CFUNC_MAGIC_DEF("reduce", 1, js_list_functional, METHOD_REDUCE),
    JS_CFUNC_MAGIC_DEF("reduceRight", 1, js_list_functional, METHOD_REDUCE_RIGHT),
    JS_CFUNC_MAGIC_DEF("sort", 0, js_list_functional, METHOD_SORT),
    JS_CFUNC_MAGIC_DEF("values", 0, js_list_iterator, ITERATOR_KIND_VALUE),
    JS_CFUNC_MAGIC_DEF("keys", 0, js_list_iterator, ITERATOR_KIND_KEY),
    JS_CFUNC_MAGIC_DEF("entries", 0, js_list_iterator, ITERATOR_KIND_KEY_AND_VALUE),
    JS_ALIAS_DEF("[Symbol.iterator]", "values"),
    JS_CGETSET_MAGIC_DEF("size", js_list_get, 0, PROP_LENGTH),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "List", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_list_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("from", 1, js_list_static, STATIC_FROM),
    JS_CFUNC_MAGIC_DEF("of", 0, js_list_static, STATIC_OF),
    JS_CFUNC_MAGIC_DEF("isList", 1, js_list_static, STATIC_IS_LIST),
    JS_CONSTANT(FWD),
    JS_CONSTANT(REV),
};

static const JSCFunctionListEntry js_list_iterator_proto_funcs[] = {
    //  JS_ITERATOR_NEXT_DEF("next", 0, js_list_iterator_next, 0),
    JS_CFUNC_DEF("next", 0, js_list_iterator_fn),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, (void*)&JS_DupValue),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ListIterator", JS_PROP_CONFIGURABLE),
};

static int
js_list_get_own_property(JSContext* ctx, JSPropertyDescriptor* pdesc, JSValueConst obj, JSAtom prop) {
  List* list = js_list_data2(ctx, obj);
  int64_t index;

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
  List* list = js_list_data2(ctx, obj);
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop))
    return list_has(list, index);

  return FALSE;
}

static JSValue
js_list_get_property(JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst receiver) {
  List* list = js_list_data2(ctx, obj);
  JSValue value = JS_UNDEFINED;
  int64_t index;
  int32_t entry;

  if(js_atom_is_index(ctx, &index, prop)) {
    if(list_has(list, index))
      value = JS_DupValue(ctx, list_value(list, index));

  } else if(js_atom_is_length(ctx, prop)) {
    value = JS_NewInt64(ctx, list->size);
  } else if((entry = js_find_cfunction_atom(ctx, js_list_proto_funcs, countof(js_list_proto_funcs), prop, JS_DEF_CGETSET_MAGIC)) >= 0) {

    value = js_list_get(ctx, obj, js_list_proto_funcs[entry].magic);

  } else {
    JSValue proto = JS_IsUndefined(list_proto) ? JS_GetPrototype(ctx, obj) : list_proto;

    if(JS_IsObject(proto))
      value = JS_GetProperty(ctx, proto, prop);
  }

  return value;
}

static int
js_list_set_property(JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst value, JSValueConst receiver, int flags) {
  List* list = js_list_data2(ctx, obj);
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    if(index >= (int64_t)list->size) {
      for(int64_t i = list->size; i < index; i++)
        list_push(list, JS_UNDEFINED, ctx);

      list_push(list, value, ctx);

    } else if(index < 0) {

      for(int64_t i = index; i < -1; i++)
        list_unshift(list, JS_UNDEFINED, ctx);

      list_unshift(list, value, ctx);
    } else {
      Node* node;

      node = list_at(list, index);

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
    .get_property = js_list_get_property,
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

static int
js_list_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_list_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_list_class_id, &js_list_class);

  list_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, list_proto, js_list_proto_funcs, countof(js_list_proto_funcs));

  JSValue array_proto = js_global_prototype(ctx, "Array");

  if(JS_IsObject(array_proto)) {
    JS_DefinePropertyValueStr(ctx, list_proto, "join", JS_GetPropertyStr(ctx, array_proto, "join"), JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, list_proto, "toString", JS_GetPropertyStr(ctx, array_proto, "toString"), JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, list_proto, "toLocaleString", JS_GetPropertyStr(ctx, array_proto, "toLocaleString"), JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, list_proto, "flat", JS_GetPropertyStr(ctx, array_proto, "flat"), JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, list_proto, "flatMap", JS_GetPropertyStr(ctx, array_proto, "flatMap"), JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, list_proto, "copyWithin", JS_GetPropertyStr(ctx, array_proto, "copyWithin"), JS_PROP_CONFIGURABLE);
  }

  // js_set_inspect_method(ctx, list_proto, js_list_inspect);

  JS_SetClassProto(ctx, js_list_class_id, list_proto);

  list_ctor = JS_NewCFunction2(ctx, js_list_constructor, "List", 1, JS_CFUNC_constructor, 0);

  JSAtom species_atom = js_symbol_static_atom(ctx, "species");
  JS_SetProperty(ctx, list_ctor, species_atom, list_ctor);
  JS_FreeAtom(ctx, species_atom);

  JS_SetConstructor(ctx, list_ctor, list_proto);
  JS_SetPropertyFunctionList(ctx, list_ctor, js_list_static_funcs, countof(js_list_static_funcs));

  JS_NewClassID(&js_list_iterator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_list_iterator_class_id, &js_list_iterator_class);

  list_iterator_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, list_iterator_proto, js_list_iterator_proto_funcs, countof(js_list_iterator_proto_funcs));

  JS_SetClassProto(ctx, js_list_iterator_class_id, list_iterator_proto);

  list_iterator_ctor = JS_NewCFunction2(ctx, js_list_iterator_constructor, "ListIterator", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, list_iterator_ctor, list_iterator_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "List", list_ctor);
    JS_SetModuleExport(ctx, m, "ListIterator", list_iterator_ctor);
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
  }

  return m;
}

/**
 * @}
 */
