#include "defines.h"
#include "quickjs-pointer.h"
#include "pointer.h"
#include "utils.h"
#include "buffer-utils.h"
#include "debug.h"

static JSAtom deref_key(JSContext* ctx, JSValueConst obj, JSAtom atom);
static JSValue deref_value(JSContext* ctx, JSValueConst obj, JSAtom atom);

/**
 * \addtogroup pointer
 * @{
 */

void
pointer_clear(Pointer* ptr, JSRuntime* rt) {
  if(ptr->atoms && ptr->n) {
    for(size_t i = 0; i < ptr->n; i++) {
      JS_FreeAtomRT(rt, ptr->atoms[i]);
      ptr->atoms[i] = 0;
    }
  }

  ptr->n = 0;
}

void
pointer_reset(Pointer* ptr, JSRuntime* rt) {
  pointer_clear(ptr, rt);

  if(ptr->atoms) {
    js_free_rt(rt, ptr->atoms);
    ptr->atoms = 0;
  }

  ptr->a = 0;
}

BOOL
pointer_copy(Pointer* dst, Pointer const* src, JSContext* ctx) {
  pointer_clear(dst, JS_GetRuntime(ctx));

  return pointer_fromatoms(dst, src->atoms, src->n, ctx);
}

/** Returns <= -1 if equal, and >= 0 for mismatch */
int
pointer_compare(Pointer const* a, Pointer const* b, int32_t aoffs, int32_t boffs, uint32_t len) {
  uint32_t alen = a->n, blen = b->n;

  aoffs = CLAMP_NUM(WRAP_NUM(aoffs, (int32_t)alen), 0, (int32_t)alen);
  boffs = CLAMP_NUM(WRAP_NUM(boffs, (int32_t)blen), 0, (int32_t)blen);

  alen -= aoffs;
  blen -= boffs;

  len = MIN_NUM(len, alen);
  len = MIN_NUM(len, blen);

  for(uint32_t i = 0; i < len; i++)
    if(a->atoms[i + aoffs] != b->atoms[i + boffs])
      return i;

  return -1 - len;
}

BOOL
pointer_equal(Pointer const* a, Pointer const* b) {
  if(a->n != b->n)
    return FALSE;

  for(uint32_t i = 0; i < a->n; i++)
    if(a->atoms[i] != b->atoms[i])
      return FALSE;

  return TRUE;
}

BOOL
pointer_allocate(Pointer* ptr, size_t size, JSContext* ctx) {
  size_t i;

  if(size == 0) {
    pointer_clear(ptr, JS_GetRuntime(ctx));
    return TRUE;
  }

  if(size < ptr->n)
    pointer_truncate(ptr, size, ctx);
  if(size > ptr->a)
    if(!pointer_reserve(ptr, size, ctx))
      return FALSE;

  if(size > ptr->n)
    for(i = ptr->n; i < size; i++)
      ptr->atoms[i] = JS_DupAtom(ctx, JS_ATOM_NULL);

  ptr->n = size;
  return TRUE;
}

BOOL
pointer_reserve(Pointer* ptr, size_t alloc, JSContext* ctx) {
  if(alloc > ptr->a) {
    size_t reserve = ptr->a * 3 / 2;

    if(alloc < reserve)
      alloc = reserve;

    JSAtom* tab_atom;

    if(!(tab_atom = js_realloc(ctx, ptr->atoms, sizeof(JSAtom) * alloc)))
      return FALSE;

    if(ptr->a < alloc)
      memset(&tab_atom[ptr->a], 0, sizeof(JSAtom) * (alloc - ptr->a));

    ptr->atoms = tab_atom;
    ptr->a = alloc;
  }

  return TRUE;
}

BOOL
pointer_truncate(Pointer* ptr, size_t new_size, JSContext* ctx) {
  if(new_size >= ptr->n)
    return FALSE;

  for(size_t i = new_size; i < ptr->n; i++) {
    JS_FreeAtom(ctx, ptr->atoms[i]);
    ptr->atoms[i] = 0;
  }

  ptr->n = new_size;
  return TRUE;
}

