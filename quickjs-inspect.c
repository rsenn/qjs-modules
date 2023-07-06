#include "defines.h"
#include <cutils.h>
#include "iteration.h"
#include <list.h>
#include "property-enumeration.h"
#include "quickjs-internal.h"
#include "quickjs-predicate.h"
#include <quickjs.h>
#include <quickjs-config.h>
#include "utils.h"
#include "vector.h"
#include "buffer-utils.h"
#include "stream-utils.h"
#include "debug.h"

#include <ctype.h>
#include <math.h>
#include <string.h>
#include <fcntl.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#if HAVE_TERMIOS_H
#include <termios.h>
#include <sys/ioctl.h>
#endif
#endif

/*static JSClassID function_class_id_ceil = JS_CLASS_ASYNC_GENERATOR;*/

/**
 * \defgroup quickjs-inspect QuickJS module: inspect - Inspection
 * @{
 */

thread_local JSAtom inspect_custom_atom = 0, inspect_custom_atom_node = 0;
static thread_local JSValue object_tostring;

#define INT32_IN_RANGE(i) ((i) > INT32_MIN && (i) < INT32_MAX)
#define IS_COMPACT(d) (/*(opts->compact != INT32_MIN) &&*/ ((d) > opts->compact))

typedef struct {
  const char* name;
  JSAtom atom;
} PropertyKey;

typedef struct {
  unsigned colors : 1;
  unsigned show_hidden : 1;
  unsigned custom_inspect : 1;
  unsigned show_proxy : 1;
  unsigned getters : 1;
  unsigned string_break_newline : 1;
  unsigned reparseable : 1;
  int32_t depth;
  int32_t max_array_length;
  int32_t max_string_length;
  int32_t break_length;
  int32_t compact;
  int32_t proto_chain;
  int32_t number_base;
  Vector hide_keys;
  PropertyKey class_key;
} InspectOptions;

static int stdout_isatty, stderr_isatty;
static int32_t width = -1;

static int inspect_value(JSContext*, Writer*, JSValueConst, InspectOptions*, int32_t);
static int inspect_string(JSContext*, Writer*, JSValueConst, InspectOptions*, int32_t);
static int inspect_number(JSContext*, Writer*, JSValueConst, InspectOptions*, int32_t);

/*static int
regexp_predicate(int c) {
  switch(c) {
    case 8: return 'u';
    case 9: return 't';
    case 10: return 'n';
    case 11: return 'v';
    case 12: return 'f';
    case 13: return 'r';
  }

  if(c < 0x20)
    return 'u';

  return 0;
}*/

static inline int
screen_width(void) {
  if(width != -1)
    return width;

#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO csbi;

  GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
  width = csbi.srWindow.Right - csbi.srWindow.Left + 1;

#elif defined(HAVE_TERMIOS_H)
  {
    struct winsize w = {.ws_col = -1, .ws_row = -1};

    if(stdout_isatty)
      ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    else if(stderr_isatty)
      ioctl(STDERR_FILENO, TIOCGWINSZ, &w);

    width = w.ws_col;
  }
#else
  width = 80;
#endif
  return width;
}

static void
options_init(InspectOptions* opts, JSContext* ctx) {
  opts->colors = TRUE;
  opts->show_hidden = FALSE;
  opts->custom_inspect = TRUE;
  opts->show_proxy = FALSE;
  opts->getters = FALSE;
  opts->string_break_newline = FALSE;
  opts->reparseable = FALSE;
  opts->depth = INT32_MAX;
  opts->max_array_length = 300;
  opts->max_string_length = INT32_MAX;
  opts->break_length = screen_width();
  opts->compact = 5;
  opts->proto_chain = TRUE;
  opts->number_base = 10;

  vector_init(&opts->hide_keys, ctx);

  opts->class_key = (PropertyKey){0, JS_DupAtom(ctx, js_atom_from(ctx, "[Symbol.toStringTag]"))};
  opts->class_key.name = JS_AtomToCString(ctx, opts->class_key.atom);

  vector_push(&opts->hide_keys, opts->class_key);
}

static void
options_free(InspectOptions* opts, JSContext* ctx) {

  PropertyKey* key;

  vector_foreach_t(&opts->hide_keys, key) {
    JS_FreeAtom(ctx, key->atom);
    js_cstring_free(ctx, key->name);
  }
  vector_free(&opts->hide_keys);
}

