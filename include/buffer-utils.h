#ifndef BUFFER_UTILS_H
#define BUFFER_UTILS_H

#include <quickjs.h>
#include <cutils.h>
#include <stdarg.h>

#include "char-utils.h"

/**
 * \defgroup buffer-utils buffer-utils: Buffer Utilities
 * @{
 */
int64_t array_search(void* a, size_t m, size_t elsz, void* needle);
#define array_contains(a, m, elsz, needle) (array_search((a), (m), (elsz), (needle)) != -1)

size_t ansi_length(const char*, size_t);
size_t ansi_skip(const char*, size_t);
size_t ansi_truncate(const char*, size_t, size_t limit);
int64_t array_search(void*, size_t, size_t elsz, void* needle);
char* str_escape(const char*);

char* byte_escape(const void*, size_t);
size_t byte_findb(const void*, size_t, const void* what, size_t wlen);
size_t byte_finds(const void*, size_t, const char* what);
size_t byte_equal(const void* s, size_t n, const void* t);
void byte_copy(void* out, size_t len, const void* in);
void byte_copyr(void* out, size_t len, const void* in);
size_t byte_rchrs(const char* in, size_t len, const char needles[], size_t nn);

#define DBUF_INIT_0() \
  (DynBuf) { 0, 0, 0, 0, 0, 0 }
#define DBUF_INIT_CTX(ctx) \
  (DynBuf) { 0, 0, 0, 0, (DynBufReallocFunc*)js_realloc_rt, JS_GetRuntime(ctx) }

extern const uint8_t escape_url_tab[256], escape_noquote_tab[256], escape_singlequote_tab[256], escape_doublequote_tab[256], escape_backquote_tab[256];

char* dbuf_at_n(const DynBuf*, size_t, size_t* n, char sep);
const char* dbuf_last_line(DynBuf*, size_t*);
int dbuf_prepend(DynBuf*, const uint8_t*, size_t len);
void dbuf_put_colorstr(DynBuf*, const char*, const char* color, int with_color);
void dbuf_put_escaped_pred(DynBuf*, const char*, size_t len, int (*pred)(int));
void dbuf_put_escaped_table(DynBuf*, const char*, size_t len, const uint8_t table[256]);
void dbuf_put_unescaped_table(DynBuf* db, const char* str, size_t len, const uint8_t table[256]);
void dbuf_put_unescaped_pred(DynBuf*, const char*, size_t len, int (*pred)());
void dbuf_put_escaped(DynBuf*, const char*, size_t len);
void dbuf_put_value(DynBuf*, JSContext*, JSValue value);
void dbuf_put_uint32(DynBuf* db, uint32_t num);
void dbuf_put_atom(DynBuf* db, JSContext* ctx, JSAtom atom);
int dbuf_reserve_start(DynBuf*, size_t);
uint8_t* dbuf_reserve(DynBuf*, size_t);
size_t dbuf_token_pop(DynBuf*, char);
size_t dbuf_token_push(DynBuf*, const char*, size_t len, char delim);
JSValue dbuf_tostring_free(DynBuf*, JSContext*);
ssize_t dbuf_load(DynBuf*, const char*);
int dbuf_vprintf(DynBuf*, const char*, va_list);

int screen_size(int size[2]);

static inline int
dbuf_putm(DynBuf* db, ...) {
  int r = 0;
  va_list a;
  const char* s;
  va_start(a, db);
  while((s = va_arg(a, char*)))
    if(dbuf_putstr(db, s))
      return -1;
  va_end(a);
  return r;
}

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
  db->size = 0;
}

size_t dbuf_bitflags(DynBuf* db, uint32_t bits, const char* const names[]);

#define js_dbuf_init(ctx, buf) dbuf_init2((buf), (ctx), (realloc_func*)&utils_js_realloc)
#define js_dbuf_init_rt(rt, buf) dbuf_init2((buf), (rt), (realloc_func*)&utils_js_realloc_rt)

void js_dbuf_allocator(JSContext* ctx, DynBuf* s);

typedef struct {
  uint8_t* base;
  size_t size;
} MemoryBlock;

static inline void
block_init(MemoryBlock* mb) {
  mb->base = 0;
  mb->size = 0;
}

/* clang-format off */
static inline void* block_data(const MemoryBlock* mb) { return mb->base; }
static inline size_t block_length(const MemoryBlock* mb) { return mb->size; }
static inline void* block_begin(const MemoryBlock* mb) { return mb->base; }
static inline void* block_end(const MemoryBlock* mb) { return mb->base + mb->size; }
/* clang-format on */

