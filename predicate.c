#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "predicate.h"
#include "libregexp.h"
#include "quickjs-predicate.h"
#include "cutils.h"

#define max(left, right) ((left) > (right) ? (left) : (right))
#define min(left, right) ((left) < (right) ? (left) : (right))

static size_t
utf8_to_unicode(const char* str, size_t len, Vector* out) {
  const uint8_t *p, *next, *end;

  for(p = (uint8_t*)str, end = p + len; p != end; p = next) {
    uint32_t codepoint = unicode_from_utf8(p, end - p, &next);
    vector_put(out, &codepoint, sizeof(uint32_t));
    p = next;
  }

  return vector_size(out, sizeof(uint32_t));
}

JSValue
predicate_eval(Predicate* pr, JSContext* ctx, Arguments* args) {
  JSValue ret = JS_UNDEFINED;

  switch(pr->id) {
    case PREDICATE_TYPE: {
      int id = js_value_type(ctx, js_arguments_shift(args));
      ret = JS_NewBool(ctx, !!(id & pr->type.flags));
      break;
    }

    case PREDICATE_CHARSET: {
      InputBuffer input = js_input_buffer(ctx, js_arguments_shift(args));
      if(pr->charset.chars.size == 0 && pr->charset.chars.data == 0) {
        vector_init(&pr->charset.chars, ctx);
        utf8_to_unicode(pr->charset.set, pr->charset.len, &pr->charset.chars);
      }
      ret = JS_NewInt32(ctx, 1);
      while(!input_buffer_eof(&input)) {
        uint32_t codepoint = input_buffer_getc(&input);
        ssize_t idx = vector_find(&pr->charset.chars, sizeof(uint32_t), &codepoint);
        if(idx == -1) {
          ret = JS_NewInt32(ctx, 0);
          break;
        }
      }
      input_buffer_free(&input, ctx);
      break;
    }

    case PREDICATE_STRING: {
      InputBuffer input = js_input_buffer(ctx, js_arguments_shift(args));

      if(input.size >= pr->string.len) {
        if(!memcmp(input.data, pr->string.str, pr->string.len))
          ret = JS_NewInt32(ctx, 1);
      }
      break;
    }

    case PREDICATE_NOTNOT: {
      ret = JS_NewBool(ctx, !!JS_ToBool(ctx, predicate_value(ctx, pr->unary.predicate, args)));
      break;
    }

    case PREDICATE_NOT: {
      ret = JS_NewBool(ctx, !JS_ToBool(ctx, predicate_value(ctx, pr->unary.predicate, args)));
      break;
    }

    case PREDICATE_ADD:
    case PREDICATE_SUB:
    case PREDICATE_MUL:
    case PREDICATE_DIV:
    case PREDICATE_MOD: {
      JSValue values[2] = {pr->binary.left, pr->binary.right};
      BOOL nullish[2] = {js_is_null_or_undefined(pr->binary.left), js_is_null_or_undefined(pr->binary.right)};
      size_t i, j = 0;
      int64_t left, right, r;
      for(i = 0; i < 2; i++) {
        if(nullish[i]) {
          values[i] = js_arguments_shift(args);
        }
        values[i] = predicate_value(ctx, values[0], args);
      }

      JS_ToInt64(ctx, &left, values[0]);
      JS_ToInt64(ctx, &right, values[1]);

      switch(pr->id) {

        case PREDICATE_ADD: r = left + right; break;
        case PREDICATE_SUB: r = left - right; break;
        case PREDICATE_MUL: r = left * right; break;
        case PREDICATE_DIV: r = left / right; break;
        case PREDICATE_MOD: r = left % right; break;
        default: break;
      }

      ret = JS_NewInt64(ctx, r);
      break;
    }

    case PREDICATE_OR: {
      size_t i;

      for(i = 0; i < pr->boolean.npredicates; i++) {
        if(JS_ToBool(ctx, (ret = predicate_value(ctx, pr->boolean.predicates[i], args))))
          break;
      }
      break;
    }

    case PREDICATE_AND: {
      size_t i;

      for(i = 0; i < pr->boolean.npredicates; i++) {
        if(!JS_ToBool(ctx, (ret = predicate_value(ctx, pr->boolean.predicates[i], args))))
          break;
      }
      break;
    }

    case PREDICATE_XOR: {
      size_t i;
      int64_t r = 0;
      for(i = 0; i < pr->boolean.npredicates; i++) {
        int64_t i64 = 0;
        JS_ToInt64(ctx, &i64, predicate_value(ctx, pr->boolean.predicates[i], args));
        r ^= i64;
      }
      ret = JS_NewInt64(ctx, r);
      break;
    }

    case PREDICATE_REGEXP: {
      JSValue re = js_arguments_shift(args);
      InputBuffer input = js_input_buffer(ctx, re);
      uint8_t* capture[CAPTURE_COUNT_MAX * 2];
      int capture_count = 0, result;

      if(pr->regexp.bytecode == 0)
        capture_count = predicate_regexp_compile(pr, ctx);

      result = lre_exec(capture, pr->regexp.bytecode, (uint8_t*)input.data, 0, input.size, 0, ctx);

      if(result && args->c > 1) {
        JSValue arg = js_arguments_shift(args);

        if(JS_IsFunction(ctx, arg)) {
          JSValue args[] = {predicate_regexp_capture(capture, capture_count, input.data, ctx), re};

          JS_Call(ctx, arg, JS_NULL, 2, args);

          JS_FreeValue(ctx, args[0]);

        } else if(JS_IsArray(ctx, arg)) {
          int i;
          JS_SetPropertyStr(ctx, arg, "length", JS_NewUint32(ctx, capture_count));
          for(i = 0; i < 2 * capture_count; i += 2) {
            JSValue left = JS_NULL;

            if(capture[i]) {
              left = JS_NewArray(ctx);
              JS_SetPropertyUint32(ctx, left, 0, JS_NewUint32(ctx, capture[i] - input.data));
              JS_SetPropertyUint32(ctx, left, 1, JS_NewUint32(ctx, capture[i + 1] - input.data));
            }

            JS_SetPropertyUint32(ctx, arg, i >> 1, left);
          }
        }
      }

      ret = JS_NewBool(ctx, result);
      break;
    }

    case PREDICATE_INSTANCEOF: {
      ret = JS_NewBool(ctx, JS_IsInstanceOf(ctx, js_arguments_shift(args), pr->unary.predicate));
      break;
    }

    case PREDICATE_PROTOTYPEIS: {
      JSValue proto = JS_GetPrototype(ctx, js_arguments_shift(args));

      ret = JS_NewBool(ctx, JS_VALUE_GET_OBJ(proto) == JS_VALUE_GET_OBJ(pr->unary.predicate));
      break;
    }

    case PREDICATE_EQUAL: {
      ret = JS_NewBool(ctx, js_value_equals(ctx, js_arguments_shift(args), pr->unary.predicate));
      break;
    }

    case PREDICATE_PROPERTY: {
      int j = 0;
      JSAtom prop = pr->property.atom;
      JSValue obj = pr->property.predicate;

      if(!prop && j < args->c)
        prop = JS_ValueToAtom(ctx, js_arguments_shift(args));

      if(js_is_falsish(obj) && j < args->c && JS_IsObject(args->v[j]))
        obj = js_arguments_shift(args);

      if(JS_GetOpaque(obj, js_predicate_class_id))
        obj = predicate_value(ctx, obj, args);
      else // if(JS_IsObject(ctx, obj))
        obj = JS_DupValue(ctx, obj);

      if(JS_IsObject(obj)) {
        ret = JS_GetProperty(ctx, obj, prop);

      } else {
        ret = JS_ThrowTypeError(ctx, "target must be object, but is %s", js_value_typestr(ctx, obj));
      }
      break;
    }
    case PREDICATE_SHIFT: {
      int shift = min(args->c, pr->shift.n);

      if(pr->shift.n <= args->c) {

        js_arguments_shiftn(args, pr->shift.n);
        ret = predicate_value(ctx, pr->shift.predicate, args);
      }
      break;
    }
    default: {
      assert(0);
      break;
    }
  }

  return ret;
}

