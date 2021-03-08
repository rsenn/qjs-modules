#define _GNU_SOURCE
#include "predicate.h"
#include "quickjs-predicate.h"

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
      size_t i, len;
      const char* str = JS_ToCStringLen(ctx, &len, arg);

      ret = 1;

      for(i = 0; i < len; i++)
        if(byte_chr(pr->charset.set, pr->charset.len, str[0]) == pr->charset.len) {
          ret = 0;
          break;
        }

      break;
    }
    case PREDICATE_NOT: {
      JSValue value;
      ret = js_predicate_call(ctx, pr->unary.fn, arg) == 0;
      JS_FreeValue(ctx, value);
      break;
    }
    case PREDICATE_OR: {
      ret = js_predicate_call(ctx, pr->binary.a, arg);
      if(!ret)
        ret = js_predicate_call(ctx, pr->binary.b, arg);
      break;
    }
    case PREDICATE_AND: {
      ret = js_predicate_call(ctx, pr->binary.a, arg);
      if(ret)
        ret = js_predicate_call(ctx, pr->binary.b, arg);
      break;
    }
    case PREDICATE_XOR: {
      ret = js_predicate_call(ctx, pr->binary.a, arg) ^
            js_predicate_call(ctx, pr->binary.b, arg);
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
      dbuf_putstr(dbuf, "[ '");
      dbuf_put_escaped(dbuf, pr->charset.set, pr->charset.len);
      dbuf_printf(dbuf, "' len=%zx ]", pr->charset.len);
      break;
    }
    case PREDICATE_NOT: {
      dbuf_putstr(dbuf, "!( ");
      dbuf_put_value(dbuf, ctx, pr->unary.fn);
      dbuf_putstr(dbuf, " )");
      break;
    }
    case PREDICATE_AND:
    case PREDICATE_OR:
    case PREDICATE_XOR: {
      dbuf_putstr(dbuf, "( ");
      dbuf_put_value(dbuf, ctx, pr->binary.a);
      dbuf_putstr(dbuf,
                  pr->id == PREDICATE_XOR ? " ^ "
                                          : pr->id == PREDICATE_AND ? " && " : " || ");
      dbuf_put_value(dbuf, ctx, pr->binary.a);
      dbuf_putstr(dbuf, " )");

      break;
    }

    default: assert(0); break;
  }
}
