/* walk.c: walk_init(), walk_next(), walk_close().
 * depends on: property-enumeration.c (the frame stack), walk.h (callbacks, hooks).
 * rule: one callback per walk_next(); the walker knows no output format and
 * no value rules; WalkHooks.prepare decides what a property is. */

#include "walk.h"
#include "property-enumeration.h"

/**
 * \addtogroup walk
 * @{
 */

enum { WALK_START, WALK_RUNNING, WALK_DONE };

/* per open container: the next array index to report, and the array length */
typedef struct {
  uint32_t next, length;
} WalkMeta;

#define WALK_META(it) ((WalkMeta*)vector_back(&(it)->meta, sizeof(WalkMeta)))

/* the objects of the open containers, as a hash set of their addresses (open addressing,
 * 0 is an empty slot): a walk of depth n costs O(n), not O(n^2) as a scan of the stack would */
static uint32_t
walk_active_slot(uintptr_t p, uint32_t cap) {
  return (uint32_t)(((uint64_t)p * 0x9E3779B97F4A7C15ull) >> 32) & (cap - 1);
}

static BOOL
walk_active_has(const WalkIterator* it, uintptr_t p) {
  uint32_t mask = it->active_cap - 1;

  if(!it->active_cap)
    return FALSE;

  for(uint32_t i = walk_active_slot(p, it->active_cap); it->active[i]; i = (i + 1) & mask)
    if(it->active[i] == p)
      return TRUE;

  return FALSE;
}

/* returns 0, or -1 with an exception pending (out of memory) */
static int
walk_active_add(WalkIterator* it, JSContext* ctx, uintptr_t p) {
  uint32_t mask;

  if((it->active_len + 1) * 2 > it->active_cap) {
    uint32_t cap = it->active_cap ? it->active_cap * 2 : 64;
    uintptr_t* tab = js_mallocz(ctx, sizeof(uintptr_t) * cap);

    if(!tab)
      return -1;

    for(uint32_t j = 0; j < it->active_cap; j++) {
      if(it->active[j]) {
        uint32_t i = walk_active_slot(it->active[j], cap);

        while(tab[i])
          i = (i + 1) & (cap - 1);

        tab[i] = it->active[j];
      }
    }

    js_free(ctx, it->active);
    it->active = tab;
    it->active_cap = cap;
  }

  mask = it->active_cap - 1;

  uint32_t i = walk_active_slot(p, it->active_cap);

  while(it->active[i])
    i = (i + 1) & mask;

  it->active[i] = p;
  it->active_len++;
  return 0;
}

static void
walk_active_del(WalkIterator* it, uintptr_t p) {
  uint32_t mask = it->active_cap - 1, i, j;

  if(!it->active_cap)
    return;

  for(i = walk_active_slot(p, it->active_cap); it->active[i] != p; i = (i + 1) & mask)
    if(!it->active[i])
      return;

  /* backward-shift deletion keeps every probe sequence unbroken */
  for(j = i;;) {
    it->active[i] = 0;

    for(;;) {
      uint32_t k;

      j = (j + 1) & mask;

      if(!it->active[j]) {
        it->active_len--;
        return;
      }

      k = walk_active_slot(it->active[j], it->active_cap);

      if(!(i <= j ? (i < k && k <= j) : (i < k || k <= j)))
        break;
    }

    it->active[i] = it->active[j];
    i = j;
  }
}

/* walk_prepare_plain: the default prepare; objects are containers, the rest values. */
static int
walk_prepare_plain(void* opaque, JSContext* ctx, JSValueConst holder, JSValueConst key, JSValue* val) {
  return JS_IsObject(*val) ? WALK_KIND_CONTAINER : WALK_KIND_VALUE;
}

/* walk_init: starts walking `root`, reporting to `iface`; emits nothing yet.
 *
 *   WalkIterator*  it     iterator to initialize
 *   JSValueConst   root   value to walk, dup'ed (refcount: freed by close)
 *   WalkInterface  iface  callbacks, copied by value
 *   WalkHooks      hooks  value rules, copied by value; zeroed for the plain ones
 *
 *   returns int  0, or -1 with an exception pending.
 */
int
walk_init(WalkIterator* it, JSContext* ctx, JSValueConst root, WalkInterface iface, WalkHooks hooks) {
  vector_init_rt(&it->stack, JS_GetRuntime(ctx)); /* rt, not ctx: walk_close_rt() may run in a finalizer */
  vector_init_rt(&it->meta, JS_GetRuntime(ctx));
  it->iface = iface;
  it->hooks = hooks;
  it->flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
  it->root = JS_DupValue(ctx, root);
  it->pending = JS_UNDEFINED;
  it->state = WALK_START;
  it->key_done = FALSE;
  it->entered = FALSE;
  it->pending_kind = WALK_KIND_VALUE;
  it->root_omitted = FALSE;
  it->active = 0;
  it->active_cap = it->active_len = 0;
  return 0;
}

