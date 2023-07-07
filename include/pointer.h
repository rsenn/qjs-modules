#ifndef POINTER_H
#define POINTER_H

#include <quickjs.h>
#include <cutils.h>
#include <assert.h>

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

#define POINTER_INDEX(ptr, ind) ((((ind) % ((ptr)->n)) + (ptr)->n) % (ptr)->n)
#define POINTER_INRANGE(ptr, ind) ((ind) >= 0 && (ind) < (ptr)->n)

typedef Pointer* DataFunc(JSContext*, JSValueConst);

void pointer_reset(Pointer*, JSContext*);
BOOL pointer_copy(Pointer*, Pointer const* src, JSContext*);
BOOL pointer_allocate(Pointer*, size_t size, JSContext*);
void pointer_dump(Pointer const*, DynBuf* db, BOOL color, size_t index, JSContext*);
void pointer_debug(Pointer const*, JSContext*);
size_t pointer_parse(Pointer*, const char* str, size_t len, JSContext*);
Pointer* pointer_slice(Pointer*, int64_t start, int64_t end, JSContext*);
JSValue pointer_shift(Pointer*, JSContext*);
BOOL pointer_unshift(Pointer* ptr, JSValueConst, JSContext*);
JSValue pointer_pop(Pointer* ptr, JSContext*);
void pointer_push(Pointer* ptr, JSValueConst, JSContext*);
void pointer_pushfree(Pointer* ptr, JSValue item, JSContext* ctx);
JSValue pointer_deref(Pointer const*, JSValueConst arg, JSContext*);
JSValue pointer_acquire(Pointer const*, JSValueConst arg, JSContext*);
BOOL pointer_fromstring(Pointer*, JSValueConst value, JSContext*);
void pointer_fromarray(Pointer*, JSValueConst array, JSContext*);
void pointer_fromiterable(Pointer*, JSValueConst arg, JSContext*);
BOOL pointer_fromatoms(Pointer* ptr, JSAtom* vec, size_t len, JSContext* ctx);
int pointer_from(Pointer*, JSValueConst value, JSContext*);
Pointer* pointer_concat(Pointer const*, JSValueConst arr, JSContext*);
void pointer_tostring(Pointer const*, DynBuf* db, JSContext*);
JSValue pointer_toarray(Pointer const*, JSContext*);
JSValue pointer_toatoms(Pointer const*, JSContext*);

static inline Pointer*
pointer_new(JSContext* ctx) {
  return js_mallocz(ctx, sizeof(Pointer));
}

static inline void
pointer_free(Pointer* ptr, JSContext* ctx) {
  pointer_reset(ptr, ctx);
  js_free(ctx, ptr);
}

static inline JSAtom
pointer_at(Pointer const* ptr, int32_t index) {
  JSAtom atom;

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
