#include "jsonst.h"
#include "char-utils.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#define jsonst_sizeof(x) (ptrdiff_t)sizeof(x)
#define jsonst_alignof(x) (ptrdiff_t) _Alignof(x)
#define jsonst_count(a) (jsonst_sizeof(a) / jsonst_sizeof(*(a)))
#define jsonst_length(s) (jsonst_count(s) - 1)

/* Utils - JsonStArena jsonst_alloc, strings. */

/* Inspired by https://nullprogram.com/blog/2023/09/27/. */
typedef struct {
  uint8_t *beg, *end;
} JsonStArena;

/*
 * Inspired by https://nullprogram.com/blog/2023/10/08/, modified to always be null byte terminated.
 * The length field is excluding the 0 byte.
 */
typedef struct {
  char* buf;
  ptrdiff_t len;
} JsonStS8;

#define JSONSTS8_INIT(s) \
  (JsonStS8){ (char*)(s), strlen((s)) }

static JsonStArena
jsonst_new_arena(uint8_t* mem, const ptrdiff_t memsz) {
  JsonStArena a = {0};
  a.beg = mem;
  a.end = a.beg ? a.beg + memsz : 0;
  return a;
}

/* Returns NULL on OOM. */
static __attribute((malloc, alloc_size(2, 4), alloc_align(3))) void*
jsonst_alloc(JsonStArena* a, ptrdiff_t size, ptrdiff_t align, ptrdiff_t count) {
  ptrdiff_t avail = a->end - a->beg;
  ptrdiff_t padding = -(uintptr_t)a->beg & (align - 1);

  if(count > (avail - padding) / size)
    return NULL;

  ptrdiff_t total = size * count;
  uint8_t* p = a->beg + padding;
  a->beg += padding + total;

  /* memset(p, 0, total) */
  for(ptrdiff_t i = 0; i < total; i++)
    p[i] = 0;

#ifndef NDEBUG
  for(uint8_t* m = a->beg; m <= a->end; m++)
    *m = '~';
#endif

  return p;
}

#define JSONST_NEW(a, t, n) (t*)jsonst_alloc(a, jsonst_sizeof(t), _Alignof(t), n)

/* May return JsonStS8.buf = NULL on OOM. */
static JsonStS8
jsonst_new_s8(JsonStArena* a, const ptrdiff_t len) {
  JsonStS8 s = {0};
  /* +1 is for C 0 byte interop. */
  s.buf = JSONST_NEW(a, char, len + 1);

  if(s.buf != NULL)
    s.len = len;

  return s;
}

/* Implementation structs. */

/*
 * In most places where JsonStType is used, JsonStInternalState may also be used.
 * Thus, we need to be careful to no accidentally introduce overlaps.
 */
typedef enum {
  jsonst_arry_elm_next = '+',

  jsonst_obj_post_key = ':',
  jsonst_obj_post_sep = '-',
  jsonst_obj_next = ';',

  jsonst_str_utf8 = '8',
  jsonst_str_escp = '\\',
  jsonst_str_escp_uhex = '%',
  jsonst_str_escp_uhex_utf16 = '6',
} JsonStInternalState;

typedef struct jsonst_frame JsonStFrame;
struct jsonst_frame {
  JsonStFrame* prev;
  int type; /* Is a JsonStType or JsonStInternalState. */
  JsonStArena a;

  /*
   * Processing state, usage depends on type.
   * Some types will use len separately without str.
   */
  JsonStS8 str;
  ptrdiff_t len;
};

typedef struct jsonst_internal JsonStInternal;
typedef struct jsonst_internal {
  JsonStCallback cb;
  void* cb_user_data;
  JsonStConfig config;
  JsonStError failed;
  JsonStFrame* sp;
} JsonStInternal;

/* Helpers for error handling. */
#define RET_ON_ERR(expr) \
  { \
    const JsonStError ret = (expr); \
    if(ret != JsonStError_Success) { \
      return ret; \
    } \
  }

