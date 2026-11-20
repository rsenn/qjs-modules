/* ywrite.c: minimal YAML writer, ywrite_*().
 * depends on: utils.h (atom strings, property enums), cutils.h (DynBuf).
 * rule: output is block style, strings are quoted only when plain would
 * read back as another type or break the syntax. */

#include "defines.h"
#include "utils.h"
#include "yaml/ywrite.h"
#include <string.h>
#include <stdio.h>

/**
 * \addtogroup ywrite
 * @{
 */

static void
ywrite_indent(DynBuf* db, int width, int depth) {
  for(int n = width * depth; n > 0; n--)
    dbuf_putc(db, ' ');
}

static BOOL
ywrite_scalar_is_number(const char* s, size_t len) {
  size_t i = 0;

  if(i < len && (s[i] == '+' || s[i] == '-'))
    i++;

  if(i >= len)
    return FALSE;

  BOOL digits = FALSE;

  for(; i < len && s[i] >= '0' && s[i] <= '9'; i++)
    digits = TRUE;

  if(i < len && s[i] == '.') {
    i++;

    for(; i < len && s[i] >= '0' && s[i] <= '9'; i++)
      digits = TRUE;
  }

  if(!digits)
    return FALSE;

  if(i < len && (s[i] == 'e' || s[i] == 'E')) {
    i++;

    if(i < len && (s[i] == '+' || s[i] == '-'))
      i++;

    if(i >= len || s[i] < '0' || s[i] > '9')
      return FALSE;

    for(; i < len && s[i] >= '0' && s[i] <= '9'; i++) {
    }
  }

  return i == len;
}

static BOOL
ywrite_scalar_is_keyword(const char* s, size_t len) {
  static const char* const kw[] = {
      "null", "Null", "NULL", "~",  "true", "True", "TRUE", "false", "False", "FALSE", "yes",
      "Yes",  "YES",  "no",   "No", "NO",   "on",   "On",   "ON",    "off",   "Off",   "OFF",
  };

  for(size_t i = 0; i < countof(kw); i++)
    if(strlen(kw[i]) == len && !memcmp(kw[i], s, len))
      return TRUE;

  return FALSE;
}

/* YAML 1.2 core numbers that a JS number never prints as: 0x1F, 0o17, .inf, -.Inf, .nan */
static BOOL
ywrite_scalar_is_special_number(const char* s, size_t len) {
  static const char* const inf[] = {".inf", ".Inf", ".INF"}, *const nan[] = {".nan", ".NaN", ".NAN"};
  size_t i = len > 0 && (s[0] == '+' || s[0] == '-') ? 1 : 0;

  if(len > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'o'))
    return TRUE;

  if(len - i != 4)
    return FALSE;

  for(size_t k = 0; k < 3; k++)
    if(!memcmp(s + i, inf[k], 4) || (!i && !memcmp(s, nan[k], 4)))
      return TRUE;

  return FALSE;
}

static BOOL
ywrite_scalar_needs_quote(const char* s, size_t len) {
  if(len == 0)
    return TRUE;

  if(s[0] == ' ' || s[len - 1] == ' ')
    return TRUE;

  if(strchr("-?:,[]{}#&*!|>'\"%@` \t", (unsigned char)s[0]))
    return TRUE;

  for(size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];

    if(c < 0x20)
      return TRUE;

    if(c == ':' && (i + 1 == len || s[i + 1] == ' '))
      return TRUE;

    if(c == '#' && i > 0 && s[i - 1] == ' ')
      return TRUE;
  }

  if(ywrite_scalar_is_number(s, len) || ywrite_scalar_is_special_number(s, len))
    return TRUE;

  if(ywrite_scalar_is_keyword(s, len))
    return TRUE;

  return FALSE;
}

void
ywrite_quoted(DynBuf* db, const char* s, size_t len) {
  dbuf_putc(db, '"');

  for(size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];

    switch(c) {
      case '"': dbuf_put(db, (const uint8_t*)"\\\"", 2); break;
      case '\\': dbuf_put(db, (const uint8_t*)"\\\\", 2); break;
      case '\n': dbuf_put(db, (const uint8_t*)"\\n", 2); break;
      case '\t': dbuf_put(db, (const uint8_t*)"\\t", 2); break;
      case '\r': dbuf_put(db, (const uint8_t*)"\\r", 2); break;
      default: {
        if(c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\x%02x", c);
          dbuf_put(db, (const uint8_t*)buf, 4);
        } else {
          dbuf_putc(db, c);
        }

        break;
      }
    }
  }

  dbuf_putc(db, '"');
}

void
ywrite_string(DynBuf* db, const char* s, size_t len) {
  if(ywrite_scalar_needs_quote(s, len))
    ywrite_quoted(db, s, len);
  else
    dbuf_put(db, (const uint8_t*)s, len);
}

int
ywrite_scalar(JSContext* ctx, DynBuf* db, JSValueConst val) {
  if(JS_IsNull(val) || JS_IsUndefined(val)) {
    dbuf_put(db, (const uint8_t*)"null", 4);
    return 0;
  }

  if(JS_IsBool(val)) {
    BOOL b = JS_ToBool(ctx, val);
    dbuf_put(db, (const uint8_t*)(b ? "true" : "false"), b ? 4 : 5);
    return 0;
  }

  if(JS_IsNumber(val)) {
    size_t len;
    const char* s;
    double d;

    if(JS_ToFloat64(ctx, &d, val))
      return -1;

    /* YAML 1.2 spells them .nan, .inf and -.inf: the JS words would read back as strings */
    if(d != d || d - d != 0) {
      const char* word = d != d ? ".nan" : d > 0 ? ".inf" : "-.inf";

      dbuf_put(db, (const uint8_t*)word, strlen(word));
      return 0;
    }

    if(!(s = JS_ToCStringLen(ctx, &len, val)))
      return -1;

    dbuf_put(db, (const uint8_t*)s, len);
    JS_FreeCString(ctx, s);
    return 0;
  }

  if(JS_IsString(val)) {
    size_t len;
    const char* s = JS_ToCStringLen(ctx, &len, val);

    if(!s)
      return -1;

    ywrite_string(db, s, len);
    JS_FreeCString(ctx, s);
    return 0;
  }

  JS_ThrowTypeError(ctx, "yaml.write: unsupported value type");
  return -1;
}

