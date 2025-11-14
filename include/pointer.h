#ifndef POINTER_H
#define POINTER_H

#include <stdint.h>
#include <quickjs.h>
#include <assert.h>
#include "stream-utils.h"

/**
 * \defgroup pointer pointer: JS Object pointer (deep key)
 * @{
 */

void atoms_dump(JSAtom const[], size_t, Writer*, BOOL, ssize_t, JSContext*);
void atoms_serialize(JSAtom const[], size_t, Writer*, JSContext*);
JSValue atoms_deref(JSAtom const[], size_t, size_t*, JSValueConst, JSContext*);
JSValue atoms_acquire(JSAtom const[], size_t, size_t, JSValueConst, JSContext*);
JSValue atoms_toarray(JSAtom const[], size_t, JSContext*);
JSValue atoms_uint32array(JSAtom const[], size_t, JSContext*);
BOOL atoms_equal(JSAtom const[], JSAtom const[], size_t);

typedef struct Pointer {
  size_t n, a;
  JSAtom* atoms;
} Pointer;

#define POINTER_INIT() \
  (Pointer) { \
    0, 0, NULL \
  }
#define POINTER_LENGTH(ptr) ((ptr)->n)
#define POINTER_ATOMS(ptr) ((ptr)->atoms)

#define POINTER_INDEX(ptr, ind) (((((signed)(ind)) % (signed)(ptr)->n) + (ptr)->n) % (signed)(ptr)->n)
#define POINTER_INRANGE(ptr, ind) ((ind) >= 0 && (ind) < (signed long)(ptr)->n)

void pointer_reset(Pointer*, JSRuntime* rt);
BOOL pointer_copy(Pointer*, Pointer const*, JSContext*);
BOOL pointer_allocate(Pointer*, size_t, JSContext*);
BOOL pointer_reserve(Pointer*, size_t, JSContext*);
BOOL pointer_truncate(Pointer*, size_t, JSContext*);
void pointer_dump(Pointer const*, Writer*, BOOL color, ssize_t index, JSContext*);
char* pointer_tostring(Pointer const* ptr, BOOL color, ssize_t index, JSContext*);
void pointer_serialize(Pointer const*, Writer* db, JSContext*);
ssize_t pointer_parse(Pointer*, const char* str, size_t len, JSContext*);
Pointer* pointer_slice(Pointer*, int64_t start, int64_t end, JSContext*);
Pointer* pointer_splice(Pointer*, int64_t start, int64_t count, JSAtom atoms[], size_t insert, JSContext*);
BOOL pointer_fromatoms(Pointer*, JSAtom const vec[], size_t len, JSContext*);
JSValue pointer_shift(Pointer*, JSContext*);
JSValue pointer_pop(Pointer*, JSContext*);
BOOL pointer_unshift(Pointer*, JSValueConst, JSContext*);
BOOL pointer_push(Pointer*, JSValueConst, JSContext*);
BOOL pointer_pushfree(Pointer*, JSValue item, JSContext*);
JSValue pointer_deref(Pointer const*, size_t*, JSValueConst, JSContext*);
JSValue pointer_acquire(Pointer const*, size_t, JSValueConst, JSContext*);
BOOL pointer_fromstring(Pointer*, JSValueConst, JSContext*);
BOOL pointer_fromarray(Pointer*, JSValueConst, JSContext*);
BOOL pointer_fromiterable(Pointer*, JSValueConst, JSContext*);
BOOL pointer_from(Pointer*, JSValueConst, JSContext*);
Pointer* pointer_concat(Pointer const*, JSValueConst, JSContext*);
JSValue pointer_toarray(Pointer const*, JSContext*);
JSValue pointer_arraybuffer(Pointer const* ptr, JSContext*);
BOOL pointer_equal(Pointer const* a, Pointer const* b);
JSValue pointer_uint32array(Pointer const* ptr, JSContext* ctx);
BOOL pointer_append(Pointer*, int, JSValueConst[], JSContext*);
int pointer_compare(Pointer const*, Pointer const*, int32_t, int32_t, uint32_t);

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

static inline BOOL
pointer_pushatom(Pointer* ptr, JSAtom atom, JSContext* ctx) {
  BOOL ret;

  if((ret = pointer_reserve(ptr, ptr->n + 1, ctx)))
    ptr->atoms[ptr->n++] = atom;

  return ret;
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
BOOL pointer_startswith(Pointer const*, Pointer const*);
BOOL pointer_endswith(Pointer const*, Pointer const*);

/**
 * @}
 */
#endif /* defined(POINTER_H) */
