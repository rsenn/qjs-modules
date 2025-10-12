#include "defines.h"
#include <quickjs.h>
#include "quickjs-predicate.h"
#include "buffer-utils.h"
#include "debug.h"

/**
 * \addtogroup quickjs-predicate
 * @{
 */
VISIBLE JSClassID js_predicate_class_id = 0;
static JSValue predicate_proto, predicate_ctor;

enum PredicateId
predicate_id(JSValueConst value) {
  Predicate* pred;
  enum PredicateId ret = -1;

  if((pred = JS_GetOpaque(value, js_predicate_class_id)))
    ret = pred->id;

  return ret;
}

static JSValue
predicate_constant(const Predicate* pr, JSContext* ctx, BOOL color) {
  DynBuf dbuf = {0};

  dbuf_init2(&dbuf, 0, 0);
  // js_dbuf_init(ctx, &dbuf);

  dbuf_putstr(&dbuf, color ? "Predicate" COLOR_CYAN : "Predicate");
  dbuf_putstr(&dbuf, color ? "." COLOR_YELLOW : ".");
  dbuf_putstr(&dbuf, predicate_typename(pr));
  dbuf_putstr(&dbuf, color ? COLOR_NONE : "");

  return dbuf_tostring_free(&dbuf, ctx);
}

static JSValue
predicate_duparg(JSContext* ctx, JSValueConst value) {
  if(js_is_null_or_undefined(value))
    return JS_UNDEFINED;

  return JS_DupValue(ctx, value);
}

static JSValue
predicate_nextarg(JSContext* ctx, JSArguments* args) {
  return predicate_duparg(ctx, js_arguments_shift(args));
}

VISIBLE JSValue
js_predicate_new(JSContext* ctx, JSValueConst proto, JSValueConst value) {
  Predicate* pr;
  JSValue obj;

  if(!(pr = js_mallocz(ctx, sizeof(Predicate))))
    return JS_EXCEPTION;

  pr->id = -1;
  obj = JS_NewObjectProtoClass(ctx, proto, js_predicate_class_id);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, pr);
  return obj;

fail:
  js_free(ctx, pr);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

VISIBLE JSValue
js_predicate_wrap(JSContext* ctx, Predicate pr) {
  JSValue obj;
  Predicate* ret;

  if(!(ret = js_mallocz(ctx, sizeof(Predicate))))
    return JS_EXCEPTION;

  *ret = pr;

  obj = JS_NewObjectProtoClass(ctx, predicate_proto, js_predicate_class_id);
  JS_SetOpaque(obj, ret);
  return obj;
}