JSValue
predicate_call(JSContext* ctx, JSValueConst value, int argc, JSValueConst argv[]) {
  Predicate* pred;
  JSValue ret = JS_UNDEFINED;

  if((pred = JS_GetOpaque(value, js_predicate_class_id))) {
    Arguments args = js_arguments_new(argc, argv);
    return predicate_eval(pred, ctx, &args);
  }

  if(JS_IsFunction(ctx, value)) {
    ret = JS_Call(ctx, value, JS_UNDEFINED, argc, argv);
    /* if(JS_IsException(ret)) {
       result = -1;
     } else {
       JS_ToInt32(ctx, &result, ret);
      }
     JS_FreeValue(ctx, ret);
     return result;*/
  }

  return ret;
}

JSValue
predicate_value(JSContext* ctx, JSValueConst value, Arguments* args) {
  Predicate* pred;
  JSValue ret = JS_EXCEPTION;

  if((pred = JS_GetOpaque(value, js_predicate_class_id)))
    return predicate_eval(pred, ctx, args);

  if(JS_IsFunction(ctx, value))
    return predicate_call(ctx, value, args->c, args->v);

  ret = JS_DupValue(ctx, value);

  return ret;
}

const char*
predicate_typename(const Predicate* pr) {
  return ((const char*[]){
      "TYPE", "CHARSET", "STRING", "NOTNOT", "NOT",        "ADD",         "SUB",   "MUL",      "DIV",   "MOD",
      "OR",   "AND",     "XOR",    "REGEXP", "INSTANCEOF", "PROTOTYPEIS", "EQUAL", "PROPERTY", "SHIFT", 0,
  })[pr->id];
}

