#ifndef POINTER_H
#define POINTER_H

#include <quickjs.h>
#include <assert.h>
#include "stream-utils.h"

/**
 * \defgroup pointer pointer: JS Object pointer (deep key)
 * @{
 */

typedef struct Pointer {
  size_t n;
  JSAtom* atoms;
} Pointer;

#define POINTER_LENGTH(ptr) ((ptr)->n)
#define POINTER_ATOMS(ptr) ((ptr)->atoms)

#define POINTER_INDEX(ptr, ind) (((((signed)(ind)) % (signed)(ptr)->n) + (ptr)->n) % (signed)(ptr)->n)
#define POINTER_INRANGE(ptr, ind) ((ind) >= 0 && (ind) < (signed long)(ptr)->n)

void pointer_reset(Pointer*, JSRuntime* rt);
BOOL pointer_copy(Pointer*, Pointer const* src, JSContext*);
BOOL pointer_allocate(Pointer*, size_t size, JSContext*);
void pointer_dump(Pointer const*, Writer*, BOOL color, ssize_t index, JSContext*);
char* pointer_tostring(Pointer const* ptr, BOOL color, ssize_t index, JSContext*);
void pointer_serialize(Pointer const*, Writer* db, JSContext*);
size_t pointer_parse(Pointer*, const char* str, size_t len, JSContext*);
Pointer* pointer_slice(Pointer*, int64_t start, int64_t end, JSContext*);
Pointer* pointer_splice(Pointer*, int64_t start, int64_t count, JSAtom* atoms, size_t insert, JSContext*);
BOOL pointer_fromatoms(Pointer*, JSAtom* vec, size_t len, JSContext*);
JSValue pointer_shift(Pointer*, JSContext*);
JSValue pointer_pop(Pointer*, JSContext*);
BOOL pointer_unshift(Pointer*, JSValueConst value, JSContext*);
void pointer_push(Pointer*, JSValueConst item, JSContext*);
void pointer_pushfree(Pointer*, JSValue item, JSContext*);
JSValue pointer_deref(Pointer const*, JSValueConst arg, JSContext*);
JSValue pointer_acquire(Pointer const*, JSValueConst arg, JSContext*);
BOOL pointer_fromstring(Pointer*, JSValueConst value, JSContext*);
void pointer_fromarray(Pointer*, JSValueConst array, JSContext*);
void pointer_fromiterable(Pointer*, JSValueConst arg, JSContext*);
int pointer_from(Pointer*, JSValueConst value, JSContext*);
Pointer* pointer_concat(Pointer const*, JSValueConst iterable, JSContext*);
JSValue pointer_toarray(Pointer const*, JSContext*);

static inline Pointer*
pointer_new(JSContext* ctx) {
  return js_mallocz(ctx, sizeof(Pointer));
}

static inline void
pointer_free(Pointer* ptr, JSRuntime* rt) {
  pointer_reset(ptr, rt);
  js_free_rt(rt, ptr);
}

static inline JSAtom
pointer_at(Pointer const* ptr, int32_t index) {
  if(ptr->n)
    return ptr->atoms[POINTER_INDEX(ptr, index)];

  return 0;
}

static inline JSAtom*
pointer_ptr(Pointer const* ptr, int32_t index) {
  assert(ptr->n);
  return &ptr->atoms[POINTER_INDEX(ptr, index)];
}

static inline void
pointer_pushatom(Pointer* ptr, JSAtom atom, JSContext* ctx) {
  if((ptr->atoms = js_realloc(ctx, ptr->atoms, (ptr->n + 1) * sizeof(JSAtom))))
    ptr->atoms[ptr->n++] = atom;
}

static inline Pointer*
pointer_clone(Pointer const* other, JSContext* ctx) {
  Pointer* ptr;

  if((ptr = pointer_new(ctx)))
    pointer_copy(ptr, other, ctx);

  return ptr;
}

static inline JSAtom
pointer_popatom(Pointer* ptr) {
  JSAtom ret = JS_ATOM_NULL;
  size_t size = ptr->n;

  if(size > 0) {
    ret = ptr->atoms[size - 1];
    ptr->atoms[--ptr->n] = 0;
  }

  return ret;
}

/**
 * @}
 */
#endif /* defined(POINTER_H) */
