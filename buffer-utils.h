#ifndef BUFFER_UTILS_H
#define BUFFER_UTILS_H

#include <quickjs.h>
#include <cutils.h>
//#include "quickjs-internal.h"
#include "char-utils.h"

#ifndef MAX_NUM
#define MAX_NUM(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN_NUM
#define MIN_NUM(a, b) ((a) < (b) ? (a) : (b))
#endif

size_t ansi_length(const char*, size_t);
size_t ansi_skip(const char*, size_t);
size_t ansi_truncate(const char*, size_t, size_t limit);
int64_t array_search(void*, size_t, size_t elsz, void* needle);
char* str_escape(const char*);
char* byte_escape(const char*, size_t);
char* dbuf_at_n(const DynBuf*, size_t, size_t* n, char sep);
const char* dbuf_last_line(DynBuf*, size_t*);
int dbuf_prepend(DynBuf*, const uint8_t*, size_t len);
void dbuf_put_colorstr(DynBuf*, const char*, const char* color, int with_color);
void dbuf_put_escaped_pred(DynBuf*, const char*, size_t len, int (*pred)(int));
void dbuf_put_escaped_table(DynBuf*, const char*, size_t len, const char table[256]);
void dbuf_put_unescaped_pred(DynBuf*, const char*, size_t len, int (*pred)(int));
void dbuf_put_escaped(DynBuf*, const char*, size_t len);
void dbuf_put_value(DynBuf*, JSContext*, JSValue value);
int dbuf_reserve_start(DynBuf*, size_t);
size_t dbuf_token_pop(DynBuf*, char);
size_t dbuf_token_push(DynBuf*, const char*, size_t len, char delim);
JSValue dbuf_tostring_free(DynBuf*, JSContext*);
ssize_t dbuf_load(DynBuf*, const char*);

#define dbuf_append(d, x, n) dbuf_put((d), (const uint8_t*)(x), (n))

static inline size_t
dbuf_count(DynBuf* db, int ch) {
  return byte_count(db->buf, db->size, ch);
}

static inline void
dbuf_0(DynBuf* db) {
  dbuf_putc(db, '\0');
  db->size--;
}

static inline void
dbuf_zero(DynBuf* db) {
  dbuf_realloc(db, 0);
}
static inline int32_t
dbuf_get_column(DynBuf* db) {
  size_t len;
  const char* str;
  if(db->size) {
    str = dbuf_last_line(db, &len);
    return ansi_length(str, len);
  }
  return 0;
}

static inline size_t
dbuf_bitflags(DynBuf* db, uint32_t bits, const char* const names[]) {
  size_t i, n = 0;
  for(i = 0; i < sizeof(bits) * 8; i++) {
    if(bits & (1 << i)) {
      size_t len = strlen(names[i]);
      if(n) {
        n++;
        dbuf_putstr(db, "|");
      }
      dbuf_append(db, names[i], len);
      n += len;
    }
  }
  return n;
}

struct memory_block;
struct pointer_range;
struct offset_length;

typedef struct memory_block {
  uint8_t* base;
  size_t size;
} MemoryBlock;

static inline void
block_init(MemoryBlock* mb) {
  mb->base = 0;
  mb->size = 0;
}

static inline void*
block_data(MemoryBlock* mb) {
  return mb->base;
}

static inline size_t
block_length(MemoryBlock* mb) {
  return mb->size;
}

typedef struct pointer_range {
  uint8_t *start, *end;
} PointerRange;

static inline void
range_init(PointerRange* pr) {
  pr->end = pr->start = 0;
}

static inline PointerRange
range_from(const MemoryBlock* mb) {
  return (PointerRange){mb->base, mb->base + mb->size};
}

typedef struct offset_length {
  int64_t offset, length;
} OffsetLength;

static inline void
offset_init(OffsetLength* ol) {
  ol->offset = 0;
  ol->length = INT64_MAX;
}

static inline BOOL
offset_is_default(const OffsetLength* ol) {
  return ol->offset == 0 && ol->length == INT64_MAX;
}

static inline uint8_t*
offset_data(const OffsetLength* ol, const void* x) {
  return (uint8_t)x + ol->offset;
}