void
predicate_tostring(const Predicate* pr, JSContext* ctx, DynBuf* dbuf) {
  const char* type = predicate_typename(pr);

  dbuf_putstr(dbuf, type);
  // dbuf_putc(dbuf, ' ');

  switch(pr->id) {
    case PREDICATE_TYPE: {
      dbuf_putstr(dbuf, "type == ");
      dbuf_bitflags(dbuf,
                    pr->type.flags,
                    ((const char* const[]){
                        "UNDEFINED",
                        "NULL",
                        "BOOL",
                        "INT",
                        "OBJECT",
                        "STRING",
                        "SYMBOL",
                        "BIG_FLOAT",
                        "BIG_INT",
                        "BIG_DECIMAL",
                        "FLOAT64",
                        "FUNCTION",
                        "ARRAY",
                    }));
      break;
    }

    case PREDICATE_CHARSET: {
      uint32_t i = 0, *p;
      dbuf_putstr(dbuf, "[ ");

      vector_foreach(&pr->charset.chars, sizeof(uint32_t), p) {
        if(i > 0)
          dbuf_putstr(dbuf, ", ");
        if(*p < 128)
          dbuf_printf(dbuf, "'%c'", (char)*p);
        else
          dbuf_printf(dbuf, *p > 0xffffff ? "'\\u%08x'" : *p > 0xffff ? "\\u%06x" : "'\\u%04x'", *p);
        i++;
      }
      dbuf_printf(dbuf, " (len = %zu) ]", pr->charset.len);
      break;
    }

    case PREDICATE_STRING: {
      dbuf_putc(dbuf, '"');
      dbuf_append(dbuf, pr->string.str, pr->string.len);
      dbuf_putc(dbuf, '"');
      dbuf_printf(dbuf, " (len = %zu)", pr->string.len);
      break;
    }

    case PREDICATE_NOTNOT: dbuf_putc(dbuf, '!'); __attribute__((fallthrough));
    case PREDICATE_NOT: {
      dbuf_putstr(dbuf, "!( ");
      dbuf_put_value(dbuf, ctx, pr->unary.predicate);
      dbuf_putstr(dbuf, " )");
      break;
    }

    case PREDICATE_ADD:
    case PREDICATE_SUB:
    case PREDICATE_MUL:
    case PREDICATE_DIV:
    case PREDICATE_MOD: {
      size_t i;
      static const char* op_str[] = {" + ", " - ", " * ", " / ", " % "};
      dbuf_putstr(dbuf, "(");
      dbuf_put_value(dbuf, ctx, pr->binary.left);

      dbuf_putstr(dbuf, op_str[pr->id - PREDICATE_ADD]);
      dbuf_put_value(dbuf, ctx, pr->binary.right);
      dbuf_putstr(dbuf, ")");
      break;
    }

    case PREDICATE_AND:
    case PREDICATE_OR:
    case PREDICATE_XOR: {
      size_t i;
      dbuf_putstr(dbuf, "( ");

      for(i = 0; i < pr->boolean.npredicates; i++) {
        if(i > 0)
          dbuf_putstr(dbuf, pr->id == PREDICATE_XOR ? " ^ " : pr->id == PREDICATE_AND ? " && " : " || ");

        dbuf_put_value(dbuf, ctx, pr->boolean.predicates[i]);
      }
      dbuf_putstr(dbuf, " )");

      break;
    }

    case PREDICATE_REGEXP: {
      char flagbuf[16];

      dbuf_putc(dbuf, '/');
      dbuf_append(dbuf, pr->regexp.expr.source, pr->regexp.expr.len);
      dbuf_putc(dbuf, '/');
      dbuf_append(dbuf, flagbuf, regexp_flags_tostring(pr->regexp.expr.flags, flagbuf));
      dbuf_0(dbuf);
      break;
    }

    case PREDICATE_INSTANCEOF: {
      const char* name = js_function_name(ctx, pr->unary.predicate);
      dbuf_putstr(dbuf, name);
      js_cstring_free(ctx, name);
      break;
    }

    case PREDICATE_PROTOTYPEIS: {
      const char* name = js_object_tostring(ctx, pr->unary.predicate);
      dbuf_putstr(dbuf, name);
      js_cstring_free(ctx, name);
      break;
    }

    case PREDICATE_EQUAL: {
      js_value_dump(ctx, pr->unary.predicate, dbuf);
      break;
    }

    case PREDICATE_PROPERTY: {
      js_atom_dump(ctx, pr->property.atom, dbuf, TRUE);
      dbuf_putc(dbuf, ' ');

      js_value_dump(ctx, pr->property.predicate, dbuf);
      break;
    }

    case PREDICATE_SHIFT: {
      dbuf_printf(dbuf, ">> %d", pr->shift.n);
      dbuf_putc(dbuf, ' ');
      js_value_dump(ctx, pr->shift.predicate, dbuf);
      break;
    }

    default: assert(0); break;
  }
}