#define RET_OOM_IFNULL(expr) \
  if((expr) == NULL) { \
    return JsonStError_Oom; \
  }

/* Implementation. */

static JsonStFrame*
jsonst_new_frame(JsonStArena a, JsonStFrame* prev, const JsonStType /* or JsonStInternalState */ type) {
  /* Allocate the JsonStFrame on its own copy of the JsonStArena. */
  JsonStFrame* f = JSONST_NEW(&a, JsonStFrame, 1);

  if(f == NULL)
    return NULL;

  f->prev = prev;
  f->type = type;
  f->a = a;

  return f;
}

static JsonStInternal*
jsonst_new_internal(
    uint8_t* mem, const ptrdiff_t memsz, const JsonStCallback cb, void* cb_user_data, const JsonStConfig conf) {
  JsonStArena a = jsonst_new_arena(mem, memsz);
  JsonSt j = JSONST_NEW(&a, JsonStInternal, 1);

  if(j == NULL)
    return NULL;

  assert(cb != NULL);
  j->cb = cb;
  j->cb_user_data = cb_user_data;

  j->config = conf;

  if(j->config.str_alloc_bytes == 0)
    j->config.str_alloc_bytes = JSONST_DEFAULT_STR_ALLOC_BYTES;

  if(j->config.obj_key_alloc_bytes == 0)
    j->config.obj_key_alloc_bytes = JSONST_DEFAULT_OBJ_KEY_ALLOC_BYTES;

  if(j->config.num_alloc_bytes == 0)
    j->config.num_alloc_bytes = JSONST_DEFAULT_NUM_ALLOC_BYTES;

  j->failed = JsonStError_Success;

  j->sp = jsonst_new_frame(a, NULL, JsonStType_Doc);

  if(j->sp == NULL)
    return NULL;

  return j;
}

JsonSt
jsonst_new(
    void* mem, const ptrdiff_t memsz, const JsonStCallback cb, void* cb_user_data, const JsonStConfig conf) {
  return jsonst_new_internal(mem, memsz, cb, cb_user_data, conf);
}

/*static JsonStError JsonStFrame_buf_putc(JsonStFrame* f, const char c) __attribute((warn_unused_result));*/

static JsonStError
JsonStFrame_buf_putc(JsonStFrame* f, const char c) {
  assert(f->str.buf != NULL);

  if(f->len >= f->str.len)
    return JsonStError_StrBufferFull;

  f->str.buf[f->len] = c;
  f->len++;
  return JsonStError_Success;
}

typedef enum {
  num_init,
  num_int_minus,
  num_int_zero,
  num_int_digit,
  num_int_dot,
  num_int_frac,
  num_exp_e,
  num_exp_sign,
  num_exp_int,
} NumberState;

/*
 * By not directly integrating this into the parsing state machine we might waste
 * a bit of performance, but it's much easier to read.
 */
