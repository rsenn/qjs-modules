#ifndef BUFFER_UTILS_H
#define BUFFER_UTILS_H

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

static inline MemoryBlock
offset_block(const OffsetLength* ol, const void* x, size_t n) {
  return (MemoryBlock){MIN_NUM(n, ol->offset), MIN_NUM(ol->length, n - ol->offset)};
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

#endif /* defined(BUFFER_UTILS) */