static JSValue
js_predicate_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  Predicate* pr;
  JSValue obj = JS_UNDEFINED, proto = JS_UNDEFINED;

  if(!(pr = js_mallocz(ctx, sizeof(Predicate))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_predicate_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, pr);

  if(argc > 0 && JS_IsString(argv[0])) {
    size_t len = 0;
    const char* str = js_tostringlen(ctx, &len, argv[0]);

    *pr = predicate_string(str, len);
  } else if(argc > 0 && js_is_regexp(ctx, argv[0])) {
    argc++;
    argv--;

    goto regexp;
  } else if(argc > 0 && JS_IsNumber(argv[0])) {
    int32_t id;
    JSArguments args = js_arguments_new(argc, argv);

    JS_ToInt32(ctx, &id, js_arguments_shift(&args));

    switch(id) {
      case PREDICATE_TYPE: {
        id = -1;
        JS_ToInt32(ctx, &id, js_arguments_shift(&args));
        *pr = predicate_type(id);
        break;
      }

      case PREDICATE_CHARSET: {
        size_t len = 0;
        JSValue lenv;
        const char* str = js_tostringlen(ctx, &len, js_arguments_shift(&args));

        if(JS_IsNumber((lenv = js_arguments_shift(&args))))
          js_value_tosize(ctx, &len, lenv);

        *pr = predicate_charset(str, len);
        break;
      }

      case PREDICATE_STRING: {
        size_t len = 0;
        const char* str = js_tostringlen(ctx, &len, js_arguments_shift(&args));

        *pr = predicate_string(str, len);
        break;
      }

      case PREDICATE_NOTNOT: {
        *pr = predicate_notnot(predicate_nextarg(ctx, &args));
        break;
      }

      case PREDICATE_NOT: {
        *pr = predicate_not(predicate_nextarg(ctx, &args));
        break;
      }

      case PREDICATE_ADD: {
        JSValue left = predicate_nextarg(ctx, &args);
        JSValue right = predicate_nextarg(ctx, &args);

        *pr = predicate_add(left, right);
        break;
      }

      case PREDICATE_SUB: {
        JSValue left = predicate_nextarg(ctx, &args);
        JSValue right = predicate_nextarg(ctx, &args);

        *pr = predicate_sub(left, right);
        break;
      }

      case PREDICATE_MUL: {
        JSValue left = predicate_nextarg(ctx, &args);
        JSValue right = predicate_nextarg(ctx, &args);

        *pr = predicate_mul(left, right);
        break;
      }

      case PREDICATE_DIV: {
        JSValue left = predicate_nextarg(ctx, &args);
        JSValue right = predicate_nextarg(ctx, &args);

        *pr = predicate_div(left, right);
        break;
      }

      case PREDICATE_MOD: {
        JSValue left = predicate_nextarg(ctx, &args);
        JSValue right = predicate_nextarg(ctx, &args);

        *pr = predicate_mod(left, right);
        break;
      }

      case PREDICATE_BOR: {
        JSValue left = predicate_nextarg(ctx, &args);
        JSValue right = predicate_nextarg(ctx, &args);

        *pr = predicate_bor(left, right);
        break;
      }

      case PREDICATE_BAND: {
        JSValue left = predicate_nextarg(ctx, &args);
        JSValue right = predicate_nextarg(ctx, &args);

        *pr = predicate_band(left, right);
        break;
      }

      case PREDICATE_OR: {
        *pr = predicate_or(argc, js_values_dup(ctx, argc, argv));
        break;
      }

      case PREDICATE_AND: {
        *pr = predicate_and(argc, js_values_dup(ctx, argc, argv));
        break;
      }

      case PREDICATE_XOR: {
        *pr = predicate_xor(argc, js_values_dup(ctx, argc, argv));
        break;
      }

      case PREDICATE_REGEXP: {
        RegExp expr;

      regexp:
        expr = regexp_from_argv(argc - 1, argv + 1, ctx);
        *pr = predicate_regexp(expr.source, expr.len, expr.flags);
        break;
      }

      case PREDICATE_INSTANCEOF: {
        *pr = predicate_instanceof(predicate_nextarg(ctx, &args));
        break;
      }

      case PREDICATE_PROTOTYPEIS: {
        *pr = predicate_prototype(predicate_nextarg(ctx, &args));
        break;
      }

      case PREDICATE_EQUAL: {
        *pr = predicate_equal(predicate_nextarg(ctx, &args));
        break;
      }

      case PREDICATE_PROPERTY: {
        JSAtom prop = 0;
        JSValue arg, pred = JS_UNDEFINED;

        if(!js_is_null_or_undefined((arg = js_arguments_shift(&args))))
          prop = JS_ValueToAtom(ctx, arg);

        if(predicate_callable(ctx, (arg = js_arguments_shift(&args))))
          pred = JS_DupValue(ctx, arg);

        *pr = predicate_property(prop, pred);
        break;
      }

      case PREDICATE_HAS: {
        JSAtom prop = 0;
        JSValue arg;

        if(!js_is_null_or_undefined((arg = js_arguments_shift(&args))))
          prop = JS_ValueToAtom(ctx, arg);

        *pr = predicate_has(prop);
        break;
      }

      case PREDICATE_MEMBER: {
        JSValue objv = js_arguments_shift(&args);
        JSValue arg, pred = JS_UNDEFINED;

        if(predicate_callable(ctx, (arg = js_arguments_shift(&args))))
          pred = JS_DupValue(ctx, arg);

        *pr = predicate_member(objv, pred);
        break;
      }

      case PREDICATE_SHIFT: {
        int32_t shift;

        JS_ToInt32(ctx, &shift, js_arguments_shift(&args));
        *pr = predicate_shift(shift, js_arguments_shift(&args));
        break;
      }

      case PREDICATE_SLICE: {
        int64_t start = 0, end = INT64_MAX;

        if(js_arguments_count(&args) > 0) {
          JS_ToInt64(ctx, &start, js_arguments_shift(&args));

          if(js_arguments_count(&args) > 0)
            JS_ToInt64(ctx, &end, js_arguments_shift(&args));
        }

        *pr = predicate_slice(start, end);
        break;
      }

      case PREDICATE_INDEX: {
        int64_t pos;
        JSValue arg, pred = JS_UNDEFINED;

        JS_ToInt64(ctx, &pos, js_arguments_shift(&args));

        if(predicate_callable(ctx, (arg = js_arguments_shift(&args))))
          pred = JS_DupValue(ctx, arg);

        *pr = predicate_index(pos, pred);
        break;
      }

      case PREDICATE_FUNCTION: {
        JSValue this_obj = JS_UNDEFINED, func = predicate_nextarg(ctx, &args);

        if(args.c)
          this_obj = predicate_nextarg(ctx, &args);

        *pr = predicate_function(func, this_obj, MAX_NUM(1, js_get_propertystr_int32(ctx, func, "length")));
        break;
      }

      case PREDICATE_SOME:
      case PREDICATE_EVERY: {
        JSValue func = predicate_nextarg(ctx, &args);

        *pr = id == PREDICATE_SOME ? predicate_some(func) : predicate_every(func);
        break;
      }
    }
  }

  return obj;

fail:
  js_free(ctx, pr);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_EVAL = 0,
  METHOD_KEYS,
  METHOD_VALUES,
};

