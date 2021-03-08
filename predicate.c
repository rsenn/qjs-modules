#define _GNU_SOURCE
#include "predicate.h"

int32_t
predicate_eval(const Predicate* pr, JSContext* ctx, JSValueConst arg) {
  int32_t ret = 0;

  switch(pr->id) {
    case PREDICATE_TYPE: {
      int32_t id = js_value_type(arg);
      ret = pr->type.flags == id;
      break;
    }
    case PREDICATE_CHARSET: {
      size_t len;
      const char* str = JS_ToCStringLen(ctx, &len, arg);
      ret = !!(byte_chr(pr->charset.set, pr->charset.len, str[0]) < pr->charset.len);
      break;
    }
    case PREDICATE_NOT: {
      JSValue value;
      value = JS_Call(ctx, pr->not .fn, JS_UNDEFINED, 1, &arg);
      ret = !!JS_ToBool(ctx, value);
      JS_FreeValue(ctx, value);
      break;
    }
    case PREDICATE_OR: {
      JSValue va, vb;
      va = JS_Call(ctx, pr->or.a, JS_UNDEFINED, 1, &arg);
      ret = !!JS_ToBool(ctx, va);
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
      ret = !!JS_ToBool(ctx, va);
      JS_FreeValue(ctx, va);
      if(ret) {
        vb = JS_Call(ctx, pr->and.b, JS_UNDEFINED, 1, &arg);
        ret = !!JS_ToBool(ctx, vb);
        JS_FreeValue(ctx, vb);
      }
      break;
    }

    default: assert(0); break;
  }
  return ret;
}

void
predicate_tostring(const Predicate* pr, JSContext* ctx, DynBuf* dbuf) {
  int32_t ret = 0;

  dbuf_putstr(dbuf, ((const char*[]){"TYPE", "CHARSET", "NOT", "OR", 0})[pr->id]);
  dbuf_putc(dbuf, ' ');
  switch(pr->id) {
    case PREDICATE_TYPE: {
      dbuf_printf(dbuf, "id == %d\n", pr->type.flags);
      break;
    }
    case PREDICATE_CHARSET: {
      dbuf_putstr(dbuf, "charset '");
      dbuf_put_escaped(dbuf, pr->charset.set, pr->charset.len);
      dbuf_printf(dbuf, "' setlen=%zx\n", pr->charset.len);
      break;
    }
    case PREDICATE_NOT: {
      break;
    }
    case PREDICATE_OR: {
      break;
    }
    case PREDICATE_AND: {
      break;
    }

    default: assert(0); break;
  }
}
