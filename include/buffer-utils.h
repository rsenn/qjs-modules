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
  (DynBuf) { \
    0, 0, 0, 0, 0, 0 \
  }
#define DBUF_INIT_CTX(ctx) \
  (DynBuf) { \
    0, 0, 0, 0, (DynBufReallocFunc*)js_realloc_rt, JS_GetRuntime(ctx) \
  }

#define DBUF_DATA(db) ((void*)(db)->buf)
#define DBUF_SIZE(db) ((db)->size)

extern const uint8_t escape_url_tab[256], escape_noquote_tab[256], escape_singlequote_tab[256],
    escape_doublequote_tab[256], escape_backquote_tab[256];

char* dbuf_at_n(const DynBuf*, size_t, size_t*, char);
const char* dbuf_last_line(DynBuf*, size_t*);
int dbuf_prepend(DynBuf*, const uint8_t*, size_t);
void dbuf_put_colorstr(DynBuf*, const char*, const char*, int);
void dbuf_put_escaped_pred(DynBuf*, const char*, size_t, int (*)(int));
void dbuf_put_escaped_table(DynBuf*, const char*, size_t, const uint8_t[256]);
void dbuf_put_unescaped_pred(DynBuf*, const char*, size_t, int (*)(const char*, size_t*));
void dbuf_put_unescaped_table(DynBuf*, const char*, size_t, const uint8_t[256]);
void dbuf_put_escaped(DynBuf*, const char*, size_t);
void dbuf_put_value(DynBuf*, JSContext*, JSValueConst);
void dbuf_put_uint32(DynBuf*, uint32_t);
void dbuf_put_atom(DynBuf*, JSContext*, JSAtom);
int dbuf_reserve_start(DynBuf*, size_t);
uint8_t* dbuf_reserve(DynBuf*, size_t);
size_t dbuf_token_pop(DynBuf*, char);
size_t dbuf_token_push(DynBuf*, const char*, size_t, char);
JSValue dbuf_tostring_free(DynBuf*, JSContext*);
ssize_t dbuf_load(DynBuf*, const char*);
int dbuf_vprintf(DynBuf*, const char*, va_list);
void js_dbuf_allocator(JSContext*, DynBuf*);
size_t dbuf_bitflags(DynBuf*, uint32_t, const char* const[]);

int screen_size(int size[2]);

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

#define BLOCK_INIT_DATA(buf, len) \
  { (buf), (len) }

#define MEMORY_BLOCK(buf, len) (MemoryBlock) BLOCK_INIT_DATA(buf, len)

int block_realloc(MemoryBlock*, size_t, JSContext*);
void block_free(MemoryBlock*, JSRuntime*);
int block_mmap(MemoryBlock*, const char*);
void block_munmap(MemoryBlock*);
int block_from_file(MemoryBlock*, const char*, JSContext*);

static inline void
block_zero(MemoryBlock* mb) {
  mb->base = 0;
  mb->size = 0;
}