static void
options_get(InspectOptions* opts, JSContext* ctx, JSValueConst object) {
  JSValue value;

  value = JS_GetPropertyStr(ctx, object, "colors");

  if(!JS_IsException(value) && !JS_IsUndefined(value)) {
    opts->colors = JS_ToBool(ctx, value);
    JS_FreeValue(ctx, value);
  }

  value = JS_GetPropertyStr(ctx, object, "showHidden");

  if(!JS_IsException(value) && !JS_IsUndefined(value)) {
    opts->show_hidden = JS_ToBool(ctx, value);
    JS_FreeValue(ctx, value);
  }

  value = JS_GetPropertyStr(ctx, object, "customInspect");

  if(!JS_IsException(value) && !JS_IsUndefined(value)) {
    opts->custom_inspect = JS_ToBool(ctx, value);
    JS_FreeValue(ctx, value);
  }

  value = JS_GetPropertyStr(ctx, object, "showProxy");

  if(!JS_IsException(value) && !JS_IsUndefined(value)) {
    opts->show_proxy = JS_ToBool(ctx, value);
    JS_FreeValue(ctx, value);
  }

  value = JS_GetPropertyStr(ctx, object, "getters");

  if(!JS_IsException(value) && !JS_IsUndefined(value)) {
    opts->getters = JS_ToBool(ctx, value);
    JS_FreeValue(ctx, value);
  }

  value = JS_GetPropertyStr(ctx, object, "stringBreakNewline");

  if(!JS_IsException(value) && !JS_IsUndefined(value)) {
    opts->string_break_newline = JS_ToBool(ctx, value);
    JS_FreeValue(ctx, value);
  }

  value = JS_GetPropertyStr(ctx, object, "reparseable");

  if(!JS_IsException(value) && !JS_IsUndefined(value)) {
    opts->reparseable = JS_ToBool(ctx, value);
    JS_FreeValue(ctx, value);
  }

  value = JS_GetPropertyStr(ctx, object, "depth");

  if(!JS_IsUndefined(value) && !JS_IsException(value)) {
    if(JS_VALUE_GET_TAG(value) == JS_TAG_FLOAT64 && isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->depth = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->depth, value);

    JS_FreeValue(ctx, value);
  }

  value = JS_GetPropertyStr(ctx, object, "maxArrayLength");

  if(!JS_IsUndefined(value) && !JS_IsException(value)) {
    if(JS_VALUE_GET_TAG(value) == JS_TAG_FLOAT64 && isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->max_array_length = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->max_array_length, value);

    JS_FreeValue(ctx, value);
  }

  value = JS_GetPropertyStr(ctx, object, "maxStringLength");

  if(!JS_IsUndefined(value) && !JS_IsException(value)) {
    if(JS_VALUE_GET_TAG(value) == JS_TAG_FLOAT64 && isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->max_string_length = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->max_string_length, value);

    JS_FreeValue(ctx, value);
  }

  value = JS_GetPropertyStr(ctx, object, "breakLength");

  if(!JS_IsUndefined(value) && !JS_IsException(value)) {
    if(JS_VALUE_GET_TAG(value) == JS_TAG_FLOAT64 && isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->break_length = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->break_length, value);

    JS_FreeValue(ctx, value);
  }

  value = JS_GetPropertyStr(ctx, object, "compact");

  if(!JS_IsUndefined(value) && !JS_IsException(value)) {
    if(JS_VALUE_GET_TAG(value) == JS_TAG_BOOL)
      opts->compact = JS_VALUE_GET_BOOL(value) == FALSE ? INT32_MAX : INT32_MIN;
    else if(JS_VALUE_GET_TAG(value) == JS_TAG_FLOAT64 && isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->compact = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->compact, value);

    JS_FreeValue(ctx, value);
  }

  value = JS_GetPropertyStr(ctx, object, "hideKeys");

  if(js_is_array(ctx, value)) {
    int64_t len, pos;
    len = js_array_length(ctx, value);

    for(pos = 0; pos < len; pos++) {
      JSValue item = JS_GetPropertyUint32(ctx, value, pos);
      PropertyKey key;
      key.name = JS_VALUE_GET_TAG(item) == JS_TAG_SYMBOL ? 0 : JS_ToCString(ctx, item);
      key.atom = JS_ValueToAtom(ctx, item);
      vector_push(&opts->hide_keys, key);
      JS_FreeValue(ctx, item);
    }

    JS_FreeValue(ctx, value);
  }

  value = JS_GetPropertyStr(ctx, object, "protoChain");
  JS_ToInt32(ctx, &opts->proto_chain, value);
  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, object, "numberBase");

  JS_ToInt32(ctx, &opts->number_base, value);
  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, object, "classKey");

  opts->class_key.atom = JS_ValueToAtom(ctx, value);
  opts->class_key.name = JS_AtomToCString(ctx, opts->class_key.atom);

  JS_FreeValue(ctx, value);
}

static JSValue
options_object(InspectOptions* opts, JSContext* ctx) {
  JSValue arr, ret = JS_NewObject(ctx);
  uint32_t n;
  PropertyKey* key;

  JS_SetPropertyStr(ctx, ret, "colors", JS_NewBool(ctx, opts->colors));
  JS_SetPropertyStr(ctx, ret, "showHidden", JS_NewBool(ctx, opts->show_hidden));
  JS_SetPropertyStr(ctx, ret, "customInspect", JS_NewBool(ctx, opts->custom_inspect));
  JS_SetPropertyStr(ctx, ret, "showProxy", JS_NewBool(ctx, opts->show_proxy));
  JS_SetPropertyStr(ctx, ret, "getters", JS_NewBool(ctx, opts->getters));
  JS_SetPropertyStr(ctx, ret, "stringBreakNewline", JS_NewBool(ctx, opts->string_break_newline));
  JS_SetPropertyStr(ctx, ret, "reparseable", JS_NewBool(ctx, opts->reparseable));
  JS_SetPropertyStr(ctx, ret, "depth", js_number_new(ctx, opts->depth));
  JS_SetPropertyStr(ctx, ret, "maxArrayLength", js_number_new(ctx, opts->max_array_length));
  JS_SetPropertyStr(ctx, ret, "maxStringLength", js_number_new(ctx, opts->max_string_length));
  JS_SetPropertyStr(ctx, ret, "breakLength", js_number_new(ctx, opts->break_length));
  JS_SetPropertyStr(ctx, ret, "compact", js_new_bool_or_number(ctx, opts->compact));

  if(opts->proto_chain)
    JS_SetPropertyStr(ctx, ret, "protoChain", js_number_new(ctx, opts->proto_chain));

  arr = JS_NewArray(ctx);
  n = 0;

  vector_foreach_t(&opts->hide_keys, key) { JS_SetPropertyUint32(ctx, arr, n++, js_atom_tovalue(ctx, key->atom)); }

  JS_SetPropertyStr(ctx, ret, "hideKeys", arr);
  JS_SetPropertyStr(ctx, ret, "numberBase", js_number_new(ctx, opts->number_base));
  JS_SetPropertyStr(ctx, ret, "classKey", JS_AtomToValue(ctx, opts->class_key.atom));

  return ret;
}

static int
options_hidden(InspectOptions* opts, JSAtom atom) {
  PropertyKey* key;

  vector_foreach_t(&opts->hide_keys, key) {
    if(key->atom == atom) //! strcmp(key->name, str))
      return 1;
  }

  return 0;
}

static void
put_newline(Writer* wr, int32_t depth) {
  writer_putc(wr, '\n');

  while(depth-- > 0)
    writer_puts(wr, "  ");
}

static void
put_spacing(Writer* wr, const InspectOptions* opts, int32_t depth) {
  if(IS_COMPACT(depth))
    writer_putc(wr, ' ');
  else
    put_newline(wr, depth);
}

