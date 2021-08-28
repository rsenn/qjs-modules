#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>
#include "location.h"
#include "buffer-utils.h"

void
location_print(const Location* loc, DynBuf* dbuf) {
  if(loc->file) {
    dbuf_putstr(dbuf, loc->file);
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

  location_print(loc, &dbuf);
  dbuf_0(&dbuf);

  return (char*)dbuf.buf;
}

JSValue
location_tovalue(const Location* loc, JSContext* ctx) {
  char* str = location_tostring(loc, ctx);
  JSValue ret = JS_NewString(ctx, str);
  js_free(ctx, str);
  return ret;
}

Location
location_clone(const Location* loc, JSContext* ctx) {
  Location ret = {0, 0, 0, 0, 0};
  if(loc->file)
    ret.file = js_strdup(ctx, loc->file);
  ret.line = loc->line;
  ret.column = loc->column;
  ret.pos = loc->pos;
  return ret;
}

void
location_init(Location* loc) {
  loc->file = 0;
  loc->str = 0;
  location_zero(loc);
}

void
location_zero(Location* loc) {
  loc->line = 0;
  loc->column = 0;
  loc->pos = 0;
}

void
location_add(Location* loc, const Location* other) {
  loc->line += other->line;
  loc->column += other->column;
  loc->pos += other->pos;
}
void
location_sub(Location* loc, const Location* other) {
  loc->line -= other->line;
  loc->column -= other->column;
  loc->pos -= other->pos;
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

void
location_free(Location* loc, JSContext* ctx) {
  if(loc->file)
    js_free(ctx, (char*)loc->file);
  if(loc->str)
    js_free(ctx, (char*)loc->str);
  memset(loc, 0, sizeof(Location));
}

void
location_free_rt(Location* loc, JSRuntime* rt) {
  if(loc->file)
    js_free_rt(rt, (char*)loc->file);
  if(loc->str)
    js_free_rt(rt, (char*)loc->str);
  memset(loc, 0, sizeof(Location));
}