/* walk_prepare: the hook, or the plain rule; returns a WalkKind or -1. */
static int
walk_prepare(WalkIterator* it, JSContext* ctx, JSValueConst holder, JSValueConst key, JSValue* val) {
  return it->hooks.prepare ? it->hooks.prepare(it->hooks.opaque, ctx, holder, key, val) : walk_prepare_plain(NULL, ctx, holder, key, val);
}

/* walk_enter: pushes a frame for `obj` and emits its start event.
 *
 * refcount: takes `obj`, freed with the frame (or here on failure).
 * returns the WalkEvent emitted, or -1 with an exception pending. */
static int
walk_enter(WalkIterator* it, JSContext* ctx, JSValue obj) {
  BOOL array = JS_IsArray(ctx, obj);
  WalkMeta* meta;
  PropertyEnumeration* top;
  uint32_t length = 0;

  if(walk_active_has(it, (uintptr_t)JS_VALUE_GET_PTR(obj))) {
    JS_FreeValue(ctx, obj);
    JS_ThrowTypeError(ctx, "%s", it->hooks.circular ? it->hooks.circular : "circular structure");
    return -1;
  }

  if(array) {
    JSValue lv = JS_GetPropertyStr(ctx, obj, "length");

    if(JS_IsException(lv) || JS_ToUint32(ctx, &length, lv)) {
      JS_FreeValue(ctx, lv);
      JS_FreeValue(ctx, obj);
      return -1;
    }

    JS_FreeValue(ctx, lv);
  }

  if(!(meta = vector_emplace(&it->meta, sizeof(WalkMeta)))) {
    JS_FreeValue(ctx, obj);
    return -1;
  }

  *meta = (WalkMeta){0, length};

  if(!property_recursion_push(&it->stack, ctx, obj, it->flags)) {
    JS_FreeValue(ctx, obj);
    vector_pop(&it->meta, sizeof(WalkMeta));

    if(!JS_HasException(ctx))
      JS_ThrowInternalError(ctx, "walk: cannot enumerate properties");

    return -1;
  }

  top = WALK_TOP(it);

  if(walk_active_add(it, ctx, (uintptr_t)JS_VALUE_GET_PTR(top->obj)) < 0)
    return -1;

  if(it->hooks.keys && it->hooks.keys(it->hooks.opaque, ctx, top, array))
    return -1;

  it->key_done = FALSE;
  it->entered = TRUE;

  if((array ? it->iface.array_start : it->iface.object_start)(it->iface.opaque, ctx) < 0)
    return -1;

  return array ? WALK_EVENT_ARRAY_START : WALK_EVENT_OBJECT_START;
}

/* walk_report: reports a prepared value, or enters a prepared container.
 *
 * refcount: takes `val`.
 * returns the WalkEvent emitted, or -1 with an exception pending. */
static int
walk_report(WalkIterator* it, JSContext* ctx, int kind, JSValue val) {
  ssize_t r;

  if(kind == WALK_KIND_CONTAINER)
    return walk_enter(it, ctx, val);

  r = kind == WALK_KIND_RAW && it->iface.raw ? it->iface.raw(it->iface.opaque, ctx, val) : it->iface.value(it->iface.opaque, ctx, val);
  JS_FreeValue(ctx, val);
  return r < 0 ? -1 : WALK_EVENT_VALUE;
}

/* walk_next: reports the next event to the callbacks.
 *
 * {a: 1} gives, over three calls: object_start, key("a") + value(1)
 * (two calls), object_end; what is skipped or rewritten is up to the hooks.
 *
 *   returns int  the WalkEvent emitted (> 0), 0 when done, -1 with an
 *                exception pending (callback error, toJSON/replacer error,
 *                BigInt, circular reference or enumeration failure).
 */
