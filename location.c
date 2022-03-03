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
  if(loc->column != UINT32_MAX)
    dbuf_printf(dbuf, "%" PRId32 ":%" PRId32, loc->line + 1, loc->column + 1);
  else
    dbuf_printf(dbuf, "%" PRId32, loc->line + 1);
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

VISIBLE void
location_init(Location* loc) {
  loc->ref_count = 1;
  loc->file = -1;
  loc->str = 0;
  location_zero(loc);
}

void
location_zero(Location* loc) {
  loc->line = 0;
  loc->column = 0;
  loc->char_offset = 0;
  loc->byte_offset = 0;
}

void
location_add(Location* loc, const Location* other) {
  loc->line += other->line;
  loc->column += other->column;
  loc->char_offset += other->char_offset;
  loc->byte_offset += other->byte_offset;
}

void
location_sub(Location* loc, const Location* other) {
  loc->line -= other->line;
  loc->column -= other->column;
  loc->char_offset -= other->char_offset;
  loc->byte_offset -= other->byte_offset;
}

BOOL
location_release_rt(Location* loc, JSRuntime* rt) {
  if(--loc->ref_count == 0) {
    if(loc->file >= 0)
      JS_FreeAtomRT(rt, loc->file);

    if(loc->str)
      js_free_rt(rt, (char*)loc->str);
    memset(loc, 0, sizeof(Location));
    loc->file = -1;
    return TRUE;
  }
  // printf("location_release_rt %p ref_count=%d\n", loc, loc->ref_count);
  return FALSE;
}

Location*
location_free_rt(Location* loc, JSRuntime* rt) {
  location_release_rt(loc, rt);
  if(loc->ref_count == 0) {
    js_free_rt(rt, loc);
    loc = 0;
  }
  return loc;
}

size_t
location_count(Location* loc, const char* x, size_t n) {
  size_t start = loc->char_offset, i;

  for(i = 0; i < n;) {
    size_t bytes = utf8_charlen(&x[i], n - i);

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
  dst->str = src->str ? js_strdup(ctx, src->str) : 0;
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
    ret->str = loc->str ? js_strdup(ctx, loc->str) : 0;
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

/**
 * @}
 */