JSValue
predicate_regexp_capture(uint8_t** capture, int capture_count, uint8_t* input, JSContext* ctx) {
  int i;
  uint32_t buf[capture_count * 2];
  memset(buf, 0, sizeof(buf));

  for(i = 0; i < 2 * capture_count; i += 2) {
    if(capture[i]) {
      buf[i] = capture[i] - input;
      buf[i + 1] = capture[i + 1] - input;
    }
  }

  return JS_NewArrayBufferCopy(ctx, (const uint8_t*)buf, (capture_count * 2) * sizeof(uint32_t));
}

void
predicate_free_rt(Predicate* pred, JSRuntime* rt) {
  switch(pred->id) {
    case PREDICATE_TYPE: {
      break;
    }

    case PREDICATE_CHARSET: {
      js_free_rt(rt, pred->charset.set);
      vector_free(&pred->charset.chars);
      break;
    }

    case PREDICATE_STRING: {
      js_free_rt(rt, pred->string.str);
      break;
    }

    case PREDICATE_EQUAL:
    case PREDICATE_INSTANCEOF:
    case PREDICATE_PROTOTYPEIS:
    case PREDICATE_NOTNOT:
    case PREDICATE_NOT: {
      JS_FreeValueRT(rt, pred->unary.predicate);
      break;
    }

    case PREDICATE_ADD:
    case PREDICATE_SUB:
    case PREDICATE_MUL:
    case PREDICATE_DIV:
    case PREDICATE_MOD: {
      JS_FreeValueRT(rt, pred->binary.left);
      JS_FreeValueRT(rt, pred->binary.right);
      break;
    }

    case PREDICATE_AND:
    case PREDICATE_OR:
    case PREDICATE_XOR: {
      js_values_free(rt, pred->boolean.npredicates, pred->boolean.predicates);
      break;
    }

    case PREDICATE_REGEXP: {
      // if(pred->regexp.bytecode) js_free_rt(rt, pred->regexp.bytecode);
      js_free_rt(rt, pred->regexp.expr.source);
      break;
    }
    case PREDICATE_PROPERTY: {
      JS_FreeAtomRT(rt, pred->property.atom);
      JS_FreeValueRT(rt, pred->property.predicate);

      break;
    }
    case PREDICATE_SHIFT: {
      JS_FreeValueRT(rt, pred->shift.predicate);
      break;
    }
  }
  memset(pred, 0, sizeof(Predicate));
}

