#define _GNU_SOURCE

#include "quickjs-pointer.h"
#include "pointer.h"
#include "utils.h"

void
pointer_reset(Pointer* ptr, JSContext* ctx) {
  size_t i;

  if(ptr->atoms) {
    for(i = 0; i < ptr->n; i++) JS_FreeAtom(ctx, ptr->atoms[i]);
    free(ptr->atoms);
    ptr->atoms = 0;
  }
  ptr->n = 0;
}

void
pointer_copy(Pointer* dst, Pointer* src, JSContext* ctx) {
  if(dst->n)
    pointer_reset(dst, ctx);

  if((dst->atoms = malloc(sizeof(JSAtom) * src->n))) {
    size_t i;
    dst->n = src->n;
    for(i = 0; i < src->n; i++) dst->atoms[i] = JS_DupAtom(ctx, src->atoms[i]);
  }
}

void
pointer_truncate(Pointer* ptr, JSContext* ctx, size_t size) {
  if(size == 0) {
    pointer_reset(ptr, ctx);
    return;
  }
  if(ptr->atoms) {
    size_t i;
    for(i = ptr->n - 1; i >= size; i--) JS_FreeAtom(ctx, ptr->atoms[i]);

    ptr->atoms = realloc(ptr->atoms, sizeof(JSAtom) * size);
    ptr->n = size;
  }
}

#define pointer_color(s)                                                                                   \
  ((index) >= 0 && (i) >= (index) ? "\x1b[31m" : (is_integer(s) ? "\x1b[1;30m" : "\x1b[0;33m"))

void
pointer_dump(Pointer* ptr, JSContext* ctx, DynBuf* db, BOOL color, int index) {
  size_t i;
  const char* s;

  dbuf_printf(db, "Pointer(%zx) ", ptr->n);

  for(i = 0; i < ptr->n; i++) {
    if(i > 0)
      dbuf_putstr(db, color ? "\x1b[1;36m." : ".");
    dbuf_putstr(db, color ? pointer_color(s) : "");
    // if(index == i) dbuf_putstr(db, "\x1b[31m");
    s = JS_AtomToCString(ctx, ptr->atoms[i]);
    dbuf_putstr(db, s);
  }
  dbuf_putstr(db, color ? "\x1b[m" : "");
}

void
pointer_debug(Pointer* ptr, JSContext* ctx) {
  DynBuf db;
  dbuf_init(&db);
  pointer_dump(ptr, ctx, &db, TRUE, -1);
  dbuf_0(&db);

  puts(db.buf);

  dbuf_free(&db);
}

void
pointer_tostring(Pointer* ptr, JSContext* ctx, DynBuf* db) {
  size_t i, j;
  const char* str;

  for(i = 0; i < ptr->n; i++) {
    if(i > 0)
      dbuf_putc(db, '.');

    str = JS_AtomToCString(ctx, ptr->atoms[i]);

    for(j = 0; str[j]; j++) {
      if(str[j] == '.')
        dbuf_putc(db, '\\');
      dbuf_putc(db, str[j]);
    }
  }
}

size_t
pointer_parse(Pointer* ptr, JSContext* ctx, const char* str, size_t len) {
  size_t pos, delim;
  JSAtom atom;
  while(len) {
    delim = 0;
    for(;;) {
      delim += byte_chr(&str[delim], len - delim, '.');
      if(delim < len && delim > 0 && str[delim - 1] == '\\') {
        ++delim;
        continue;
      }
      break;
    }
    atom = JS_NewAtomLen(ctx, str, delim);
    pointer_push(ptr, atom);
    str += delim;
    len -= delim;
    if(len > 0) {
      ++str;
      --len;
    }
  }
  return ptr->n;
}

Pointer*
pointer_slice(Pointer* ptr, JSContext* ctx, int64_t start, int64_t end) {
  Pointer* ret = js_mallocz(ctx, sizeof(Pointer));
  int64_t i;

  start = mod_int32(start, ptr->n);
  end = mod_int32(end, ptr->n);
  if(end == 0)
    end = ptr->n;

  ret->n = end - start;
  ret->atoms = ret->n ? malloc(sizeof(JSAtom) * ret->n) : 0;

  for(i = start; i < end; i++) ret->atoms[i - start] = JS_DupAtom(ctx, ptr->atoms[i]);

  return ret;
}