#define pointer_color(s) ((i) >= (size_t)(index) ? COLOR_RED : (is_int ? COLOR_LIGHTGRAY : COLOR_YELLOW))

void
pointer_dump(Pointer const* ptr, Writer* wr, BOOL color, ssize_t index, JSContext* ctx) {
  size_t i, j;

  for(i = 0; i < ptr->n; i++) {
    const char* s = JS_AtomToCString(ctx, ptr->atoms[i]);

    BOOL is_int = is_integer(s);

    writer_puts(wr, color ? (is_int ? COLOR_CYAN "[" : COLOR_CYAN ".") : (is_int ? "[" : "."));
    writer_puts(wr, color ? pointer_color(s) : "");

    for(j = 0; s[j]; j++) {
      if(s[j] == '.' || s[j] == '[' || s[j] == ']')
        writer_putc(wr, '\\');

      writer_putc(wr, s[j]);
    }

    if(is_int)
      writer_puts(wr, color ? COLOR_CYAN "]" : "]");

    JS_FreeCString(ctx, s);
  }

  writer_puts(wr, color ? COLOR_NONE : "");
}

char*
pointer_tostring(Pointer const* ptr, BOOL color, ssize_t index, JSContext* ctx) {
  DynBuf db;
  js_dbuf_init(ctx, &db);
  Writer wr = writer_from_dynbuf(&db);
  pointer_dump(ptr, &wr, color, index, ctx);
  dbuf_0(&db);

  return (char*)db.buf;
}

void
pointer_serialize(Pointer const* ptr, Writer* wr, JSContext* ctx) {
  size_t i, j;

  for(i = 0; i < ptr->n; i++) {
    const char* str;
    int64_t idx;

    if(js_atom_is_index(ctx, &idx, ptr->atoms[i])) {
      char buf[FMT_ULONG];

      writer_putc(wr, '[');
      writer_write(wr, (const uint8_t*)buf, fmt_ulong(buf, idx));
      writer_putc(wr, ']');
      continue;
    }

    if(i > 0)
      writer_putc(wr, '.');

    str = JS_AtomToCString(ctx, ptr->atoms[i]);

    for(j = 0; str[j]; j++) {
      if(str[j] == '.')
        writer_putc(wr, '\\');

      writer_putc(wr, str[j]);
    }

    JS_FreeCString(ctx, str);
  }
}

static int
pointer_parse_unescape(const char* x, size_t* nptr) {
  *nptr = 2;
  return *x;
}

ssize_t
pointer_parse(Pointer* ptr, const char* str, size_t len, JSContext* ctx) {
  DynBuf dbuf = DBUF_INIT_CTX(ctx);

  if(len > 0 && str[0] == '.') {
    ++str;
    --len;
  }

  while(len) {
    int32_t val;
    const char c = *str;
    size_t start, delim, n, m = 0;
    BOOL unescape = FALSE;
    JSAtom atom;

    start = c == '[' ? 1 : 0;
    delim = start;

    for(;;) {
      delim += byte_chrs(&str[delim], len - delim, c == '[' ? "]" : ".[", c == '[' ? 1 : 2);

      if(delim < len && delim > 0 && str[delim - 1] == '\\') {
        ++delim;
        unescape = TRUE;
        continue;
      }

      break;
    }

    n = delim - start;
    val = 0;

    if(unescape) {
      dbuf.size = 0;
      dbuf_put_unescaped_pred(&dbuf, &str[start], n, pointer_parse_unescape);
      atom = JS_NewAtomLen(ctx, (const char*)dbuf.buf, dbuf.size);
    } else {
      if(start)
        m = scan_int(&str[start], &val);

      atom = m > 0 && m == n ? js_atom_from_integer(ctx, val) : JS_NewAtomLen(ctx, &str[start], n);
    }

    pointer_pushatom(ptr, atom, ctx);

    if(str[start + n] != ']' && str[start + n] != '.')
      n--;

    str += start + n;
    len -= start + n;

    if(len > 0) {
      ++str;
      --len;

      if(start && *str == '.') {
        ++str;
        --len;
      }
    }
  }

  dbuf_free(&dbuf);

  return ptr->n;
}