static void
put_escaped(Writer* wr, const char* str, size_t len) {
  char buf[FMT_ULONG];
  size_t i = 0;
  const uint8_t *pos, *end, *next;
  static const uint8_t table[256] = {
      'x', 'x', 'x',  'x', 'x', 'x', 'x', 'x', 'b', 't', 'n', 'v', 'f', 'r', 'x', 'x', 'x', 'x', 'x',  'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 0,   0,   0,   0,   0,
      0,   0,   '\'', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   '\\', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   'x', 0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,    0,   0,   0,   'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u',  'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u',
      'u', 0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
  };

  for(pos = (const uint8_t*)str, end = pos + len; pos < end; pos = next) {
    size_t clen;
    int32_t c;
    uint8_t r, ch;

    if((c = unicode_from_utf8(pos, end - pos, &next)) < 0)
      break;

    clen = next - pos;
    ch = c;
    r = c > 0xff ? 'u' : table[c];

    if(clen > 2 || c > 0xffff)
      r = 'u';

    if(r == 'u' && clen > 1 && (c & 0xff) == 0) {
      r = 'x';
      ch = c >> 8;
    }

    if(c == 0x1b) {
      writer_puts(wr, "\\x1b");
    } else if(r == 'u') {
      writer_puts(wr, c > 0xffff ? "\\u{" : "\\u");
      writer_write(wr, buf, fmt_xlong0(buf, c, 4));
      writer_puts(wr, c > 0xffff ? "}" : "");
    } else if(r == 'x') {
      writer_puts(wr, "\\x");
      writer_write(wr, buf, fmt_xlong0(buf, c, 2));
    } else if(r) {
      writer_putc(wr, '\\');
      writer_putc(wr, (r > 1 && r <= 127) ? r : (c = escape_char_letter(ch)) ? c : ch);
    } else {
      writer_write(wr, pos, next - pos);
    }

    i++;
  }
}

/*static JSAtom
js_inspect_custom_atom(JSContext* ctx, const char* sym_for) {
  JSValue key = JS_NewString(ctx, sym_for ? sym_for : "quickjs.inspect.custom");
  JSValue sym = js_symbol_invoke_static(ctx, "for", key);
  JS_FreeValue(ctx, key);
  JSAtom atom = JS_ValueToAtom(ctx, sym);
  JS_FreeValue(ctx, sym);

  return atom;
}*/

static JSValue
inspect_custom(JSContext* ctx, JSValueConst obj, InspectOptions* opts, int32_t level) {
  JSValue ret = JS_UNDEFINED, inspect = JS_UNDEFINED;

  if(JS_HasProperty(ctx, obj, inspect_custom_atom))
    inspect = JS_GetProperty(ctx, obj, inspect_custom_atom);
  else if(JS_HasProperty(ctx, obj, inspect_custom_atom_node))
    inspect = JS_GetProperty(ctx, obj, inspect_custom_atom_node);

  if(JS_IsFunction(ctx, inspect)) {
    JSValueConst args[2];
    InspectOptions opts_nocustom;

    memcpy(&opts_nocustom, opts, sizeof(InspectOptions));
    opts_nocustom.custom_inspect = FALSE;

    args[0] = js_number_new(ctx, level + 1);
    args[1] = options_object(&opts_nocustom, ctx);

    ret = JS_Call(ctx, inspect, obj, 2, args);

    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
  }

  JS_FreeValue(ctx, inspect);
  return ret;
}

static int
inspect_date(JSContext* ctx, Writer* wr, JSValueConst obj, InspectOptions* opts, int32_t depth) {
  const char* str;
  JSValue date = js_invoke(ctx, obj, "toISOString", 0, 0);

  if((str = JS_ToCString(ctx, date))) {
    if(opts->reparseable)
      writer_puts(wr, "new Date('");
    else if(opts->colors)
      writer_puts(wr, COLOR_PURPLE);

    writer_puts(wr, str);

    if(opts->reparseable)
      writer_puts(wr, "')");
    else if(opts->colors)
      writer_puts(wr, COLOR_NONE);

    JS_FreeCString(ctx, str);
  }

  JS_FreeValue(ctx, date);
  return 1;
}

static int
inspect_map(JSContext* ctx, Writer* wr, JSValueConst obj, InspectOptions* opts, int32_t level) {
  BOOL ret, finish = FALSE;
  size_t i = 0;
  int32_t depth = INT32_IN_RANGE(level) ? level : 0;
  JSValue data, key, value;
  Iteration it;

  if(!(ret = iteration_method_symbol(&it, ctx, obj, "iterator"))) {
    JS_ThrowTypeError(ctx, "inspect_map tag=%d\n", JS_VALUE_GET_TAG(obj));
    return 0;
  }

  writer_puts(wr, opts->colors ? COLOR_LIGHTRED "Map" COLOR_NONE " {" : "Map {");

  if(IS_COMPACT(depth + 1))
    writer_putc(wr, ' ');
  else
    put_newline(wr, depth);

  for(i = 0; !(finish = iteration_next(&it, ctx)); i++) {
    if(!finish) {
      data = iteration_value(&it, ctx);

      if(i) {
        writer_puts(wr, ",");
        if(!IS_COMPACT(depth + 1))
          put_newline(wr, depth);
      }

      writer_puts(wr, IS_COMPACT(depth + 1) ? " " : "  ");
      key = JS_GetPropertyUint32(ctx, data, 0);
      inspect_value(ctx, wr, key, opts, depth + 1);

      writer_puts(wr, " => ");
      value = JS_GetPropertyUint32(ctx, data, 1);
      inspect_value(ctx, wr, value, opts, depth + 1);

      JS_FreeValue(ctx, key);
      JS_FreeValue(ctx, value);
      JS_FreeValue(ctx, data);
    }
  }

  if(IS_COMPACT(depth + 1))
    writer_putc(wr, ' ');
  else
    put_newline(wr, depth);

  writer_putc(wr, '}');
  iteration_reset_rt(&it, JS_GetRuntime(ctx));

  return 1;
}