static inline MemoryBlock
block_new(const void* buf, size_t len) {
  return (MemoryBlock){(void*)buf, len};
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

#define OFFSET_LENGTH_DATA(o, l) \
  { (o), (l) }

#define OFFSET_LENGTH_0() (OffsetLength) OFFSET_LENGTH_DATA(0, SIZE_MAX)
#define OFFSET_LENGTH(o, l) (OffsetLength) OFFSET_LENGTH_DATA(o, l)

#define offsetlength_in_range(ol, num) ((num) >= (ol).offset && (num) < ((ol).offset + (ol).length))

int offsetlength_from_argv(OffsetLength*, int64_t, int, JSValueConst[], JSContext*);
OffsetLength offsetlength_char2byte(const OffsetLength src, const void* buf, size_t len);
OffsetLength offsetlength_byte2char(const OffsetLength src, const void* buf, size_t len);
JSValue offsetlength_typedarray(OffsetLength*, JSValueConst, JSContext*);

static inline void
offsetlength_zero(OffsetLength* ol) {
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
offsetlength_begin(OffsetLength ol, const void* x) {
  return (uint8_t*)x + ol.offset;
}

static inline void*
offsetlength_end(OffsetLength ol, const void* x) {
  return (uint8_t*)x + ol.offset + ol.length;
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
offsetlength_toarray(OffsetLength ol, JSContext* ctx) {
  JSValue ret = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, ret, 0, JS_NewInt64(ctx, ol.offset));
  JS_SetPropertyUint32(ctx, ret, 1, JS_NewInt64(ctx, ol.length));
  return ret;
}

typedef struct IndexRange {
  int64_t start, end;
} IndexRange;

#define INDEX_RANGE_DATA(s, e) \
  { (s), (e) }
#define INDEX_RANGE_INIT() INDEX_RANGE_DATA(0, INT64_MAX)
#define INDEX_RANGE(s, e) (IndexRange) INDEX_RANGE_DATA(s, e)

#define indexrange_in_range(ir, num) ((num) >= (ir).start && (num) < ((ir).end))

int indexrange_from_argv(IndexRange*, int64_t, int, JSValueConst[], JSContext*);

static inline void
indexrange_zero(IndexRange* ir) {
  ir->start = 0;
  ir->end = INT64_MAX;
}

static inline IndexRange
indexrange_from_int(int64_t s, int64_t e) {
  return INDEX_RANGE(s, e);
}

static inline BOOL
indexrange_is_null(IndexRange ir) {
  return ir.start == 0 && ir.end == 0;
}

static inline BOOL
indexrange_is_default(IndexRange ir) {
  return ir.start == 0 && ir.end == INT64_MAX;
}

static inline IndexRange
indexrange_from_offsetlength(OffsetLength ol) {
  return (IndexRange){ol.offset, ol.length == SIZE_MAX ? INT64_MAX : ol.offset + ol.length};
}

static inline int64_t
indexrange_head(IndexRange ir, size_t len) {
  return CLAMP_NUM(WRAP_NUM(ir.start, len), 0, len);
}

static inline int64_t
indexrange_tail(IndexRange ir, size_t len) {
  return CLAMP_NUM(WRAP_NUM(ir.end, len), 0, len);
}

static inline void*
indexrange_begin(IndexRange ir, const void* buf, size_t len) {
  return (uint8_t*)buf + indexrange_head(ir, len);
}

static inline void*
indexrange_end(IndexRange ir, const void* buf, size_t len) {
  return (uint8_t*)buf + indexrange_tail(ir, len);
}

static inline int64_t
indexrange_size(IndexRange ir, size_t len) {
  return indexrange_tail(ir, len) - indexrange_head(ir, len);
}

static inline MemoryBlock
indexrange_block(IndexRange ir, MemoryBlock b) {
  return (MemoryBlock){
      indexrange_begin(ir, b.base, b.size),
      indexrange_size(ir, b.size),
  };
}

static inline OffsetLength
indexrange_to_offsetlength(IndexRange ir, size_t len) {
  return (OffsetLength){indexrange_head(ir, len), indexrange_size(ir, len)};
}

static inline MemoryBlock
indexrange_to_block(IndexRange ir, const void* buf, size_t len) {
  return (MemoryBlock){indexrange_begin(ir, buf, len), indexrange_size(ir, len)};
}

static inline JSValue
indexrange_toarray(IndexRange ir, JSContext* ctx) {
  JSValue ret = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, ret, 0, JS_NewInt64(ctx, ir.start));
  JS_SetPropertyUint32(ctx, ret, 1, JS_NewInt64(ctx, ir.end));
  return ret;
}

typedef struct {
  void *start, *end;
} PointerRange;

#define RANGE_INIT(s, e) \
  { (s), (e) }

#define RANGE(s, e) (PointerRange) RANGE_INIT(s, e)

#define RANGE_0() (PointerRange) RANGE_INIT(0, 0)

int range_overlap(const PointerRange*, const PointerRange*);
PointerRange range_null(void);
PointerRange range_from_buf(const void*, uintptr_t);
PointerRange range_from_str(const char*);
int range_resize(PointerRange*, uintptr_t);
int range_write(PointerRange*, const void*, uintptr_t);
int range_puts(PointerRange*, const void*);
int range_append(PointerRange*, PointerRange);

static inline char*
range_begin(const PointerRange* pr) {
  return (char*)pr->start;
}

static inline char*
range_end(const PointerRange* pr) {
  return (char*)pr->end;
}

static inline char*
range_str(PointerRange* pr) {
  *range_end(pr) = '\0';
  return range_begin(pr);
}

static inline BOOL
range_is_null(PointerRange pr) {
  return pr.start == 0 && pr.end == 0;
}

static inline size_t
range_size(PointerRange pr) {
  return (const char*)pr.end - (const char*)pr.start;
}

static inline PointerRange
range_from_indexrange(IndexRange ir, const void* buf, size_t len) {
  return (PointerRange){indexrange_begin(ir, buf, len), indexrange_end(ir, buf, len)};
}

static inline PointerRange
range_from_offsetlength(OffsetLength ol, const void* buf) {
  return (PointerRange){offsetlength_begin(ol, buf), offsetlength_end(ol, buf)};
}

static inline PointerRange
range_from_block(MemoryBlock mb) {
  return (PointerRange){mb.base, mb.base + mb.size};
}

static inline PointerRange
range_offset_length(PointerRange pr, OffsetLength ol) {
  return (PointerRange){offsetlength_begin(ol, pr.start), offsetlength_end(ol, pr.start)};
}

static inline MemoryBlock
range_to_block(PointerRange pr) {
  return (MemoryBlock){pr.start, range_size(pr)};
}

static inline IndexRange
range_to_indexrange(PointerRange pr, const void* base) {
  char* x = (void*)base;
  return (IndexRange){range_begin(&pr) - x, range_end(&pr) - x};
}

static inline OffsetLength
range_to_offsetlength(PointerRange pr, const void* base) {
  return (OffsetLength){range_begin(&pr) - (char*)base, range_size(pr)};
}

static inline int
range_in(const PointerRange* r, const void* ptr) {
  return (char*)ptr >= range_begin(r) && (char*)ptr < range_end(r);
}

static inline int64_t
range_index(PointerRange r, const void* ptr) {
  return (char*)ptr - range_begin(&r);
}

typedef struct InputBuffer {
  union {
    MemoryBlock block;
    struct {
      uint8_t* data;
      size_t size;
    };
  };
  union {
    OffsetLength range;
    size_t pos;
  };
  void (*free)(JSContext*, const char*, JSValue);
  JSValue value;
} InputBuffer;

#define INPUTBUFFER() INPUTBUFFER_FREE(&inputbuffer_free_default)
#define INPUTBUFFER_FREE(fn) INPUTBUFFER_DATA_FREE(0, 0, fn)
#define INPUTBUFFER_DATA(buf, len) INPUTBUFFER_DATA_FREE(buf, len, &inputbuffer_free_default)
#define INPUTBUFFER_DATA_FREE(buf, len, fn) \
  (InputBuffer) { \
    {BLOCK_INIT_DATA(buf, len)}, {OFFSET_LENGTH_0()}, (fn), JS_UNDEFINED \
  }

static inline void
inputbuffer_free_default(JSContext* ctx, const char* str, JSValue val) {
  if(JS_IsString(val))
    JS_FreeCString(ctx, str);

  if(!JS_IsUndefined(val))
    JS_FreeValue(ctx, val);
}

InputBuffer js_input_buffer(JSContext* ctx, JSValueConst value);
InputBuffer js_input_chars(JSContext* ctx, JSValueConst value);
InputBuffer js_input_args(JSContext* ctx, int argc, JSValueConst argv[]);
InputBuffer js_output_args(JSContext* ctx, int argc, JSValueConst argv[]);

int inputbuffer_from_argv(InputBuffer*, int, JSValueConst[], JSContext*);
BOOL inputbuffer_valid(const InputBuffer*);
void inputbuffer_clone2(InputBuffer*, const InputBuffer*, JSContext*);
InputBuffer inputbuffer_clone(const InputBuffer*, JSContext*);
void inputbuffer_dump(const InputBuffer*, DynBuf*);
void inputbuffer_free(InputBuffer*, JSContext*);
const uint8_t* inputbuffer_peek(InputBuffer*, size_t*);
const uint8_t* inputbuffer_get(InputBuffer*, size_t*);
const char* inputbuffer_currentline(InputBuffer*, size_t*);
size_t inputbuffer_column(InputBuffer*, size_t*);
JSValue inputbuffer_tostring_free(InputBuffer*, JSContext*);
JSValue inputbuffer_toarraybuffer_free(InputBuffer*, JSContext*);
InputBuffer inputbuffer_from_file(const char*, JSContext*);

static inline void*
inputbuffer_data(const InputBuffer* in) {
  return offsetlength_begin(in->range, in->data);
}

static inline uint8_t*
inputbuffer_begin(const InputBuffer* in) {
  return inputbuffer_data(in);
}

static inline size_t
inputbuffer_length(const InputBuffer* in) {
  return offsetlength_size(in->range, in->size);
}

static inline uint8_t*
inputbuffer_end(const InputBuffer* in) {
  return inputbuffer_data(in) + inputbuffer_length(in);
}

static inline PointerRange
inputbuffer_range(const InputBuffer* in) {
  uint8_t* data = inputbuffer_data(in);
  return (PointerRange){data, data + inputbuffer_length(in)};
}

static inline MemoryBlock
inputbuffer_block(InputBuffer* in) {
  return (MemoryBlock){inputbuffer_data(in), inputbuffer_length(in)};
}

static inline MemoryBlock*
inputbuffer_blockptr(InputBuffer* in) {
  return &in->block;
}

const uint8_t* inputbuffer_get(InputBuffer* in, size_t* lenp);
const uint8_t* inputbuffer_peek(InputBuffer* in, size_t* lenp);
const char* inputbuffer_currentline(InputBuffer*, size_t* len);
size_t inputbuffer_column(InputBuffer*, size_t* len);

int inputbuffer_peekc(InputBuffer* in, size_t* lenp);
int inputbuffer_putc(InputBuffer*, unsigned int, JSContext*);

static inline int
inputbuffer_getc(InputBuffer* in) {
  size_t n;
  int ret;
  ret = inputbuffer_peekc(in, &n);
  in->pos += n;
  return ret;
}

static inline BOOL
inputbuffer_eof(const InputBuffer* in) {
  return in->pos == in->size;
}

static inline size_t
inputbuffer_remain(const InputBuffer* in) {
  return inputbuffer_length(in) - in->pos;
}

/**
 * @}
 */
#endif /* defined(BUFFER_UTILS) */