Pointer*
pointer_slice(Pointer* ptr, int64_t start, int64_t end, JSContext* ctx) {
  Pointer* ret;

  if((ret = pointer_new(ctx))) {
    start = WRAP_NUM(start, (int64_t)ptr->n);
    end = end == INT64_MAX ? (int64_t)ptr->n : WRAP_NUM(end, (int64_t)ptr->n);

    if(start > end) {
      int64_t tmp = start;
      start = end;
      end = tmp;
    }

    if(!pointer_allocate(ret, end - start, ctx)) {
      js_free(ctx, ret);
      return NULL;
    }

    for(int64_t i = start; i < end; i++)
      ret->atoms[i - start] = JS_DupAtom(ctx, ptr->atoms[i]);
  }

  return ret;
}

Pointer*
pointer_splice(Pointer* ptr, int64_t start, int64_t end, JSAtom* atoms, size_t ins, JSContext* ctx) {
  Pointer* ret = 0;
  size_t i, del, len, newlen;

  start = int64_mod(start, ptr->n + 1);
  end = end == INT64_MAX ? (int64_t)ptr->n : int64_mod(end, ptr->n + 1);

  if(start > end) {
    int64_t tmp = start;
    start = end;
    end = tmp;
  }

  del = end - start;
  newlen = (len = ptr->n) - del + ins;

#ifdef DEBUG_OUTPUT
  printf("%s() start: %li end: %li del: %li ins: %lu len: %lu newlen: %lu remain: %li\n",
         __func__,
         (long)start,
         (long)end,
         (long)del,
         (unsigned long)ins,
         (unsigned long)len,
         (unsigned long)newlen,
         (long)(len - end));
#endif

  if(ins > del) {
    if(!pointer_allocate(ptr, (ptr->n = newlen), ctx)) {
      js_free(ctx, ret);
      return NULL;
    }
  }

  if(del) {
    if(!(ret = pointer_new(ctx)) || !pointer_allocate(ret, del, ctx)) {
      js_free(ctx, ret);
      return NULL;
    }
    memcpy(ret->atoms, &ptr->atoms[start], del * sizeof(JSAtom));
  }

  if(end < (int64_t)len && del != ins)
    memmove(&ptr->atoms[end - del + ins], &ptr->atoms[end], (len - end) * sizeof(JSAtom));

  if(ins)
    memcpy(&ptr->atoms[start], atoms, ins * sizeof(JSAtom));

  /* shrink */
  if(del > ins) {
    if(!pointer_allocate(ptr, (ptr->n = newlen), ctx)) {
      js_free(ctx, ret);
      return NULL;
    }
  }

  return ret;
}

BOOL
pointer_fromatoms(Pointer* ptr, JSAtom* vec, size_t len, JSContext* ctx) {
  if(!pointer_reserve(ptr, ptr->n + len, ctx))
    return FALSE;

  for(size_t i = 0; i < len; i++)
    ptr->atoms[ptr->n + i] = JS_DupAtom(ctx, vec[i]);

  ptr->n += len;
  return TRUE;
}

JSValue
pointer_shift(Pointer* ptr, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  if(ptr->n) {
    size_t i;
    JSAtom atom = ptr->atoms[0];

    for(i = 1; i < ptr->n; i++)
      ptr->atoms[i - 1] = ptr->atoms[i];

    --ptr->n;
    ret = JS_AtomToValue(ctx, atom);
    JS_FreeAtom(ctx, atom);
  }

  return ret;
}

JSValue
pointer_pop(Pointer* ptr, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  if(ptr->n) {
    JSAtom atom = ptr->atoms[--ptr->n];
    ret = JS_AtomToValue(ctx, atom);
    JS_FreeAtom(ctx, atom);
  }

  return ret;
}

BOOL
pointer_unshift(Pointer* ptr, JSValueConst value, JSContext* ctx) {
  size_t copy_len = ptr->n;

  if(pointer_reserve(ptr, ptr->n + 1, ctx)) {
    if(copy_len)
      memmove(&ptr->atoms[1], ptr->atoms, sizeof(JSAtom) * copy_len);

    ptr->atoms[0] = JS_ValueToAtom(ctx, value);
    ptr->n++;

    return TRUE;
  }

  return FALSE;
}