JSValue
predicate_values(const Predicate* pred, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  switch(pred->id) {
    case PREDICATE_TYPE:
    case PREDICATE_CHARSET:
    case PREDICATE_STRING:
    case PREDICATE_REGEXP: {
      break;
    }

    case PREDICATE_EQUAL:
    case PREDICATE_INSTANCEOF:
    case PREDICATE_PROTOTYPEIS:
    case PREDICATE_NOTNOT:
    case PREDICATE_NOT: {
      ret = js_values_toarray(ctx, 1, (JSValue*)&pred->unary.predicate);
      break;
    }

    case PREDICATE_ADD:
    case PREDICATE_SUB:
    case PREDICATE_MUL:
    case PREDICATE_DIV:
    case PREDICATE_MOD: {
      ret = js_values_toarray(ctx, 2, (JSValue*)&pred->binary.left);
      break;
    }

    case PREDICATE_OR:
    case PREDICATE_AND:
    case PREDICATE_XOR: {
      ret = js_values_toarray(ctx, pred->boolean.npredicates, pred->boolean.predicates);
      break;
    }

    case PREDICATE_PROPERTY: {
      ret = JS_DupValue(ctx, pred->property.predicate);
      break;
    }
    case PREDICATE_SHIFT: {
      ret = JS_DupValue(ctx, pred->shift.predicate);
      break;
    }
  }
  return ret;
}

Predicate*
predicate_clone(const Predicate* pred, JSContext* ctx) {
  Predicate* ret = js_mallocz(ctx, sizeof(Predicate));

  ret->id = pred->id;

  switch(pred->id) {
    case PREDICATE_TYPE: {
      ret->type.flags = pred->type.flags;
      break;
    }

    case PREDICATE_CHARSET: {
      ret->charset.len = pred->charset.len;
      ret->charset.set = js_strndup(ctx, pred->charset.set, pred->charset.len);
      vector_copy(&ret->charset.chars, &pred->charset.chars);
      break;
    }

    case PREDICATE_STRING: {
      ret->string.len = pred->string.len;
      ret->string.str = js_strndup(ctx, pred->string.str, pred->string.len);
      break;
    }

    case PREDICATE_EQUAL:
    case PREDICATE_INSTANCEOF:
    case PREDICATE_PROTOTYPEIS:
    case PREDICATE_NOTNOT:
    case PREDICATE_NOT: {
      ret->unary.predicate = JS_DupValue(ctx, pred->unary.predicate);
      break;
    }

    case PREDICATE_ADD:
    case PREDICATE_SUB:
    case PREDICATE_MUL:
    case PREDICATE_DIV:
    case PREDICATE_MOD: {
      ret->binary.left = JS_DupValue(ctx, pred->binary.left);
      ret->binary.right = JS_DupValue(ctx, pred->binary.right);
      break;
    }

    case PREDICATE_OR:
    case PREDICATE_AND:
    case PREDICATE_XOR: {
      ret->boolean.npredicates = pred->boolean.npredicates;
      ret->boolean.predicates = js_values_dup(ctx, pred->boolean.npredicates, pred->boolean.predicates);
      break;
    }

    case PREDICATE_REGEXP: {
      ret->regexp.expr.source = js_strndup(ctx, pred->regexp.expr.source, pred->regexp.expr.len);
      ret->regexp.expr.len = pred->regexp.expr.len;
      ret->regexp.expr.flags = pred->regexp.expr.flags;
      ret->regexp.bytecode = 0;
      break;
    }
    case PREDICATE_PROPERTY: {
      ret->property.atom = JS_DupAtom(ctx, pred->property.atom);
      ret->property.predicate = JS_DupValue(ctx, pred->property.predicate);
      break;
    }
    case PREDICATE_SHIFT: {
      ret->shift.n = pred->shift.n;
      ret->shift.predicate = JS_DupValue(ctx, pred->shift.predicate);
      break;
    }
  }

  return ret;
}

int
predicate_regexp_compile(Predicate* pred, JSContext* ctx) {
  assert(pred->id == PREDICATE_REGEXP);
  assert(pred->regexp.bytecode == 0);

  if((pred->regexp.bytecode = regexp_compile(pred->regexp.expr, ctx)))
    return lre_get_capture_count(pred->regexp.bytecode);

  return 0;
}
