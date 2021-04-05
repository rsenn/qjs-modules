#define _GNU_SOURCE
#include "predicate.h"
#include "libregexp.h"
#include "quickjs-predicate.h"

int
predicate_eval(const Predicate* pr, JSContext* ctx, int argc, JSValueConst* argv) {
  int ret = 0;

  switch(pr->id) {
    case PREDICATE_TYPE: {
      int id = js_value_type(argv[0]);
      ret = pr->type.flags == id;
      break;
    }
    case PREDICATE_INSTANCEOF: {
      ret = JS_IsInstanceOf(ctx, argv[0], pr->unary.value);
      break;
    }
    case PREDICATE_PROTOTYPE: {
      JSValue proto = JS_GetPrototype(ctx, argv[0]);

      ret = JS_VALUE_GET_OBJ(proto) == JS_VALUE_GET_OBJ(pr->unary.value);
      break;
    }
    case PREDICATE_CHARSET: {
      InputBuffer input = js_input_buffer(ctx, argv[0]);
      ret = 1;
      for(input.p = 0; input.p < input.n; input.p++) {
        if(byte_chr(pr->charset.set, pr->charset.len, input.x[input.p]) == pr->charset.len) {
          ret = 0;
          break;
        }
      }
      input_buffer_free(&input, ctx);
      break;
    }
    case PREDICATE_NOTNOT: {
      ret = !!predicate_call(ctx, pr->unary.value, argc, argv);
      break;
    }
    case PREDICATE_NOT: {
      ret = !predicate_call(ctx, pr->unary.value, argc, argv);
      break;
    }
    case PREDICATE_OR: {
      size_t i;

      for(i = 0; i < pr->boolean.nvalues; i++) {
        if((ret = predicate_call(ctx, pr->boolean.values[i], argc, argv)) == 1)
          break;
      }
      break;
    }
    case PREDICATE_AND: {
      size_t i;

      for(i = 0; i < pr->boolean.nvalues; i++) {
        if((ret = predicate_call(ctx, pr->boolean.values[i], argc, argv)) != 1)
          break;
      }
      break;
    }
    case PREDICATE_XOR: {
      size_t i;

      for(i = 0; i < pr->boolean.nvalues; i++) { ret ^= predicate_call(ctx, pr->boolean.values[i], argc, argv); }
      break;
    }
    case PREDICATE_REGEXP: {
      const size_t CAPTURE_COUNT_MAX = 255;
      InputBuffer input = js_input_buffer(ctx, argv[0]);
      uint8_t* capture[CAPTURE_COUNT_MAX * 2];
      ret = lre_exec(capture, pr->regexp.bytecode, (uint8_t*)input.x, 0, input.n, 0, ctx);

      if(ret && argc > 1) {
        int capture_count = lre_get_capture_count(pr->regexp.bytecode);

        if(JS_IsFunction(ctx, argv[1])) {
          JSValue args[] = {predicate_regexp_capture(capture, capture_count, input.x, ctx), argv[0]};

          JS_Call(ctx, argv[1], JS_NULL, 2, args);

          JS_FreeValue(ctx, args[0]);

        } else if(JS_IsArray(ctx, argv[1])) {
          int i;
          JS_SetPropertyStr(ctx, argv[1], "length", JS_NewUint32(ctx, capture_count));
          for(i = 0; i < 2 * capture_count; i += 2) {
            JSValue a = JS_NULL;

            if(capture[i]) {
              a = JS_NewArray(ctx);
              JS_SetPropertyUint32(ctx, a, 0, JS_NewUint32(ctx, capture[i] - input.x));
              JS_SetPropertyUint32(ctx, a, 1, JS_NewUint32(ctx, capture[i + 1] - input.x));
            }

            JS_SetPropertyUint32(ctx, argv[1], i >> 1, a);
          }
        }
      }

      break;
    }

    default: assert(0); break;
  }
  return ret;
}

int
predicate_call(JSContext* ctx, JSValueConst value, int argc, JSValueConst* argv) {
  Predicate* pred;

  if((pred = JS_GetOpaque2(ctx, value, js_predicate_class_id)))
    return predicate_eval(pred, ctx, argc, argv);

  if(JS_IsFunction(ctx, value)) {
    int result = 0;
    JSValue ret;
    ret = JS_Call(ctx, value, JS_UNDEFINED, argc, argv);
    if(JS_IsException(ret)) {
      result = -1;
    } else {
      result = !!JS_ToBool(ctx, ret);
      JS_FreeValue(ctx, ret);
    }
    return result;
  }
  assert(0);
  return -1;
}

