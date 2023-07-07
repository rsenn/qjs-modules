#include "defines.h"
#include "quickjs-pointer.h"
#include "pointer.h"
#include "utils.h"
#include "buffer-utils.h"
#include "debug.h"

/**
 * \addtogroup pointer
 * @{
 */

Pointer*
pointer_new(JSContext* ctx) {
  return js_mallocz(ctx, sizeof(Pointer));
}

void
pointer_free(Pointer* ptr, JSContext* ctx) {
  pointer_reset(ptr, ctx);
  js_free(ctx, ptr);
}

void
pointer_reset(Pointer* ptr, JSContext* ctx) {
  if(ptr->atoms) {
    size_t i;

    for(i = 0; i < ptr->n; i++)
      JS_FreeAtom(ctx, ptr->atoms[i]);

    js_free(ctx, ptr->atoms);
  }

  ptr->atoms = 0;
  ptr->n = 0;
}

void
pointer_copy(Pointer* dst, Pointer* src, JSContext* ctx) {
  pointer_reset(dst, ctx);

  if((dst->atoms = js_mallocz(ctx, sizeof(JSAtom) * src->n))) {
    size_t i;

    dst->n = src->n;

    for(i = 0; i < src->n; i++)
      dst->atoms[i] = JS_DupAtom(ctx, src->atoms[i]);
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

    for(i = size; i < ptr->n; i++)
      JS_FreeAtom(ctx, ptr->atoms[i]);

    ptr->atoms = js_realloc(ctx, ptr->atoms, sizeof(JSAtom) * size);
    ptr->n = size;
  }
}

#define pointer_color(s) ((i) >= (index) ? COLOR_RED : (is_integer(s) ? COLOR_LIGHTGRAY : COLOR_YELLOW))

void
pointer_dump(Pointer* ptr, JSContext* ctx, DynBuf* db, BOOL color, size_t index) {
  size_t i;

  for(i = 0; i < ptr->n; i++) {
    const char* s = JS_AtomToCString(ctx, ptr->atoms[i]);
    BOOL is_int = is_integer(s);

    dbuf_putstr(db, color ? (is_int ? COLOR_CYAN "[" : COLOR_CYAN ".") : (is_integer(s) ? "[" : "."));
    dbuf_putstr(db, color ? pointer_color(s) : "");
    dbuf_putstr(db, s);

    if(is_int)
      dbuf_putstr(db, color ? COLOR_CYAN "]" : "]");

    js_cstring_free(ctx, s);
  }

  dbuf_putstr(db, color ? COLOR_NONE : "");
}

void
pointer_debug(Pointer* ptr, JSContext* ctx) {
  DynBuf db;

  js_dbuf_init(ctx, &db);
  pointer_dump(ptr, ctx, &db, TRUE, -1);
  dbuf_0(&db);

  puts((const char*)db.buf);

  dbuf_free(&db);
}