static int
inspect_set(JSContext* ctx, Writer* wr, JSValueConst obj, InspectOptions* opts, int32_t level) {
  BOOL ret, finish = FALSE;
  size_t i = 0;
  int32_t depth = INT32_IN_RANGE(level) ? level : 0;
  JSValue value;
  Iteration it;

  if(!(ret = iteration_method_symbol(&it, ctx, obj, "iterator"))) {
    JS_ThrowTypeError(ctx, "inspect_map tag=%d\n", JS_VALUE_GET_TAG(obj));
    return 0;
  }

  writer_puts(wr, opts->colors ? COLOR_LIGHTRED "Set" COLOR_NONE " [" : "Set [");

  if(IS_COMPACT(depth + 1))
    writer_putc(wr, ' ');
  else
    put_newline(wr, depth);

  for(i = 0; !(finish = iteration_next(&it, ctx)); i++) {
    if(!finish) {
      value = iteration_value(&it, ctx);

      if(i) {
        writer_puts(wr, ",");
        if(!IS_COMPACT(depth + 1))
          put_newline(wr, depth);
      }

      writer_puts(wr, IS_COMPACT(depth + 1) ? " " : "  ");
      inspect_value(ctx, wr, value, opts, depth + 1);
      JS_FreeValue(ctx, value);
    }
  }

  if(IS_COMPACT(depth + 1))
    writer_putc(wr, ' ');
  else
    put_newline(wr, depth);

  writer_putc(wr, ']');
  iteration_reset_rt(&it, JS_GetRuntime(ctx));
  return 0;
}

static int
inspect_arraybuffer(JSContext* ctx, Writer* wr, JSValueConst value, InspectOptions* opts, int32_t level) {
  char buf[FMT_ULONG];
  const char *str, *str2;
  uint8_t* ptr;
  size_t i, slen, size;
  int32_t depth = INT32_IN_RANGE(level) ? level : 0;
  int column, break_len = opts->break_length;
  JSValue proto;

  break_len = (break_len + 1) / 3;
  break_len *= 3;

  if(break_len > opts->break_length)
    break_len = opts->break_length;

  ptr = JS_GetArrayBuffer(ctx, &size, value);
  proto = JS_GetPrototype(ctx, value);
  str = js_object_tostring2(ctx, object_tostring, proto);
  JS_FreeValue(ctx, proto);

  if(opts->reparseable) {
    writer_puts(wr, "new Uint8Array([");
  } else {
    if(js_is_arraybuffer(ctx, value))
      writer_puts(wr, "ArrayBuffer");
    else if(js_is_sharedarraybuffer(ctx, value))
      writer_puts(wr, "SharedArrayBuffer");
    else if(str && (str2 = strchr(str, ' '))) {
      str2++;
      slen = byte_chr(str2, strlen(str2), ']');
      writer_write(wr, str2, slen);
    }

    if(str)
      js_cstring_free(ctx, str);

    writer_puts(wr, " {");

    if(IS_COMPACT(depth + 1))
      writer_putc(wr, ' ');
    else
      put_newline(wr, depth + 2);

    writer_puts(wr, "byteLength: ");
    writer_write(wr, buf, fmt_ulong(buf, size));
    writer_puts(wr, " [");
  }

  if(IS_COMPACT(depth + 2))
    writer_putc(wr, ' ');
  else
    put_newline(wr, depth + 3);

  break_len -= (depth + 3) * 2;
  column = 0;

  for(i = 0; i < size; i++) {
    if(column + (opts->reparseable ? 6 : 3) >= break_len && opts->break_length != INT32_MAX) {
      if(opts->reparseable && i > 0)
        writer_putc(wr, ',');

      if(IS_COMPACT(depth + 2))
        writer_putc(wr, ' ');
      else
        put_newline(wr, depth + 3);

      column = 0;
    }

    if(opts->reparseable) {
      if(i > 0) {
        writer_puts(wr, column > 0 ? ", " : "");
        column += 2;
      }

      if(ptr[i] == 0) {
        writer_putc(wr, '0');
        column += 1;
      } else {
        writer_puts(wr, "0x");
        writer_write(wr, buf, fmt_xlong0(buf, ptr[i], 2));
        column += 4;
      }
    } else if(i == (size_t)opts->max_array_length) {
      break;
    } else {
      writer_puts(wr, column ? " " : "");
      writer_write(wr, buf, fmt_xlong0(buf, ptr[i], 2));

      column += column ? 3 : 2;
    }
  }

  if(opts->reparseable) {

    if(IS_COMPACT(depth + 2))
      writer_putc(wr, ' ');
    else
      put_newline(wr, depth + 2);

    writer_puts(wr, "]).buffer");
  } else {
    if(i < size) {
      if(IS_COMPACT(depth + 3))
        writer_putc(wr, ' ');
      else
        put_newline(wr, depth + 3);

      writer_puts(wr, "... ");
      writer_write(wr, buf, fmt_ulong(buf, size - i));
      writer_puts(wr, " more bytes");
    }

    if(IS_COMPACT(depth + 2))
      writer_putc(wr, ' ');
    else
      put_newline(wr, depth + 2);

    writer_puts(wr, "]");

    if(IS_COMPACT(depth + 1))
      writer_putc(wr, ' ');
    else
      put_newline(wr, depth);

    writer_puts(wr, "}");
  }

  return 1;
}

static int
inspect_regexp(JSContext* ctx, Writer* wr, JSValueConst value, InspectOptions* opts, int32_t depth) {
  size_t len;
  const char* str = JS_ToCStringLen(ctx, &len, value);

  if(opts->colors)
    writer_puts(wr, COLOR_RED);

  put_escaped(wr, str, len);

  if(opts->colors)
    writer_puts(wr, COLOR_NONE);

  js_cstring_free(ctx, str);
  return 1;
}

