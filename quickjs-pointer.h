#ifndef QJS_MODULES_POINTER_H
#define QJS_MODULES_POINTER_H

typedef struct {
  int64_t n;
  JSAtom* atoms;
} Pointer;

void         pointer_atom_add(Pointer*, JSContext* ctx, JSAtom atom);
void         pointer_dump(Pointer*, JSContext* ctx, DynBuf* db, BOOL color);
void         pointer_reset(Pointer*, JSContext* ctx);
JSValue      pointer_shift(Pointer*, JSContext* ctx, JSValue obj);
Pointer*     pointer_slice(Pointer*, JSContext* ctx, int64_t start, int64_t end);
#endif /* defined(QJS_MODULES_POINTER_H) */
