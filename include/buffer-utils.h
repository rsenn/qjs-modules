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
ssize_t alloc_len(uintptr_t);

int64_t array_search(void* a, size_t m, size_t elsz, void* needle);
#define array_contains(a, m, elsz, needle) (array_search((a), (m), (elsz), (needle)) != -1)
int64_t array_search(void*, size_t, size_t elsz, void* needle);

#define DBUF_INIT_0() \
  (DynBuf) { 0, 0, 0, 0, 0, 0 }
#define DBUF_INIT_CTX(ctx) \
  (DynBuf) { 0, 0, 0, 0, (DynBufReallocFunc*)js_realloc_rt, JS_GetRuntime(ctx) }

#define DBUF_DATA(db) ((void*)(db)->buf)
#define DBUF_SIZE(db) ((db)->size)

extern const uint8_t escape_url_tab[256], escape_noquote_tab[256], escape_singlequote_tab[256],
    escape_doublequote_tab[256], escape_backquote_tab[256];

char* dbuf_at_n(const DynBuf*, size_t, size_t* n, char sep);
const char* dbuf_last_line(DynBuf*, size_t*);
int dbuf_prepend(DynBuf*, const uint8_t*, size_t len);
void dbuf_put_colorstr(DynBuf*, const char*, const char* color, int with_color);
void dbuf_put_escaped_pred(DynBuf*, const char*, size_t len, int (*pred)(int));
void dbuf_put_escaped_table(DynBuf*, const char*, size_t len, const uint8_t table[256]);
void dbuf_put_unescaped_table(DynBuf* db, const char* str, size_t len, const uint8_t table[256]);
void dbuf_put_unescaped_pred(DynBuf*, const char*, size_t len, int (*pred)(const char*, size_t*));
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
  // dbuf_realloc(db, 0);
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

#define BLOCK_INIT() \
  { 0, 0 }

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

static inline JSValue
block_toarraybuffer(MemoryBlock* mb, JSContext* ctx) {
  if(mb->base)
    return JS_NewArrayBufferCopy(ctx, mb->base, mb->size);

  return JS_NULL;
}

static inline MemoryBlock
block_slice(MemoryBlock mb, int64_t start, int64_t end) {
  int64_t n = (int64_t)mb.size;
  start = CLAMP_NUM(WRAP_NUM(start, n), 0, n);
  end = CLAMP_NUM(WRAP_NUM(end, n), 0, n);

  return (MemoryBlock){mb.base + start, end - start};
}

static inline MemoryBlock
block_range(MemoryBlock mb, size_t offset, size_t length) {
  offset = MIN_NUM(mb.size, offset);
  length = MIN_NUM((mb.size - offset), length);

  return (MemoryBlock){mb.base + offset, length};
}

static inline int
block_realloc(MemoryBlock* mb, size_t new_size, JSContext* ctx) {
  if((mb->base = js_realloc(ctx, mb->base, new_size))) {
    mb->size = new_size;
    return 0;
  }

  return -1;
}

static inline void
block_free(MemoryBlock* mb, JSRuntime* rt) {
  if(mb->base) {
    js_free_rt(rt, mb->base);
    mb->base = NULL;
  }
}

static inline uint8_t*
block_grow(MemoryBlock* mb, size_t add_size, JSContext* ctx) {
  uint8_t* ptr = block_end(mb);

  if(block_realloc(mb, mb->size + add_size, ctx))
    return 0;

  return ptr;
}

static inline int
block_append(MemoryBlock* mb, const void* buf, size_t len, JSContext* ctx) {
  uint8_t* ptr;

  if(!(ptr = block_grow(mb, len, ctx)))
    return -1;

  memcpy(ptr, buf, len);
  return 0;
}

typedef struct OffsetLength {
  size_t offset, length;
} OffsetLength;

#define OFFSETLENGTH_INIT() \
  (OffsetLength) { 0, SIZE_MAX }

#define offsetlength_in_range(ol, num) ((num) >= (ol).offset && (num) < ((ol).offset + (ol).length))

int offsetlength_from_argv(OffsetLength*, int64_t, int, JSValueConst[], JSContext*);

static inline void
offsetlength_init(OffsetLength* ol) {
  ol->offset = 0;
  ol->length = SIZE_MAX;
}

static inline BOOL
offsetlength_is_default(OffsetLength ol) {
  return ol.offset == 0 && ol.length == SIZE_MAX;
}

static inline size_t
offsetlength_offset(OffsetLength ol, size_t n) {
  return MIN_NUM(ol.offset, n);
}

static inline void*
offsetlength_data(OffsetLength ol, const void* x) {
  return (uint8_t*)x + ol.offset;
}

static inline size_t
offsetlength_size(OffsetLength ol, size_t n) {
  size_t offs = MIN_NUM(ol.offset, n);

  if(ol.length == SIZE_MAX)
    return n - offs;

  return MIN_NUM(ol.length, n - offs);
}

static inline MemoryBlock
offsetlength_block(OffsetLength ol, MemoryBlock mb) {
  return block_range(mb, ol.offset, ol.length);
}

