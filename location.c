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

void
location_init(Location* loc) {
  loc->file = -1;
  loc->str = 0;
  location_zero(loc);
}

void
location_zero(Location* loc) {
  loc->line = 0;
  loc->column = 0;
  loc->pos = 0;
  loc->byte_offset = 0;
}

void
location_add(Location* loc, const Location* other) {
  loc->line += other->line;
  loc->column += other->column;
  loc->pos += other->pos;
  loc->byte_offset += other->byte_offset;
}

void
location_sub(Location* loc, const Location* other) {
  loc->line -= other->line;
  loc->column -= other->column;
  loc->pos -= other->pos;
  loc->byte_offset -= other->byte_offset;
}

void
location_free(Location* loc, JSContext* ctx) {
  if(loc->file >= 0)
    JS_FreeAtom(ctx, loc->file);
  if(loc->str)
    js_free(ctx, (char*)loc->str);
  memset(loc, 0, sizeof(Location));
  loc->file = -1;
}

void
location_free_rt(Location* loc, JSRuntime* rt) {
  if(loc->file >= 0)
    JS_FreeAtomRT(rt, loc->file);

  if(loc->str)
    js_free_rt(rt, (char*)loc->str);
  memset(loc, 0, sizeof(Location));
  loc->file = -1;
}

void
location_count(Location* loc, const char* x, size_t n) {
  size_t i;
  for(i = 0; i < n;) {
    size_t bytes = byte_charlen(&x[i], n - i);

    if(x[i] == '\n') {
      loc->line++;
      loc->column = 0;
    } else {
      loc->column++;
    }

    loc->pos++;
    i += bytes;
  }
}

Location
location_clone(const Location* loc, JSContext* ctx) {
  Location ret = {loc->file >= 0 ? JS_DupAtom(ctx, loc->file) : -1, loc->line, loc->column, loc->pos, loc->byte_offset, 0};
  return ret;
}
/**
 * @}
 */
