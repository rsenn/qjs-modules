/* jbuild.c: JsonBuilder, jbuild_*() events.
 * depends on: jread.h (event types), char-utils.h (scan_double).
 * rule: a container is attached to its parent when it is pushed, so the
 * partial tree is always reachable from jbuild_root(). */

#include "jbuild.h"
#include "char-utils.h"
#include <string.h>

typedef struct JsonBuilderFrame {
  struct JsonBuilderFrame* parent;
  JSValue obj;
  uint32_t index, my_index;
  char *current_key, *my_key;
  unsigned is_object : 1;
} JsonBuilderFrame;

void
jbuild_init(JsonBuilder* b, JSContext* ctx) {
  b->ctx = ctx;
  b->top = NULL;
  b->root = JS_UNDEFINED;
  b->has_root = FALSE;
}

void
jbuild_push(JsonBuilder* b, jr_type_t type) {
  JSContext* ctx = b->ctx;
  BOOL is_object = (type == jr_type_object_start);
  JSValue container = is_object ? JS_NewObjectProto(ctx, JS_NULL) : JS_NewArray(ctx);

  char* child_key = NULL;
  uint32_t child_index = 0;

  if(!b->top) {
    b->root = JS_DupValue(ctx, container);
    b->has_root = TRUE;
  } else {
    JsonBuilderFrame* parent = b->top;

    if(parent->is_object) {
      if(parent->current_key) {
        child_key = parent->current_key;
        parent->current_key = NULL;

        JSAtom atom = JS_NewAtomLen(ctx, child_key, strlen(child_key));
        JS_SetProperty(ctx, parent->obj, atom, JS_DupValue(ctx, container));
        JS_FreeAtom(ctx, atom);
      }
    } else {
      child_index = parent->index++;
      JS_SetPropertyUint32(ctx, parent->obj, child_index, JS_DupValue(ctx, container));
    }
  }

  JsonBuilderFrame* frame = js_mallocz(ctx, sizeof(JsonBuilderFrame));
  frame->parent = b->top;
  frame->obj = container;
  frame->is_object = is_object;
  frame->index = 0;
  frame->current_key = NULL;
  frame->my_key = child_key;
  frame->my_index = child_index;
  b->top = frame;
}

void
jbuild_pop(JsonBuilder* b) {
  JsonBuilderFrame* frame;

  if(!(frame = b->top))
    return;

  b->top = frame->parent;

  if(frame->current_key)
    js_free(b->ctx, frame->current_key);
  if(frame->my_key)
    js_free(b->ctx, frame->my_key);

  JS_FreeValue(b->ctx, frame->obj);
  js_free(b->ctx, frame);
}

void
jbuild_key(JsonBuilder* b, const char* name, size_t len) {
  JSContext* ctx = b->ctx;

  if(!b->top)
    return;

  if(b->top->current_key)
    js_free(ctx, b->top->current_key);

  if((b->top->current_key = js_malloc(ctx, len + 1))) {
    memcpy(b->top->current_key, name, len);
    b->top->current_key[len] = '\0';
  }
}

void
jbuild_value(JsonBuilder* b, jr_type_t type, const char* data, size_t len) {
  JSContext* ctx = b->ctx;
  JSValue val = JS_UNDEFINED;

  switch(type) {
    case jr_type_null: val = JS_NULL; break;
    case jr_type_true: val = JS_TRUE; break;
    case jr_type_false: val = JS_FALSE; break;
    case jr_type_number: {
      double num = 0;

      if(data) {
        char* buf;

        if((buf = js_malloc(ctx, len + 1))) {
          memcpy(buf, data, len);
          buf[len] = '\0';
          scan_double(buf, &num);
          js_free(ctx, buf);
        }
      }

      val = JS_NewFloat64(ctx, num);
      break;
    }
    case jr_type_string: {
      val = data ? JS_NewStringLen(ctx, data, len) : JS_NewString(ctx, "");
      break;
    }

    default: return;
  }

  if(!b->top) {
    b->root = val;
    b->has_root = TRUE;
  } else {
    JsonBuilderFrame* parent = b->top;

    if(parent->is_object) {
      if(parent->current_key) {
        JSAtom atom = JS_NewAtomLen(ctx, parent->current_key, strlen(parent->current_key));
        JS_SetProperty(ctx, parent->obj, atom, val);
        JS_FreeAtom(ctx, atom);
        js_free(ctx, parent->current_key);
        parent->current_key = NULL;
      } else {
        JS_FreeValue(ctx, val);
      }
    } else {
      JS_SetPropertyUint32(ctx, parent->obj, parent->index++, val);
    }
  }
}

JSValue
jbuild_path(JsonBuilder* b) {
  JSContext* ctx = b->ctx;
  JSValue ret = JS_NewArray(ctx);
  int count = 0;
  JsonBuilderFrame* f;

  for(f = b->top; f; f = f->parent)
    count++;

  BOOL has_current = FALSE;

  if(b->top) {
    if(b->top->is_object && b->top->current_key)
      has_current = TRUE;
    else if(!b->top->is_object)
      has_current = TRUE;
  }

  int total_len = count - 1 + (has_current ? 1 : 0);

  if(total_len < 0)
    total_len = 0;

  int index_to_set = total_len - 1;

  if(has_current && b->top) {
    JSValue val;

    if(b->top->is_object)
      val = JS_NewString(ctx, b->top->current_key);
    else
      val = JS_NewUint32(ctx, b->top->index);

    JS_SetPropertyUint32(ctx, ret, index_to_set--, val);
  }

  for(f = b->top; f && f->parent; f = f->parent) {
    JSValue val;

    if(f->parent->is_object)
      val = f->my_key ? JS_NewString(ctx, f->my_key) : JS_NewString(ctx, "");
    else
      val = JS_NewUint32(ctx, f->my_index);

    JS_SetPropertyUint32(ctx, ret, index_to_set--, val);
  }

  return ret;
}

JSValue
jbuild_root(JsonBuilder* b) {
  return b->has_root ? JS_DupValue(b->ctx, b->root) : JS_UNDEFINED;
}

void
jbuild_free(JsonBuilder* b, JSRuntime* rt) {
  JsonBuilderFrame* frame = b->top;

  while(frame) {
    JsonBuilderFrame* parent = frame->parent;

    if(frame->current_key)
      js_free_rt(rt, frame->current_key);
    if(frame->my_key)
      js_free_rt(rt, frame->my_key);

    JS_FreeValueRT(rt, frame->obj);
    js_free_rt(rt, frame);
    frame = parent;
  }

  b->top = NULL;
  JS_FreeValueRT(rt, b->root);
  b->root = JS_UNDEFINED;
  b->has_root = FALSE;
}
