/* jwalk.c: jwalk_init(), jwalk_set_replacer(), jwalk_close().
 * depends on: walk.c (the stepper), jwalk.h.
 * rule: JSON.stringify's value rules live here as WalkHooks, nowhere else. */

#define _GNU_SOURCE 1

#include "json/jwalk.h"
#include "json/jwrite.h"
#include "json/jopts.h"
#include <math.h>

/**
 * \addtogroup jwalk
 * @{
 */

/* class ids of Number, String, Boolean and BigInt objects; found once, they are engine-wide */
static JSClassID jwalk_number_id, jwalk_string_id, jwalk_boolean_id, jwalk_bigint_id;

static JSClassID
jwalk_boxed_id(JSContext* ctx, JSValueConst global, JSValueConst primitive) {
  JSValue ctor = JS_GetPropertyStr(ctx, global, "Object");
  JSValue obj = JS_Call(ctx, ctor, JS_UNDEFINED, 1, &primitive);
  JSClassID id = JS_IsObject(obj) ? JS_GetClassID(obj) : 0;

  JS_FreeValue(ctx, obj);
  JS_FreeValue(ctx, ctor);
  return id;
}

static void
jwalk_classes_init(JSContext* ctx) {
  JSValue global, v;

  if(jwalk_number_id)
    return;

  global = JS_GetGlobalObject(ctx);

  v = JS_NewInt32(ctx, 0);
  jwalk_number_id = jwalk_boxed_id(ctx, global, v);
  v = JS_NewString(ctx, "");
  jwalk_string_id = jwalk_boxed_id(ctx, global, v);
  JS_FreeValue(ctx, v);
  v = JS_NewBool(ctx, 0);
  jwalk_boolean_id = jwalk_boxed_id(ctx, global, v);
  v = JS_NewBigInt64(ctx, 0);
  jwalk_bigint_id = jwalk_boxed_id(ctx, global, v);
  JS_FreeValue(ctx, v);
  JS_FreeValue(ctx, global);
}

/* jwalk_key_string: the key as a JS string, as toJSON() and the replacer get it. */
static JSValue
jwalk_key_string(JSContext* ctx, JSValueConst key) {
  return JS_IsString(key) ? JS_DupValue(ctx, key) : JS_ToString(ctx, key);
}

/* jwalk_prepare: SerializeJSONProperty for one property (WalkHooks.prepare), up to the choice of event.
 *
 *   JSValueConst  holder  the object or array the property belongs to
 *   JSValueConst  key     its key: a string, or the number for an array index
 *   JSValue*      val     in: the property value (owned); out: what to report (owned)
 *
 * the result is null, a boolean, a finite number, a string, a container or a rawJSON object.
 * returns a WalkKind, or -1 with an exception pending (`*val` is freed). */