static int
inspect_number(JSContext* ctx, Writer* wr, JSValueConst value, InspectOptions* opts, int32_t depth) {
  int tag = JS_VALUE_GET_TAG(value);

  if(tag != JS_TAG_SYMBOL && opts->colors)
    writer_puts(wr, COLOR_YELLOW);

  if(opts->number_base == 16 && (!JS_TAG_IS_FLOAT64(tag) || (isfinite(JS_VALUE_GET_FLOAT64(value)) && floor(JS_VALUE_GET_FLOAT64(value)) == JS_VALUE_GET_FLOAT64(value)))) {
    int64_t num;
    char buf[FMT_XLONG];

    JS_ToInt64(ctx, &num, value);

    writer_puts(wr, "0x");
    writer_write(wr, buf, fmt_xlonglong(buf, num));
  } else {
    const char* str;
    size_t len;
    JSValue number;
    int base = 10;

    if(opts->number_base && opts->number_base != 10 && tag != JS_TAG_FLOAT64) {
      JSValue arg = JS_NewInt32(ctx, opts->number_base);
      number = js_invoke(ctx, value, "toString", 1, &arg);
      JS_FreeValue(ctx, arg);
      base = opts->number_base;
    } else {
      number = JS_DupValue(ctx, value);
    }

    str = JS_ToCStringLen(ctx, &len, number);
    JS_FreeValue(ctx, number);

    switch(base) {
      case 16: writer_puts(wr, "0x"); break;
      case 2: writer_puts(wr, "0b"); break;
      case 8: writer_puts(wr, "0"); break;
    }

    writer_write(wr, str, len);

    js_cstring_free(ctx, str);
  }

  if(tag <= JS_TAG_BIG_FLOAT)
    writer_putc(wr, tag == JS_TAG_BIG_DECIMAL ? 'm' : tag == JS_TAG_BIG_FLOAT ? 'l' : 'n');

  if(opts->colors)
    writer_puts(wr, COLOR_NONE);

  return 1;
}

static int
inspect_string(JSContext* ctx, Writer* wr, JSValueConst value, InspectOptions* opts, int32_t level) {
  int col = 0, tag = JS_VALUE_GET_TAG(value);
  int32_t depth = INT32_IN_RANGE(level) ? level : 0;
  size_t len, pos, limit, max_len, column_start = (depth * 2);
  const char* str = JS_ToCStringLen(ctx, &len, value);

  if(tag != JS_TAG_SYMBOL && opts->colors)
    writer_puts(wr, COLOR_GREEN);

  writer_putc(wr, tag == JS_TAG_SYMBOL ? '(' : '\'');

  limit = opts->reparseable ? len : min_size(opts->max_string_length, len);
  max_len = min_size(opts->break_length - col - 10, len);

  for(pos = 0; pos < limit;) {
    size_t i, n, eol;
    n = limit - pos;

    if(opts->string_break_newline) {
      if(pos > 0) {
        writer_puts(wr, opts->colors ? "'" COLOR_NONE " +" : "' +");
        max_len = opts->break_length - column_start - 8;
        put_newline(wr, depth + 1);
        writer_puts(wr, opts->colors ? COLOR_GREEN "'" : "'");
      }

      for(i = pos; i < limit; i += eol) {
        eol = byte_chr(&str[i], limit - i, '\n');
        if(str[i + eol] == '\n')
          eol++;

        if(i > pos && ansi_truncate(&str[pos], i + eol - pos, max_len) < i + eol - pos) {
          n = i - pos;
          break;
        }
      }
    } else if(IS_COMPACT(depth + 1)) {
      n = ansi_truncate(&str[pos], n, max_len);
    }

    put_escaped(wr, &str[pos], n);
    pos += n;
  }

  js_cstring_free(ctx, str);
  writer_putc(wr, tag == JS_TAG_SYMBOL ? ')' : '\'');

  if(opts->colors)
    writer_puts(wr, COLOR_NONE);

  if(limit < len) {
    char lenbuf[FMT_ULONG];

    // if(opts->break_length != INT32_MAX)
    //  if(dbuf_get_column(wr) + 26 > opts->break_length)
    put_newline(wr, depth + 1);

    writer_puts(wr, "... ");
    writer_write(wr, lenbuf, fmt_ulong(lenbuf, len - pos));
    writer_puts(wr, " more characters");
  }

  return 1;
}

static int
inspect_key(JSContext* ctx, Writer* wr, JSAtom key, InspectOptions* opts) {
  const char* str = 0;
  JSValue value = JS_AtomToValue(ctx, key);
  BOOL is_string = JS_IsString(value);

  if(is_string && (str = JS_AtomToCString(ctx, key)) && is_identifier(str)) {
    writer_puts(wr, str);
  } else {
    if(!is_string)
      writer_putc(wr, '[');

    inspect_value(ctx, wr, value, opts, 0);
    if(!is_string)
      writer_putc(wr, ']');
  }

  if(str)
    JS_FreeCString(ctx, str);

  return 1;
}

static int
inspect_atom(JSContext* ctx, Writer* wr, JSAtom atom, InspectOptions* opts, int32_t depth) {
  JSValue value = JS_AtomToValue(ctx, atom);
  int r = inspect_value(ctx, wr, value, opts, depth);
  JS_FreeValue(ctx, value);
  return r;
}

static int
inspect_module(JSContext* ctx, Writer* wr, JSModuleDef* def, InspectOptions* opts, int32_t depth) {
  char buf[FMT_ULONG];

  writer_puts(wr, opts->colors ? COLOR_CYAN "[module" COLOR_NONE : "[module");

  if(def) {
    int index = js_module_indexof(ctx, def);
    assert(js_module_at(ctx, index) == def);

    if(opts->colors)
      writer_puts(wr, COLOR_WHITE);

    writer_puts(wr, " #");
    writer_write(wr, buf, fmt_longlong(buf, index));

    if(opts->colors)
      writer_puts(wr, COLOR_NONE);

    writer_puts(wr, COLOR_YELLOW);
    writer_puts(wr, " 0x");
    writer_write(wr, buf, fmt_xlonglong(buf, (intptr_t)def));

    if(opts->colors)
      writer_puts(wr, COLOR_NONE);

    writer_putc(wr, ' ');
    // pos = wr->size;
    inspect_atom(ctx, wr, def->module_name, opts, depth + 1);

    if(JS_IsFunction(ctx, def->func_obj))
      writer_puts(wr, COLOR_RED " JS" COLOR_NONE);
    else if(def->init_func)
      writer_puts(wr, COLOR_RED " NATIVE" COLOR_NONE);
    else
      writer_puts(wr, COLOR_RED " BYTECODE" COLOR_NONE);

    if(!def->resolved)
      writer_puts(wr, COLOR_YELLOW " (not resolved)" COLOR_NONE);
    else if(!def->func_created)
      writer_puts(wr, COLOR_YELLOW " (no function created)" COLOR_NONE);
    else if(!def->instantiated)
      writer_puts(wr, COLOR_YELLOW " (not instantiated)" COLOR_NONE);
    else if(!def->evaluated)
      writer_puts(wr, COLOR_YELLOW " (not evaluated)" COLOR_NONE);

    if(JS_IsFunction(ctx, def->func_obj)) {
      writer_putc(wr, ' ');
      inspect_value(ctx, wr, def->func_obj, opts, depth + 1);
    }
  }

  writer_puts(wr, opts->colors ? COLOR_CYAN "]" COLOR_NONE : "]");
  return 1;
}