static bool
is_valid_json_number(const char* buf, const ptrdiff_t len) {
  NumberState s = num_init;

  for(ptrdiff_t i = 0; i < len; i++) {
    const char c = buf[i];

    switch(s) {
      case num_init:
        if(c == '-') {
          s = num_int_minus;
          continue;
        }

        if(c == '0') {
          s = num_int_zero;
          continue;
        }

        if(!is_digit_char(c))
          return false;

        s = num_int_digit;
        continue;

      case num_int_minus:
        if(c == '0') {
          s = num_int_zero;
          continue;
        }

        if(!is_digit_char(c))
          return false;

        s = num_int_digit;
        continue;

      case num_int_zero:
        if(is_digit_char(c))
          return false;

        if(c == '.') {
          s = num_int_dot;
          continue;
        }

        if(c == 'E' || c == 'e') {
          s = num_exp_e;
          continue;
        }

        return false;

      case num_int_digit:
        if(is_digit_char(c))
          continue;

        if(c == '.') {
          s = num_int_dot;
          continue;
        }

        if(c == 'E' || c == 'e') {
          s = num_exp_e;
          continue;
        }

        return false;

      case num_int_dot:
        if(!is_digit_char(c))
          return false;

        s = num_int_frac;
        continue;

      case num_int_frac:
        if(is_digit_char(c))
          continue;

        if(c != 'E' && c != 'e')
          return false;

        s = num_exp_e;
        continue;

      case num_exp_e:
        if(c == '-' || c == '+') {
          s = num_exp_sign;
          continue;
        }

        if(!is_digit_char(c) || c == '0')
          return false;

        s = num_exp_int;
        continue;

      case num_exp_sign:
        if(!is_digit_char(c) || c == '0')
          return false;

        s = num_exp_int;
        continue;

      case num_exp_int:
        if(!is_digit_char(c))
          return false;
    }
  }

  return s == num_int_zero || s == num_int_digit || s == num_int_frac || s == num_exp_int;
}

// static JsonStError emit(const JsonStInternal* j, const JsonStType type) __attribute((warn_unused_result));

static JsonStError
emit(const JsonStInternal* j, const JsonStType /* or JsonStInternalState */ type) {
  JsonStArena scratch = j->sp->a;
  JsonStValue* v = JSONST_NEW(&scratch, JsonStValue, 1);
  RET_OOM_IFNULL(v);
  v->type = type;

  switch(type) {
    case JsonStType_Number: {
      assert(j->sp->type == JsonStType_Number);
      if(!is_valid_json_number(j->sp->str.buf, j->sp->len))
        return JsonStError_InvalidNumber;

      v->val_str.str = j->sp->str.buf;
      v->val_str.str_len = j->sp->len;
      break;
    }

    case JsonStType_String:
    case JsonStType_ObjectKey:
      assert(j->sp->type == JsonStType_String || j->sp->type == JsonStType_ObjectKey);
      v->val_str.str = j->sp->str.buf;
      v->val_str.str_len = j->sp->len;
      break;

    default:
      /* Nothing else to do for the others. */
      break;
  }

  /* Fill in path, in reverse order of stack. */
  JsonStPath* p = NULL;

  for(const JsonStFrame* f = j->sp; f != NULL; f = f->prev) {
    switch(f->type) {
      case JsonStType_ArrayElm:
      case JsonStType_ObjectKey: break;
      default: continue;
    }

    JsonStPath* p_new = JSONST_NEW(&scratch, JsonStPath, 1);

    if(p_new == NULL)
      return JsonStError_Oom;

    p_new->type = f->type;

    switch(f->type) {
      case JsonStType_ArrayElm:
        assert(f->prev->type == JsonStType_Array);
        p_new->props.arry_ix = f->prev->len;
        break;

      case JsonStType_ObjectKey:
        p_new->props.obj_key.str = f->str.buf;
        p_new->props.obj_key.str_len = f->len;
        break;
    }

    if(p == NULL) {
      p = p_new;
    } else {
      p_new->next = p;
      p = p_new;
    }
  }

  j->cb(j->cb_user_data, v, p);
  return JsonStError_Success;
}
 
static int32_t
conv_xdigit(const char c) {
  if(is_digit_char(c))
    return c - '0';

  if(c >= 'a' && c <= 'f')
    return c - 'a' + 10;

  if(c >= 'A' && c <= 'F')
    return c - 'A' + 10;

  return -1;
}

static bool
is_num(const char c) {
  if(is_digit_char(c))
    return true;

  switch(c) {
    case '.':
    case 'e':
    case 'E':
    case '+':
    case '-': return true;
  }

  return false;
}

static bool
is_utf16_high_surrogate(const uint32_t c) {
  return c >= 0xd800 && c < 0xdc00;
}

static bool
is_utf16_low_surrogate(const uint32_t c) {
  return c >= 0xdc00 && c < 0xe000;
}

