/* walk.c: walk_init(), walk_next(), walk_close().
 * depends on: property-enumeration.c (the frame stack), walk.h (callbacks).
 * rule: one callback per walk_next(); the walker knows no output format.
 * also: js_walk_define()/js_walk_wrap(), the callbacks as a JS object. */

#include "walk.h"
#include "property-enumeration.h"

enum { WALK_START, WALK_RUNNING, WALK_DONE };

/* walk_init: starts walking `root`, reporting to `iface`; emits nothing yet.
 *
 *   WalkIterator*  it     iterator to initialize
 *   JSValueConst   root   value to walk, dup'ed (refcount: freed by close)
 *   WalkInterface  iface  callbacks, copied by value
 *
 *   returns int  0, or -1 with an exception pending.
 */
int
walk_init(WalkIterator* it, JSContext* ctx, JSValueConst root, WalkInterface iface) {
  vector_init(&it->stack, ctx);
  it->walk = iface;
  it->flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
  it->root = JS_DupValue(ctx, root);
  it->state = WALK_START;
  it->key_done = FALSE;
  return 0;
}

static BOOL
walk_is_container(JSContext* ctx, JSValueConst val) {
  return JS_IsObject(val) && !JS_IsFunction(ctx, val);
}

/* walk_enter: pushes a frame for `obj` and emits its start event.
 *
 * refcount: takes `obj`, freed with the frame (or here on failure).
 * returns the WalkEvent emitted, or -1 with an exception pending. */
static int
walk_enter(WalkIterator* it, JSContext* ctx, JSValue obj) {
  BOOL array = JS_IsArray(ctx, obj);

  if(!property_recursion_push(&it->stack, ctx, obj, it->flags)) {
    JS_FreeValue(ctx, obj);

    if(!JS_HasException(ctx))
      JS_ThrowInternalError(ctx, "walk: cannot enumerate properties");

    return -1;
  }

  it->key_done = FALSE;
  it->entered = TRUE;

  if((array ? it->walk.array_start : it->walk.object_start)(it->walk.opaque, ctx) < 0)
    return -1;

  return array ? WALK_EVENT_ARRAY_START : WALK_EVENT_OBJECT_START;
}

/* walk_next: reports the next event to the callbacks.
 *
 * {a: 1} gives, over three calls: object_start, key("a") + value(1)
 * (two calls), object_end; a circular reference is written as null.
 *
 *   returns int  the WalkEvent emitted (> 0), 0 when done, -1 with an
 *                exception pending (callback error or enumeration failure).
 */
int
walk_next(WalkIterator* it, JSContext* ctx) {
  void* opaque = it->walk.opaque;
  PropertyEnumeration* top;
  ssize_t r;
  BOOL array;
  JSValue val;

  it->entered = FALSE;

  if(it->state == WALK_DONE)
    return 0;

  if(it->state == WALK_START) {
    it->state = WALK_RUNNING;

    if(walk_is_container(ctx, it->root))
      return walk_enter(it, ctx, JS_DupValue(ctx, it->root));

    it->state = WALK_DONE;
    return it->walk.value(opaque, ctx, it->root) < 0 ? -1 : WALK_EVENT_VALUE;
  }

  top = vector_back(&it->stack, sizeof(PropertyEnumeration));
  array = JS_IsArray(ctx, top->obj);

  if(top->idx >= top->tab_atom_len) {
    r = (array ? it->walk.array_end : it->walk.object_end)(opaque, ctx);
    property_recursion_pop(&it->stack, ctx);

    if(vector_empty(&it->stack))
      it->state = WALK_DONE;

    return r < 0 ? -1 : array ? WALK_EVENT_ARRAY_END : WALK_EVENT_OBJECT_END;
  }

  if(!array && !it->key_done) {
    JSValue key = property_enumeration_key(top, ctx);

    if(JS_IsException(key))
      return -1;

    r = it->walk.key(opaque, ctx, key);
    JS_FreeValue(ctx, key);

    if(r < 0)
      return -1;

    it->key_done = TRUE;
    return WALK_EVENT_KEY;
  }

  if(JS_IsException(val = property_enumeration_value(top, ctx)))
    return -1;

  it->key_done = FALSE;
  top->idx++; /* before the push: it may move the frames */

  if(walk_is_container(ctx, val) && !property_recursion_circular(&it->stack, val))
    return walk_enter(it, ctx, val);

  r = it->walk.value(opaque, ctx, walk_is_container(ctx, val) ? JS_NULL : val);
  JS_FreeValue(ctx, val);
  return r < 0 ? -1 : WALK_EVENT_VALUE;
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

  if(!it->entered) {
    JS_ThrowRangeError(ctx, "walk_skip: not right after a container start");
    return -1;
  }

  top = vector_back(&it->stack, sizeof(PropertyEnumeration));
  top->idx = top->tab_atom_len;
  it->entered = FALSE;
  return 0;
}