static inline BOOL
block_arraybuffer(MemoryBlock* mb, JSValueConst ab, JSContext* ctx) {
  mb->base = JS_GetArrayBuffer(ctx, &mb->size, ab);
  return mb->base != 0;
}

typedef struct {
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

typedef struct {
  int64_t start, end;
} IndexRange;

typedef struct {
  int64_t offset, length;
} OffsetLength;

#define OFFSET_INIT() \
  (OffsetLength) { 0, INT64_MAX }

static inline void
offset_init(OffsetLength* ol) {
  ol->offset = 0;
  ol->length = INT64_MAX;
}

static inline BOOL
offset_is_default(const OffsetLength* ol) {
  return ol->offset == 0 && ol->length == INT64_MAX;
}

static inline void*
offset_data(const OffsetLength* ol, const void* x) {
  return (uint8_t*)x + ol->offset;
}

static inline size_t
offset_size(const OffsetLength* ol, size_t n) {
  if(ol->length == -1)
    return (signed long)n - ol->offset;
  return MIN_NUM(ol->length, (signed long)n - ol->offset);
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

static inline OffsetLength
offset_from_indexrange(const IndexRange* ir) {
  OffsetLength ret;
  ret.offset = ir->start;
  ret.length = ir->end - ir->start;
  return ret;
}

static inline JSValue
offset_typedarray(OffsetLength* ol, JSValueConst array, JSContext* ctx) {
  JSValue ret;
  size_t offset, length;

  ret = JS_GetTypedArrayBuffer(ctx, array, &offset, &length, NULL);

  if(!JS_IsException(ret)) {
    ol->offset = offset;
    ol->length = length;
  }

  return ret;
}

static inline IndexRange
indexrange_from_offset(const OffsetLength* ol) {
  IndexRange ret;
  ret.start = ol->offset;
  ret.end = ol->offset + ol->length;
  return ret;
}

static inline MemoryBlock
block_range(const MemoryBlock* mb, const OffsetLength* range) {
  MemoryBlock ret;
  ret.base = mb->base + range->offset;
  ret.size = MIN_NUM((size_t)range->length, mb->size - range->offset);
  return ret;
}

static inline int
block_realloc(MemoryBlock* mb, size_t new_size, JSContext* ctx) {
  if((mb->base = js_realloc(ctx, mb->base, new_size))) {
    mb->size = new_size;
    return 0;
  }
  return -1;
}

typedef struct InputBuffer {
  union {
    MemoryBlock block;
    struct {
      uint8_t* data;
      size_t size;
    };
  };
  size_t pos;
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
InputBuffer js_input_chars(JSContext* ctx, JSValueConst value);
InputBuffer js_input_args(JSContext* ctx, int argc, JSValueConst argv[]);
InputBuffer js_output_args(JSContext* ctx, int argc, JSValueConst argv[]);

InputBuffer input_buffer_clone(const InputBuffer* in, JSContext* ctx);
BOOL input_buffer_valid(const InputBuffer* in);
void input_buffer_dump(const InputBuffer* in, DynBuf* db);
void input_buffer_free(InputBuffer* in, JSContext* ctx);

static inline uint8_t*
input_buffer_data(const InputBuffer* in) {
  return offset_data(&in->range, in->data);
}

static inline size_t
input_buffer_length(const InputBuffer* in) {
  return offset_size(&in->range, in->size);
}

static inline MemoryBlock
input_buffer_block(InputBuffer* in) {
  return (MemoryBlock){input_buffer_data(in), input_buffer_length(in)};
}

static inline MemoryBlock*
input_buffer_blockptr(InputBuffer* in) {
  return &in->block;
}

const uint8_t* input_buffer_get(InputBuffer* in, size_t* lenp);
const uint8_t* input_buffer_peek(InputBuffer* in, size_t* lenp);
const char* input_buffer_currentline(InputBuffer*, size_t* len);
size_t input_buffer_column(InputBuffer*, size_t* len);

int input_buffer_peekc(InputBuffer* in, size_t* lenp);
int input_buffer_putc(InputBuffer*, unsigned int, JSContext*);

static inline int
input_buffer_getc(InputBuffer* in) {
  size_t n;
  int ret;
  ret = input_buffer_peekc(in, &n);
  in->pos += n;
  return ret;
}

static inline void*
input_buffer_begin(const InputBuffer* in) {
  return input_buffer_data(in);
}

static inline void*
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

int js_offset_length(JSContext*, int64_t size, int argc, JSValueConst argv[], OffsetLength* off_len_p);
int js_index_range(JSContext*, int64_t size, int argc, JSValueConst argv[], IndexRange* idx_rng_p);

/**
 * @}
 */
#endif /* defined(BUFFER_UTILS) */