static int
inspect_error(JSContext* ctx, Writer* wr, JSValueConst value, InspectOptions* opts, int32_t level) {
  JSValue stack;
  char* class_name;
  const char* str;
  int32_t depth = INT32_IN_RANGE(level) ? level : 0;

  if(!(class_name = js_get_tostringtag_str(ctx, value)))
    class_name = js_object_classname(ctx, value);

  writer_puts(wr, opts->colors ? COLOR_LIGHTRED : "");
  writer_puts(wr, class_name);
  writer_puts(wr, opts->colors ? COLOR_NONE " {" : " {");

  if((str = js_get_propertystr_cstring(ctx, value, "message"))) {
    put_newline(wr, depth + 1);
    writer_puts(wr, "message: ");
    writer_puts(wr, str);
    JS_FreeCString(ctx, str);
  }

  stack = JS_GetPropertyStr(ctx, value, "stack");

  if(!JS_IsUndefined(stack)) {
    const char *s, *p, *e;
    size_t len;

    if((s = JS_ToCStringLen(ctx, &len, stack))) {

      put_newline(wr, depth + 1);
      writer_puts(wr, "stack:");

      for(p = s, e = s + len; p < e;) {
        size_t ll = scan_line(p, e - p);
        size_t next = ll + scan_lineskip(&p[ll], e - p - ll);

        put_newline(wr, depth + 2);
        writer_write(wr, "|", 1);
        writer_write(wr, p, ll);

        p += next;
      }

      JS_FreeCString(ctx, s);
    }
  }

  put_newline(wr, depth);
  writer_puts(wr, opts->colors ? COLOR_NONE "}" : "}");

  return 1;
}

static int
inspect_object(JSContext* ctx, Writer* wr, JSValueConst value, InspectOptions* opts, int32_t level) {
  int32_t depth = INT32_IN_RANGE(level) ? level : 0;
  JSObject* obj = JS_VALUE_GET_OBJ(value);

  if(!obj->prop || !obj->shape)
    return -1;

  BOOL is_array = js_is_array(ctx, value);

  if(opts->depth != INT32_MAX && depth + 1 > opts->depth) {
    writer_puts(wr, is_array ? (opts->colors ? COLOR_MARINE "[Array]" COLOR_NONE : "[Array]") : (opts->colors ? COLOR_MARINE "[Object]" COLOR_NONE : "[Object]"));
    return 1;
  }

  if(opts->custom_inspect) {
    JSValue tmp = inspect_custom(ctx, value, opts, depth + 1);

    if(JS_IsString(tmp)) {
      const char* s = JS_ToCString(ctx, tmp);
      writer_puts(wr, s);
      js_cstring_free(ctx, s);
      return 1;
    }

    if(JS_IsException(tmp)) {
      JSValue exception = ctx->rt->current_exception;
      return -1;
    }

    if(!JS_IsUndefined(tmp)) {
      inspect_value(ctx, wr, tmp, opts, depth + 1);
      return 1;
    }
  }

  BOOL is_function = JS_IsFunction(ctx, value);

  if(!is_function) {
    BOOL is_typedarray = js_is_typedarray(ctx, value);

    if(!is_array && !is_typedarray) {
      if(js_is_arraybuffer(ctx, value) || js_is_sharedarraybuffer(ctx, value))
        return inspect_arraybuffer(ctx, wr, value, opts, depth);
      if(js_is_date(ctx, value))
        return inspect_date(ctx, wr, value, opts, depth);
      if(js_is_map(ctx, value))
        return inspect_map(ctx, wr, value, opts, depth);
      if(js_is_set(ctx, value))
        return inspect_set(ctx, wr, value, opts, depth);
      if(js_is_regexp(ctx, value))
        return inspect_regexp(ctx, wr, value, opts, depth);
      if(js_is_error(ctx, value))
        return inspect_error(ctx, wr, value, opts, depth);
    }

    if(js_is_generator(ctx, value)) {
      writer_puts(wr, "Object [Generator] {}");
      // js_cstring_free(ctx, s);
      return 1;
    }
  }

  if(!is_function) {
    if(opts->class_key.atom != 1 && JS_HasProperty(ctx, value, opts->class_key.atom)) {
      char* tostring_tag;

      if((tostring_tag = js_get_property_string(ctx, value, opts->class_key.atom))) {
        writer_puts(wr, opts->colors ? COLOR_LIGHTRED : "");
        writer_puts(wr, tostring_tag);
        writer_puts(wr, opts->colors ? COLOR_NONE " " : " ");
        js_free(ctx, tostring_tag);
      }
    } else if(!is_array) {
      const char* s = 0;

      if(s == 0 && JS_IsFunction(ctx, object_tostring))
        s = js_object_tostring2(ctx, object_tostring, value);

      if(s && !strncmp(s, "[object ", 8)) {
        const char* e = strchr(s, ']');
        size_t slen = e - (s + 8);

        if(slen != 6 || memcmp(s + 8, "Object", 6)) {
          writer_puts(wr, opts->colors ? COLOR_LIGHTRED : "[");
          writer_write(wr, s + 8, e - (s + 8));
          writer_puts(wr, opts->colors ? COLOR_NONE " " : "] ");
        }
      }

      if(s)
        js_cstring_free(ctx, s);
    }
  } else {
    JSValue name = JS_GetPropertyStr(ctx, value, "name");

    writer_puts(wr, opts->colors ? COLOR_MARINE "[" : "[");
    writer_puts(wr, js_object_classname(ctx, value));

    if(!JS_IsUndefined(name)) {
      const char* s = JS_ToCString(ctx, name);

      if(*s) {
        writer_puts(wr, ": ");
        writer_puts(wr, s);
      }

      js_cstring_free(ctx, s);
    }

    JS_FreeValue(ctx, name);
    writer_puts(wr, opts->colors ? "] " COLOR_NONE : "] ");
  }

  return 0;
}