/* walk_close: frees the frames and the root; safe after done or error.
 *
 * never throws; the iterator must be walk_init()'ed again to reuse. */
void
walk_close(WalkIterator* it, JSContext* ctx) {
  property_recursion_free(&it->stack, JS_GetRuntime(ctx));
  JS_FreeValue(ctx, it->root);
  it->root = JS_UNDEFINED;
  it->state = WALK_DONE;
}

/* WalkInterface as a JS object: key, objectStart, arrayStart, objectEnd,
 * arrayEnd, value, each calling through the matching function pointer.
 *
 * ```js
 * const w = walk;           // from js_walk_wrap(ctx, iface)
 * w.objectStart();
 * w.key('a');
 * w.value(1);               // returns the bytes written
 * w.objectEnd();
 * ```
 *
 *   magic  JS method       calls            arguments
 *   0      key             iface->key       key
 *   1      objectStart     object_start     none
 *   2      arrayStart      array_start      none
 *   3      objectEnd       object_end       none
 *   4      arrayEnd        array_end        none
 *   5      value           value            value
 *
 *   returns  bytes written, as the callback reports
 *   throws   TypeError for a missing argument or a NULL callback
 */
enum {
  WALK_METHOD_KEY,
  WALK_METHOD_OBJECT_START,
  WALK_METHOD_ARRAY_START,
  WALK_METHOD_OBJECT_END,
  WALK_METHOD_ARRAY_END,
  WALK_METHOD_VALUE,
};

static JSClassID js_walk_class_id = 0;

/* classes whose objects can use the methods: class id -> WalkInterface */
static struct {
  JSClassID class_id;
  WalkFromOpaque* from_opaque;
} walk_classes[16];

static size_t walk_classes_len;

/* js_walk_register: lets objects of `class_id` call the WalkInterface
 * methods; `from_opaque` makes the interface from the object's opaque.
 *
 *   js_walk_register(js_jsonwriter_class_id, js_jsonwriter_walk);
 *
 * JS thread only: call it while the class is set up.
 * returns 0, or -1 when the table is full (never throws). */
int
js_walk_register(JSClassID class_id, WalkFromOpaque* from_opaque) {
  for(size_t i = 0; i < walk_classes_len; i++)
    if(walk_classes[i].class_id == class_id)
      return 0;

  if(walk_classes_len == countof(walk_classes))
    return -1;

  walk_classes[walk_classes_len].class_id = class_id;
  walk_classes[walk_classes_len++].from_opaque = from_opaque;
  return 0;
}

static WalkInterface
js_walk_copy(void* opaque) {
  return *(WalkInterface*)opaque;
}

static JSValue
js_walk_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  WalkInterface iface;
  JSClassID class_id = JS_GetClassID(this_val);
  void* opaque = 0;
  ssize_t w = -2;

  for(size_t i = 0; i < walk_classes_len; i++) {
    if(walk_classes[i].class_id == class_id) {
      if(!(opaque = JS_GetOpaque2(ctx, this_val, class_id)))
        return JS_EXCEPTION;

      iface = walk_classes[i].from_opaque(opaque);
      break;
    }
  }

  if(!opaque)
    return JS_ThrowTypeError(ctx, "WalkInterface: not a walkable object");

  if((magic == WALK_METHOD_KEY || magic == WALK_METHOD_VALUE) && argc < 1)
    return JS_ThrowTypeError(ctx, "%s() requires an argument", magic == WALK_METHOD_KEY ? "key" : "value");

  switch(magic) {
    case WALK_METHOD_KEY: w = iface.key ? iface.key(iface.opaque, ctx, argv[0]) : -2; break;
    case WALK_METHOD_OBJECT_START: w = iface.object_start ? iface.object_start(iface.opaque, ctx) : -2; break;
    case WALK_METHOD_ARRAY_START: w = iface.array_start ? iface.array_start(iface.opaque, ctx) : -2; break;
    case WALK_METHOD_OBJECT_END: w = iface.object_end ? iface.object_end(iface.opaque, ctx) : -2; break;
    case WALK_METHOD_ARRAY_END: w = iface.array_end ? iface.array_end(iface.opaque, ctx) : -2; break;
    case WALK_METHOD_VALUE: w = iface.value ? iface.value(iface.opaque, ctx, argv[0]) : -2; break;
  }

  if(w == -2)
    return JS_ThrowTypeError(ctx, "WalkInterface: callback not set");

  return w < 0 ? JS_EXCEPTION : JS_NewInt64(ctx, w);
}