BOOL
pointer_push(Pointer* ptr, JSValueConst item, JSContext* ctx) {
  return pointer_pushatom(ptr, JS_ValueToAtom(ctx, item), ctx);
}

BOOL
pointer_pushfree(Pointer* ptr, JSValue item, JSContext* ctx) {
  BOOL ret = pointer_push(ptr, item, ctx);
  JS_FreeValue(ctx, item);
  return ret;
}

JSValue
pointer_deref(Pointer const* ptr, JSValueConst arg, JSContext* ctx) {
  JSValue obj = JS_DupValue(ctx, arg);

  for(size_t i = 0; i < ptr->n; i++) {
    JSValue child = deref_value(ctx, obj, ptr->atoms[i]);

    JS_FreeValue(ctx, obj);

    if(JS_IsException(child)) {
      char* str = pointer_tostring(ptr, TRUE, i, ctx);

      obj = JS_ThrowReferenceError(ctx, "Pointer dereferencing at: %s", str ? str : "(null)");

      if(str)
        js_free(ctx, str);

      break;
    }

    obj = child;
  }

  return obj;
}

JSValue
pointer_acquire(Pointer const* ptr, JSValueConst arg, JSContext* ctx) {
  JSValue obj = JS_DupValue(ctx, arg);

  for(size_t i = 0; i < ptr->n; i++) {
    JSValue child;

    if(!JS_HasProperty(ctx, obj, ptr->atoms[i])) {
      BOOL is_array = i + 1 < ptr->n && JS_ATOM_ISINT(ptr->atoms[i + 1]);
      child = is_array ? JS_NewArray(ctx) : JS_NewObject(ctx);
      JS_SetProperty(ctx, obj, ptr->atoms[i], JS_DupValue(ctx, child));
    } else {
      child = JS_GetProperty(ctx, obj, ptr->atoms[i]);
    }

    JS_FreeValue(ctx, obj);
    obj = child;
  }

  return obj;
}

BOOL
pointer_fromstring(Pointer* ptr, JSValueConst value, JSContext* ctx) {
  size_t len;
  const char* str;
  BOOL ret = FALSE;

  if((str = JS_ToCStringLen(ctx, &len, value))) {
    if(pointer_parse(ptr, str, len, ctx) >= 0)
      ret = TRUE;

    JS_FreeCString(ctx, str);
  }

  return ret;
}

BOOL
pointer_fromarray(Pointer* ptr, JSValueConst array, JSContext* ctx) {
  int64_t i, len;

  if((len = js_array_length(ctx, array)) > 0)
    for(i = 0; i < len; i++)
      if(!pointer_pushfree(ptr, JS_GetPropertyUint32(ctx, array, i), ctx))
        return FALSE;

  return TRUE;
}

BOOL
pointer_fromiterable(Pointer* ptr, JSValueConst arg, JSContext* ctx) {
  JSValue iter = js_iterator_new(ctx, arg);
  BOOL ret = FALSE;

  for(;;) {
    BOOL done = FALSE;
    JSValue item = js_iterator_next(ctx, iter, &done);

    if(done) {
      JS_FreeValue(ctx, item);
      ret = TRUE;
      break;
    }

    if(!pointer_pushfree(ptr, item, ctx))
      return FALSE;
  }

  JS_FreeValue(ctx, iter);
  return ret;
}

BOOL
pointer_from(Pointer* ptr, JSValueConst value, JSContext* ctx) {
  Pointer* ptr2;

  if((ptr2 = js_pointer_data(value)))
    if(pointer_copy(ptr, ptr2, ctx))
      return TRUE;

  if(JS_IsString(value))
    if(pointer_fromstring(ptr, value, ctx))
      return TRUE;

  if(JS_IsArray(ctx, value))
    if(pointer_fromarray(ptr, value, ctx))
      return TRUE;

  if(pointer_fromiterable(ptr, value, ctx))
    return TRUE;

  return FALSE;
}