static JSValue
js_predicate_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Predicate* pr;
  JSValue ret = JS_UNDEFINED;

  if(!(pr = js_predicate_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_EVAL: {
      JSArguments args = js_arguments_new(argc, argv);
      ret = predicate_eval(pr, ctx, &args);
      break;
    }

    case METHOD_KEYS: {
      ret = predicate_keys(pr, ctx);
      break;
    }

    case METHOD_VALUES: {
      ret = predicate_values(pr, ctx);
      break;
    }
  }

  return ret;
}

enum {
  OPERATOR_PLUS = 0,
  OPERATOR_MINUS,
  OPERATOR_MUL,
  OPERATOR_DIV,
  OPERATOR_MOD,
  OPERATOR_BOR,
  OPERATOR_BAND,
  OPERATOR_POW,
};

static JSValue
js_predicate_operator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  JSArguments args = js_arguments_new(argc, argv);
  JSValue left = predicate_nextarg(ctx, &args);
  JSValue right = predicate_nextarg(ctx, &args);

  switch(magic) {
    case OPERATOR_PLUS: {
      ret = js_predicate_wrap(ctx, predicate_add(left, right));
      break;
    }

    case OPERATOR_MINUS: {
      ret = js_predicate_wrap(ctx, predicate_sub(left, right));
      break;
    }

    case OPERATOR_MUL: {
      ret = js_predicate_wrap(ctx, predicate_mul(left, right));
      break;
    }

    case OPERATOR_DIV: {
      ret = js_predicate_wrap(ctx, predicate_div(left, right));
      break;
    }

    case OPERATOR_MOD: {
      ret = js_predicate_wrap(ctx, predicate_mod(left, right));
      break;
    }

    case OPERATOR_BOR: {
      ret = js_predicate_wrap(ctx, predicate_bor(left, right));
      break;
    }

    case OPERATOR_BAND: {
      ret = js_predicate_wrap(ctx, predicate_band(left, right));
      break;
    }

    case OPERATOR_POW: {
      ret = js_predicate_wrap(ctx, predicate_pow(left, right));
      break;
    }
  }

  return ret;
}

