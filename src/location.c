#include <stddef.h>
#include "location.h"
#include "buffer-utils.h"
#include "debug.h"

/**
 * \addtogroup location
 * @{
 */
void
location_dump(const Location* loc, FILE* out) {
  char* s;

  if((s = location_tostring(loc, NULL))) {
    fputs(s, out);
    free(s);
  }

  fflush(out);
}

void
location_print(const Location* loc, DynBuf* dbuf, JSContext* ctx) {
  uint8_t buf[FMT_LONG];
  char* file;

  if((file = location_file(loc, ctx))) {
    dbuf_putstr(dbuf, file);
    dbuf_putc(dbuf, ':');
  }

  if(loc->line != -1) {
    dbuf_put(dbuf, buf, fmt_long(buf, loc->line + 1));

    if(loc->column >= 0) {
      dbuf_putc(dbuf, ':');
      dbuf_put(dbuf, buf, fmt_long(buf, loc->column + 1));
    }
  } else if(loc->char_offset != -1) {
    dbuf_putc(dbuf, '@');
    dbuf_put(dbuf, buf, fmt_longlong(buf, loc->char_offset));
  }
}

char*
location_tostring(const Location* loc, JSContext* ctx) {
  DynBuf dbuf;
  dbuf_init_ctx(ctx, &dbuf);

  location_print(loc, &dbuf, ctx);
  dbuf_0(&dbuf);

  return (char*)dbuf.buf;
}

char*
location_file(const Location* loc, JSContext* ctx) {
  const char* file;
  char* s = 0;

  if(ctx && loc->file > -1 && (file = JS_AtomToCString(ctx, loc->file))) {
    s = ctx ? js_strdup(ctx, file) : strdup(file);
    JS_FreeCString(ctx, file);
  } else if(loc->has_filename && (loc->filename && loc->filename[0])) {
    s = ctx ? js_strdup(ctx, loc->filename) : strdup(loc->filename);
  }

  return s;
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
  loc->read_only = FALSE;
  loc->has_filename = FALSE;
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

  if(loc->has_filename) {
    if(loc->filename) {
      js_free_rt(rt, loc->filename);
      loc->filename = NULL;
    }
    loc->has_filename = FALSE;
  } else if(loc->file > -1) {
    JS_FreeAtomRT(rt, loc->file);
    loc->file = -1;
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

static inline void
location_initvalues(Location* loc) {
  if(loc->byte_offset == -1)
    loc->byte_offset = 0;

  if(loc->char_offset == -1)
    loc->char_offset = 0;

  if(loc->line == -1)
    loc->line = 0;

  if(loc->column == -1)
    loc->column = 0;
}

size_t
location_count(Location* loc, const uint8_t* x, size_t n) {
  location_initvalues(loc);

  size_t start = loc->char_offset;

  for(size_t i = 0; i < n;) {
    size_t bytes = utf8_char((const void*)x + i, n - i).len;

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

size_t
location_nextchar(Location* loc, int c) {
  location_initvalues(loc);

  size_t bytes = unicode_len_utf8(c);

  if(c == '\n') {
    loc->line++;
    loc->column = 0;
  } else {
    loc->column++;
  }

  loc->char_offset++;
  loc->byte_offset += bytes;

  return bytes;
}

BOOL
location_equal(const Location* loc, const Location* other) {
  char *a = 0, *b = 0;

  if(loc->file != -1 && other->file != -1) {
    if(loc->file != other->file)
      return FALSE;

  } else if((a = location_file(loc, 0)) && (b = location_file(other, 0))) {
    if(strcmp(a, b))
      return FALSE;

    if(a)
      free(a);
    if(b)
      free(b);
  }

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

  dst->read_only = src->read_only;
  dst->has_filename = src->has_filename;

  if(src->has_filename) {
    dst->filename = src->filename ? (ctx ? js_strdup(ctx, src->filename) : strdup(src->filename)) : 0;
  } else if(src->file != -1) {
    dst->file = (int32_t)JS_DupAtom(ctx, src->file);
  }

  dst->line = src->line;
  dst->column = src->column;
  dst->char_offset = src->char_offset;
  dst->byte_offset = src->byte_offset;

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
  if(loc->has_filename) {
    if(loc->filename) {
      if(ctx)
        js_free(ctx, loc->filename);
      else
        free(loc->filename);

      loc->filename = NULL;
    }
  } else if(loc->file != -1) {
    assert(ctx);
    JS_FreeAtom(ctx, loc->file);
  }

  loc->has_filename = FALSE;

  if((loc->file = file) != -1) {
    assert(ctx);
    JS_DupAtom(ctx, file);
  }
}

void
location_set_filename(Location* loc, const char* filename, JSContext* ctx) {
  location_set_file(loc, -1, ctx);

  if(loc->filename) {
    loc->has_filename = TRUE;
    loc->filename = ctx ? js_strdup(ctx, filename) : strdup(filename);
  }
}

void
location_set_byteoffset(Location* loc, const void* str, size_t ofs) {
  loc->byte_offset = ofs;
  loc->char_offset = utf8_strlen(str, ofs);
}

void
location_set_charoffset(Location* loc, const void* buf, size_t len, size_t ofs) {
  loc->char_offset = ofs;
  loc->byte_offset = utf8_byteoffset(buf, len, ofs);
}

void*
location_pointer(const Location* loc, const void* buf) {
  return (uint8_t*)buf + loc->byte_offset;
}

/**
 * @}
 */