static int
inspect_value(JSContext* ctx, Writer* wr, JSValueConst value, InspectOptions* opts, int32_t level) {
  int tag = JS_VALUE_GET_TAG(value);
  int32_t depth = INT32_IN_RANGE(level) ? level : 0;

  switch(tag) {
    case JS_TAG_FLOAT64:
    case JS_TAG_BIG_DECIMAL:
    case JS_TAG_BIG_INT:
    case JS_TAG_INT:
    case JS_TAG_BIG_FLOAT: {
      return inspect_number(ctx, wr, value, opts, level);
    }

    case JS_TAG_BOOL: {
      writer_puts(wr, JS_VALUE_GET_BOOL(value) ? (opts->colors ? COLOR_BROWN "true" COLOR_NONE : "true") : (opts->colors ? COLOR_BROWN "false" COLOR_NONE : "false"));
      break;
    }

    case JS_TAG_NULL: {
      writer_puts(wr, opts->colors ? COLOR_MAGENTA "null" COLOR_NONE : "null");
      break;
    }

    case JS_TAG_UNDEFINED: {
      writer_puts(wr, opts->colors ? COLOR_GRAY "undefined" COLOR_NONE : "undefined");
      break;
    }

    case JS_TAG_EXCEPTION: {
      writer_puts(wr, opts->colors ? COLOR_RED "[exception" : "[exception");

      if(JS_IsObject(ctx->rt->current_exception)) {
        JSValue message = JS_GetPropertyStr(ctx, ctx->rt->current_exception, "message");
        const char* msg;

        if((msg = JS_ToCString(ctx, message))) {
          writer_puts(wr, " \"");
          writer_puts(wr, msg);
          writer_putc(wr, '"');
          JS_FreeCString(ctx, msg);
        }

        JS_FreeValue(ctx, message);

        JSValue stack = JS_GetPropertyStr(ctx, ctx->rt->current_exception, "stack");

        if((msg = JS_ToCString(ctx, stack))) {
          writer_puts(wr, "\n");
          writer_puts(wr, msg);
          writer_putc(wr, '\n');
          JS_FreeCString(ctx, msg);
        }

        JS_FreeValue(ctx, stack);
      }

      writer_puts(wr, opts->colors ? "]" COLOR_NONE : "]");
      break;
    }

    case JS_TAG_SYMBOL: {
      if(opts->reparseable) {
        const char* str = js_symbol_to_cstring(ctx, value);

        writer_puts(wr, str);
        JS_FreeCString(ctx, str);
        break;
      }

      value = js_symbol_to_string(ctx, value);

      if(opts->colors)
        writer_puts(wr, COLOR_PURPLE);

      const char* str = JS_ToCString(ctx, value);
      writer_puts(wr, str);
      JS_FreeCString(ctx, str);

      if(opts->colors)
        writer_puts(wr, COLOR_NONE);

      break;
      //__attribute__((fallthrough));
    }

    case JS_TAG_STRING: {
      return inspect_string(ctx, wr, value, opts, level);
    }

    case JS_TAG_OBJECT: {
      return inspect_object(ctx, wr, value, opts, depth);
    }

    case JS_TAG_FUNCTION_BYTECODE: {
      writer_puts(wr, opts->colors ? COLOR_LIGHTRED "[bytecode]" COLOR_NONE : "[bytecode]");
      break;
    }

    case JS_TAG_MODULE: {
      JSModuleDef* def = JS_VALUE_GET_PTR(value);
      const char* name = JS_AtomToCString(ctx, def->module_name);

      writer_puts(wr, opts->colors ? COLOR_LIGHTRED "[module '" : "[module '");
      writer_puts(wr, name);
      writer_puts(wr, opts->colors ? "']" COLOR_NONE : "']");

      JS_FreeCString(ctx, name);
      break;
    }

    default: {
      JS_ThrowTypeError(ctx, "Unhandled value tag in inspect_value: %d\n", tag);
      return -1;
    }
  }

  return 0;
}

static int
inspect_recursive(JSContext* ctx, Writer* wr, JSValueConst obj, InspectOptions* opts, int32_t level) {
  PropertyEnumeration* it;
  Vector frames;
  BOOL is_array;
  int32_t depth = INT32_IN_RANGE(level) ? level : 0;
  uint32_t index = 0;

  vector_init(&frames, ctx);

  it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, obj), PROPENUM_DEFAULT_FLAGS);
  is_array = js_is_array(ctx, obj);

  writer_puts(wr, is_array ? "[" : "{");
  if(it)
    ++depth;

  while(it) {
    JSValue value = property_enumeration_value(it, ctx);
    index = property_enumeration_index(it);

#ifdef DEBUG_OUTPUT
    printf("%s() depth: %u idx: %u/%u\n", __func__, property_recursion_depth(&frames), index, it->tab_atom_len);
#endif

    if(index > 0)
      writer_puts(wr, ",");

    put_spacing(wr, opts, depth);

    if(!is_array) {
      inspect_key(ctx, wr, property_enumeration_atom(it), opts);
      writer_puts(wr, ": ");
    }

    BOOL is_object = JS_IsObject(value);
    int ret = 0;

    if(is_object && property_recursion_circular(&frames, value)) {
      writer_puts(wr, opts->colors ? COLOR_LIGHTRED "[loop]" COLOR_NONE : "[loop]");
      ret = 1;
    } else {
      ret = is_object ? inspect_object(ctx, wr, value, opts, depth) : 0;
    }

    if(ret != 1 && is_object) {
      writer_putc(wr, ' ');

      it = property_recursion_enter(&frames, ctx, 0, PROPENUM_DEFAULT_FLAGS | JS_GPN_RECURSIVE);
      is_array = js_is_array(ctx, value);

      if(it) {
        index = 0;
        writer_puts(wr, is_array ? "<[<" : "<{<");

        ++depth;

        put_spacing(wr, opts, depth);

        continue;
      } else {
        writer_puts(wr, is_array ? "[]" : "{}");
      }
    }

    if(it) {
      if(ret != 1 && !is_object) {
        assert(!JS_IsObject(value));
        // writer_putc(wr, '<');
        inspect_value(ctx, wr, value, opts, depth);
        // writer_putc(wr, '>');
      }
    }

    // BOOL end = index < property_enumeration_length( it ? it : property_recursion_top(&frames));

    while(!(it = it ? it : property_recursion_top(&frames), it = (opts->proto_chain ? property_enumeration_prototype(it, ctx, PROPENUM_DEFAULT_FLAGS) : property_enumeration_next(it)))) {

      /* no more nested enumerations */
      it = property_recursion_pop(&frames, ctx);

      put_spacing(wr, opts, --depth);

      if(!it)
        break;

      writer_puts(wr, is_array ? "]" : "}");

      is_array = js_is_array(ctx, it->obj);
    }
  }