size_t
pointer_parse(Pointer* ptr, JSContext* ctx, const char* str, size_t len) {
  while(len) {
    unsigned long val;
    char c = *str, *endptr;
    size_t start, delim, n;
    JSAtom atom;

    start = c == '[' ? 1 : 0;
    delim = start;

    for(;;) {
      delim += byte_chrs(&str[delim], len - delim, c == '[' ? "." : ".[", c == '[' ? 2 : 2);

      if(delim < len && delim > 0 && str[delim - 1] == '\\') {
        ++delim;
        continue;
      }

      break;
    }

    n = delim - start;
    endptr = 0;
    val = 0;

    if(delim && str[delim - 1] == ']')
      n--;

    if(is_digit_char(str[start]))
      val = strtoul(&str[start], &endptr, 10);

    if(endptr == &str[start + n])
      atom = js_atom_fromint(val);
    else
      atom = JS_NewAtomLen(ctx, &str[start], n);

    pointer_pushatom(ptr, ctx, atom);

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

  start = int32_mod(start, ptr->n);
  end = int32_mod(end, ptr->n);

  ret->n = end - start;
  ret->atoms = ret->n ? js_mallocz(ctx, sizeof(JSAtom) * ret->n) : 0;

  for(i = start; i < end; i++)
    ret->atoms[i - start] = JS_DupAtom(ctx, ptr->atoms[i]);

  return ret;
}

JSValue
pointer_shift(Pointer* ptr, JSContext* ctx, JSValueConst obj) {
  JSValue ret = JS_UNDEFINED;

  if(ptr->n) {
    size_t i;
    JSAtom atom = ptr->atoms[0];

    for(i = 1; i < ptr->n; i++)
      ptr->atoms[i - 1] = ptr->atoms[i];

    ptr->n--;
    ret = JS_GetProperty(ctx, obj, atom);
    JS_FreeAtom(ctx, atom);
  }

  return ret;
}

JSAtom
pointer_at(Pointer const* ptr, int32_t index) {
  JSAtom atom;

  if(ptr->n) {
    if((index %= (int32_t)ptr->n) < 0)
      index += ptr->n;

    return ptr->atoms[index];
  }

  return 0;
}

void
pointer_push(Pointer* ptr, JSContext* ctx, JSValueConst key) {
  if((ptr->atoms = js_realloc(ctx, ptr->atoms, sizeof(JSAtom) * (ptr->n + 1))))
    ptr->atoms[ptr->n++] = JS_ValueToAtom(ctx, key);
}

static JSValue
deref_atom(JSContext* ctx, JSValueConst obj, JSAtom atom) {
  if(!JS_HasProperty(ctx, obj, atom)) {
    JSValue value = JS_AtomToValue(ctx, atom);

    if(JS_IsFunction(ctx, value)) {
      JSPropertyEnum* tmp_tab;
      uint32_t tmp_len;

      if(!JS_GetOwnPropertyNames(ctx, &tmp_tab, &tmp_len, obj, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY)) {
        uint32_t i;

        for(i = 0; i < tmp_len; i++) {
          JSValueConst args[3] = {
              JS_GetProperty(ctx, obj, tmp_tab[i].atom),
              JS_AtomToValue(ctx, tmp_tab[i].atom),
              obj,
          };

          JSValue ret = JS_Call(ctx, value, JS_NULL, countof(args), args);
          BOOL match = JS_ToBool(ctx, ret);

          JS_FreeValue(ctx, args[1]);

          if(match) {
            JS_FreeValue(ctx, value);
            orig_js_free(ctx, tmp_tab);
            return args[0];
          }

          JS_FreeValue(ctx, args[0]);
        }

        orig_js_free(ctx, tmp_tab);
      }
    }
    JS_FreeValue(ctx, value);
    return JS_EXCEPTION;
  }
  return JS_GetProperty(ctx, obj, atom);
}

JSValue
pointer_deref(Pointer* ptr, JSContext* ctx, JSValueConst arg) {
  size_t i;
  JSValue obj = JS_DupValue(ctx, arg);

  for(i = 0; i < ptr->n; i++) {
    JSAtom atom = ptr->atoms[i]; // JS_DupAtom(ctx, ptr->atoms[i]);
    JSValue child = deref_atom(ctx, obj, atom);

    if(JS_IsException(child)) {

      //  if(!JS_HasProperty(ctx, obj, atom)) {
      DynBuf dbuf;
      js_dbuf_init(ctx, &dbuf);

      pointer_dump(ptr, ctx, &dbuf, TRUE, i);
      dbuf_0(&dbuf);
      obj = JS_ThrowReferenceError(ctx, "%s", dbuf.buf);
      dbuf_free(&dbuf);
      break;
    }

    // JSValue child = JS_GetProperty(ctx, obj, atom);
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

BOOL
pointer_fromstring(Pointer* ptr, JSContext* ctx, JSValueConst value) {
  size_t len;
  const char* str;

  if((str = JS_ToCStringLen(ctx, &len, value))) {
    pointer_parse(ptr, ctx, str, len);
    js_cstring_free(ctx, str);
    return TRUE;
  }

  return FALSE;
}

void
pointer_fromarray(Pointer* ptr, JSContext* ctx, JSValueConst array) {
  JSValueConst prop;
  size_t i, len = js_array_length(ctx, array);

  pointer_reset(ptr, ctx);

  if(len > 0) {
    ptr->atoms = js_malloc(ctx, sizeof(JSAtom) * len);

    for(i = 0; i < len; i++) {
      prop = JS_GetPropertyUint32(ctx, array, i);
      ptr->atoms[i] = JS_ValueToAtom(ctx, prop);
      JS_FreeValue(ctx, prop);
    }
  }

  ptr->n = len;
}

void
pointer_fromiterable(Pointer* ptr, JSContext* ctx, JSValueConst arg) {
  JSValue iter = js_iterator_new(ctx, arg);

  pointer_reset(ptr, ctx);

  for(;;) {
    BOOL done = FALSE;
    JSValue item = js_iterator_next(ctx, iter, &done);

    if(done) {
      JS_FreeValue(ctx, item);
      break;
    }

    pointer_push(ptr, ctx, item);
    JS_FreeValue(ctx, item);
  }

  JS_FreeValue(ctx, iter);
}

int
pointer_from(Pointer* ptr, JSContext* ctx, JSValueConst value) {
  Pointer* ptr2;

  if((ptr2 = js_pointer_data(value)))
    pointer_copy(ptr, ptr2, ctx);
  else if(JS_IsString(value))
    pointer_fromstring(ptr, ctx, value);
  else if(JS_IsArray(ctx, value))
    pointer_fromarray(ptr, ctx, value);
  else if(!JS_IsUndefined(value))
    return 0;

  return 1;
}

Pointer*
pointer_concat(Pointer const* ptr, JSContext* ctx, JSValueConst arr) {
  JSValue iterator;
  Pointer* ret = pointer_new(ctx);
  size_t i;

  ret->n = ptr->n;
  ret->atoms = js_realloc(ctx, ret->atoms, sizeof(JSAtom) * ret->n);

  for(i = 0; i < ptr->n; i++)
    ret->atoms[i] = JS_DupAtom(ctx, ptr->atoms[i]);

  iterator = js_iterator_new(ctx, arr);

  for(;;) {
    BOOL done = FALSE;
    JSValue item = js_iterator_next(ctx, iterator, &done);

    if(done)
      break;

    pointer_push(ret, ctx, item);
  }

  return ret;
}

void
pointer_tostring(Pointer const* ptr, JSContext* ctx, DynBuf* db) {
  size_t i, j;

  for(i = 0; i < ptr->n; i++) {
    const char* str;

    if(js_atom_isint(ptr->atoms[i])) {
      dbuf_printf(db, "[%" PRIu32 "]", js_atom_toint(ptr->atoms[i]));
      continue;
    }

    if(i > 0)
      dbuf_putc(db, '.');

    str = JS_AtomToCString(ctx, ptr->atoms[i]);

    for(j = 0; str[j]; j++) {
      if(str[j] == '.')
        dbuf_putc(db, '\\');

      dbuf_putc(db, str[j]);
    }

    JS_FreeCString(ctx, str);
  }
}

JSValue
pointer_toarray(Pointer const* ptr, JSContext* ctx) {
  size_t i;
  JSValue array = JS_NewArray(ctx);

  for(i = 0; i < ptr->n; i++)
    JS_SetPropertyUint32(ctx, array, i, js_atom_tovalue(ctx, ptr->atoms[i]));

  return array;
}

JSValue
pointer_toatoms(Pointer const* ptr, JSContext* ctx) {
  size_t i;
  JSValue array = JS_NewArray(ctx);

  for(i = 0; i < ptr->n; i++)
    JS_SetPropertyUint32(ctx, array, i, JS_NewUint32(ctx, ptr->atoms[i]));

  return array;
}

int
pointer_fromatoms(Pointer* ptr, JSContext* ctx, JSValueConst arr) {
  size_t i, len = js_array_length(ctx, arr);

  pointer_reset(ptr, ctx);

  if(len > 0) {
    ptr->atoms = js_realloc(ctx, ptr->atoms, sizeof(JSAtom) * len);
    ptr->n = len;

    for(i = 0; i < len; i++) {
      uint32_t atom;
      JS_ToUint32(ctx, &atom, JS_GetPropertyUint32(ctx, arr, i));
      ptr->atoms[i] = atom;
    }
  }

  return ptr->n;
}

void
pointer_pushatom(Pointer* ptr, JSContext* ctx, JSAtom atom) {
  if((ptr->atoms = js_realloc(ctx, ptr->atoms, (ptr->n + 1) * sizeof(JSAtom))))
    ptr->atoms[ptr->n++] = atom;
}

/**
 * @}
 */