static inline size_t
offset_size(const OffsetLength* ol, size_t n) {
  return MIN_NUM(ol->length, n - ol->offset);
}

static inline MemoryBlock
offset_block(const OffsetLength* ol, const void* x, size_t n) {
  return (MemoryBlock){offset_data(ol, x), offset_size(ol, n)};
}

static inline PointerRange
offset_range(const OffsetLength* ol, const void* x, size_t n) {
  MemoryBlock mb = offset_block(ol, x, n);
  return range_from(&mb);
}

static inline OffsetLength
offset_slice(const OffsetLength ol, int64_t start, int64_t end) {
  if(start < 0)
    start = ol.length + (start % ol.length);
  else if(start > ol.length)
    start = ol.length;
  if(end < 0)
    end = ol.length + (end % ol.length);
  else if(end > ol.length)
    end = ol.length;

  return (OffsetLength){start, end - start};
}

static inline OffsetLength
offset_offset(const OffsetLength* ol, const OffsetLength* by) {
  OffsetLength ret;
  ret.offset = ol->offset + by->offset;
  ret.length = MIN_NUM(by->length, ol->length - by->offset);
  return ret;
}

static inline MemoryBlock
block_range(const MemoryBlock* mb, struct offset_length* range) {
  MemoryBlock ret;
  ret.base = mb->base + range->offset;
  ret.size = MIN_NUM(range->length, mb->size - range->offset);
  return ret;
}

typedef struct InputBuffer {
  uint8_t* data;
  size_t pos, size;
  void (*free)(JSContext*, const char*, JSValue);
  JSValue value;
  OffsetLength range;
} InputBuffer;

static inline void
input_buffer_free_default(JSContext* ctx, const char* str, JSValue val) {
  if(JS_IsString(val))
    JS_FreeCString(ctx, str);

  if(!JS_IsUndefined(val))
    JS_FreeValue(ctx, val);
}

InputBuffer js_input_buffer(JSContext* ctx, JSValueConst value);
InputBuffer input_buffer_clone(const InputBuffer* in, JSContext* ctx);
BOOL input_buffer_valid(const InputBuffer* in);
void input_buffer_dump(const InputBuffer* in, DynBuf* db);
void input_buffer_free(InputBuffer* in, JSContext* ctx);

static inline uint8_t*
input_buffer_data(InputBuffer* in) {
  return in->data + in->range.offset;
}

static inline size_t
input_buffer_length(InputBuffer* in) {
  return MIN_NUM(in->range.length, in->size);
}

static inline MemoryBlock
input_buffer_block(InputBuffer* in) {
  return (MemoryBlock){input_buffer_data(in), input_buffer_length(in)};
}

static inline InputBuffer
input_buffer_offset(InputBuffer in, OffsetLength off) {
  InputBuffer ret = in;

  ret.data += off.offset;
  ret.size -= off.offset;

  if(ret.size > off.length)
    ret.size = off.length;

  return ret;
}

const uint8_t* input_buffer_get(InputBuffer* in, size_t* lenp);
const uint8_t* input_buffer_peek(InputBuffer* in, size_t* lenp);
const char* input_buffer_currentline(InputBuffer*, size_t* len);
size_t input_buffer_column(InputBuffer*, size_t* len);

int input_buffer_peekc(InputBuffer* in, size_t* lenp);

static inline int
input_buffer_getc(InputBuffer* in) {
  size_t n;
  int ret;
  ret = input_buffer_peekc(in, &n);
  in->pos += n;
  return ret;
}

static inline const uint8_t*
input_buffer_begin(const InputBuffer* in) {
  return input_buffer_data(in);
}
static inline const uint8_t*
input_buffer_end(const InputBuffer* in) {
  return input_buffer_data(in) + input_buffer_length(in);
}
static inline BOOL
input_buffer_eof(const InputBuffer* in) {
  return in->pos == input_buffer_length(in);
}
static inline size_t
input_buffer_remain(const InputBuffer* in) {
  return input_buffer_length(in) - in->pos;
}

OffsetLength js_offset_length(JSContext*, int64_t size, int argc, JSValue argv[]);

#endif /* defined(BUFFER_UTILS) */