void
predicate_tostring(const Predicate* pr, JSContext* ctx, DynBuf* dbuf) {
  int ret = 0;

  dbuf_putstr(dbuf, ((const char*[]){"TYPE", "CHARSET", "NOTNOT", "NOT", "OR", "AND", "XOR", "REGEXP", 0})[pr->id]);
  dbuf_putc(dbuf, ' ');

  switch(pr->id) {
    case PREDICATE_TYPE: {
      dbuf_printf(dbuf, "id == %d\n", pr->type.flags);
      break;
    }
    case PREDICATE_CHARSET: {
      dbuf_putstr(dbuf, "[ \"");
      dbuf_put_escaped(dbuf, pr->charset.set, pr->charset.len);
      dbuf_printf(dbuf, "\" len = %zu ]", pr->charset.len);
      break;
    }
    case PREDICATE_NOTNOT: dbuf_putc(dbuf, '!');

    case PREDICATE_NOT: {
      dbuf_putstr(dbuf, "!( ");
      dbuf_put_value(dbuf, ctx, pr->unary.value);
      dbuf_putstr(dbuf, " )");
      break;
    }
    case PREDICATE_AND:
    case PREDICATE_OR:
    case PREDICATE_XOR: {
      int i;
      dbuf_putstr(dbuf, "( ");

      for(i = 0; i < pr->boolean.nvalues; i++) {
        if(i > 0)
          dbuf_putstr(dbuf, pr->id == PREDICATE_XOR ? " ^ " : pr->id == PREDICATE_AND ? " && " : " || ");

        dbuf_put_value(dbuf, ctx, pr->boolean.values[i]);
      }
      dbuf_putstr(dbuf, " )");

      break;
    }
    case PREDICATE_REGEXP: {
      int flags = lre_get_flags(pr->regexp.bytecode);
      char flagbuf[16];

      dbuf_putc(dbuf, '/');
      dbuf_putstr(dbuf, pr->regexp.expr);
      dbuf_putc(dbuf, '/');
      dbuf_put(dbuf, flagbuf, predicate_regexp_flags2str(flags, flagbuf));
      dbuf_0(dbuf);
      break;
    }

    default: assert(0); break;
  }
}

Predicate
predicate_regexp(const char* regexp, int flags, void* opaque) {
  Predicate ret = PREDICATE_INIT(PREDICATE_REGEXP);
  uint8_t* bc;
  char error_msg[64];
  ret.regexp.expr = strdup(regexp);
  ret.regexp.bytecode =
      lre_compile(&ret.regexp.len, error_msg, sizeof(error_msg), regexp, strlen(regexp), flags, opaque);
  return ret;
}

int
predicate_regexp_str2flags(const char* s) {
  int flags = 0;

  if(str_contains(s, 'g'))
    flags |= LRE_FLAG_GLOBAL;
  if(str_contains(s, 'i'))
    flags |= LRE_FLAG_IGNORECASE;
  if(str_contains(s, 'm'))
    flags |= LRE_FLAG_MULTILINE;
  if(str_contains(s, 's'))
    flags |= LRE_FLAG_DOTALL;
  if(str_contains(s, 'u'))
    flags |= LRE_FLAG_UTF16;
  if(str_contains(s, 'y'))
    flags |= LRE_FLAG_STICKY;

  return flags;
}

int
predicate_regexp_flags2str(int flags, char* buf) {
  char* out = buf;

  if(flags & LRE_FLAG_GLOBAL)
    *out++ = 'g';
  if(flags & LRE_FLAG_IGNORECASE)
    *out++ = 'i';
  if(flags & LRE_FLAG_MULTILINE)
    *out++ = 'm';
  if(flags & LRE_FLAG_DOTALL)
    *out++ = 's';
  if(flags & LRE_FLAG_UTF16)
    *out++ = 'u';
  if(flags & LRE_FLAG_STICKY)
    *out++ = 'y';

  *out = '\0';
  return out - buf;
}

JSValue
predicate_regexp_capture(uint8_t* capture[], int capture_count, uint8_t* input, JSContext* ctx) {
  int i;
  uint32_t buf[capture_count * 2];
  memset(buf, 0, sizeof(buf));

  for(i = 0; i < 2 * capture_count; i += 2) {
    if(capture[i]) {
      buf[i] = capture[i] - input;
      buf[i + 1] = capture[i + 1] - input;
    }
  }

  return JS_NewArrayBufferCopy(ctx, buf, (capture_count * 2) * sizeof(uint32_t));
}

void
predicate_free_rt(Predicate* pred, JSRuntime* rt) {
  switch(pred->id) {
    case PREDICATE_CHARSET: {
      js_free_rt(rt, pred->charset.set);
      break;
    }
    case PREDICATE_NOTNOT:
    case PREDICATE_NOT: {
      JS_FreeValueRT(rt, pred->unary.value);
      break;
    }
    case PREDICATE_AND:
    case PREDICATE_OR:
    case PREDICATE_XOR: {
      js_values_free_rt(rt, pred->boolean.nvalues, pred->boolean.values);
      break;
    }
    case PREDICATE_REGEXP: {
      js_free_rt(rt, pred->regexp.bytecode);
      break;
    }
  }
  memset(pred, 0, sizeof(Predicate));
}
