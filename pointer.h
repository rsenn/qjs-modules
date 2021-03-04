#ifndef POINTER_H
#define POINTER_H

#include "quickjs.h"
#include "cutils.h"
#include <stdint.h>

typedef struct {
  int64_t n;
  JSAtom* atoms;
} Pointer;

typedef Pointer* DataFunc(JSContext*, JSValueConst);

void pointer_copy(Pointer*, Pointer*, JSContext*);
JSValue pointer_deref(Pointer*, JSContext*, JSValue);
JSValue pointer_acquire(Pointer*, JSContext*, JSValue);
void pointer_dump(Pointer*, JSContext*, DynBuf*, BOOL, int);
void pointer_debug(Pointer*, JSContext*);
int pointer_from(Pointer*, JSContext*, JSValue, DataFunc* data);
void pointer_fromarray(Pointer*, JSContext*, JSValue);
void pointer_fromiterable(Pointer*, JSContext*, JSValue);
void pointer_fromstring(Pointer*, JSContext*, JSValue);
size_t pointer_parse(Pointer*, JSContext*, const char*, size_t);
void pointer_reset(Pointer*, JSContext*);
JSValue pointer_shift(Pointer*, JSContext*, JSValue);
Pointer* pointer_slice(Pointer*, JSContext*, int64_t, int64_t);
void pointer_tostring(Pointer*, JSContext*, DynBuf*);
void pointer_truncate(Pointer*, JSContext*, size_t);

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
pointer_dup(Pointer* other, JSContext* ctx) {
  Pointer* ptr;
  if((ptr = pointer_new(ctx)))
    pointer_copy(ptr, other, ctx);
  return ptr;
}

static inline void
pointer_push(Pointer* ptr, JSAtom atom) {
  ptr->atoms = realloc(ptr->atoms, (ptr->n + 1) * sizeof(JSAtom));
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