static int
jwalk_prepare(void* opaque, JSContext* ctx, JSValueConst holder, JSValueConst key, JSValue* val) {
  JwalkIterator* it = opaque;
  JSValue v = *val, ks = JS_UNDEFINED;
  int ret = -1;

  /* the root: a replacer sees it as the value of "" in {"": root} */
  if(JS_IsUndefined(holder) && JS_IsFunction(ctx, it->replacer)) {
    it->holder = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, it->holder, "", JS_DupValue(ctx, v));
    holder = it->holder;
  }

  if(JS_IsObject(v) || JS_IsBigInt(ctx, v)) {
    JSValue tojson = JS_GetPropertyStr(ctx, v, "toJSON");

    if(JS_IsException(tojson))
      goto fail;

    if(JS_IsFunction(ctx, tojson)) {
      JSValue r;

      ks = jwalk_key_string(ctx, key);
      r = JS_IsException(ks) ? ks : JS_Call(ctx, tojson, v, 1, &ks);
      JS_FreeValue(ctx, tojson);
      JS_FreeValue(ctx, v);
      v = r;

      if(JS_IsException(v))
        goto fail;
    } else {
      JS_FreeValue(ctx, tojson);
    }
  }

  if(JS_IsFunction(ctx, it->replacer)) {
    JSValueConst args[2];
    JSValue r;

    if(JS_IsUndefined(ks))
      ks = jwalk_key_string(ctx, key);

    if(JS_IsException(ks))
      goto fail;

    args[0] = ks;
    args[1] = v;
    r = JS_Call(ctx, it->replacer, holder, 2, args);
    JS_FreeValue(ctx, v);
    v = r;

    if(JS_IsException(v))
      goto fail;
  }

  if(JS_IsObject(v) && !JS_IsFunction(ctx, v)) {
    JSClassID id = JS_GetClassID(v);
    JSValue prim = JS_UNDEFINED;

    if(id == jwalk_number_id) {
      double d;

      if(JS_ToFloat64(ctx, &d, v))
        goto fail;

      prim = JS_NewFloat64(ctx, d);
    } else if(id == jwalk_string_id) {
      prim = JS_ToString(ctx, v);

      if(JS_IsException(prim))
        goto fail;
    } else if(id == jwalk_boolean_id) {
      double d;

      if(JS_ToFloat64(ctx, &d, v)) /* ToBoolean of the object is always true; its value is not */
        goto fail;

      prim = JS_NewBool(ctx, d != 0);
    } else if(id == jwalk_bigint_id) {
      JS_ThrowTypeError(ctx, "Do not know how to serialize a BigInt");
      goto fail;
    }

    if(!JS_IsUndefined(prim)) {
      JS_FreeValue(ctx, v);
      v = prim;
    }
  }

  if(JS_IsBigInt(ctx, v)) {
    JS_ThrowTypeError(ctx, "Do not know how to serialize a BigInt");
    goto fail;
  }

  if(JS_IsUndefined(v) || JS_IsSymbol(v) || JS_IsFunction(ctx, v)) {
    JS_FreeValue(ctx, v);
    *val = JS_UNDEFINED;
    JS_FreeValue(ctx, ks);
    return WALK_KIND_OMIT;
  }

  if(JS_IsNumber(v)) {
    double d;

    if(JS_ToFloat64(ctx, &d, v))
      goto fail;

    if(!isfinite(d)) {
      JS_FreeValue(ctx, v);
      v = JS_NULL;
    }

    ret = WALK_KIND_VALUE;
  } else if(JS_IsObject(v)) {
    ret = WALK_KIND_CONTAINER;

    if(JS_IsFunction(ctx, it->is_raw)) {
      JSValue proto = JS_GetPrototype(ctx, v);
      BOOL maybe = JS_IsNull(proto);

      JS_FreeValue(ctx, proto);

      if(maybe) {
        JSValue r = JS_Call(ctx, it->is_raw, JS_UNDEFINED, 1, (JSValueConst*)&v);

        if(JS_IsException(r))
          goto fail;

        if(JS_ToBool(ctx, r))
          ret = WALK_KIND_RAW;

        JS_FreeValue(ctx, r);
      }
    }
  } else {
    ret = WALK_KIND_VALUE; /* null, boolean, string */
  }

  *val = v;
  JS_FreeValue(ctx, ks);
  return ret;

fail:
  JS_FreeValue(ctx, v);
  JS_FreeValue(ctx, ks);
  *val = JS_UNDEFINED;
  return -1;
}

/* jwalk_keys: an array replacer replaces an object's own keys by its list (WalkHooks.keys). */
static int
jwalk_keys(void* opaque, JSContext* ctx, PropertyEnumeration* top, int array) {
  JwalkIterator* it = opaque;

  if(it->allow && !array) {
    JSAtom* list = it->allow_len ? js_malloc(ctx, sizeof(JSAtom) * it->allow_len) : NULL;

    if(it->allow_len && !list)
      return -1;

    for(uint32_t i = 0; i < top->tab_atom_len; i++)
      JS_FreeAtom(ctx, top->tab_atom[i]);

    js_free(ctx, top->tab_atom);

    for(uint32_t i = 0; i < it->allow_len; i++)
      list[i] = JS_DupAtom(ctx, it->allow[i]);

    top->tab_atom = list;
    top->tab_atom_len = it->allow_len;
  }

  return 0;
}

/* jwalk_set_replacer: JSON.stringify's replacer argument.
 *
 * ```js
 * jwalk_set_replacer(&it, ctx, (k, v) => typeof v === 'number' ? v * 2 : v);
 * jwalk_set_replacer(&it, ctx, ['a', 'b']);   // only the keys a and b, in that order
 * ```
 *
 * a function is called as replacer.call(holder, key, value) for every property, after
 * toJSON; an array lets only its string and number items through as object keys; any
 * other value is ignored. call it before the first jwalk_next().
 *
 *   returns int  0, or -1 with an exception pending. */
