#include "jsonst.h"
#include "char-utils.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#define jsonst_sizeof(x) (ptrdiff_t)sizeof(x)
#define jsonst_alignof(x) (ptrdiff_t) _Alignof(x)
#define jsonst_count(a) (jsonst_sizeof(a) / jsonst_sizeof(*(a)))
#define jsonst_length(s) (jsonst_count(s) - 1)

/* Utils - JsonSt_Arena jsonst_alloc, strings. */

/* Inspired by https://nullprogram.com/blog/2023/09/27/. */
typedef struct {
  uint8_t *beg, *end;
} JsonSt_Arena;

/*
 * Inspired by https://nullprogram.com/blog/2023/10/08/, modified to always be null byte terminated.
 * The length field is excluding the 0 byte.
 */
typedef struct {
  char* buf;
  ptrdiff_t len;
} JsonSt_S8;

#define JSONSTS8_INIT(s) \
  (JsonSt_S8) { (char*)(s), strlen((s)) }

static JsonSt_Arena
jsonst_new_arena(uint8_t* mem, const ptrdiff_t memsz) {
  JsonSt_Arena a = {0};
  a.beg = mem;
  a.end = a.beg ? a.beg + memsz : 0;
  return a;
}

/* Returns NULL on OOM. */
static __attribute((malloc, alloc_size(2, 4), alloc_align(3))) void*
jsonst_alloc(JsonSt_Arena* a, ptrdiff_t size, ptrdiff_t align, ptrdiff_t count) {
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

/* May return JsonSt_S8.buf = NULL on OOM. */
static JsonSt_S8
jsonst_new_s8(JsonSt_Arena* a, const ptrdiff_t len) {
  JsonSt_S8 s = {0};
  /* +1 is for C 0 byte interop. */
  s.buf = JSONST_NEW(a, char, len + 1);

  if(s.buf != NULL)
    s.len = len;

  return s;
}

/* Implementation structs. */

/*
 * In most places where JsonSt_Type is used, JsonSt_InternalState may also be used.
 * Thus, we need to be careful to no accidentally introduce overlaps.
 */
typedef enum {
  InternalState_ArrayElementNext = '+',

  InternalState_ObjectPostKey = ':',
  InternalState_ObjectPostSep = '-',
  InternalState_ObjectNext = ';',

  InternalState_StringUtf8 = '8',
  InternalState_StringEscape = '\\',
  InternalState_StringEscapeUhex = '%',
  InternalState_StringEscapeUhexUtf16 = '6',
} JsonSt_InternalState;

typedef struct jsonst_frame JsonSt_Frame;
struct jsonst_frame {
  JsonSt_Frame* prev;
  int type; /* Is a JsonSt_Type or JsonSt_InternalState. */
  JsonSt_Arena a;

  /*
   * Processing state, usage depends on type.
   * Some types will use len separately without str.
   */
  JsonSt_S8 str;
  ptrdiff_t len;
};

typedef struct jsonst_internal JsonSt_Internal;
typedef struct jsonst_internal {
  JsonSt_Callback cb;
  void* cb_user_data;
  JsonSt_Config config;
  JsonSt_Error failed;
  JsonSt_Frame* sp;
} JsonSt_Internal;

/* Helpers for error handling. */
#define RETURN_ON_ERR(expr) \
  { \
    const JsonSt_Error ret = (expr); \
\
    if(ret != Error_Success) \
      return ret; \
  }

#define RETURN_OOM_IFNULL(expr) \
  if((expr) == NULL) \
  return Error_Oom

/* Implementation. */

static JsonSt_Frame*
jsonst_new_frame(JsonSt_Arena a, JsonSt_Frame* prev, const JsonSt_Type type) {
  /* Allocate the JsonSt_Frame on its own copy of the JsonSt_Arena. */
  JsonSt_Frame* f = JSONST_NEW(&a, JsonSt_Frame, 1);

  if(f == NULL)
    return NULL;

  f->prev = prev;
  f->type = type;
  f->a = a;

  return f;
}

static JsonSt_Internal*
jsonst_new_internal(
    uint8_t* mem, const ptrdiff_t memsz, const JsonSt_Callback cb, void* cb_user_data, const JsonSt_Config conf) {
  JsonSt_Arena a = jsonst_new_arena(mem, memsz);
  JsonSt j = JSONST_NEW(&a, JsonSt_Internal, 1);

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

  j->failed = Error_Success;

  j->sp = jsonst_new_frame(a, NULL, Type_Doc);

  if(j->sp == NULL)
    return NULL;

  return j;
}

JsonSt
jsonst_new(void* mem, const ptrdiff_t memsz, const JsonSt_Callback cb, void* cb_user_data, const JsonSt_Config conf) {
  return jsonst_new_internal(mem, memsz, cb, cb_user_data, conf);
}

/*static JsonSt_Error frame_putc(JsonSt_Frame* f, const char c) __attribute((warn_unused_result));*/

static JsonSt_Error
frame_putc(JsonSt_Frame* f, const char c) {
  assert(f->str.buf != NULL);

  if(f->len >= f->str.len)
    return Error_StrBufferFull;

  f->str.buf[f->len] = c;
  f->len++;
  return Error_Success;
}

typedef enum {
  State_Init,
  State_IntMinus,
  State_IntZero,
  State_IntDigit,
  State_IntDot,
  State_IntFrac,
  State_ExpE,
  State_ExpSign,
  State_ExpInt,
} NumberState;

/*
 * By not directly integrating this into the parsing state machine we might waste
 * a bit of performance, but it's much easier to read.
 */
static bool
is_valid_json_number(const char* buf, const ptrdiff_t len) {
  NumberState s = State_Init;

  for(ptrdiff_t i = 0; i < len; i++) {
    const char c = buf[i];

    switch(s) {
      case State_Init: {
        if(c == '-') {
          s = State_IntMinus;
          continue;
        }

        if(c == '0') {
          s = State_IntZero;
          continue;
        }

        if(!is_digit_char(c))
          return false;

        s = State_IntDigit;
        continue;
      }

      case State_IntMinus: {
        if(c == '0') {
          s = State_IntZero;
          continue;
        }

        if(!is_digit_char(c))
          return false;

        s = State_IntDigit;
        continue;
      }

      case State_IntZero: {
        if(is_digit_char(c))
          return false;

        if(c == '.') {
          s = State_IntDot;
          continue;
        }

        if(c == 'E' || c == 'e') {
          s = State_ExpE;
          continue;
        }

        return false;
      }

      case State_IntDigit: {
        if(is_digit_char(c))
          continue;

        if(c == '.') {
          s = State_IntDot;
          continue;
        }

        if(c == 'E' || c == 'e') {
          s = State_ExpE;
          continue;
        }

        return false;
      }

      case State_IntDot: {
        if(!is_digit_char(c))
          return false;

        s = State_IntFrac;
        continue;
      }

      case State_IntFrac: {
        if(is_digit_char(c))
          continue;

        if(c != 'E' && c != 'e')
          return false;

        s = State_ExpE;
        continue;
      }

      case State_ExpE: {
        if(c == '-' || c == '+') {
          s = State_ExpSign;
          continue;
        }

        if(!is_digit_char(c) || c == '0')
          return false;

        s = State_ExpInt;
        continue;
      }

      case State_ExpSign: {
        if(!is_digit_char(c) || c == '0')
          return false;

        s = State_ExpInt;
        continue;
      }

      case State_ExpInt: {
        if(!is_digit_char(c))
          return false;
      }
    }
  }

  return s == State_IntZero || s == State_IntDigit || s == State_IntFrac || s == State_ExpInt;
}

// static JsonSt_Error jsonst_emit(const JsonSt_Internal* j, const JsonSt_Type type) __attribute((warn_unused_result));

static JsonSt_Error
jsonst_emit(const JsonSt_Internal* j, const JsonSt_Type type) {
  JsonSt_Arena scratch = j->sp->a;
  JsonSt_Value* v = JSONST_NEW(&scratch, JsonSt_Value, 1);
  RETURN_OOM_IFNULL(v);
  v->type = type;

  switch(type) {
    case Type_Number: {
      assert(j->sp->type == Type_Number);
      if(!is_valid_json_number(j->sp->str.buf, j->sp->len))
        return Error_InvalidNumber;

      v->val_str.str = j->sp->str.buf;
      v->val_str.str_len = j->sp->len;
      break;
    }

    case Type_String:
    case Type_ObjectKey: {
      assert(j->sp->type == Type_String || j->sp->type == Type_ObjectKey);
      v->val_str.str = j->sp->str.buf;
      v->val_str.str_len = j->sp->len;
      break;
    }
    default: {
      /* Nothing else to do for the others. */
      break;
    }
  }

  /* Fill in path, in reverse order of stack. */
  JsonSt_Path* p = NULL;

  for(const JsonSt_Frame* f = j->sp; f != NULL; f = f->prev) {
    switch(f->type) {
      case Type_ArrayElm:
      case Type_ObjectKey: {
        break;
      }

      default: {
        continue;
      }
    }

    JsonSt_Path* p_new = JSONST_NEW(&scratch, JsonSt_Path, 1);

    if(p_new == NULL)
      return Error_Oom;

    p_new->type = f->type;

    switch(f->type) {
      case Type_ArrayElm: {
        assert(f->prev->type == Type_Array);
        p_new->props.arry_ix = f->prev->len;
        break;
      }

      case Type_ObjectKey: {
        p_new->props.obj_key.str = f->str.buf;
        p_new->props.obj_key.str_len = f->len;
        break;
      }
    }

    if(p == NULL) {
      p = p_new;
    } else {
      p_new->next = p;
      p = p_new;
    }
  }

  j->cb(j->cb_user_data, v, p);
  return Error_Success;
}

/* Str must be of length 4, and characters must all be hex digits. */
static uint32_t
parse_hex4(const JsonSt_S8 str) {
  const ptrdiff_t len = 4;
  assert(str.len == len);

  uint32_t ret = 0;

  for(int i = 0; i < len; i++) {
    unsigned char c = str.buf[i];
    assert(is_xdigit_char(c));

    ret <<= 4;
    ret |= (uint32_t)scan_fromhex(c);
  }

  return ret;
}

// static JsonSt_Error frame_utf8_encode(JsonSt_Frame* f, const uint32_t c) __attribute((warn_unused_result));

static JsonSt_Error
frame_utf8_encode(JsonSt_Frame* f, const uint32_t c) {
  if(c <= 0x7f) {
    RETURN_ON_ERR(frame_putc(f, c));
    return Error_Success;
  }

  if(c <= 0x07ff) {
    RETURN_ON_ERR(frame_putc(f, (((c >> 6) & 0x1f) | 0xc0)));
    RETURN_ON_ERR(frame_putc(f, (((c >> 0) & 0x3f) | 0x80)));
    return Error_Success;
  }

  if(c <= 0xffff) {
    RETURN_ON_ERR(frame_putc(f, (((c >> 12) & 0x0f) | 0xe0)));
    RETURN_ON_ERR(frame_putc(f, (((c >> 6) & 0x3f) | 0x80)));
    RETURN_ON_ERR(frame_putc(f, (((c >> 0) & 0x3f) | 0x80)));
    return Error_Success;
  }

  if(c <= 0x10ffff) {
    RETURN_ON_ERR(frame_putc(f, (((c >> 18) & 0x07) | 0xf0)));
    RETURN_ON_ERR(frame_putc(f, (((c >> 12) & 0x3f) | 0x80)));
    RETURN_ON_ERR(frame_putc(f, (((c >> 6) & 0x3f) | 0x80)));
    RETURN_ON_ERR(frame_putc(f, (((c >> 0) & 0x3f) | 0x80)));
    return Error_Success;
  }

  return Error_InvalidUnicodeCodepoint;
}

// static JsonSt_Error jsonst_push(JsonSt_Internal* j, const JsonSt_Type type) __attribute((warn_unused_result));

static JsonSt_Error
jsonst_push(JsonSt_Internal* j, const JsonSt_Type type) {
  if((j->sp = jsonst_new_frame(j->sp->a, j->sp, type)) == NULL)
    return Error_Oom;

  return Error_Success;
}

static void
jsonst_pop(JsonSt_Internal* j, __attribute((unused)) const JsonSt_Type expect_type) {
  assert(j->sp != NULL);
  assert(j->sp->type == (int)expect_type);

  j->sp = j->sp->prev;
}

// static JsonSt_Error expect_start_value(JsonSt_Internal* j, const char c) __attribute((warn_unused_result));

static JsonSt_Error
expect_start_value(JsonSt_Internal* j, const char c) {
  switch(c) {
    case '[': {
      RETURN_ON_ERR(jsonst_push(j, Type_Array));
      RETURN_ON_ERR(jsonst_emit(j, Type_Array));
      return Error_Success;
    }

    case '{': {
      RETURN_ON_ERR(jsonst_push(j, Type_Object));
      RETURN_ON_ERR(jsonst_emit(j, Type_Object));
      return Error_Success;
    }

    case 'n': {
      RETURN_ON_ERR(jsonst_push(j, Type_Null));
      j->sp->len++;
      return Error_Success;
    }

    case 't': {
      RETURN_ON_ERR(jsonst_push(j, Type_True));
      j->sp->len++;
      return Error_Success;
    }

    case 'f': {
      RETURN_ON_ERR(jsonst_push(j, Type_False));
      j->sp->len++;
      return Error_Success;
    }

    case '"': {
      RETURN_ON_ERR(jsonst_push(j, Type_String));
      j->sp->str = jsonst_new_s8(&j->sp->a, j->config.str_alloc_bytes);
      RETURN_OOM_IFNULL(j->sp->str.buf);
      return Error_Success;
    }
    default: {
      if(is_digit_char(c) || c == '-') {
        RETURN_ON_ERR(jsonst_push(j, Type_Number));
        j->sp->str = jsonst_new_s8(&j->sp->a, j->config.num_alloc_bytes);
        RETURN_OOM_IFNULL(j->sp->str.buf);
        RETURN_ON_ERR(frame_putc(j->sp, c));
        return Error_Success;
      }

      return Error_ExpectedNewValue;
    }
  }
}

#define STR_NULL "null"
#define STR_NULL_LEN (jsonst_sizeof(STR_NULL) - 1)
#define STR_TRUE "true"
#define STR_TRUE_LEN (jsonst_sizeof(STR_TRUE) - 1)
#define STR_FALSE "false"
#define STR_FALSE_LEN (jsonst_sizeof(STR_FALSE) - 1)

// static JsonSt_Error feed(JsonSt_Internal* j, const char c) __attribute((warn_unused_result));

static JsonSt_Error
feed(JsonSt_Internal* j, const char c) {
  if(j->sp == NULL) {
    if(is_whitespace_char(c))
      return Error_Success;

    return Error_EndOfDoc;
  }

  /* EOF, with special treatment for numbers (which have no delimiters themselves).
     TODO: Maybe move to switch statement. */
  if(c == JSONST_EOF && j->sp->type != Type_Number) {
    if(j->sp->type == Type_Doc && j->sp->len > 0)
      j->sp = NULL;

    if(j->sp != NULL)
      return Error_InvalidEof;

    return Error_Success;
  }

  switch(j->sp->type) {
    case Type_Doc: {
      if(is_whitespace_char(c))
        return Error_Success;

      if(j->sp->len > 0)
        return Error_EndOfDoc;

      j->sp->len++;
      RETURN_ON_ERR(expect_start_value(j, c));
      return Error_Success;
    }

    case Type_Array: {
      if(is_whitespace_char(c))
        return Error_Success;

      if(c == ']') {
        RETURN_ON_ERR(jsonst_emit(j, Type_ArrayEnd));
        jsonst_pop(j, Type_Array);
        return Error_Success;
      }

      RETURN_ON_ERR(jsonst_push(j, Type_ArrayElm));
      RETURN_ON_ERR(expect_start_value(j, c));
      return Error_Success;
    }

    case Type_ArrayElm: {
      if(is_whitespace_char(c))
        return Error_Success;

      if(c == ',') {
        jsonst_pop(j, Type_ArrayElm);
        assert(j->sp->type == Type_Array);
        j->sp->len++;
        RETURN_ON_ERR(jsonst_push(j, (int)InternalState_ArrayElementNext));
        return Error_Success;
      }

      if(c == ']') {
        jsonst_pop(j, Type_ArrayElm);
        RETURN_ON_ERR(jsonst_emit(j, Type_ArrayEnd));
        jsonst_pop(j, Type_Array);
        return Error_Success;
      }

      /* Expected ',' or ']'. */
      return Error_UnexpectedChar;
    }

    case InternalState_ArrayElementNext: {
      if(is_whitespace_char(c))
        return Error_Success;

      jsonst_pop(j, (int)InternalState_ArrayElementNext);
      assert(j->sp->type == Type_Array);
      RETURN_ON_ERR(jsonst_push(j, Type_ArrayElm));
      RETURN_ON_ERR(expect_start_value(j, c));
      return Error_Success;
    }

    case Type_Object: {
      if(is_whitespace_char(c))
        return Error_Success;

      if(c == '}') {
        if(j->sp->len > 0)
          /* We are already after the comma. */
          return Error_ExpectedNewKey;

        RETURN_ON_ERR(jsonst_emit(j, Type_ObjectEnd));
        jsonst_pop(j, Type_Object);
        return Error_Success;
      }

      if(c == '"') {
        RETURN_ON_ERR(jsonst_push(j, Type_ObjectKey));
        j->sp->str = jsonst_new_s8(&j->sp->a, j->config.obj_key_alloc_bytes);
        RETURN_OOM_IFNULL(j->sp->str.buf);
        return Error_Success;
      }

      /* Expected '\"' or '}'. */
      return Error_UnexpectedChar;
    }

    case InternalState_ObjectPostKey: {
      if(is_whitespace_char(c))
        return Error_Success;

      if(c == ':') {
        jsonst_pop(j, (int)InternalState_ObjectPostKey);
        assert(j->sp->type == Type_ObjectKey);
        RETURN_ON_ERR(jsonst_push(j, (int)InternalState_ObjectPostSep));
        /* We now still have the key on the stack one below. */
        return Error_Success;
      }

      /* Expected ':'. */
      return Error_UnexpectedChar;
    }

    case InternalState_ObjectPostSep: {
      if(is_whitespace_char(c))
        return Error_Success;

      jsonst_pop(j, (int)InternalState_ObjectPostSep);
      assert(j->sp->type == Type_ObjectKey);
      RETURN_ON_ERR(jsonst_push(j, (int)InternalState_ObjectNext));
      RETURN_ON_ERR(expect_start_value(j, c));
      return Error_Success;
    }

    case InternalState_ObjectNext: {
      if(is_whitespace_char(c))
        return Error_Success;

      if(c == ',') {
        jsonst_pop(j, (int)InternalState_ObjectNext);
        jsonst_pop(j, Type_ObjectKey);
        assert(j->sp->type == Type_Object);
        j->sp->len++; /* Count members. */
        return Error_Success;
      }

      if(c == '}') {
        jsonst_pop(j, (int)InternalState_ObjectNext);
        jsonst_pop(j, Type_ObjectKey);
        RETURN_ON_ERR(jsonst_emit(j, Type_ObjectEnd));
        jsonst_pop(j, Type_Object);
        return Error_Success;
      }

      /* Expected ',' or '}'. */
      return Error_UnexpectedChar;
    }

    case Type_Null: {
      if(c != STR_NULL[j->sp->len])
        return Error_InvalidLiteral;

      j->sp->len++;

      if(j->sp->len == STR_NULL_LEN) {
        RETURN_ON_ERR(jsonst_emit(j, Type_Null));
        jsonst_pop(j, Type_Null);
      }

      return Error_Success;
    }

    case Type_True: {
      if(c != STR_TRUE[j->sp->len])
        return Error_InvalidLiteral;

      j->sp->len++;

      if(j->sp->len == STR_TRUE_LEN) {
        RETURN_ON_ERR(jsonst_emit(j, Type_True));
        jsonst_pop(j, Type_True);
      }

      return Error_Success;
    }

    case Type_False: {
      if(c != STR_FALSE[j->sp->len])
        return Error_InvalidLiteral;

      j->sp->len++;

      if(j->sp->len == STR_FALSE_LEN) {
        RETURN_ON_ERR(jsonst_emit(j, Type_False));
        jsonst_pop(j, Type_False);
      }

      return Error_Success;
    }

    case Type_String: {
    }

    case Type_ObjectKey: {
      if(c == '"') {
        if(j->sp->type == Type_String) {
          RETURN_ON_ERR(jsonst_emit(j, Type_String));
          jsonst_pop(j, Type_String);
          return Error_Success;
        }
        assert(j->sp->type == Type_ObjectKey);
        RETURN_ON_ERR(jsonst_emit(j, Type_ObjectKey));
        RETURN_ON_ERR(jsonst_push(j, (int)InternalState_ObjectPostKey));
        /* We leave the key on the stack for now. */
        return Error_Success;
      }

      if(c == '\\') {
        RETURN_ON_ERR(jsonst_push(j, (int)InternalState_StringEscape));
        return Error_Success;
      }

      /* Single byte, 0xxxxxxx. */
      if(((unsigned char)c & 0x80) == 0) {
        if(is_control_char(c))
          return Error_InvalidControlChar;

        RETURN_ON_ERR(frame_putc(j->sp, c));
        return Error_Success;
      }

      /* 2 bytes, 110xxxxx. */
      if(((unsigned char)c & 0xe0) == 0xc0) {
        RETURN_ON_ERR(frame_putc(j->sp, c));
        RETURN_ON_ERR(jsonst_push(j, (int)InternalState_StringUtf8));
        j->sp->len = 1; /* Remaining bytes for this codepoint. */
        return Error_Success;
      }

      /* 3 bytes, 1110xxxx. */
      if(((unsigned char)c & 0xf0) == 0xe0) {
        RETURN_ON_ERR(frame_putc(j->sp, c));
        RETURN_ON_ERR(jsonst_push(j, (int)InternalState_StringUtf8));
        j->sp->len = 2; /* Remaining bytes for this codepoint. */
        return Error_Success;
      }

      /* 4 bytes, 11110xxx. */
      if(((unsigned char)c & 0xf8) == 0xf0) {
        RETURN_ON_ERR(frame_putc(j->sp, c));
        RETURN_ON_ERR(jsonst_push(j, (int)InternalState_StringUtf8));
        j->sp->len = 3; /* Remaining bytes for this codepoint. */
        return Error_Success;
      }

      return Error_InvalidUtf8Encoding;
    }

    case InternalState_StringEscape: {
      if(c == 'u') {
        RETURN_ON_ERR(jsonst_push(j, (int)InternalState_StringEscapeUhex));
        j->sp->str = jsonst_new_s8(&j->sp->a, 4);
        RETURN_OOM_IFNULL(j->sp->str.buf);
        return Error_Success;
      }

      const char unquoted = is_quotable_char(c);

      if(unquoted != 0) {
        /* Write to underlying string buffer. */
        assert(j->sp->prev->type == Type_String || j->sp->prev->type == Type_ObjectKey);
        RETURN_ON_ERR(frame_putc(j->sp->prev, unquoted));
        jsonst_pop(j, (int)InternalState_StringEscape);
        return Error_Success;
      }

      return Error_InvalidQuotedChar;
    }

    case InternalState_StringUtf8: {
      /* Check that conforms to 10xxxxxx. */
      if(((unsigned char)c & 0xc0) != 0x80)
        return Error_InvalidUtf8Encoding;

      /* Write to underlying string buffer. */
      assert(j->sp->prev->type == Type_String || j->sp->prev->type == Type_ObjectKey);
      RETURN_ON_ERR(frame_putc(j->sp->prev, c));

      j->sp->len--;
      if(j->sp->len == 0)
        jsonst_pop(j, (int)InternalState_StringUtf8);

      return Error_Success;
    }

    case InternalState_StringEscapeUhex: {
      assert(j->sp->prev->type == InternalState_StringEscape);
      assert(j->sp->prev->prev->type == Type_String || j->sp->prev->prev->type == Type_ObjectKey);
      if(!is_xdigit_char(c))
        return Error_InvalidHexDigit;

      RETURN_ON_ERR(frame_putc(j->sp, c));
      if(j->sp->len < 4)
        return Error_Success;

      const uint32_t n = parse_hex4(j->sp->str);

      if(is_utf16_high_surrogate(n)) {
        RETURN_ON_ERR(jsonst_push(j, (int)InternalState_StringEscapeUhexUtf16));
        j->sp->str = jsonst_new_s8(&j->sp->a, 4);
        RETURN_OOM_IFNULL(j->sp->str.buf);
        j->sp->len = -2; /* Eat the \u sequence */
      } else {
        RETURN_ON_ERR(frame_utf8_encode(j->sp->prev->prev, n));
        jsonst_pop(j, (int)InternalState_StringEscapeUhex);
        jsonst_pop(j, (int)InternalState_StringEscape);
      }

      return Error_Success;
    }

    case InternalState_StringEscapeUhexUtf16: {
      assert(j->sp->prev->type == InternalState_StringEscapeUhex);
      assert(j->sp->prev->prev->type == InternalState_StringEscape);
      assert(j->sp->prev->prev->prev->type == Type_String || j->sp->prev->prev->prev->type == Type_ObjectKey);

      if(j->sp->len == -2 && c == '\\') {
        j->sp->len++;
        return Error_Success;
      }

      if(j->sp->len == -1 && c == 'u') {
        j->sp->len++;
        return Error_Success;
      }

      if(j->sp->len < 0)
        return Error_InvalidUtf16Surrogate;

      if(!is_xdigit_char(c))
        return Error_InvalidHexDigit;

      RETURN_ON_ERR(frame_putc(j->sp, c));
      if(j->sp->len < 4)
        return Error_Success;

      {
        const uint32_t low = parse_hex4(j->sp->str);
        if(!is_utf16_low_surrogate(low))
          return Error_InvalidUtf16Surrogate;

        const uint32_t high = parse_hex4(j->sp->prev->str);
        assert(is_utf16_high_surrogate(high));

        const uint32_t decoded = ((high - 0xd800) << 10 | (low - 0xdc00)) + 0x10000;

        RETURN_ON_ERR(frame_utf8_encode(j->sp->prev->prev->prev, decoded));
        jsonst_pop(j, (int)InternalState_StringEscapeUhexUtf16);
        jsonst_pop(j, (int)InternalState_StringEscapeUhex);
        jsonst_pop(j, (int)InternalState_StringEscape);
        return Error_Success;
      }
    }

    case Type_Number: {
      if(!is_number_char(c)) {
        RETURN_ON_ERR(jsonst_emit(j, Type_Number));
        jsonst_pop(j, Type_Number);

        /* Numbers are the only type delimited by a char which already belongs to the next
         * token. Thus, we have feed that token to the parser again.
         */
        RETURN_ON_ERR(feed(j, c));

        return Error_Success;
      }

      RETURN_ON_ERR(frame_putc(j->sp, c));
      return Error_Success;
    }
    default: {
      assert(false);
      return Error_InternalBug;
    }
  }
}

JsonSt_Error
jsonst_feed(JsonSt j, const char c) {
  if(j->failed != Error_Success)
    return Error_PreviousError;

  const JsonSt_Error ret = feed(j, c);

  if(ret != Error_Success)
    j->failed = ret;

  return ret;
}

JsonSt_FeedDocRet
jsonst_feed_doc(JsonSt j, const char* doc, const size_t docsz) {
  JsonSt_FeedDocRet ret = {0};

  for(ret.parsed_bytes = 0; ret.parsed_bytes < docsz; ret.parsed_bytes++) {
    ret.err = jsonst_feed(j, doc[ret.parsed_bytes]);

    if(ret.err != Error_Success)
      return ret;
  }

  ret.err = jsonst_feed(j, JSONST_EOF);
  return ret;
}
