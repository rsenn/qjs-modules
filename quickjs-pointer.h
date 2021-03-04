#ifndef QJS_MODULES_POINTER_H
#define QJS_MODULES_POINTER_H

#include "quickjs.h"
#include "cutils.h"
#include <stdint.h>

typedef struct {
  int64_t n;
  JSAtom* atoms;
} Pointer;

Pointer* pointer_new(JSContext*);
void pointer_push(Pointer*, JSAtom atom);
void pointer_dump(Pointer*, JSContext* ctx, DynBuf* db, BOOL color);
void pointer_reset(Pointer*, JSContext* ctx);
void pointer_truncate(Pointer* ptr, JSContext* ctx, size_t size);
JSValue pointer_shift(Pointer*, JSContext* ctx, JSValue obj);
Pointer* pointer_slice(Pointer*, JSContext* ctx, int64_t start, int64_t end);

Pointer* js_pointer_data(JSContext*, JSValueConst value);
JSValue js_pointer_wrap(JSContext*, Pointer* ptr);

#endif /* defined(QJS_MODULES_POINTER_H) */