static inline JSValue
offsetlength_typedarray(OffsetLength* ol, JSValueConst array, JSContext* ctx) {
  JSValue ret;
  size_t offset, length;

  ret = JS_GetTypedArrayBuffer(ctx, array, &offset, &length, NULL);

  if(!JS_IsException(ret)) {
    ol->offset = offset;
    ol->length = length;
  }

  return ret;
}

typedef struct IndexRange {
  int64_t start, end;
} IndexRange;

#define INDEXRANGE_INIT() \
  (IndexRange) { 0, INT64_MAX }

#define indexrange_in_range(ir, num) ((num) >= (ir).start && (num) < ((ir).end))

int indexrange_from_argv(IndexRange*, int64_t, int, JSValueConst[], JSContext*);

static inline void
indexrange_init(IndexRange* ir) {
  ir->start = 0;
  ir->end = INT64_MAX;
}

static inline BOOL
indexrange_is_default(IndexRange ir) {
  return ir.start == 0 && ir.end == INT64_MAX;
}

static inline IndexRange
indexrange_from_offset(OffsetLength ol) {
  return (IndexRange){ol.offset, ol.length == SIZE_MAX ? INT64_MAX : ol.offset + ol.length};
}

static inline int64_t
indexrange_start(IndexRange ir, size_t n) {
  int64_t l = n;
  return CLAMP_NUM(WRAP_NUM(ir.start, l), 0, l);
}

static inline int64_t
indexrange_end(IndexRange ir, size_t n) {
  int64_t l = n;
  return CLAMP_NUM(WRAP_NUM(ir.end, l), 0, l);
}

static inline void*
indexrange_data(IndexRange ir, const void* x, size_t n) {
  return (uint8_t*)x + indexrange_start(ir, n);
}

static inline int64_t
indexrange_size(IndexRange ir, size_t n) {
  return indexrange_end(ir, n) - indexrange_start(ir, n);
}

static inline MemoryBlock
indexrange_block(IndexRange ir, MemoryBlock b) {
  return (MemoryBlock){
      indexrange_data(ir, b.base, b.size),
      indexrange_size(ir, b.size),
  };
}

typedef struct {
  void *start, *end;
} PointerRange;

#define RANGE_INIT() \
  (PointerRange) { 0, 0 }

int range_overlap(const PointerRange*, const PointerRange*);
PointerRange range_null(void);
PointerRange range_frombuf(const void*, uintptr_t);
PointerRange range_fromstr(const char*);
int range_resize(PointerRange*, uintptr_t);
int range_write(PointerRange*, const void*, uintptr_t);
int range_puts(PointerRange*, const void*);
int range_append(PointerRange*, PointerRange);

static inline void
range_init(PointerRange* pr) {
  pr->end = pr->start = 0;
}

static inline char*
range_begin(PointerRange* pr) {
  return (char*)pr->start;
}

static inline char*
range_end(PointerRange* pr) {
  return (char*)pr->end;
}

static inline char*
range_str(PointerRange* pr) {
  *range_end(pr) = '\0';
  return range_begin(pr);
}

static inline BOOL
range_is_default(PointerRange pr) {
  return pr.start == 0 && pr.end == 0;
}

static inline intptr_t
range_size(PointerRange pr) {
  return pr.end - pr.start;
}

static inline PointerRange
range_fromindex(IndexRange ir, const void* base, size_t n) {
  uint8_t* data = indexrange_data(ir, base, n);
  size_t size = indexrange_size(ir, n);

  return (PointerRange){data, data + size};
}

static inline PointerRange
range_fromblock(MemoryBlock mb) {
  return (PointerRange){mb.base, mb.base + mb.size};
}

static inline PointerRange
range_offset_length(PointerRange pr, OffsetLength ol) {
  size_t size = range_size(pr);
  uint8_t* base = pr.start + offsetlength_offset(ol, size);

  return (PointerRange){base, base + offsetlength_size(ol, size)};
}

static inline int
range_in(const PointerRange* r, const void* ptr) {
  return (const uint8_t*)ptr >= (const uint8_t*)r->start && (const uint8_t*)ptr <= (const uint8_t*)r->end;
}

static inline ssize_t
range_len(const PointerRange* r) {
  return r->end - r->start;
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

#define INPUT_BUFFER_INIT() \
  (InputBuffer) { {BLOCK_INIT()}, 0, NULL, JS_UNDEFINED, OFFSETLENGTH_INIT() }

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

static inline void*
input_buffer_data(const InputBuffer* in) {
  return offsetlength_data(in->range, in->data);
}

static inline uint8_t*
input_buffer_begin(const InputBuffer* in) {
  return input_buffer_data(in);
}

static inline size_t
input_buffer_length(const InputBuffer* in) {
  return offsetlength_size(in->range, in->size);
}

static inline uint8_t*
input_buffer_end(const InputBuffer* in) {
  return input_buffer_data(in) + input_buffer_length(in);
}

static inline PointerRange
input_buffer_range(const InputBuffer* in) {
  uint8_t* data = input_buffer_data(in);
  return (PointerRange){data, data + input_buffer_length(in)};
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

static inline BOOL
input_buffer_eof(const InputBuffer* in) {
  return in->pos == input_buffer_length(in);
}

static inline size_t
input_buffer_remain(const InputBuffer* in) {
  return input_buffer_length(in) - in->pos;
}

/**
 * @}
 */
#endif /* defined(BUFFER_UTILS) */
