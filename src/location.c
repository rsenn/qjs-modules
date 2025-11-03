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
    const char* str;

    if((str = JS_AtomToCString(ctx, loc->file))) {
      dbuf_putstr(dbuf, str);
      dbuf_putc(dbuf, ':');

      JS_FreeCString(ctx, str);
    }
  }

  dbuf_printf(dbuf, loc->column >= 0 ? "%" PRId32 ":%" PRId32 : "%" PRId32, loc->line + 1, loc->column + 1);
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

void
location_init(Location* loc) {
  loc->ref_count = 1;
  loc->file = -1;
  loc->line = -1;
  loc->column = -1;
  loc->char_offset = -1;
  loc->byte_offset = -1;
  loc->str = 0;
  loc->allocated = FALSE;
}

void
location_zero(Location* loc) {
  loc->line = 0;
  loc->column = 0;
  loc->char_offset = 0;
  loc->byte_offset = 0;
}

void
location_release(Location* loc, JSRuntime* rt) {
  if(loc->file >= 0)
    JS_FreeAtomRT(rt, loc->file);
  loc->file = -1;

  if(loc->str) {
    if(loc->allocated)
      js_free_rt(rt, (char*)loc->str);
    loc->str = 0;
    loc->allocated = FALSE;
  }

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
  size_t start, i;

  if(loc->byte_offset == -1)
    loc->byte_offset = 0;

  if(loc->char_offset == -1)
    loc->char_offset = 0;

  if(loc->line == -1)
    loc->line = 0;

  if(loc->column == -1)
    loc->column = 0;

  start = loc->char_offset;

  for(i = 0; i < n;) {
    size_t bytes = utf8_charlen((const void*)&x[i], n - i);

    if(bytes == 1 && x[i] == '\n') {
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

BOOL
location_equal(const Location* loc, const Location* other) {
  if(loc->file != -1 && other->file != -1)
    if(loc->file != other->file)
      return FALSE;

  if(loc->line != -1 && other->line != -1)
    if(loc->line != other->line)
      return FALSE;

  if(loc->column != -1 && other->column != -1)
    if(loc->column != other->column)
      return FALSE;

  if(loc->char_offset != -1 && other->char_offset != -1)
    if(loc->char_offset != other->char_offset)
      return FALSE;

  if(loc->byte_offset != -1 && other->byte_offset != -1)
    if(loc->byte_offset != other->byte_offset)
      return FALSE;

  return TRUE;
}

Location*
location_copy(Location* dst, const Location* src, JSContext* ctx) {
  location_release(dst, JS_GetRuntime(ctx));

  dst->file = src->file >= 0 ? (int32_t)JS_DupAtom(ctx, src->file) : src->file;
  dst->line = src->line;
  dst->column = src->column;
  dst->char_offset = src->char_offset;
  dst->byte_offset = src->byte_offset;

  dst->str = src->str ? js_strdup(ctx, src->str) : 0;
  dst->allocated = !!src->str;

  return dst;
}

Location*
location_clone(const Location* loc, JSContext* ctx) {
  Location* ret;

  if((ret = location_new(ctx)))
    location_copy(ret, loc, ctx);

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

void
location_set_file(Location* loc, int32_t file, JSContext* ctx) {
  if(loc->file != -1)
    JS_FreeAtom(ctx, loc->file);

  loc->file = file != -1 ? JS_DupAtom(ctx, file) : file;
}

void
location_set_buffer(Location* loc, void* str, size_t ofs, JSContext* ctx) {
  if(loc->str && loc->allocated)
    js_free(ctx, loc->str);

  loc->str = str;
  loc->byte_offset = ofs;
  loc->char_offset = utf8_strlen(loc->str - loc->byte_offset, ofs);
}

/**
 * @}
 */