/* Str must be of length 4, and characters must all be hex digits. */
static uint32_t
parse_hex4(const JsonStS8 str) {
  const ptrdiff_t len = 4;
  assert(str.len == len);

  uint32_t ret = 0;

  for(int i = 0; i < len; i++) {
    unsigned char c = str.buf[i];
    assert(is_xdigit_char(c));

    ret <<= 4;
    ret |= (uint32_t)conv_xdigit(c);
  }

  return ret;
}

// static JsonStError utf8_encode(JsonStFrame* f, const uint32_t c) __attribute((warn_unused_result));

static JsonStError
utf8_encode(JsonStFrame* f, const uint32_t c) {
  if(c <= 0x7F) {
    RET_ON_ERR(JsonStFrame_buf_putc(f, c));
    return JsonStError_Success;
  }

  if(c <= 0x07FF) {
    RET_ON_ERR(JsonStFrame_buf_putc(f, (((c >> 6) & 0x1F) | 0xC0)));
    RET_ON_ERR(JsonStFrame_buf_putc(f, (((c >> 0) & 0x3F) | 0x80)));
    return JsonStError_Success;
  }

  if(c <= 0xFFFF) {
    RET_ON_ERR(JsonStFrame_buf_putc(f, (((c >> 12) & 0x0F) | 0xE0)));
    RET_ON_ERR(JsonStFrame_buf_putc(f, (((c >> 6) & 0x3F) | 0x80)));
    RET_ON_ERR(JsonStFrame_buf_putc(f, (((c >> 0) & 0x3F) | 0x80)));
    return JsonStError_Success;
  }

  if(c <= 0x10FFFF) {
    RET_ON_ERR(JsonStFrame_buf_putc(f, (((c >> 18) & 0x07) | 0xF0)));
    RET_ON_ERR(JsonStFrame_buf_putc(f, (((c >> 12) & 0x3F) | 0x80)));
    RET_ON_ERR(JsonStFrame_buf_putc(f, (((c >> 6) & 0x3F) | 0x80)));
    RET_ON_ERR(JsonStFrame_buf_putc(f, (((c >> 0) & 0x3F) | 0x80)));
    return JsonStError_Success;
  }

  return JsonStError_InvalidUnicodeCodepoint;
}

// static JsonStError jsonst_push(JsonStInternal* j, const JsonStType type) __attribute((warn_unused_result));

static JsonStError
jsonst_push(JsonStInternal* j, const JsonStType /* or JsonStInternalState */ type) {
  j->sp = jsonst_new_frame(j->sp->a, j->sp, type);

  if(j->sp == NULL)
    return JsonStError_Oom;

  return JsonStError_Success;
}

static void
jsonst_pop(JsonStInternal* j, __attribute((unused)) const JsonStType /* or JsonStInternalState */ expect_type) {
  assert(j->sp != NULL);
  assert(j->sp->type == (int)expect_type);

  j->sp = j->sp->prev;
}

// static JsonStError expect_start_value(JsonStInternal* j, const char c) __attribute((warn_unused_result));