static JSValue
js_predicate_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  DynBuf dbuf = {0};
  Predicate* pr = 0;
  JSValue ret = JS_UNDEFINED;

  js_dbuf_init(ctx, &dbuf);

  if(!(pr = js_predicate_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(magic)
    predicate_tosource(pr, ctx, &dbuf, 0);
  else
    predicate_dump(pr, ctx, &dbuf);

  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
  return ret;
}

enum {
  PROP_ID = 0,
  PROP_ARGC,
};

static JSValue
js_predicate_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Predicate* pr;
  JSValue ret = JS_UNDEFINED;

  if(!(pr = js_predicate_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_ID: {
      ret = JS_NewInt32(ctx, pr->id);
      break;
    }

    case PROP_ARGC: {
      ret = JS_NewUint32(ctx, predicate_direct_num_args(pr));
      break;
    }
  }

  return ret;
}

static JSValue
js_predicate_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;

  JSArguments args = js_arguments_new(argc, argv);

  switch(magic) {
    case PREDICATE_TYPE: {
      int32_t type = 0;

      JS_ToInt32(ctx, &type, argv[0]);
      ret = js_predicate_wrap(ctx, predicate_type(type));
      break;
    }

    case PREDICATE_CHARSET: {
      size_t size = 0;
      const char* str = js_tostringlen(ctx, &size, argv[0]);

      ret = js_predicate_wrap(ctx, predicate_charset(str, size));
      break;
    }

    case PREDICATE_STRING: {
      size_t size = 0;
      const char* str = js_tostringlen(ctx, &size, argv[0]);

      ret = js_predicate_wrap(ctx, predicate_string(str, size));
      break;
    }

    case PREDICATE_NOTNOT: {
      ret = js_predicate_wrap(ctx, predicate_notnot(JS_DupValue(ctx, argv[0])));
      break;
    }

    case PREDICATE_NOT: {
      ret = js_predicate_wrap(ctx, predicate_not(predicate_nextarg(ctx, &args)));
      break;
    }

    case PREDICATE_ADD: {
      JSValue left = predicate_nextarg(ctx, &args);
      JSValue right = predicate_nextarg(ctx, &args);

      ret = js_predicate_wrap(ctx, predicate_add(left, right));
      break;
    }

    case PREDICATE_SUB: {
      JSValue left = predicate_nextarg(ctx, &args);
      JSValue right = predicate_nextarg(ctx, &args);

      ret = js_predicate_wrap(ctx, predicate_sub(left, right));
      break;
    }

    case PREDICATE_MUL: {
      JSValue left = predicate_nextarg(ctx, &args);
      JSValue right = predicate_nextarg(ctx, &args);

      ret = js_predicate_wrap(ctx, predicate_mul(left, right));
      break;
    }

    case PREDICATE_DIV: {
      JSValue left = predicate_nextarg(ctx, &args);
      JSValue right = predicate_nextarg(ctx, &args);

      ret = js_predicate_wrap(ctx, predicate_div(left, right));
      break;
    }

    case PREDICATE_MOD: {
      JSValue left = predicate_nextarg(ctx, &args);
      JSValue right = predicate_nextarg(ctx, &args);

      ret = js_predicate_wrap(ctx, predicate_mod(left, right));
      break;
    }

    case PREDICATE_BOR: {
      JSValue left = predicate_nextarg(ctx, &args);
      JSValue right = predicate_nextarg(ctx, &args);

      ret = js_predicate_wrap(ctx, predicate_bor(left, right));
      break;
    }

    case PREDICATE_BAND: {
      JSValue left = predicate_nextarg(ctx, &args);
      JSValue right = predicate_nextarg(ctx, &args);

      ret = js_predicate_wrap(ctx, predicate_band(left, right));
      break;
    }

    case PREDICATE_POW: {
      JSValue left = predicate_nextarg(ctx, &args);
      JSValue right = predicate_nextarg(ctx, &args);

      ret = js_predicate_wrap(ctx, predicate_pow(left, right));
      break;
    }

    case PREDICATE_OR: {
      ret = js_predicate_wrap(ctx, predicate_or(argc, js_values_dup(ctx, argc, argv)));
      break;
    }

    case PREDICATE_AND: {
      ret = js_predicate_wrap(ctx, predicate_and(argc, js_values_dup(ctx, argc, argv)));
      break;
    }

    case PREDICATE_XOR: {
      ret = js_predicate_wrap(ctx, predicate_xor(argc, js_values_dup(ctx, argc, argv)));
      break;
    }

    case PREDICATE_REGEXP: {
      RegExp expr = regexp_from_argv(argc, argv, ctx);
      Predicate pred = predicate_regexp(expr.source, expr.len, expr.flags);

      ret = js_predicate_wrap(ctx, pred);
      break;
    }

    case PREDICATE_INSTANCEOF: {
      ret = js_predicate_wrap(ctx, predicate_instanceof(predicate_nextarg(ctx, &args)));
      break;
    }

    case PREDICATE_PROTOTYPEIS: {
      ret = js_predicate_wrap(ctx, predicate_prototype(predicate_nextarg(ctx, &args)));
      break;
    }

    case PREDICATE_EQUAL: {
      ret = js_predicate_wrap(ctx, predicate_equal(predicate_nextarg(ctx, &args)));
      break;
    }

    case PREDICATE_PROPERTY: {
      JSAtom prop = 0;
      JSValue pred = JS_UNDEFINED;

      if(argc >= 1 && !JS_IsNull(argv[0]) && !JS_IsUndefined(argv[0]))
        prop = JS_ValueToAtom(ctx, argv[0]);

      if(argc >= 2 && predicate_callable(ctx, argv[1]))
        pred = JS_DupValue(ctx, argv[1]);

      ret = js_predicate_wrap(ctx, predicate_property(prop, pred));
      break;
    }

    case PREDICATE_HAS: {
      JSAtom prop = 0;

      if(argc >= 1 && !JS_IsNull(argv[0]) && !JS_IsUndefined(argv[0]))
        prop = JS_ValueToAtom(ctx, argv[0]);

      ret = js_predicate_wrap(ctx, predicate_has(prop));
      break;
    }

    case PREDICATE_MEMBER: {
      JSValue pred = JS_UNDEFINED;

      if(argc >= 2 && predicate_callable(ctx, argv[1]))
        pred = JS_DupValue(ctx, argv[1]);

      ret = js_predicate_wrap(ctx, predicate_member(JS_DupValue(ctx, argv[0]), pred));
      break;
    }

    case PREDICATE_SHIFT: {
      int32_t shift;

      JS_ToInt32(ctx, &shift, predicate_nextarg(ctx, &args));
      ret = js_predicate_wrap(ctx, predicate_shift(shift, predicate_nextarg(ctx, &args)));
      break;
    }

    case PREDICATE_SLICE: {
      int64_t start, end;

      JS_ToInt64(ctx, &start, predicate_nextarg(ctx, &args));
      JS_ToInt64(ctx, &end, predicate_nextarg(ctx, &args));
      ret = js_predicate_wrap(ctx, predicate_slice(start, end));
      break;
    }

    case PREDICATE_INDEX: {
      int64_t pos;
      JSValue pred = JS_UNDEFINED;

      JS_ToInt64(ctx, &pos, predicate_nextarg(ctx, &args));

      if(argc >= 2 && predicate_callable(ctx, argv[1]))
        pred = JS_DupValue(ctx, argv[1]);

      ret = js_predicate_wrap(ctx, predicate_index(pos, pred));
      break;
    }

    case PREDICATE_FUNCTION: {
      JSValue func, this_obj = JS_UNDEFINED;

      func = predicate_nextarg(ctx, &args);

      if(args.c)
        this_obj = predicate_nextarg(ctx, &args);

      ret = js_predicate_wrap(
          ctx, predicate_function(func, this_obj, MAX_NUM(1, js_get_propertystr_int32(ctx, func, "length"))));
      break;
    }

    case PREDICATE_SOME:
    case PREDICATE_EVERY: {
      JSValue func, this_obj = JS_UNDEFINED;

      func = predicate_nextarg(ctx, &args);

      if(args.c)
        this_obj = predicate_nextarg(ctx, &args);

      ret = js_predicate_wrap(ctx, magic == PREDICATE_SOME ? predicate_some(func) : predicate_every(func));
      break;
    }
  }

  return ret;
}