int
walk_next(WalkIterator* it, JSContext* ctx) {
  void* opaque = it->iface.opaque;
  PropertyEnumeration* top;
  WalkMeta* meta;
  ssize_t r;
  BOOL array;
  JSValue val;

  it->entered = FALSE;

  if(it->state == WALK_DONE)
    return 0;

  if(it->state == WALK_START) {
    JSValue key = JS_NewString(ctx, "");
    int kind;

    it->state = WALK_RUNNING;

    val = JS_DupValue(ctx, it->root);
    kind = walk_prepare(it, ctx, JS_UNDEFINED, key, &val);
    JS_FreeValue(ctx, key);

    if(kind < 0) {
      it->state = WALK_DONE;
      return -1;
    }

    if(kind == WALK_KIND_OMIT) {
      it->state = WALK_DONE;
      it->root_omitted = TRUE;
      return 0;
    }

    if(kind != WALK_KIND_CONTAINER)
      it->state = WALK_DONE;

    return walk_report(it, ctx, kind, val);
  }

  top = WALK_TOP(it);
  meta = WALK_META(it);
  array = JS_IsArray(ctx, top->obj);

  /* the value of the key just reported */
  if(!array && it->key_done) {
    int kind = it->pending_kind;

    val = it->pending;
    it->pending = JS_UNDEFINED;
    it->key_done = FALSE;

    if(kind != WALK_KIND_CONTAINER)
      top->idx++; /* a container advances it when it ends */

    return walk_report(it, ctx, kind, val);
  }

  if(array) {
    if(meta->next >= meta->length) {
      top->idx = top->tab_atom_len;
      goto end;
    }

    val = JS_GetPropertyUint32(ctx, top->obj, meta->next);

    if(JS_IsException(val))
      return -1;

    {
      JSValue key = JS_NewUint32(ctx, meta->next);
      int kind = walk_prepare(it, ctx, top->obj, key, &val);

      JS_FreeValue(ctx, key);

      if(kind < 0)
        return -1;

      if(top->idx < top->tab_atom_len && meta->next < top->tab_atom_len)
        top->idx = meta->next;

      meta->next++;

      if(kind == WALK_KIND_OMIT)
        return it->iface.value(opaque, ctx, JS_NULL) < 0 ? -1 : WALK_EVENT_VALUE;

      return walk_report(it, ctx, kind, val);
    }
  }

  while(top->idx < top->tab_atom_len) {
    JSValue key = property_enumeration_key(top, ctx);
    int kind;

    if(JS_IsException(key))
      return -1;

    val = property_enumeration_value(top, ctx);

    if(JS_IsException(val)) {
      JS_FreeValue(ctx, key);
      return -1;
    }

    kind = walk_prepare(it, ctx, top->obj, key, &val);

    if(kind < 0) {
      JS_FreeValue(ctx, key);
      return -1;
    }

    if(kind == WALK_KIND_OMIT) {
      JS_FreeValue(ctx, key);
      top->idx++;
      continue;
    }

    r = it->iface.key(opaque, ctx, key);
    JS_FreeValue(ctx, key);

    if(r < 0) {
      JS_FreeValue(ctx, val);
      return -1;
    }

    it->pending = val;
    it->pending_kind = kind;
    it->key_done = TRUE;
    return WALK_EVENT_KEY;
  }

end:
  r = (array ? it->iface.array_end : it->iface.object_end)(opaque, ctx);
  walk_active_del(it, (uintptr_t)JS_VALUE_GET_PTR(top->obj));
  property_recursion_pop(&it->stack, ctx);
  vector_pop(&it->meta, sizeof(WalkMeta));

  if(vector_empty(&it->stack)) {
    it->state = WALK_DONE;
  } else {
    top = WALK_TOP(it);

    if(!JS_IsArray(ctx, top->obj))
      top->idx++; /* the property the container was the value of */
  }

  return r < 0 ? -1 : array ? WALK_EVENT_ARRAY_END : WALK_EVENT_OBJECT_END;
}

/* walk_skip: does not descend into the container just started.
 *
 * Call it right after walk_next() returned WALK_EVENT_OBJECT_START or
 * WALK_EVENT_ARRAY_START; the next walk_next() then reports the matching
 * end, so `{a: [1, 2]}` skipped at `[` gives array_start, array_end.
 *
 *   returns int  0, or -1 with a RangeError pending when the last event
 *                was not a container start.
 */
int
walk_skip(WalkIterator* it, JSContext* ctx) {
  PropertyEnumeration* top;
  WalkMeta* meta;

  if(!it->entered) {
    JS_ThrowRangeError(ctx, "walk_skip: not right after a container start");
    return -1;
  }

  top = WALK_TOP(it);
  meta = WALK_META(it);
  top->idx = top->tab_atom_len;
  meta->next = meta->length;
  it->entered = FALSE;
  return 0;
}

/* walk_close_rt: frees the frames and everything the iterator owns; safe after done or error.
 *
 * the form for a finalizer, which has a runtime but no context. never throws;
 * the iterator must be walk_init()'ed again to reuse. */
void
walk_close_rt(WalkIterator* it, JSRuntime* rt) {
  property_recursion_free(&it->stack, rt);
  vector_free(&it->meta);
  js_free_rt(rt, it->active);
  it->active = 0;
  it->active_cap = it->active_len = 0;
  JS_FreeValueRT(rt, it->root);
  JS_FreeValueRT(rt, it->pending);
  it->root = it->pending = JS_UNDEFINED;

  it->state = WALK_DONE;
}

/* walk_close: walk_close_rt() for the runtime of `ctx`. */
void
walk_close(WalkIterator* it, JSContext* ctx) {
  walk_close_rt(it, JS_GetRuntime(ctx));
}
