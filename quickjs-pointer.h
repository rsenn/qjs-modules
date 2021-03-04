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
void pointer_reset(Pointer*, JSContext* ctx);
void pointer_truncate(Pointer* ptr, JSContext* ctx, size_t size);
Pointer* pointer_dup(Pointer*, JSContext* ctx);
void pointer_copy(Pointer*, Pointer* src, JSContext* ctx);
void pointer_push(Pointer*, JSAtom atom);
void pointer_dump(Pointer*, JSContext* ctx, DynBuf* db, BOOL color);
JSValue pointer_shift(Pointer*, JSContext* ctx, JSValue obj);
Pointer* pointer_slice(Pointer*, JSContext* ctx, int64_t start, int64_t end);

void pointer_fromarray(Pointer*, JSContext* ctx, JSValue array);
void pointer_fromiterable(Pointer*, JSContext* ctx, JSValue arg);
void pointer_fromstring(Pointer*, JSContext* ctx, JSValue value);

Pointer* js_pointer_data(JSContext*, JSValueConst value);
JSValue js_pointer_wrap(JSContext*, Pointer* ptr);
JSValue js_pointer_new(JSContext*, JSValue proto, JSValue value);

#endif /* defined(QJS_MODULES_POINTER_H) */
