/* xwalk.c: xwalk_init(), xwalk_close(); a tree of elements for walk.c.
 * depends on: walk.h (the stepper and its WalkHooks).
 * rule: the shape lives in two hooks; walk.c knows nothing of elements. */

#include "xml/xwalk.h"

/**
 * \addtogroup xwalk
 * @{
 */

enum { ROLE_ELEMENT, ROLE_ATTRIBUTES, ROLE_CHILDREN };

/* xwalk_role: what the container `back` levels below the top is; the root is an
 * element, or a list (children) when it is an array. A container inside an
 * array is an element; inside an object it is the value of `attributes`
 * or `children` of an element, as xwalk_prepare() lets nothing else through. */
static int
xwalk_role(const XwalkIterator* x, JSContext* ctx, int back) {
  const PropertyEnumeration *f = WALK_FRAME(&x->walk, back), *p = WALK_FRAME(&x->walk, back + 1);

  if(!p)
    return JS_IsArray(ctx, f->obj) ? ROLE_CHILDREN : ROLE_ELEMENT;

  if(JS_IsArray(ctx, p->obj))
    return ROLE_ELEMENT;

  return p->tab_atom[p->idx] == x->atom[1] ? ROLE_ATTRIBUTES : ROLE_CHILDREN;
}

/* xwalk_prepare (WalkHooks.prepare): undefined is omitted; an object is a container
 * only where the shape has one, everything else is a value.
 *
 *   parent                  container when
 *   none (the root)         always
 *   children / root list    the item is an object, not an array
 *   element, key attributes the value is an object, not an array
 *   element, key children   the value is an array
 *   attributes, tag         never */
static int
xwalk_prepare(void* opaque, JSContext* ctx, JSValueConst holder, JSValueConst key, JSValue* val) {
  const XwalkIterator* x = opaque;
  const PropertyEnumeration* top = WALK_TOP(&x->walk);
  int array;

  if(JS_IsUndefined(*val))
    return WALK_KIND_OMIT;

  if(!JS_IsObject(*val) || JS_IsFunction(ctx, *val))
    return WALK_KIND_VALUE;

  if(!top)
    return WALK_KIND_CONTAINER;

  array = JS_IsArray(ctx, *val);

  switch(xwalk_role(x, ctx, 0)) {
    case ROLE_CHILDREN: return array ? WALK_KIND_VALUE : WALK_KIND_CONTAINER;
    case ROLE_ATTRIBUTES: return WALK_KIND_VALUE;
    default: {
      JSAtom atom = top->tab_atom[top->idx];

      if(atom == x->atom[1])
        return array ? WALK_KIND_VALUE : WALK_KIND_CONTAINER;

      return atom == x->atom[2] && array ? WALK_KIND_CONTAINER : WALK_KIND_VALUE;
    }
  }
}

/* xwalk_keys (WalkHooks.keys): an element's keys are the three of the shape, in
 * order, those it has; the own keys walk.c enumerated are dropped. */
static int
xwalk_keys(void* opaque, JSContext* ctx, PropertyEnumeration* top, int array) {
  const XwalkIterator* x = opaque;
  JSAtom* list;
  uint32_t n = 0;

  if(array || xwalk_role(x, ctx, 0) != ROLE_ELEMENT)
    return 0;

  if(!(list = js_malloc(ctx, sizeof(JSAtom) * 3)))
    return -1;

  for(int i = 0; i < 3; i++) {
    int has = JS_HasProperty(ctx, top->obj, x->atom[i]);

    if(has < 0) {
      while(n)
        JS_FreeAtom(ctx, list[--n]);

      js_free(ctx, list);
      return -1;
    }

    if(has)
      list[n++] = JS_DupAtom(ctx, x->atom[i]);
  }

  for(uint32_t i = 0; i < top->tab_atom_len; i++)
    JS_FreeAtom(ctx, top->tab_atom[i]);

  js_free(ctx, top->tab_atom);
  top->tab_atom = list;
  top->tab_atom_len = n;
  return 0;
}

/* xwalk_init: starts walking the tree `root`, reporting to `iface`; emits nothing yet.
 *
 *   XwalkIterator*     it     iterator to initialize; must not move while in use
 *   JSValueConst       root   an element, or an array of elements and texts; dup'ed
 *   WalkInterface      iface  callbacks, copied by value
 *   const XwalkShape*  shape  key names, or NULL for tagName/attributes/children
 *
 *   returns int  0, or -1 with an exception pending.
 */
int
xwalk_init(XwalkIterator* it, JSContext* ctx, JSValueConst root, WalkInterface iface, const XwalkShape* shape) {
  const char* names[3] = {
      shape && shape->tag ? shape->tag : "tagName",
      shape && shape->attributes ? shape->attributes : "attributes",
      shape && shape->children ? shape->children : "children",
  };
  WalkHooks hooks = {it, xwalk_prepare, xwalk_keys, "circular structure in an element tree"};

  for(int i = 0; i < 3; i++) {
    if(!(it->atom[i] = JS_NewAtom(ctx, names[i]))) {
      while(i--)
        JS_FreeAtom(ctx, it->atom[i]);

      return -1;
    }
  }

  return walk_init(&it->walk, ctx, root, iface, hooks);
}

/* xwalk_close_rt: frees the frames and the key atoms; the form for a finalizer. */
void
xwalk_close_rt(XwalkIterator* it, JSRuntime* rt) {
  walk_close_rt(&it->walk, rt);

  for(int i = 0; i < 3; i++) {
    JS_FreeAtomRT(rt, it->atom[i]);
    it->atom[i] = JS_ATOM_NULL;
  }
}

/* xwalk_close: xwalk_close_rt() for the runtime of `ctx`. */
void
xwalk_close(XwalkIterator* it, JSContext* ctx) {
  xwalk_close_rt(it, JS_GetRuntime(ctx));
}

/**
 * @}
 */