JSValue
js_predicate_call(
    JSContext* ctx, JSValueConst func_obj, JSValueConst this_val, int argc, JSValueConst argv[], int flags) {
  Predicate* pr;
  JSValue ret = JS_UNDEFINED;
  JSValueConst arg = argc > 0 ? argv[0] : JS_UNDEFINED;

  if(!(pr = js_predicate_data2(ctx, func_obj)))
    return JS_EXCEPTION;

  if(js_is_promise(ctx, arg)) {
    JSAtom then = JS_NewAtom(ctx, "then");
    ret = JS_Invoke(ctx, arg, then, 1, &func_obj);
    JS_FreeAtom(ctx, then);
  } else {
    JSArguments args = js_arguments_new(argc, argv);
    ret = predicate_eval(pr, ctx, &args);
    /*switch(result) {
      case 0: ret = JS_NewBool(ctx, FALSE); break;
      case 1: ret = JS_NewBool(ctx, TRUE); break;
      default: ret = JS_NewInt32(ctx, result); break;
    }*/
  }

  return ret;
}

static JSValue
js_predicate_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Predicate* pr;

  if(!(pr = js_predicate_data2(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObject /*Proto*/ (ctx /*, predicate_proto*/);

  JS_DefinePropertyValueStr(ctx, obj, "id", JS_NewInt32(ctx, pr->id), 0);

  js_set_tostringtag_value(ctx, obj, predicate_constant(pr, ctx, TRUE));

  switch(pr->id) {
    case PREDICATE_TYPE: {
      JS_DefinePropertyValueStr(ctx, obj, "flags", JS_NewInt32(ctx, pr->type.flags), JS_PROP_ENUMERABLE);
      break;
    }

    case PREDICATE_CHARSET: {
      JS_DefinePropertyValueStr(
          ctx, obj, "set", JS_NewStringLen(ctx, pr->charset.set, pr->charset.len), JS_PROP_ENUMERABLE);
      JS_DefinePropertyValueStr(ctx, obj, "len", JS_NewUint32(ctx, pr->charset.len), JS_PROP_ENUMERABLE);
      break;
    }

    case PREDICATE_STRING: {
      JS_DefinePropertyValueStr(
          ctx, obj, "str", JS_NewStringLen(ctx, pr->string.str, pr->string.len), JS_PROP_ENUMERABLE);
      JS_DefinePropertyValueStr(ctx, obj, "len", JS_NewUint32(ctx, pr->string.len), JS_PROP_ENUMERABLE);
      break;
    }

    case PREDICATE_NOT:
    case PREDICATE_NOTNOT:
    case PREDICATE_BNOT:
    case PREDICATE_SQRT: {
      JS_DefinePropertyValueStr(ctx, obj, "predicate", JS_DupValue(ctx, pr->unary.predicate), JS_PROP_ENUMERABLE);
      break;
    }

    case PREDICATE_ADD:
    case PREDICATE_SUB:
    case PREDICATE_MUL:
    case PREDICATE_DIV:
    case PREDICATE_MOD:
    case PREDICATE_BOR:
    case PREDICATE_BAND:
    case PREDICATE_POW:
    case PREDICATE_ATAN2: {
      JS_DefinePropertyValueStr(ctx, obj, "left", JS_DupValue(ctx, pr->binary.left), JS_PROP_ENUMERABLE);
      JS_DefinePropertyValueStr(ctx, obj, "right", JS_DupValue(ctx, pr->binary.right), JS_PROP_ENUMERABLE);
      break;
    }

    case PREDICATE_OR:
    case PREDICATE_AND:
    case PREDICATE_XOR: {
      JS_DefinePropertyValueStr(ctx,
                                obj,
                                "values",
                                js_values_toarray(ctx, pr->boolean.npredicates, pr->boolean.predicates),
                                JS_PROP_ENUMERABLE);

      break;
    }

    case PREDICATE_REGEXP: {
      JS_DefinePropertyValueStr(ctx, obj, "expr", regexp_to_value(pr->regexp.expr, ctx), JS_PROP_ENUMERABLE);
      break;
    }

    case PREDICATE_INSTANCEOF: {
      JS_DefinePropertyValueStr(ctx, obj, "constructor", JS_DupValue(ctx, pr->unary.predicate), JS_PROP_ENUMERABLE);
      break;
    }

    case PREDICATE_PROTOTYPEIS: {
      JS_DefinePropertyValueStr(ctx, obj, "prototype", JS_DupValue(ctx, pr->unary.predicate), JS_PROP_ENUMERABLE);
      break;
    }

    case PREDICATE_EQUAL: {
      JS_DefinePropertyValueStr(ctx, obj, "predicate", JS_DupValue(ctx, pr->unary.predicate), JS_PROP_ENUMERABLE);
      break;
    }

    case PREDICATE_PROPERTY:
    case PREDICATE_HAS: {
      JS_DefinePropertyValueStr(ctx, obj, "atom", JS_AtomToValue(ctx, pr->property.atom), JS_PROP_ENUMERABLE);

      if(!js_is_null_or_undefined(pr->property.predicate))
        if(predicate_callable(ctx, pr->property.predicate))
          JS_DefinePropertyValueStr(
              ctx, obj, "predicate", JS_DupValue(ctx, pr->property.predicate), JS_PROP_ENUMERABLE);

      break;
    }

    case PREDICATE_MEMBER: {
      JS_DefinePropertyValueStr(ctx, obj, "object", JS_DupValue(ctx, pr->member.object), JS_PROP_ENUMERABLE);
      break;
    }

    case PREDICATE_SHIFT: {
      JS_DefinePropertyValueStr(ctx, obj, "n", JS_NewInt32(ctx, pr->shift.n), JS_PROP_ENUMERABLE);
      JS_DefinePropertyValueStr(ctx, obj, "predicate", JS_DupValue(ctx, pr->shift.predicate), JS_PROP_ENUMERABLE);
      break;
    }

    case PREDICATE_SLICE: {
      JS_DefinePropertyValueStr(ctx, obj, "start", JS_NewInt64(ctx, pr->slice.start), JS_PROP_ENUMERABLE);
      JS_DefinePropertyValueStr(ctx, obj, "end", JS_NewInt64(ctx, pr->slice.end), JS_PROP_ENUMERABLE);
      break;
    }

    case PREDICATE_INDEX: {
      JS_DefinePropertyValueStr(ctx, obj, "pos", JS_NewInt64(ctx, pr->index.pos), JS_PROP_ENUMERABLE);
      JS_DefinePropertyValueStr(ctx, obj, "predicate", JS_DupValue(ctx, pr->index.predicate), JS_PROP_ENUMERABLE);
      break;
    }

    case PREDICATE_FUNCTION: {
      JS_DefinePropertyValueStr(ctx, obj, "func", JS_DupValue(ctx, pr->function.func), JS_PROP_ENUMERABLE);
      JS_DefinePropertyValueStr(ctx, obj, "this_obj", JS_DupValue(ctx, pr->function.this_val), JS_PROP_ENUMERABLE);
      JS_DefinePropertyValueStr(ctx, obj, "arity", JS_NewInt32(ctx, pr->function.arity), JS_PROP_ENUMERABLE);
      break;
    }

    case PREDICATE_SOME:
    case PREDICATE_EVERY: {
      JS_DefinePropertyValueStr(ctx, obj, "predicate", JS_DupValue(ctx, pr->array.predicate), JS_PROP_ENUMERABLE);
      break;
    }
  }

  return obj;
}

static void
js_predicate_finalizer(JSRuntime* rt, JSValue val) {
  Predicate* pr;

  if((pr = JS_GetOpaque(val, js_predicate_class_id)))
    predicate_free_rt(pr, rt);
}

static JSClassDef js_predicate_class = {
    .class_name = "Predicate",
    .finalizer = js_predicate_finalizer,
    .call = js_predicate_call,
};

static const JSCFunctionListEntry js_predicate_operator_funcs[] = {
    JS_CFUNC_MAGIC_DEF("+", 2, js_predicate_operator, OPERATOR_PLUS),
    JS_CFUNC_MAGIC_DEF("-", 2, js_predicate_operator, OPERATOR_MINUS),
    JS_CFUNC_MAGIC_DEF("*", 2, js_predicate_operator, OPERATOR_MUL),
    JS_CFUNC_MAGIC_DEF("/", 2, js_predicate_operator, OPERATOR_DIV),
    JS_CFUNC_MAGIC_DEF("%", 2, js_predicate_operator, OPERATOR_MOD),
    JS_CFUNC_MAGIC_DEF("|", 2, js_predicate_operator, OPERATOR_BOR),
    JS_CFUNC_MAGIC_DEF("&", 2, js_predicate_operator, OPERATOR_BAND),
    JS_CFUNC_MAGIC_DEF("**", 2, js_predicate_operator, OPERATOR_POW),
};

static const JSCFunctionListEntry js_predicate_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("eval", 1, js_predicate_method, METHOD_EVAL),
    JS_CFUNC_MAGIC_DEF("toString", 0, js_predicate_tostring, 0),
    JS_CFUNC_MAGIC_DEF("toSource", 0, js_predicate_tostring, 1),
    JS_ALIAS_DEF("call", "eval"),
    JS_CGETSET_MAGIC_FLAGS_DEF("id", js_predicate_get, 0, PROP_ID, JS_PROP_CONFIGURABLE),
    JS_CFUNC_MAGIC_DEF("keys", 0, js_predicate_method, METHOD_KEYS),
    JS_CFUNC_MAGIC_DEF("values", 0, js_predicate_method, METHOD_VALUES),
    JS_CGETSET_MAGIC_DEF("length", js_predicate_get, 0, PROP_ARGC),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Predicate", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_predicate_funcs[] = {
    JS_CFUNC_MAGIC_DEF("type", 1, js_predicate_function, PREDICATE_TYPE),
    JS_CFUNC_MAGIC_DEF("charset", 1, js_predicate_function, PREDICATE_CHARSET),
    JS_CFUNC_MAGIC_DEF("string", 1, js_predicate_function, PREDICATE_STRING),
    JS_CFUNC_MAGIC_DEF("not", 1, js_predicate_function, PREDICATE_NOT),
    JS_CFUNC_MAGIC_DEF("notnot", 1, js_predicate_function, PREDICATE_NOTNOT),
    JS_CFUNC_MAGIC_DEF("bnot", 1, js_predicate_function, PREDICATE_BNOT),
    JS_CFUNC_MAGIC_DEF("sqrt", 1, js_predicate_function, PREDICATE_SQRT),
    JS_CFUNC_MAGIC_DEF("add", 2, js_predicate_function, PREDICATE_ADD),
    JS_CFUNC_MAGIC_DEF("sub", 2, js_predicate_function, PREDICATE_SUB),
    JS_CFUNC_MAGIC_DEF("mul", 2, js_predicate_function, PREDICATE_MUL),
    JS_CFUNC_MAGIC_DEF("div", 2, js_predicate_function, PREDICATE_DIV),
    JS_CFUNC_MAGIC_DEF("mod", 2, js_predicate_function, PREDICATE_MOD),
    JS_CFUNC_MAGIC_DEF("bor", 2, js_predicate_function, PREDICATE_BOR),
    JS_CFUNC_MAGIC_DEF("band", 2, js_predicate_function, PREDICATE_BAND),
    JS_CFUNC_MAGIC_DEF("pow", 2, js_predicate_function, PREDICATE_POW),
    JS_CFUNC_MAGIC_DEF("atan2", 2, js_predicate_function, PREDICATE_ATAN2),
    JS_CFUNC_MAGIC_DEF("or", 2, js_predicate_function, PREDICATE_OR),
    JS_CFUNC_MAGIC_DEF("and", 2, js_predicate_function, PREDICATE_AND),
    JS_CFUNC_MAGIC_DEF("xor", 2, js_predicate_function, PREDICATE_XOR),
    JS_CFUNC_MAGIC_DEF("regexp", 1, js_predicate_function, PREDICATE_REGEXP),
    JS_CFUNC_MAGIC_DEF("instanceOf", 1, js_predicate_function, PREDICATE_INSTANCEOF),
    JS_CFUNC_MAGIC_DEF("prototypeIs", 1, js_predicate_function, PREDICATE_PROTOTYPEIS),
    JS_CFUNC_MAGIC_DEF("equal", 1, js_predicate_function, PREDICATE_EQUAL),
    JS_CFUNC_MAGIC_DEF("property", 1, js_predicate_function, PREDICATE_PROPERTY),
    JS_CFUNC_MAGIC_DEF("has", 1, js_predicate_function, PREDICATE_HAS),
    JS_CFUNC_MAGIC_DEF("member", 1, js_predicate_function, PREDICATE_MEMBER),
    JS_CFUNC_MAGIC_DEF("shift", 2, js_predicate_function, PREDICATE_SHIFT),
    JS_CFUNC_MAGIC_DEF("slice", 0, js_predicate_function, PREDICATE_SLICE),
    JS_CFUNC_MAGIC_DEF("index", 1, js_predicate_function, PREDICATE_INDEX),
    JS_CFUNC_MAGIC_DEF("function", 1, js_predicate_function, PREDICATE_FUNCTION),
    JS_CFUNC_MAGIC_DEF("some", 1, js_predicate_function, PREDICATE_SOME),
    JS_CFUNC_MAGIC_DEF("every", 1, js_predicate_function, PREDICATE_EVERY),
};

static const JSCFunctionListEntry js_predicate_ids[] = {
    JS_PROP_INT32_DEF("TYPE", PREDICATE_TYPE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("CHARSET", PREDICATE_CHARSET, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("STRING", PREDICATE_STRING, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("NOTNOT", PREDICATE_NOTNOT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("NOT", PREDICATE_NOT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("BNOT", PREDICATE_BNOT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SQRT", PREDICATE_SQRT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ADD", PREDICATE_ADD, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SUB", PREDICATE_SUB, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MUL", PREDICATE_MUL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("DIV", PREDICATE_DIV, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MOD", PREDICATE_MOD, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("BOR", PREDICATE_BOR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("BAND", PREDICATE_BAND, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("POW", PREDICATE_POW, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ATAN2", PREDICATE_ATAN2, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("OR", PREDICATE_OR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("AND", PREDICATE_AND, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("XOR", PREDICATE_XOR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("REGEXP", PREDICATE_REGEXP, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("INSTANCEOF", PREDICATE_INSTANCEOF, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("PROTOTYPEIS", PREDICATE_PROTOTYPEIS, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("EQUAL", PREDICATE_EQUAL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("PROPERTY", PREDICATE_PROPERTY, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("HAS", PREDICATE_HAS, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MEMBER", PREDICATE_MEMBER, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SHIFT", PREDICATE_SHIFT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SLICE", PREDICATE_SLICE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("INDEX", PREDICATE_INDEX, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SOME", PREDICATE_SOME, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("EVERY", PREDICATE_SOME, JS_PROP_ENUMERABLE),
};

static const JSCFunctionListEntry js_predicate_types[] = {
    JS_CONSTANT(TYPE_UNDEFINED),
    JS_CONSTANT(TYPE_NULL),
    JS_CONSTANT(TYPE_BOOL),
    JS_CONSTANT(TYPE_INT),
    JS_CONSTANT(TYPE_OBJECT),
    JS_CONSTANT(TYPE_STRING),
    JS_CONSTANT(TYPE_SYMBOL),
    JS_CONSTANT(TYPE_BIG_FLOAT),
    JS_CONSTANT(TYPE_BIG_INT),
    JS_CONSTANT(TYPE_BIG_DECIMAL),
    JS_CONSTANT(TYPE_FLOAT64),
    JS_CONSTANT(TYPE_NUMBER),
    JS_CONSTANT(TYPE_PRIMITIVE),
    JS_CONSTANT(TYPE_ALL),
    JS_CONSTANT(TYPE_FUNCTION),
    JS_CONSTANT(TYPE_ARRAY),

};

static int
js_predicate_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_predicate_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_predicate_class_id, &js_predicate_class);

  predicate_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, predicate_proto, js_predicate_proto_funcs, countof(js_predicate_proto_funcs));
  JS_SetClassProto(ctx, js_predicate_class_id, predicate_proto);

  predicate_ctor = JS_NewCFunction2(ctx, js_predicate_constructor, "Predicate", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, predicate_ctor, predicate_proto);
  JS_SetPropertyFunctionList(ctx, predicate_ctor, js_predicate_funcs, countof(js_predicate_funcs));
  JS_SetPropertyFunctionList(ctx, predicate_ctor, js_predicate_ids, countof(js_predicate_ids));
  JS_SetPropertyFunctionList(ctx, predicate_ctor, js_predicate_types, countof(js_predicate_types));

  js_set_inspect_method(ctx, predicate_proto, js_predicate_inspect);

  JSValue operators, operators_create;
  JSAtom operators_set = js_symbol_operatorset_atom(ctx);

  operators_create = js_operators_create(ctx, &operators);

  JSValue predicate_operators = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx,
                             predicate_operators,
                             js_predicate_operator_funcs,
                             countof(js_predicate_operator_funcs));

  JSValueConst args[3] = {predicate_operators, JS_NewObject(ctx), JS_NewObject(ctx)};

  JS_SetPropertyFunctionList(ctx, args[1], js_predicate_operator_funcs, countof(js_predicate_operator_funcs));

  JS_SetPropertyStr(ctx, args[1], "left", js_global_get_str(ctx, "Number"));

  JS_SetPropertyFunctionList(ctx, args[2], js_predicate_operator_funcs, countof(js_predicate_operator_funcs));

  JS_SetPropertyStr(ctx, args[2], "right", js_global_get_str(ctx, "Number"));

  JSValue predicate_operatorset = JS_Call(ctx, operators_create, operators, 3, args);
  JS_DefinePropertyValue(
      ctx, predicate_proto, operators_set, predicate_operatorset, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);

  JS_FreeValue(ctx, operators);
  JS_FreeValue(ctx, operators_create);
  // JS_FreeValue(ctx, predicate_operators);
  JS_FreeAtom(ctx, operators_set);

  if(m) {
    JS_SetModuleExport(ctx, m, "Predicate", predicate_ctor);
    JS_SetModuleExport(ctx, m, "PredicateOperators", predicate_operators);
    JS_SetModuleExport(ctx, m, "PredicateOperatorSet", predicate_operatorset);
    JS_SetModuleExportList(ctx, m, js_predicate_funcs, countof(js_predicate_funcs));
    JS_SetModuleExportList(ctx, m, js_predicate_types, countof(js_predicate_types));
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_predicate
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_predicate_init))) {
    JS_AddModuleExport(ctx, m, "Predicate");
    JS_AddModuleExport(ctx, m, "PredicateOperators");
    JS_AddModuleExport(ctx, m, "PredicateOperatorSet");
    JS_AddModuleExportList(ctx, m, js_predicate_funcs, countof(js_predicate_funcs));
    JS_AddModuleExportList(ctx, m, js_predicate_types, countof(js_predicate_types));
  }

  return m;
}

/**
 * @}
 */
