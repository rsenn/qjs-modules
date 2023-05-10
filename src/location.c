#include <stddef.h>
#include "location.h"
#include "buffer-utils.h"
#include "debug.h"

/**
 * \addtogroup location
 * @{
 */
void
location_print(const Location* loc, DynBuf* dbuf, JSContext* ctx) {
  if(ctx && loc->file > -1) {
    js_atom_dump(ctx, loc->file, dbuf, FALSE);
    dbuf_putc(dbuf, ':');
  }

  dbuf_printf(dbuf, loc->column != -1 ? "%" PRId32 ":%" PRId32 : "%" PRId32, loc->line + 1, loc->column + 1);
}

char*
location_tostring(const Location* loc, JSContext* ctx) {
  DynBuf dbuf;
  js_dbuf_init(ctx, &dbuf);

  location_print(loc, &dbuf, ctx);
  dbuf_0(&dbuf);

  return (char*)dbuf.buf;
}

char*
location_file(const Location* loc, JSContext* ctx) {
  const char* file;
  char* ret = 0;

  if((file = JS_AtomToCString(ctx, loc->file))) {
    ret = js_strdup(ctx, file);
    JS_FreeCString(ctx, file);
  }
  return ret;
}

JSValue
location_tovalue(const Location* loc, JSContext* ctx) {
  char* str = location_tostring(loc, ctx);
  JSValue ret = JS_NewString(ctx, str);
  js_free(ctx, str);
  return ret;
}

/*VISIBLE*/ void
location_init(Location* loc) {
  loc->ref_count = 1;
  loc->file = -1;
  loc->str = 0;
  location_zero(loc);
}

void
location_zero(Location* loc) {
  loc->file = -1;
  loc->line = -1;
  loc->column = -1;
  loc->char_offset = -1;
  loc->byte_offset = -1;
}

void
location_release(Location* loc, JSRuntime* rt) {
  if(loc->file >= 0)
    JS_FreeAtomRT(rt, loc->file);
  loc->file = -1;

  if(loc->str)
    js_free_rt(rt, (char*)loc->str);

  location_zero(loc);
}

void
location_free(Location* loc, JSRuntime* rt) {
  if(--loc->ref_count == 0) {
    location_release(loc, rt);
    js_free_rt(rt, loc);
  }
}

size_t
location_count(Location* loc, const uint8_t* x, size_t n) {
  size_t start = loc->char_offset, i;

  for(i = 0; i < n;) {
    size_t bytes = utf8_charlen((const void*)&x[i], n - i);

    if(x[i] == '\n') {
      loc->line++;
      loc->column = 0;
    } else {
      loc->column++;
    }

    loc->char_offset++;
    loc->byte_offset += bytes;
    i += bytes;
  }
  return loc->char_offset - start;
}

Location*
location_copy(Location* dst, const Location* src, JSContext* ctx) {
  dst->file = src->file >= 0 ? JS_DupAtom(ctx, src->file) : src->file;
  dst->line = src->line;
  dst->column = src->column;
  dst->char_offset = src->char_offset;
  dst->byte_offset = src->byte_offset;
  dst->str = src->str && src->str[0] ? js_strdup(ctx, src->str) : 0;
  return dst;
}

Location*
location_clone(const Location* loc, JSContext* ctx) {
  Location* ret;

  if((ret = location_new(ctx))) {
    ret->file = loc->file >= 0 ? JS_DupAtom(ctx, loc->file) : -1;
    ret->line = loc->line;
    ret->column = loc->column;
    ret->char_offset = loc->char_offset;
    ret->byte_offset = loc->byte_offset;
    ret->str = loc->str && loc->str[0] ? js_strdup(ctx, loc->str) : 0;
  }

  return ret;
}

Location*
location_new(JSContext* ctx) {
  Location* loc;
  if((loc = js_malloc(ctx, sizeof(Location))))
    location_init(loc);
  return loc;
}

Location*
location_dup(Location* loc) {
  ++loc->ref_count;
  return loc;
}

/**
 * @}
 */
