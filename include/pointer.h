#ifndef POINTER_H
#define POINTER_H

#include <quickjs.h>
#include <cutils.h>
#include <assert.h>

/**
 * \defgroup pointer JS Object pointer (deep key)
 * @{
 */

typedef struct Pointer {
  size_t n;
  JSAtom* atoms;
} Pointer;

#define POINTER_INDEX(ptr, ind) ((((ind) % ((ptr)->n)) + (ptr)->n) % (ptr)->n)
#define POINTER_INRANGE(ptr, ind) ((ind) >= 0 && (ind) < (ptr)->n)

typedef Pointer* DataFunc(JSContext*, JSValueConst);

void pointer_reset(Pointer*, JSContext* ctx);
void pointer_copy(Pointer*, Pointer* src, JSContext* ctx);
void pointer_truncate(Pointer*, JSContext* ctx, size_t size);
void pointer_dump(Pointer*, JSContext* ctx, DynBuf* db, BOOL color, size_t index);
void pointer_debug(Pointer*, JSContext* ctx);
size_t pointer_parse(Pointer*, JSContext* ctx, const char* str, size_t len);
Pointer* pointer_slice(Pointer*, JSContext* ctx, int64_t start, int64_t end);
JSValue pointer_shift(Pointer*, JSContext* ctx, JSValue obj);
JSValue pointer_deref(Pointer*, JSContext* ctx, JSValue arg);
JSValue pointer_acquire(Pointer*, JSContext* ctx, JSValue arg);
BOOL pointer_fromstring(Pointer*, JSContext* ctx, JSValue value);
void pointer_fromarray(Pointer*, JSContext* ctx, JSValue array);
void pointer_fromiterable(Pointer*, JSContext* ctx, JSValue arg);
int pointer_from(Pointer*, JSContext* ctx, JSValue value);
Pointer* pointer_concat(Pointer const*, JSContext* ctx, JSValue arr);
void pointer_tostring(Pointer const*, JSContext* ctx, DynBuf* db);
JSValue pointer_toarray(Pointer const*, JSContext* ctx);
JSValue pointer_toatoms(Pointer const*, JSContext* ctx);
int pointer_fromatoms(Pointer*, JSContext* ctx, JSValue arr);

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
pointer_pushatom(Pointer* ptr, JSContext* ctx, JSAtom atom) {
  if((ptr->atoms = js_realloc(ctx, ptr->atoms, (ptr->n + 1) * sizeof(JSAtom))))
    ptr->atoms[ptr->n++] = atom;
}

static inline void
pointer_push(Pointer* ptr, JSContext* ctx, JSValueConst key) {
  pointer_pushatom(ptr, ctx, JS_ValueToAtom(ctx, key));
}

static inline Pointer*
pointer_clone(Pointer* other, JSContext* ctx) {
  Pointer* ptr;

  if((ptr = pointer_new(ctx)))
    pointer_copy(ptr, other, ctx);

  return ptr;
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

/**
 * @}
 */
#endif /* defined(POINTER_H) */