static JsonStError
expect_start_value(JsonStInternal* j, const char c) {
  switch(c) {
    case '[':
      RET_ON_ERR(jsonst_push(j, JsonStType_Array));
      RET_ON_ERR(emit(j, JsonStType_Array));
      return JsonStError_Success;

    case '{':
      RET_ON_ERR(jsonst_push(j, JsonStType_Object));
      RET_ON_ERR(emit(j, JsonStType_Object));
      return JsonStError_Success;

    case 'n':
      RET_ON_ERR(jsonst_push(j, JsonStType_Null));
      j->sp->len++;
      return JsonStError_Success;

    case 't':
      RET_ON_ERR(jsonst_push(j, JsonStType_True));
      j->sp->len++;
      return JsonStError_Success;

    case 'f':
      RET_ON_ERR(jsonst_push(j, JsonStType_False));
      j->sp->len++;
      return JsonStError_Success;

    case '"':
      RET_ON_ERR(jsonst_push(j, JsonStType_String));
      j->sp->str = jsonst_new_s8(&j->sp->a, j->config.str_alloc_bytes);
      RET_OOM_IFNULL(j->sp->str.buf);
      return JsonStError_Success;

    default:
      if(is_digit_char(c) || c == '-') {
        RET_ON_ERR(jsonst_push(j, JsonStType_Number));
        j->sp->str = jsonst_new_s8(&j->sp->a, j->config.num_alloc_bytes);
        RET_OOM_IFNULL(j->sp->str.buf);
        RET_ON_ERR(JsonStFrame_buf_putc(j->sp, c));
        return JsonStError_Success;
      }

      return JsonStError_ExpectedNewValue;
  }
}


#define STR_NULL "null"
#define STR_NULL_LEN (jsonst_sizeof(STR_NULL) - 1)
#define STR_TRUE "true"
#define STR_TRUE_LEN (jsonst_sizeof(STR_TRUE) - 1)
#define STR_FALSE "false"
#define STR_FALSE_LEN (jsonst_sizeof(STR_FALSE) - 1)

// static JsonStError feed(JsonStInternal* j, const char c) __attribute((warn_unused_result));

