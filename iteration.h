#ifndef ITERATION_H
#define ITERATION_H

#define _GNU_SOURCE

#include "quickjs.h"
#include "utils.h"
#include "vector.h"

#include <assert.h>

typedef struct Iteration {
  JSValue iter, next, result;
  BOOL done;
} Iteration;

static inline int
iteration_init(Iteration* it, JSContext* ctx, JSValueConst iterator) {
  it->iter = JS_DupValue(ctx, iterator);
  it->next = JS_GetPropertyStr(ctx, iterator, "next");
  it->done = FALSE;
  return 1;
}

static inline void
iteration_free(Iteration* it, JSRuntime* rt) {
  JS_FreeValueRT(rt, it->iter);
  JS_FreeValueRT(rt, it->next);
  JS_FreeValueRT(rt, it->result);
  it->result =   it->iter = it->next = JS_UNDEFINED;
  it->done = FALSE;
}

static inline JSValue
iteration_value(Iteration* it, JSContext* ctx) {
  assert(JS_IsObject(ctx, it->result));
  return JS_GetPropertyStr(ctx, it->result, "value");
}

static inline const char*
iteration_valuestr(Iteration* it, JSContext* ctx) {
  JSValue value = iteration_value(it, ctx);
  const char* str = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);
  return str;
}

 
static Iteration*
iteration_next(Iteration* it, JSContext* ctx) {
  JS_FreeValue(ctx, it->result);

  it->result = JS_Call(ctx, it->next, it->iter, 0, 0);
  it->done = js_object_propertystr_bool(ctx, it->result, "done");

  return it;
}

static inline Iteration*
iteration_push(vector* vec, JSContext* ctx, JSValue object, int flags) {
  Iteration* it;

  if(!JS_IsObject(object)) {
    JS_ThrowTypeError(ctx, "not an object");
    return 0;
  }
  if((it = iteration_new(vec)))
    iteration_init(it, ctx, object, flags);

  return it;
}

static inline Iteration*
iteration_pop(vector* vec, JSContext* ctx) {
  Iteration* it;
  assert(!vector_empty(vec));
  it = vector_back(vec, sizeof(Iteration));
  iteration_free(it, JS_GetRuntime(ctx));
  vector_pop(vec, sizeof(Iteration));
  return vector_empty(vec) ? 0 : it - 1;
}

static inline Iteration*
iteration_enter(vector* vec, JSContext* ctx, int flags) {
  Iteration* it;
  JSValue value;

  assert(!vector_empty(vec));
  it = vector_back(vec, sizeof(Iteration));
  value = iteration_value(it, ctx);

  return iteration_push(vec, ctx, value, flags);
}

static inline void
iteration_dumpall(vector* vec, JSContext* ctx, DynBuf* out) {
  size_t i, n = vector_size(vec, sizeof(Iteration));
  dbuf_printf(out, "(%zu) [", n);
  for(i = 0; i < n; i++) {
    dbuf_putstr(out, i ? ",\n    " : "\n    ");
    iteration_dump(vector_at(vec, sizeof(Iteration), i), ctx, out);
  }
  dbuf_putstr(out, i ? "\n  ]" : "]");
}

static JSValue
iteration_path(vector* vec, JSContext* ctx) {
  JSValue ret;
  Iteration* it;
  size_t i = 0;
  ret = JS_NewArray(ctx);
  vector_foreach_t(vec, it) {
    JSValue key = iteration_key(it, ctx);
    JS_SetPropertyUint32(ctx, ret, i++, key);
  }
  return ret;
}

static int
iteration_insideof(vector* vec, JSValueConst val) {
  Iteration* it;
  void* obj = JS_VALUE_GET_OBJ(val);
  vector_foreach_t(vec, it) {
    void* obj2 = JS_VALUE_GET_OBJ(it->obj);
    if(obj == obj2)
      return 1;
  }
  return 0;
}

static Iteration*
iteration_recurse(vector* vec, JSContext* ctx) {
  Iteration* it;
  JSValue value = JS_UNDEFINED;
  int32_t type;
  it = vector_back(vec, sizeof(Iteration));
  for(;;) {
    if(it->tab_atom_len > 0) {
      value = iteration_value(it, ctx);
      type = JS_VALUE_GET_TAG(value);
      JS_FreeValue(ctx, value);
      if(type == JS_TAG_OBJECT) {
        if((it = iteration_enter(vec, ctx, ITERATION_DEFAULT_FLAGS)) && iteration_setpos(it, 0))
          break;
      } else {
        if(iteration_setpos(it, it->idx + 1))
          break;
      }
      return 0;
    }
    for(;;) {
      if((it = iteration_pop(vec, ctx)) == 0)
        return it;
      if(iteration_setpos(it, it->idx + 1))
        break;
    }
  end:
    /* if(!it)
       break;*/
    break;
  }
  return it;
}

static int32_t
iteration_depth(JSContext* ctx, JSValueConst object) {
  vector vec;
  int32_t depth, max_depth = 0;
  Iteration* it;
  JSValue root = JS_DupValue(ctx, object);

  vector_init(&vec);
  if(JS_IsObject(root)) {
    for(it = iteration_push(&vec, ctx, root, ITERATION_DEFAULT_FLAGS);
        (it = iteration_recurse(&vec, ctx));) {
      depth = vector_size(&vec, sizeof(Iteration));
      if(max_depth < depth)
        max_depth = depth;
    }
  }
  vector_foreach_t(&vec, it) { iteration_free(it, JS_GetRuntime(ctx)); }
  vector_free(&vec);
  return max_depth;
}

#endif // ITERATION_H
