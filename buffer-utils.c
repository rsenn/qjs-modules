#include "buffer-utils.h"

InputBuffer
js_input_buffer(JSContext* ctx, JSValueConst value) {
  InputBuffer ret = {0, 0, 0, &input_buffer_free_default, JS_UNDEFINED};
  int64_t offset = 0, length = INT64_MAX;

  offset_init(&ret.range);

  if(js_is_typedarray(value) || js_is_dataview(ctx, value)) {
    JSValue arraybuf, byteoffs, bytelen;

    arraybuf = JS_GetPropertyStr(ctx, value, "buffer");

    bytelen = JS_GetPropertyStr(ctx, value, "byteLength");
    if(JS_IsNumber(bytelen))
      JS_ToInt64(ctx, &length, bytelen);
    JS_FreeValue(ctx, bytelen);

    byteoffs = JS_GetPropertyStr(ctx, value, "byteOffset");
    if(JS_IsNumber(byteoffs))
      JS_ToInt64(ctx, &offset, byteoffs);
    JS_FreeValue(ctx, byteoffs);

    value = arraybuf;
  }

  if(js_value_isclass(ctx, value, JS_CLASS_ARRAY_BUFFER) || js_is_arraybuffer(ctx, value)) {
    ret.value = JS_DupValue(ctx, value);
    ret.data = JS_GetArrayBuffer(ctx, &ret.size, ret.value);
  } else if(JS_IsString(value)) {
    ret.data = (uint8_t*)JS_ToCStringLen(ctx, &ret.size, value);
    ret.value = js_cstring_value((const char*)ret.data);
    ret.free = &input_buffer_free_default;
  } else {
    ret.value = JS_EXCEPTION;
    //    JS_ThrowTypeError(ctx, "Invalid type for input buffer");
  }

  if(offset < 0)
    ret.range.offset = ret.size + offset % ret.size;
  else if(offset > ret.size)
    ret.range.offset = ret.size;
  else
    ret.range.offset = offset;

  if(length >= 0 && length < ret.size)
    ret.range.length = length;

  return ret;
}

BOOL
input_buffer_valid(const InputBuffer* in) {
  return !JS_IsException(in->value);
}

InputBuffer
input_buffer_clone(const InputBuffer* in, JSContext* ctx) {
  InputBuffer ret = js_input_buffer(ctx, in->value);

  ret.pos = in->pos;
  ret.size = in->size;
  ret.free = in->free;

  return ret;
}

void
input_buffer_dump(const InputBuffer* in, DynBuf* db) {
  dbuf_printf(
      db, "(InputBuffer){ .data = %p, .size = %zu, .pos = %zu, .free = %p }", in->data, in->size, in->pos, in->free);
}

void
input_buffer_free(InputBuffer* in, JSContext* ctx) {
  if(in->data) {
    in->free(ctx, (const char*)in->data, in->value);
    in->data = 0;
    in->size = 0;
    in->pos = 0;
    in->value = JS_UNDEFINED;
  }
}

int
input_buffer_peekc(InputBuffer* in, size_t* lenp) {
  const uint8_t *pos, *end, *next;
  int cp;
  pos = input_buffer_data(in) + in->pos;
  end = input_buffer_data(in) + input_buffer_length(in);
  cp = unicode_from_utf8(pos, end - pos, &next);
  if(lenp)
    *lenp = next - pos;

  return cp;
}

const uint8_t*
input_buffer_peek(InputBuffer* in, size_t* lenp) {
  input_buffer_peekc(in, lenp);
  return input_buffer_data(in) + in->pos;
}

const uint8_t*
input_buffer_get(InputBuffer* in, size_t* lenp) {
  size_t n;
  const uint8_t* ret;
  if(lenp == 0)
    lenp = &n;
  ret = input_buffer_peek(in, lenp);
  in->pos += *lenp;
  return ret;
}

const char*
input_buffer_currentline(InputBuffer* in, size_t* len) {
  size_t i;

  if((i = byte_rchr(input_buffer_data(in), in->pos, '\n')) < in->pos)
    i++;

  if(len)
    *len = in->pos - i;

  return (const char*)&input_buffer_data(in)[i];
}

size_t
input_buffer_column(InputBuffer* in, size_t* len) {
  size_t i;

  if((i = byte_rchr(input_buffer_data(in), in->pos, '\n')) < in->pos)
    i++;

  return in->pos - i;
}