JSValue
pointer_shift(Pointer* ptr, JSContext* ctx, JSValueConst obj) {
  JSValue ret;
  if(ptr->n) {
    JSAtom atom;
    int64_t i;
    atom = ptr->atoms[0];
    for(i = 1; i < ptr->n; i++) { ptr->atoms[i - 1] = ptr->atoms[i]; }
    ptr->n--;
    ret = JS_GetProperty(ctx, obj, atom);
    JS_FreeAtom(ctx, atom);
  }
  return ret;
}

JSValue
pointer_deref(Pointer* ptr, JSContext* ctx, JSValueConst arg) {
  size_t i;
  JSValue obj = JS_DupValue(ctx, arg);

  for(i = 0; i < ptr->n; i++) {
    JSAtom atom = ptr->atoms[i]; // JS_DupAtom(ctx, ptr->atoms[i]);

    if(!JS_HasProperty(ctx, obj, atom)) {
      DynBuf dbuf;
      dbuf_init(&dbuf);
      pointer_dump(ptr, ctx, &dbuf, TRUE, i);
      dbuf_0(&dbuf);
      obj = JS_ThrowReferenceError(ctx, "%s", dbuf.buf);
      dbuf_free(&dbuf);
      break;
    }

    JSValue child = JS_GetProperty(ctx, obj, atom);
    JS_FreeValue(ctx, obj);

    obj = child;
  }
  return obj;
}

JSValue
pointer_acquire(Pointer* ptr, JSContext* ctx, JSValueConst arg) {
  size_t i;
  JSValue child, obj = JS_DupValue(ctx, arg);

  for(i = 0; i < ptr->n; i++) {
    JSAtom atom = ptr->atoms[i]; // JS_DupAtom(ctx, ptr->atoms[i]);

    if(!JS_HasProperty(ctx, obj, atom)) {
      BOOL is_array = i + 1 < ptr->n && js_atom_isint(ptr->atoms[i + 1]);
      child = is_array ? JS_NewArray(ctx) : JS_NewObject(ctx);
      JS_SetProperty(ctx, obj, atom, JS_DupValue(ctx, child));
    } else {
      child = JS_GetProperty(ctx, obj, atom);
    }
    JS_FreeValue(ctx, obj);

    obj = child;
  }
  return obj;
}

void
pointer_fromstring(Pointer* ptr, JSContext* ctx, JSValueConst value) {
  size_t len;
  const char* str;
  str = JS_ToCStringLen(ctx, &len, value);
  pointer_parse(ptr, ctx, str, len);
  JS_FreeCString(ctx, str);
}

void
pointer_fromarray(Pointer* ptr, JSContext* ctx, JSValueConst array) {
  size_t i, len;
  JSValueConst prop;
  JSAtom atom;
  len = js_array_length(ctx, array);
  pointer_reset(ptr, ctx);

  if(len >= 0) {
    ptr->atoms = malloc(sizeof(JSAtom) * len);
    for(i = 0; i < len; i++) {
      prop = JS_GetPropertyUint32(ctx, array, i);
      ptr->atoms[i] = JS_ValueToAtom(ctx, prop);
      JS_FreeValue(ctx, prop);
    }
    ptr->n = len;
  }
}

void
pointer_fromiterable(Pointer* ptr, JSContext* ctx, JSValueConst arg) {
  IteratorValue item;
  JSValue iter = js_iterator_new(ctx, arg);

  pointer_reset(ptr, ctx);

  for(;;) {
    item = js_iterator_next(ctx, iter);
    if(item.done)
      break;
    pointer_push(ptr, JS_ValueToAtom(ctx, item.value));
    JS_FreeValue(ctx, item.value);
  }
}

int
pointer_from(Pointer* ptr, JSContext* ctx, JSValueConst value, DataFunc* data) {
  Pointer* ptr2;

  if(data && (ptr2 = data(ctx, value)))
    pointer_copy(ptr, ptr2, ctx);
  else if(JS_IsString(value))
    pointer_fromstring(ptr, ctx, value);
  else if(JS_IsArray(ctx, value))
    pointer_fromarray(ptr, ctx, value);
  else if(!JS_IsUndefined(value))
    return 0;

  return 1;
}