int
jwalk_set_replacer(JwalkIterator* it, JSContext* ctx, JSValueConst replacer) {
  if(JS_IsFunction(ctx, replacer)) {
    JS_FreeValue(ctx, it->replacer);
    it->replacer = JS_DupValue(ctx, replacer);
    return 0;
  }

  if(JS_IsArray(ctx, replacer)) {
    JSValue lv = JS_GetPropertyStr(ctx, replacer, "length");
    uint32_t len = 0;
    JSAtom* list;

    if(JS_IsException(lv) || JS_ToUint32(ctx, &len, lv)) {
      JS_FreeValue(ctx, lv);
      return -1;
    }

    JS_FreeValue(ctx, lv);

    if(!(list = js_malloc(ctx, sizeof(JSAtom) * (len ? len : 1))))
      return -1;

    it->allow = list;
    it->allow_len = 0;

    for(uint32_t i = 0; i < len; i++) {
      JSValue item = JS_GetPropertyUint32(ctx, replacer, i);
      JSAtom atom;
      uint32_t j;
      BOOL ok;

      if(JS_IsException(item))
        return -1;

      ok = JS_IsString(item) || JS_IsNumber(item);

      if(JS_IsObject(item)) {
        JSClassID id = JS_GetClassID(item);
        ok = id == jwalk_string_id || id == jwalk_number_id;
      }

      if(ok) {
        JSValue str = JS_ToString(ctx, item);

        if(JS_IsException(str)) {
          JS_FreeValue(ctx, item);
          return -1;
        }

        atom = JS_ValueToAtom(ctx, str);
        JS_FreeValue(ctx, str);

        for(j = 0; j < it->allow_len; j++)
          if(it->allow[j] == atom)
            break;

        if(j < it->allow_len)
          JS_FreeAtom(ctx, atom);
        else
          it->allow[it->allow_len++] = atom;
      }

      JS_FreeValue(ctx, item);
    }
  }

  return 0;
}

/* jwalk_init: starts walking `root`, reporting to `iface`; emits nothing yet.
 *
 *   JwalkIterator*  it     iterator to initialize; must not move while in use
 *   JSValueConst    root   value to walk, dup'ed (refcount: freed by close)
 *   WalkInterface   iface  callbacks, copied by value
 *
 *   returns int  0, or -1 with an exception pending.
 */
int
jwalk_init(JwalkIterator* it, JSContext* ctx, JSValueConst root, WalkInterface iface) {
  JSValue global, json;
  WalkHooks hooks = {it, jwalk_prepare, jwalk_keys, "Converting circular structure to JSON"};

  jwalk_classes_init(ctx);
  it->replacer = it->holder = it->is_raw = JS_UNDEFINED;
  it->allow = NULL;
  it->allow_len = 0;

  global = JS_GetGlobalObject(ctx);
  json = JS_GetPropertyStr(ctx, global, "JSON");
  if(JS_IsObject(json))
    it->is_raw = JS_GetPropertyStr(ctx, json, "isRawJSON");
  JS_FreeValue(ctx, json);
  JS_FreeValue(ctx, global);
  return walk_init(&it->walk, ctx, root, iface, hooks);
}

/* jwalk_close_rt: frees the frames and everything the iterator owns; safe after done or error.
 *
 * the form for a finalizer, which has a runtime but no context. never throws;
 * the iterator must be jwalk_init()'ed again to reuse. */
void
jwalk_close_rt(JwalkIterator* it, JSRuntime* rt) {
  walk_close_rt(&it->walk, rt);
  JS_FreeValueRT(rt, it->replacer);
  JS_FreeValueRT(rt, it->holder);
  JS_FreeValueRT(rt, it->is_raw);
  it->replacer = it->holder = it->is_raw = JS_UNDEFINED;

  for(uint32_t i = 0; i < it->allow_len; i++)
    JS_FreeAtomRT(rt, it->allow[i]);

  if(it->allow)
    js_free_rt(rt, it->allow);

  it->allow = NULL;
  it->allow_len = 0;
}

/* jwalk_close: jwalk_close_rt() for the runtime of `ctx`. */
void
jwalk_close(JwalkIterator* it, JSContext* ctx) {
  jwalk_close_rt(it, JS_GetRuntime(ctx));
}

/**
 * @}
 */