/* exception pending: a value that contains itself would recurse until the stack is gone */
static int
ywrite_too_deep(JSContext* ctx) {
  JS_ThrowTypeError(ctx, "yaml.write: circular structure or nesting too deep");
  return -1;
}

/* an object that is written as a mapping or sequence: a function is not, it is an unsupported value */
static BOOL
ywrite_is_collection(JSContext* ctx, JSValueConst val) {
  return JS_IsObject(val) && !JS_IsFunction(ctx, val);
}

static BOOL
ywrite_is_empty(JSContext* ctx, JSValueConst val, BOOL is_array) {
  if(is_array) {
    int64_t len = -1;
    JSValue lval = JS_GetPropertyStr(ctx, val, "length");

    JS_ToInt64(ctx, &len, lval);
    JS_FreeValue(ctx, lval);
    return len == 0;
  }

  JSPropertyEnum* tab;
  uint32_t len;

  if(JS_GetOwnPropertyNames(ctx, &tab, &len, val, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY))
    return TRUE;

  js_propertyenums_free(ctx, tab, len);
  return len == 0;
}

/* renders val as the value half of a key: or - line, either inline (scalar,
 * empty collection) or as an indented block starting on the next line. */
int
ywrite_node(JSContext* ctx, DynBuf* db, JSValueConst val, int indent, int depth) {
  BOOL is_array = JS_IsArray(ctx, val);

  if(ywrite_is_collection(ctx, val) && !ywrite_is_empty(ctx, val, is_array)) {
    dbuf_putc(db, '\n');
    return is_array ? ywrite_sequence(ctx, db, val, indent, depth + 1) : ywrite_mapping(ctx, db, val, indent, depth + 1);
  }

  dbuf_putc(db, ' ');

  if(ywrite_is_collection(ctx, val))
    dbuf_put(db, (const uint8_t*)(is_array ? "[]" : "{}"), 2);
  else if(ywrite_scalar(ctx, db, val))
    return -1;

  dbuf_putc(db, '\n');
  return 0;
}

int
ywrite_mapping(JSContext* ctx, DynBuf* db, JSValueConst obj, int indent, int depth) {
  JSPropertyEnum* tab;
  uint32_t len;

  if(depth > YWRITE_MAX_DEPTH)
    return ywrite_too_deep(ctx);

  if(JS_GetOwnPropertyNames(ctx, &tab, &len, obj, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY))
    return -1;

  for(uint32_t i = 0; i < len; i++) {
    size_t klen;
    const char* kstr = js_atom_to_cstringlen(ctx, &klen, tab[i].atom);
    JSValue val = JS_GetProperty(ctx, obj, tab[i].atom);

    if(!kstr || JS_IsException(val)) {
      if(kstr)
        JS_FreeCString(ctx, kstr);

      JS_FreeValue(ctx, val);
      js_propertyenums_free(ctx, tab, len);
      return -1;
    }

    ywrite_indent(db, indent, depth);
    ywrite_string(db, kstr, klen);
    dbuf_putc(db, ':');

    int r = ywrite_node(ctx, db, val, indent, depth);

    JS_FreeCString(ctx, kstr);
    JS_FreeValue(ctx, val);

    if(r) {
      js_propertyenums_free(ctx, tab, len);
      return -1;
    }
  }

  js_propertyenums_free(ctx, tab, len);
  return 0;
}

int
ywrite_sequence(JSContext* ctx, DynBuf* db, JSValueConst arr, int indent, int depth) {
  int64_t len = 0;
  JSValue lval;

  if(depth > YWRITE_MAX_DEPTH)
    return ywrite_too_deep(ctx);

  lval = JS_GetPropertyStr(ctx, arr, "length");

  JS_ToInt64(ctx, &len, lval);
  JS_FreeValue(ctx, lval);

  for(int64_t i = 0; i < len; i++) {
    JSValue val = JS_GetPropertyUint32(ctx, arr, (uint32_t)i);

    if(JS_IsException(val))
      return -1;

    ywrite_indent(db, indent, depth);
    dbuf_putc(db, '-');

    int r = ywrite_node(ctx, db, val, indent, depth);

    JS_FreeValue(ctx, val);

    if(r)
      return -1;
  }

  return 0;
}

int
ywrite_document(JSContext* ctx, DynBuf* db, JSValueConst val, int indent) {
  BOOL is_array = JS_IsArray(ctx, val);
  int r;

  if(ywrite_is_collection(ctx, val) && !ywrite_is_empty(ctx, val, is_array)) {
    r = is_array ? ywrite_sequence(ctx, db, val, indent, 0) : ywrite_mapping(ctx, db, val, indent, 0);
  } else if(ywrite_is_collection(ctx, val)) {
    dbuf_put(db, (const uint8_t*)(is_array ? "[]\n" : "{}\n"), 3);
    r = 0;
  } else {
    r = ywrite_scalar(ctx, db, val);

    if(!r)
      dbuf_putc(db, '\n');
  }

  return r;
}

/**
 * @}
 */
