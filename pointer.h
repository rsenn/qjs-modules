#ifndef POINTER_H
#define POINTER_H

#include "quickjs.h"
#include "cutils.h"
#include <stdint.h>

typedef struct Pointer {
  size_t n;
  JSAtom* atoms;
} Pointer;

typedef Pointer* DataFunc(JSContext*, JSValueConst);

void pointer_copy(Pointer*, Pointer*, JSContext*);
JSValue pointer_deref(Pointer*, JSContext*, JSValue);
JSValue pointer_acquire(Pointer*, JSContext*, JSValue);
void pointer_dump(Pointer*, JSContext*, DynBuf*, BOOL, size_t);
void pointer_debug(Pointer*, JSContext*);
void pointer_fromarray(Pointer*, JSContext*, JSValue);
void pointer_fromiterable(Pointer*, JSContext*, JSValue);
void pointer_fromstring(Pointer*, JSContext*, JSValue);
size_t pointer_parse(Pointer*, JSContext*, const char*, size_t);
void pointer_reset(Pointer*, JSContext*);
JSValue pointer_shift(Pointer*, JSContext*, JSValueConst);
void pointer_push(Pointer*, JSContext*, JSValueConst);
Pointer* pointer_slice(Pointer*, JSContext*, int64_t, int64_t);
void pointer_truncate(Pointer*, JSContext*, size_t);
int pointer_from(Pointer* ptr, JSContext* ctx, JSValueConst value);
Pointer* pointer_concat(Pointer* ptr, JSContext* ctx, JSValueConst arr);

static inline Pointer*
pointer_new(JSContext* ctx) {
  return js_mallocz(ctx, sizeof(Pointer));
}

static inline void
pointer_free(Pointer* ptr, JSContext* ctx) {
  pointer_reset(ptr, ctx);
  js_free(ctx, ptr);
}

static inline Pointer*
pointer_clone(Pointer* other, JSContext* ctx) {
  Pointer* ptr;
  if((ptr = pointer_new(ctx)))
    pointer_copy(ptr, other, ctx);
  return ptr;
}

static inline void
pointer_pushatom(Pointer* ptr, JSContext* ctx, JSAtom atom) {
  if((ptr->atoms = js_realloc(ctx, ptr->atoms, (ptr->n + 1) * sizeof(JSAtom))))
    ptr->atoms[ptr->n++] = atom;
}

static inline JSAtom
pointer_pop(Pointer* ptr) {
  JSAtom ret = JS_ATOM_NULL;
  size_t size = ptr->n;
  if(size > 0) {
    ret = ptr->atoms[size - 1];
    ptr->atoms[--ptr->n] = 0;
  }
  return ret;
}

#endif /* defined(POINTER_H) */