static const JSCFunctionListEntry js_walk_funcs[] = {
    JS_CFUNC_MAGIC_DEF("key", 1, js_walk_method, WALK_METHOD_KEY),
    JS_CFUNC_MAGIC_DEF("objectStart", 0, js_walk_method, WALK_METHOD_OBJECT_START),
    JS_CFUNC_MAGIC_DEF("arrayStart", 0, js_walk_method, WALK_METHOD_ARRAY_START),
    JS_CFUNC_MAGIC_DEF("objectEnd", 0, js_walk_method, WALK_METHOD_OBJECT_END),
    JS_CFUNC_MAGIC_DEF("arrayEnd", 0, js_walk_method, WALK_METHOD_ARRAY_END),
    JS_CFUNC_MAGIC_DEF("value", 1, js_walk_method, WALK_METHOD_VALUE),
};

static const JSCFunctionListEntry js_walk_tag[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WalkInterface", JS_PROP_CONFIGURABLE),
};

/* js_walk_define: puts the six WalkInterface methods onto `obj`.
 *
 * The methods work on objects of a class given to js_walk_register(), and
 * on objects made by js_walk_wrap(); sets no Symbol.toStringTag.
 *
 *   returns int  0, or -1 with an exception pending. */
int
js_walk_define(JSContext* ctx, JSValueConst obj) {
  return JS_SetPropertyFunctionList(ctx, obj, js_walk_funcs, countof(js_walk_funcs));
}

static void
js_walk_finalizer(JSRuntime* rt, JSValue val) {
  js_free_rt(rt, JS_GetOpaque(val, js_walk_class_id));
}

static JSClassDef js_walk_class = {
    .class_name = "WalkInterface",
    .finalizer = js_walk_finalizer,
};

/* js_walk_wrap: makes a JS object whose methods call `iface`.
 *
 *   const w = js_walk_wrap(ctx, iface);   // w.key('a'), ...
 *
 * borrowed: iface.opaque is not owned; it must outlive the JS object.
 * returns the object (refcount: new reference), or JS_EXCEPTION. */
JSValue
js_walk_wrap(JSContext* ctx, WalkInterface iface) {
  JSRuntime* rt = JS_GetRuntime(ctx);
  JSValue proto, obj;
  WalkInterface* copy;

  if(js_walk_class_id == 0)
    JS_NewClassID(&js_walk_class_id);

  if(!JS_IsRegisteredClass(rt, js_walk_class_id))
    JS_NewClass(rt, js_walk_class_id, &js_walk_class);

  js_walk_register(js_walk_class_id, js_walk_copy);

  proto = JS_GetClassProto(ctx, js_walk_class_id);

  if(JS_IsException(proto))
    return JS_EXCEPTION;

  if(JS_IsNull(proto) || JS_IsUndefined(proto)) {
    proto = JS_NewObject(ctx);

    if(js_walk_define(ctx, proto) < 0 || JS_SetPropertyFunctionList(ctx, proto, js_walk_tag, countof(js_walk_tag)) < 0) {
      JS_FreeValue(ctx, proto);
      return JS_EXCEPTION;
    }

    JS_SetClassProto(ctx, js_walk_class_id, JS_DupValue(ctx, proto));
  }

  if(!(copy = js_malloc(ctx, sizeof(*copy)))) {
    JS_FreeValue(ctx, proto);
    return JS_EXCEPTION;
  }

  *copy = iface;
  obj = JS_NewObjectProtoClass(ctx, proto, js_walk_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    js_free(ctx, copy);
    return obj;
  }

  JS_SetOpaque(obj, copy);
  return obj;
}