#if 1 //def DEBUG_OUTPUT
    printf("%s() depth: %u %u it: %p\n", __func__, property_recursion_depth(&frames), depth, it);
#endif
  /*if(depth >= 0) {
    if(depth > 0 || it) {
      put_spacing(wr, opts, 0);
    }

    }*/
  writer_puts(wr, JS_IsArray(ctx, obj) ? "]" : "}");
  

  property_recursion_free(&frames, JS_GetRuntime(ctx));
  return 0;
}

static JSValue
js_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  DynBuf dbuf;
  Writer wr, fd_wr, buf_wr;
  InspectOptions options;
  int32_t level;
  JSValue ret = JS_UNDEFINED;
  int optind = 1;

  js_dbuf_init(ctx, &dbuf);
  buf_wr = writer_from_dynbuf(&dbuf);
  fd_wr = writer_from_fd(open("out.tmp", O_CREAT | O_WRONLY | O_APPEND, 0644), true);

  wr = writer_tee(buf_wr, fd_wr);

  options_init(&options, ctx);

  if(argc > 1 && JS_IsNumber(argv[1]))
    optind++;

  if(optind < argc)
    options_get(&options, ctx, argv[optind]);

  if(optind > 1) {
    double d;
    JS_ToFloat64(ctx, &d, argv[1]);
    level = isinf(d) ? INT32_MAX : d;
  } else {
    level = 0;
  }

  /*if(js_is_date(ctx, argv[0]))
    inspect_date(ctx, &wr, argv[0], &options, level);
  else*/
  if(JS_IsObject(argv[0]) && level < options.depth) {
    int ret = inspect_object(ctx, &wr, argv[0], &options, level);

    if(ret != 1)
      inspect_recursive(ctx, &wr, argv[0], &options, level);
  } else
    inspect_value(ctx, &wr, argv[0], &options, level);

  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);

  writer_free(&wr);

  options_free(&options, ctx);

  return ret;
}

char*
js_inspect_tostring(JSContext* ctx, JSValueConst value) {
  DynBuf dbuf;
  InspectOptions options;

  js_dbuf_init(ctx, &dbuf);
  options_init(&options, ctx);

  options.colors = FALSE;
  options.compact = 0;
  options.getters = TRUE;

  Writer wr = writer_from_dynbuf(&dbuf);
  inspect_value(ctx, &wr, value, &options, options.depth);

  options_free(&options, ctx);

  dbuf_0(&dbuf);
  return (char*)dbuf.buf;
}

char*
js_inspect_atom(JSContext* ctx, JSAtom atom) {
  JSValue value = JS_AtomToValue(ctx, atom);
  char* str = js_inspect_tostring(ctx, value);
  JS_FreeValue(ctx, value);
  return str;
}

/*JSValue js_debugger_build_backtrace(JSContext* ctx, const uint8_t* cur_pc);

JSValue
js_inspect_stacktrace_value(JSContext* ctx) {
  JSRuntime* rt = JS_GetRuntime(ctx);
  struct JSStackFrame* frame;
  JSValue ret = JS_UNDEFINED;

  if((frame = rt->current_stack_frame)) {
#ifdef CONFIG_DEBUGGER
    ret = js_debugger_build_backtrace(ctx, frame->cur_pc);
#endif
  }

  return ret;
}

const char*
js_inspect_stacktrace(JSContext* ctx) {
  JSValue stack = js_inspect_stacktrace_value(ctx);
  return js_inspect_tostring(ctx, stack);
}*/

static const JSCFunctionListEntry js_inspect_funcs[] = {
    JS_CFUNC_DEF("inspect", 1, js_inspect),
};

static int
js_inspect_init(JSContext* ctx, JSModuleDef* m) {
  JSValue inspect, inspect_symbol, symbol_ctor;

  stdout_isatty = isatty(STDOUT_FILENO);
  stderr_isatty = isatty(STDERR_FILENO);

  inspect = JS_NewCFunction(ctx, js_inspect, "inspect", 2);

  inspect_symbol = js_symbol_for(ctx, "quickjs.inspect.custom");
  JS_SetPropertyStr(ctx, inspect, "symbol", JS_DupValue(ctx, inspect_symbol));
  symbol_ctor = js_symbol_ctor(ctx);
  JS_SetPropertyStr(ctx, symbol_ctor, "inspect", JS_DupValue(ctx, inspect_symbol));

  JS_FreeValue(ctx, symbol_ctor);
  JS_FreeValue(ctx, inspect_symbol);

  inspect_custom_atom = js_symbol_for_atom(ctx, "quickjs.inspect.custom");
  inspect_custom_atom_node = js_symbol_for_atom(ctx, "nodejs.util.inspect.custom");

  object_tostring = js_global_prototype_func(ctx, "Object", "toString");

  if(m) {
    JS_SetModuleExportList(ctx, m, js_inspect_funcs, countof(js_inspect_funcs));
    JS_SetModuleExport(ctx, m, "default", inspect);
  }

  return 0;
}

#if defined(JS_SHARED_LIBRARY) && defined(JS_INSPECT_MODULE)
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_inspect
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_inspect_init))) {
    JS_AddModuleExportList(ctx, m, js_inspect_funcs, countof(js_inspect_funcs));
    JS_AddModuleExport(ctx, m, "default");
  }

  return m;
}

/**
 * @}
 */