BOOL
pointer_append(Pointer* ptr, int argc, JSValueConst argv[], JSContext* ctx) {

  for(int i = 0; i < argc; i++) {
    if(js_is_primitive(argv[i]) && !JS_IsString(argv[i]) && pointer_push(ptr, argv[i], ctx)) {
    } else if(!pointer_from(ptr, argv[i], ctx)) {
      JS_ThrowInternalError(ctx, "pointer_append() argument %d failed", i);
      return FALSE;
    }
  }

  return TRUE;
}

Pointer*
pointer_concat(Pointer const* ptr, JSValueConst iterable, JSContext* ctx) {
  Pointer* ret;

  if((ret = pointer_clone(ptr, ctx))) {
    if(!pointer_fromiterable(ret, iterable, ctx)) {
      pointer_free(ret, JS_GetRuntime(ctx));
      ret = 0;
    }
  }

  return ret;
}

JSValue
pointer_toarray(Pointer const* ptr, JSContext* ctx) {
  JSValue array = JS_NewArray(ctx);

  for(size_t i = 0; i < ptr->n; i++)
    JS_SetPropertyUint32(ctx, array, i, js_atom_tovalue(ctx, ptr->atoms[i]));

  return array;
}

JSValue
pointer_uint32array(Pointer const* ptr, JSContext* ctx) {
  JSValue ret, buf = JS_NewArrayBufferCopy(ctx, (uint8_t*)ptr->atoms, sizeof(JSAtom) * ptr->n);
  ret = js_typedarray_new(ctx, 32, FALSE, FALSE, buf);
  JS_FreeValue(ctx, buf);
  return ret;
}

BOOL
pointer_startswith(Pointer const* ptr, Pointer const* other) {
  if(ptr->n < other->n)
    return FALSE;

  uint32_t len = other->n;

  for(uint32_t i = 0; i < len; i++)
    if(ptr->atoms[i] != other->atoms[i])
      return FALSE;

  return TRUE;
}

BOOL
pointer_endswith(Pointer const* ptr, Pointer const* other) {
  if(ptr->n < other->n)
    return FALSE;

  uint32_t len = other->n;
  uint32_t start = ptr->n - len;

  for(uint32_t i = 0; i < len; i++)
    if(ptr->atoms[start + i] != other->atoms[i])
      return FALSE;

  return TRUE;
}

/**
 * @}
 */

static JSAtom
deref_key(JSContext* ctx, JSValueConst obj, JSAtom atom) {
  JSValue value, ret;
  const int flags = JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_SET_ENUM;

  if(JS_HasProperty(ctx, obj, atom))
    return atom;

  value = JS_AtomToValue(ctx, atom);

  if(JS_IsFunction(ctx, value)) {
    JSPropertyEnum* tmp_tab;
    uint32_t i, tmp_len;

    if(!JS_GetOwnPropertyNames(ctx, &tmp_tab, &tmp_len, obj, flags)) {
      for(i = 0; i < tmp_len; i++) {
        JSValue args[3] = {
            JS_GetProperty(ctx, obj, tmp_tab[i].atom),
            JS_AtomToValue(ctx, tmp_tab[i].atom),
            obj,
        };
        ret = JS_Call(ctx, value, JS_NULL, countof(args), args);
        BOOL match = JS_ToBool(ctx, ret);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);

        if(match) {
          JSAtom ret = JS_DupAtom(ctx, tmp_tab[i].atom);
          JS_FreeValue(ctx, value);
          js_propertyenums_free(ctx, tmp_tab, tmp_len);
          orig_js_free(ctx, tmp_tab);
          return ret;
        }
      }

      js_propertyenums_free(ctx, tmp_tab, tmp_len);
      orig_js_free(ctx, tmp_tab);
    }
  }

  JS_FreeValue(ctx, value);
  return 0;
}

static JSValue
deref_value(JSContext* ctx, JSValueConst obj, JSAtom atom) {
  JSAtom key;

  if((key = deref_key(ctx, obj, atom)))
    return JS_GetProperty(ctx, obj, key);

  return JS_EXCEPTION;
}