static JsonStError
feed(JsonStInternal* j, const char c) {
  if(j->sp == NULL) {
    if(is_whitespace_char(c))
      return JsonStError_Success;

    return JsonStError_EndOfDoc;
  }

  /* EOF, with special treatment for numbers (which have no delimiters themselves).
     TODO: Maybe move to switch statement. */
  if(c == JSONST_EOF && j->sp->type != JsonStType_Number) {
    if(j->sp->type == JsonStType_Doc && j->sp->len > 0)
      j->sp = NULL;

    if(j->sp != NULL)
      return JsonStError_InvalidEof;

    return JsonStError_Success;
  }

  switch(j->sp->type) {
    case JsonStType_Doc:
      if(is_whitespace_char(c))
        return JsonStError_Success;

      if(j->sp->len > 0)
        return JsonStError_EndOfDoc;

      j->sp->len++;
      RET_ON_ERR(expect_start_value(j, c));
      return JsonStError_Success;

    case JsonStType_Array:
      if(is_whitespace_char(c))
        return JsonStError_Success;

      if(c == ']') {
        RET_ON_ERR(emit(j, JsonStType_ArrayEnd));
        jsonst_pop(j, JsonStType_Array);
        return JsonStError_Success;
      }

      RET_ON_ERR(jsonst_push(j, JsonStType_ArrayElm));
      RET_ON_ERR(expect_start_value(j, c));
      return JsonStError_Success;
    case JsonStType_ArrayElm:
      if(is_whitespace_char(c))
        return JsonStError_Success;

      if(c == ',') {
        jsonst_pop(j, JsonStType_ArrayElm);
        assert(j->sp->type == JsonStType_Array);
        j->sp->len++;
        RET_ON_ERR(jsonst_push(j, (int)jsonst_arry_elm_next));
        return JsonStError_Success;
      }

      if(c == ']') {
        jsonst_pop(j, JsonStType_ArrayElm);
        RET_ON_ERR(emit(j, JsonStType_ArrayEnd));
        jsonst_pop(j, JsonStType_Array);
        return JsonStError_Success;
      }

      /* Expected ',' or ']'. */
      return JsonStError_UnexpectedChar;
    case jsonst_arry_elm_next:
      if(is_whitespace_char(c))
        return JsonStError_Success;

      jsonst_pop(j, (int)jsonst_arry_elm_next);
      assert(j->sp->type == JsonStType_Array);
      RET_ON_ERR(jsonst_push(j, JsonStType_ArrayElm));
      RET_ON_ERR(expect_start_value(j, c));
      return JsonStError_Success;

    case JsonStType_Object:
      if(is_whitespace_char(c))
        return JsonStError_Success;

      if(c == '}') {
        if(j->sp->len > 0)
          /* We are already after the comma. */
          return JsonStError_ExpectedNewKey;

        RET_ON_ERR(emit(j, JsonStType_ObjectEnd));
        jsonst_pop(j, JsonStType_Object);
        return JsonStError_Success;
      }

      if(c == '"') {
        RET_ON_ERR(jsonst_push(j, JsonStType_ObjectKey));
        j->sp->str = jsonst_new_s8(&j->sp->a, j->config.obj_key_alloc_bytes);
        RET_OOM_IFNULL(j->sp->str.buf);
        return JsonStError_Success;
      }

      /* Expected '\"' or '}'. */
      return JsonStError_UnexpectedChar;
    case jsonst_obj_post_key:
      if(is_whitespace_char(c))
        return JsonStError_Success;

      if(c == ':') {
        jsonst_pop(j, (int)jsonst_obj_post_key);
        assert(j->sp->type == JsonStType_ObjectKey);
        RET_ON_ERR(jsonst_push(j, (int)jsonst_obj_post_sep));
        /* We now still have the key on the stack one below. */
        return JsonStError_Success;
      }

      /* Expected ':'. */
      return JsonStError_UnexpectedChar;
    case jsonst_obj_post_sep:
      if(is_whitespace_char(c))
        return JsonStError_Success;

      jsonst_pop(j, (int)jsonst_obj_post_sep);
      assert(j->sp->type == JsonStType_ObjectKey);
      RET_ON_ERR(jsonst_push(j, (int)jsonst_obj_next));
      RET_ON_ERR(expect_start_value(j, c));
      return JsonStError_Success;

    case jsonst_obj_next:
      if(is_whitespace_char(c))
        return JsonStError_Success;

      if(c == ',') {
        jsonst_pop(j, (int)jsonst_obj_next);
        jsonst_pop(j, JsonStType_ObjectKey);
        assert(j->sp->type == JsonStType_Object);
        j->sp->len++; /* Count members. */
        return JsonStError_Success;
      }

      if(c == '}') {
        jsonst_pop(j, (int)jsonst_obj_next);
        jsonst_pop(j, JsonStType_ObjectKey);
        RET_ON_ERR(emit(j, JsonStType_ObjectEnd));
        jsonst_pop(j, JsonStType_Object);
        return JsonStError_Success;
      }

      /* Expected ',' or '}'. */
      return JsonStError_UnexpectedChar;

    case JsonStType_Null:
      if(c != STR_NULL[j->sp->len])
        return JsonStError_InvalidLiteral;

      j->sp->len++;

      if(j->sp->len == STR_NULL_LEN) {
        RET_ON_ERR(emit(j, JsonStType_Null));
        jsonst_pop(j, JsonStType_Null);
      }

      return JsonStError_Success;
    case JsonStType_True:
      if(c != STR_TRUE[j->sp->len])
        return JsonStError_InvalidLiteral;

      j->sp->len++;

      if(j->sp->len == STR_TRUE_LEN) {
        RET_ON_ERR(emit(j, JsonStType_True));
        jsonst_pop(j, JsonStType_True);
      }

      return JsonStError_Success;
    case JsonStType_False:
      if(c != STR_FALSE[j->sp->len])
        return JsonStError_InvalidLiteral;

      j->sp->len++;

      if(j->sp->len == STR_FALSE_LEN) {
        RET_ON_ERR(emit(j, JsonStType_False));
        jsonst_pop(j, JsonStType_False);
      }

      return JsonStError_Success;

    case JsonStType_String:
    case JsonStType_ObjectKey:
      if(c == '"') {
        if(j->sp->type == JsonStType_String) {
          RET_ON_ERR(emit(j, JsonStType_String));
          jsonst_pop(j, JsonStType_String);
          return JsonStError_Success;
        }
        assert(j->sp->type == JsonStType_ObjectKey);
        RET_ON_ERR(emit(j, JsonStType_ObjectKey));
        RET_ON_ERR(jsonst_push(j, (int)jsonst_obj_post_key));
        /* We leave the key on the stack for now. */
        return JsonStError_Success;
      }

      if(c == '\\') {
        RET_ON_ERR(jsonst_push(j, (int)jsonst_str_escp));
        return JsonStError_Success;
      }

      /* Single byte, 0xxxxxxx. */
      if(((unsigned char)c & 0x80) == 0) {
        if(is_control_char(c))
          return JsonStError_InvalidControlChar;

        RET_ON_ERR(JsonStFrame_buf_putc(j->sp, c));
        return JsonStError_Success;
      }

      /* 2 bytes, 110xxxxx. */
      if(((unsigned char)c & 0xE0) == 0xC0) {
        RET_ON_ERR(JsonStFrame_buf_putc(j->sp, c));
        RET_ON_ERR(jsonst_push(j, (int)jsonst_str_utf8));
        j->sp->len = 1; /* Remaining bytes for this codepoint. */
        return JsonStError_Success;
      }

      /* 3 bytes, 1110xxxx. */
      if(((unsigned char)c & 0xF0) == 0xE0) {
        RET_ON_ERR(JsonStFrame_buf_putc(j->sp, c));
        RET_ON_ERR(jsonst_push(j, (int)jsonst_str_utf8));
        j->sp->len = 2; /* Remaining bytes for this codepoint. */
        return JsonStError_Success;
      }

      /* 4 bytes, 11110xxx. */
      if(((unsigned char)c & 0xF8) == 0xF0) {
        RET_ON_ERR(JsonStFrame_buf_putc(j->sp, c));
        RET_ON_ERR(jsonst_push(j, (int)jsonst_str_utf8));
        j->sp->len = 3; /* Remaining bytes for this codepoint. */
        return JsonStError_Success;
      }

      return JsonStError_InvalidUtf8Encoding;

    case jsonst_str_escp:
      if(c == 'u') {
        RET_ON_ERR(jsonst_push(j, (int)jsonst_str_escp_uhex));
        j->sp->str = jsonst_new_s8(&j->sp->a, 4);
        RET_OOM_IFNULL(j->sp->str.buf);
        return JsonStError_Success;
      }

      const char unquoted = is_quotable_char(c);

      if(unquoted != 0) {
        /* Write to underlying string buffer. */
        assert(j->sp->prev->type == JsonStType_String || j->sp->prev->type == JsonStType_ObjectKey);
        RET_ON_ERR(JsonStFrame_buf_putc(j->sp->prev, unquoted));
        jsonst_pop(j, (int)jsonst_str_escp);
        return JsonStError_Success;
      }

      return JsonStError_InvalidQuotedChar;

    case jsonst_str_utf8:
      /* Check that conforms to 10xxxxxx. */
      if(((unsigned char)c & 0xC0) != 0x80)
        return JsonStError_InvalidUtf8Encoding;

      /* Write to underlying string buffer. */
      assert(j->sp->prev->type == JsonStType_String || j->sp->prev->type == JsonStType_ObjectKey);
      RET_ON_ERR(JsonStFrame_buf_putc(j->sp->prev, c));

      j->sp->len--;
      if(j->sp->len == 0)
        jsonst_pop(j, (int)jsonst_str_utf8);

      return JsonStError_Success;

    case jsonst_str_escp_uhex:
      assert(j->sp->prev->type == jsonst_str_escp);
      assert(j->sp->prev->prev->type == JsonStType_String || j->sp->prev->prev->type == JsonStType_ObjectKey);
      if(!is_xdigit_char(c))
        return JsonStError_InvalidHexDigit;

      RET_ON_ERR(JsonStFrame_buf_putc(j->sp, c));
      if(j->sp->len < 4)
        return JsonStError_Success;

      const uint32_t n = parse_hex4(j->sp->str);

      if(is_utf16_high_surrogate(n)) {
        RET_ON_ERR(jsonst_push(j, (int)jsonst_str_escp_uhex_utf16));
        j->sp->str = jsonst_new_s8(&j->sp->a, 4);
        RET_OOM_IFNULL(j->sp->str.buf);
        j->sp->len = -2; /* Eat the \u sequence */
      } else {
        RET_ON_ERR(utf8_encode(j->sp->prev->prev, n));
        jsonst_pop(j, (int)jsonst_str_escp_uhex);
        jsonst_pop(j, (int)jsonst_str_escp);
      }

      return JsonStError_Success;

    case jsonst_str_escp_uhex_utf16:
      assert(j->sp->prev->type == jsonst_str_escp_uhex);
      assert(j->sp->prev->prev->type == jsonst_str_escp);
      assert(j->sp->prev->prev->prev->type == JsonStType_String || j->sp->prev->prev->prev->type == JsonStType_ObjectKey);

      if(j->sp->len == -2 && c == '\\') {
        j->sp->len++;
        return JsonStError_Success;
      }

      if(j->sp->len == -1 && c == 'u') {
        j->sp->len++;
        return JsonStError_Success;
      }

      if(j->sp->len < 0)
        return JsonStError_InvalidUtf16Surrogate;

      if(!is_xdigit_char(c))
        return JsonStError_InvalidHexDigit;

      RET_ON_ERR(JsonStFrame_buf_putc(j->sp, c));
      if(j->sp->len < 4)
        return JsonStError_Success;

      {
        const uint32_t low = parse_hex4(j->sp->str);
        if(!is_utf16_low_surrogate(low))
          return JsonStError_InvalidUtf16Surrogate;

        const uint32_t high = parse_hex4(j->sp->prev->str);
        assert(is_utf16_high_surrogate(high));

        const uint32_t decoded = ((high - 0xd800) << 10 | (low - 0xdc00)) + 0x10000;

        RET_ON_ERR(utf8_encode(j->sp->prev->prev->prev, decoded));
        jsonst_pop(j, (int)jsonst_str_escp_uhex_utf16);
        jsonst_pop(j, (int)jsonst_str_escp_uhex);
        jsonst_pop(j, (int)jsonst_str_escp);
        return JsonStError_Success;
      }

    case JsonStType_Number:
      if(!is_num(c)) {
        RET_ON_ERR(emit(j, JsonStType_Number));
        jsonst_pop(j, JsonStType_Number);

        /* Numbers are the only type delimited by a char which already belongs to the next
         * token. Thus, we have feed that token to the parser again.
         */
        RET_ON_ERR(feed(j, c));

        return JsonStError_Success;
      }

      RET_ON_ERR(JsonStFrame_buf_putc(j->sp, c));
      return JsonStError_Success;

    default: {
      assert(false);
      return JsonStError_InternalBug;
    }
  }
}

JsonStError
jsonst_feed(JsonSt j, const char c) {
  if(j->failed != JsonStError_Success)
    return JsonStError_PreviousError;

  const JsonStError ret = feed(j, c);

  if(ret != JsonStError_Success)
    j->failed = ret;

  return ret;
}

JsonStFeedDocRet
jsonst_feed_doc(JsonSt j, const char* doc, const size_t docsz) {
  JsonStFeedDocRet ret = {0};

  for(ret.parsed_bytes = 0; ret.parsed_bytes < docsz; ret.parsed_bytes++) {
    ret.err = jsonst_feed(j, doc[ret.parsed_bytes]);

    if(ret.err != JsonStError_Success)
      return ret;
  }

  ret.err = jsonst_feed(j, JSONST_EOF);
  return ret;
}
