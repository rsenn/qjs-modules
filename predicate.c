#define _GNU_SOURCE
#include "predicate.h"

BOOL
predicate_eval(const Predicate* pr, JSContext* ctx, JSValueConst arg) {
  BOOL ret = FALSE;

  switch(pr->id) {
    case PREDICATE_TYPE: {
      int32_t id = js_value_type(arg);
      ret = pr->type.flags == id;
      break;
    }
    case PREDICATE_CHARSET: {
      size_t len;
      const char* str = JS_ToCStringLen(ctx, &len, arg);
      ret = byte_chr(pr->charset.set, pr->charset.len, str[0]) < pr->charset.len;
      break;
    }
    case PREDICATE_NOT: {
      JSValue value;
      value = JS_Call(ctx, pr->not .fn, JS_UNDEFINED, 1, &arg);
      ret = JS_ToBool(ctx, value);
      JS_FreeValue(ctx, value);
      break;
    }
    case PREDICATE_OR: {
      JSValue va, vb;
      va = JS_Call(ctx, pr->or.a, JS_UNDEFINED, 1, &arg);
      ret = JS_ToBool(ctx, va);
      JS_FreeValue(ctx, va);
      if(!ret) {
        vb = JS_Call(ctx, pr->or.b, JS_UNDEFINED, 1, &arg);
        ret = JS_ToBool(ctx, vb);
        JS_FreeValue(ctx, vb);
      }
      break;
    }
    case PREDICATE_AND: {
      JSValue va, vb;
      va = JS_Call(ctx, pr->and.a, JS_UNDEFINED, 1, &arg);
      ret = JS_ToBool(ctx, va);
      JS_FreeValue(ctx, va);
      if(ret) {
        vb = JS_Call(ctx, pr->and.b, JS_UNDEFINED, 1, &arg);
        ret = JS_ToBool(ctx, vb);
        JS_FreeValue(ctx, vb);
      }
      break;
    }

    default: assert(0); break;
  }
  return ret;
}
