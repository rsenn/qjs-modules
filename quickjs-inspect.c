#include <cutils.h>
#include "iteration.h"
#include <list.h>
#include "property-enumeration.h"
#include "quickjs-internal.h"
#include <quickjs.h>
#include <quickjs-config.h>
#include "utils.h"
#include "vector.h"
#include "buffer-utils.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
int isatty(HANDLE);
#else
#include <unistd.h>
#if HAVE_TERMIOS_H
#include <termios.h>
#include <sys/ioctl.h>
#endif
#endif

/**
 * \defgroup quickjs-inspect QuickJS module: inspect - Inspection
 * @{
 */

thread_local JSAtom inspect_custom_atom = 0, inspect_custom_atom_node = 0;

#define INSPECT_INT32T_INRANGE(i) ((i) > INT32_MIN && (i) < INT32_MAX)
#define INSPECT_LEVEL(opts) ((opts)->depth - (depth))
#define INSPECT_IS_COMPACT(opts) \
  ((opts)->compact == INT32_MAX ? TRUE \
   : INSPECT_INT32T_INRANGE((opts)->compact) \
       ? ((opts)->compact < 0 ? INSPECT_LEVEL(opts) >= -(opts->compact) : INSPECT_LEVEL(opts) >= (opts)->compact) \
       : 0)

struct prop_key;

typedef struct prop_key {
  /* struct list_head link;*/
  const char* name;
  JSAtom atom;
} prop_key_t;

typedef struct {
  int colors : 1;
  int show_hidden : 1;
  int custom_inspect : 1;
  int show_proxy : 1;
  int getters : 1;
  int string_break_newline : 1;
  int32_t depth;
  int32_t max_array_length;
  int32_t max_string_length;
  int32_t break_length;
  int32_t compact;
  int32_t proto_chain;
  int32_t number_base;
  Vector hide_keys;
  prop_key_t class_key;
} inspect_options_t;

static int stdout_isatty, stderr_isatty;
static int32_t screen_width = -1;

static int js_inspect_print_value(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth);

static int
regexp_predicate(int c) {
  switch(c) {
    case 8: return 'u';
    case 12: return 'f';
    case 10: return 'n';
    case 13: return 'r';
    case 9: return 't';
    case 11: return 'v';
  }
  if(c < 0x20)
    return 'u';
  return 0;
}

static inline int
inspect_screen_width(void) {
  if(screen_width != -1)
    return screen_width;

#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO csbi;

  GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
  screen_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;

#elif defined(HAVE_TERMIOS_H)
  {
    struct winsize w = {.ws_col = -1, .ws_row = -1};

    if(stdout_isatty)
      ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    else if(stderr_isatty)
      ioctl(STDERR_FILENO, TIOCGWINSZ, &w);

    screen_width = w.ws_col;
  }
#else
  screen_width = 80;
#endif
  return screen_width;
}

static void
inspect_options_init(inspect_options_t* opts, JSContext* ctx) {
  opts->colors = TRUE;
  opts->show_hidden = FALSE;
  opts->custom_inspect = TRUE;
  opts->show_proxy = FALSE;
  opts->getters = FALSE;
  opts->string_break_newline = FALSE;
  opts->depth = INT32_MAX;
  opts->max_array_length = 300;
  opts->max_string_length = INT32_MAX;
  opts->break_length = inspect_screen_width();
  opts->compact = 5;
  opts->proto_chain = 0;
  opts->number_base = 10;
  vector_init(&opts->hide_keys, ctx);

  prop_key_t to_string_tag = {0, js_atom_from(ctx, "[Symbol.toStringTag]")};
  to_string_tag.name = JS_AtomToCString(ctx, to_string_tag.atom);

  vector_push(&opts->hide_keys, to_string_tag);
}

static void
inspect_options_free(inspect_options_t* opts, JSContext* ctx) {

  prop_key_t* key;

  vector_foreach_t(&opts->hide_keys, key) {
    JS_FreeAtom(ctx, key->atom);
    js_cstring_free(ctx, key->name);
  }
  vector_free(&opts->hide_keys);
}

static void
inspect_options_get(inspect_options_t* opts, JSContext* ctx, JSValueConst object) {
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
      opts->compact = JS_VALUE_GET_BOOL(value) == 0 ? 0 : INT32_MAX;
    else if(JS_VALUE_GET_TAG(value) == JS_TAG_FLOAT64 && isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->compact = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->compact, value);
    JS_FreeValue(ctx, value);
  }
  value = JS_GetPropertyStr(ctx, object, "hideKeys");
  if(JS_IsArray(ctx, value)) {
    int64_t len, pos;
    len = js_array_length(ctx, value);

    for(pos = 0; pos < len; pos++) {
      JSValue item = JS_GetPropertyUint32(ctx, value, pos);
      prop_key_t key;
      key.name = JS_VALUE_GET_TAG(item) == JS_TAG_SYMBOL ? 0 : JS_ToCString(ctx, item);
      key.atom = JS_ValueToAtom(ctx, item);
      vector_push(&opts->hide_keys, key);
      JS_FreeValue(ctx, item);
    }
    JS_FreeValue(ctx, value);
  }
  value = JS_GetPropertyStr(ctx, object, "protoChain");
  // if(JS_IsNumber(value))
  JS_ToInt32(ctx, &opts->proto_chain, value);
  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, object, "numberBase");
  if(JS_IsNumber(value))
    JS_ToInt32(ctx, &opts->number_base, value);
  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, object, "classKey");
  opts->class_key.atom = JS_ValueToAtom(ctx, value);
  opts->class_key.name = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);
}

static JSValue
inspect_options_object(inspect_options_t* opts, JSContext* ctx) {
  JSValue arr, ret = JS_NewObject(ctx);
  uint32_t n;
  prop_key_t* key;

  JS_SetPropertyStr(ctx, ret, "colors", JS_NewBool(ctx, opts->colors));
  JS_SetPropertyStr(ctx, ret, "showHidden", JS_NewBool(ctx, opts->show_hidden));
  JS_SetPropertyStr(ctx, ret, "customInspect", JS_NewBool(ctx, opts->custom_inspect));
  JS_SetPropertyStr(ctx, ret, "showProxy", JS_NewBool(ctx, opts->show_proxy));
  JS_SetPropertyStr(ctx, ret, "getters", JS_NewBool(ctx, opts->getters));
  JS_SetPropertyStr(ctx, ret, "stringBreakNewline", JS_NewBool(ctx, opts->string_break_newline));
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
inspect_options_hidden(inspect_options_t* opts, JSAtom atom) {
  prop_key_t* key;
  vector_foreach_t(&opts->hide_keys, key) {
    if(key->atom == atom) //! strcmp(key->name, str))
      return 1;
  }
  return 0;
}

static void
inspect_newline(DynBuf* buf, int32_t depth) {
  dbuf_putc(buf, '\n');
  while(depth-- > 0) dbuf_putstr(buf, "  ");
}

static int
js_object_getpropertynames(JSContext* ctx, union Vector* propenum_tab, JSValueConst obj, int flags) {
  int ret;
  JSPropertyEnum* tmp_tab;
  uint32_t i, tmp_len;
  ret = JS_GetOwnPropertyNames(ctx, &tmp_tab, &tmp_len, obj, flags);

  for(i = 0; i < tmp_len; i++) {
    if(vector_find(propenum_tab, sizeof(JSPropertyEnum), &tmp_tab[i]) == -1)
      vector_put(propenum_tab, &tmp_tab[i], sizeof(JSPropertyEnum));
  }

  js_free(ctx, tmp_tab);
  return ret;
}

static int
js_object_getpropertynames_recursive(JSContext* ctx, union Vector* propenum_tab, JSValueConst obj, int flags) {
  int ret;

  while((ret = js_object_getpropertynames(ctx, propenum_tab, obj, flags)) >= 0) {
    JSValue proto = JS_GetPrototype(ctx, obj);

    if(!JS_IsObject(proto))
      break;

    obj = proto;
    //      ret = js_object_getpropertynames_recursive(ctx, propenum_tab, proto, flags);
  }

  return ret;
}

static JSAtom
js_inspect_custom_atom(JSContext* ctx, const char* sym_for) {
  JSValue key, sym;
  JSAtom atom;
  key = JS_NewString(ctx, sym_for ? sym_for : "quickjs.inspect.custom");
  sym = js_symbol_invoke_static(ctx, "for", key);
  JS_FreeValue(ctx, key);
  atom = JS_ValueToAtom(ctx, sym);
  JS_FreeValue(ctx, sym);

  return atom;
}

static JSValue
js_inspect_custom_call(JSContext* ctx, JSValueConst obj, inspect_options_t* opts, int32_t depth) {
  JSValue ret = JS_UNDEFINED;
  JSValue inspect = JS_UNDEFINED;
  JSAtom prop;

  if(JS_VALUE_GET_OBJ(obj)->class_id) {
    if(JS_HasProperty(ctx, obj, inspect_custom_atom))
      inspect = JS_GetProperty(ctx, obj, inspect_custom_atom);
    else if(JS_HasProperty(ctx, obj, inspect_custom_atom_node))
      inspect = JS_GetProperty(ctx, obj, inspect_custom_atom_node);
  }
  if(JS_IsFunction(ctx, inspect)) {
    JSValueConst args[2];
    inspect_options_t opts_nocustom;
    memcpy(&opts_nocustom, opts, sizeof(inspect_options_t));
    opts_nocustom.custom_inspect = FALSE;
    args[0] = js_number_new(ctx, INSPECT_LEVEL(opts));
    args[1] = inspect_options_object(&opts_nocustom, ctx);
    ret = JS_Call(ctx, inspect, obj, 2, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    /*   str = JS_ToCString(ctx, ret);
       JS_FreeValue(ctx, ret);*/
  }

  JS_FreeValue(ctx, inspect);
  return ret;
}

static int
js_inspect_print_map(JSContext* ctx, DynBuf* buf, JSValueConst obj, inspect_options_t* opts, int32_t depth) {
  BOOL ret, finish = FALSE;
  size_t i = 0;
  int compact = INSPECT_IS_COMPACT(opts);
  // printf("js_inspect_print_map level=%u opts->depth=%" PRId32 " depth=%" PRId32 "
  // compact=%i\n", INSPECT_LEVEL(opts), opts->depth, depth, compact);
  JSValue data, key, value;
  Iteration it;
  if(!(ret = iteration_method_symbol(&it, ctx, obj, "iterator"))) {
    JS_ThrowTypeError(ctx, "js_inspect_print_map tag=%d\n", JS_VALUE_GET_TAG(obj));
    return 0;
  }
  dbuf_putstr(buf, "Map {");
  if(!compact && opts->break_length != INT32_MAX)
    inspect_newline(buf, INSPECT_LEVEL(opts));
  for(i = 0; !(finish = iteration_next(&it, ctx)); i++) {
    if(!finish) {
      data = iteration_value(&it, ctx);
      if(i) {
        dbuf_putstr(buf, ",");
        if(!compact && opts->break_length != INT32_MAX)
          inspect_newline(buf, INSPECT_LEVEL(opts));
      }
      dbuf_putstr(buf, compact ? " " : "  ");
      key = JS_GetPropertyUint32(ctx, data, 0);
      js_inspect_print_value(ctx, buf, key, opts, depth - 1);
      dbuf_putstr(buf, " => ");
      value = JS_GetPropertyUint32(ctx, data, 1);
      js_inspect_print_value(ctx, buf, value, opts, depth - 1);
      JS_FreeValue(ctx, key);
      JS_FreeValue(ctx, value);
      JS_FreeValue(ctx, data);
    }
  }
  if(!compact && opts->break_length != INT32_MAX)
    inspect_newline(buf, INSPECT_LEVEL(opts));
  dbuf_putstr(buf, compact ? " }" : "}");
  iteration_reset(&it, JS_GetRuntime(ctx));
  return 0;
}

static int
js_inspect_print_set(JSContext* ctx, DynBuf* buf, JSValueConst obj, inspect_options_t* opts, int32_t depth) {
  BOOL ret, finish = FALSE;
  size_t i = 0;
  int compact = INSPECT_IS_COMPACT(opts);
  JSValue value;
  Iteration it;

  if(!(ret = iteration_method_symbol(&it, ctx, obj, "iterator"))) {
    JS_ThrowTypeError(ctx, "js_inspect_print_map tag=%d\n", JS_VALUE_GET_TAG(obj));
    return 0;
  }
  dbuf_putstr(buf, "Set [");
  if(!compact && opts->break_length != INT32_MAX)
    inspect_newline(buf, INSPECT_LEVEL(opts));
  for(i = 0; !(finish = iteration_next(&it, ctx)); i++) {
    if(!finish) {
      value = iteration_value(&it, ctx);
      if(i) {
        dbuf_putstr(buf, ",");
        if(!compact && opts->break_length != INT32_MAX)
          inspect_newline(buf, INSPECT_LEVEL(opts));
      }
      dbuf_putstr(buf, compact ? " " : "  ");
      js_inspect_print_value(ctx, buf, value, opts, depth);
      JS_FreeValue(ctx, value);
    }
  }
  if(!compact && opts->break_length != INT32_MAX)
    inspect_newline(buf, INSPECT_LEVEL(opts));
  dbuf_putstr(buf, compact ? " ]" : "]");
  iteration_reset(&it, JS_GetRuntime(ctx));
  return 0;
}

static int
js_inspect_print_arraybuffer(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth) {
  const char *str, *str2;
  uint8_t* ptr;
  size_t i, slen, size;
  int break_len = opts->break_length; // inspect_screen_width();
  int column = dbuf_get_column(buf);
  JSValue proto;
  int compact = opts->compact >= 1;
  break_len = (break_len + 1) / 3;
  break_len *= 3;

  if(break_len > opts->break_length)
    break_len = opts->break_length;
  ptr = JS_GetArrayBuffer(ctx, &size, value);
  // printf("maxArrayLength: %i\n", opts->max_array_length);
  proto = JS_GetPrototype(ctx, value);
  str = js_object_tostring(ctx, proto);
  JS_FreeValue(ctx, proto);

  if(js_is_arraybuffer(ctx, value))
    dbuf_putstr(buf, "ArrayBuffer");
  else if(js_is_sharedarraybuffer(ctx, value))
    dbuf_putstr(buf, "SharedArrayBuffer");
  else if(str && (str2 = strchr(str, ' '))) {
    str2++;
    slen = byte_chr(str2, strlen(str2), ']');
    dbuf_append(buf, (const uint8_t*)str2, slen);
  }

  if(str)
    js_cstring_free(ctx, str);

  dbuf_putstr(buf, " {");
  if(compact)
    dbuf_putc(buf, ' ');
  else
    inspect_newline(buf, (opts->depth - depth) + 2);
  dbuf_printf(buf, "byteLength: %zu [", size);
  if(compact)
    dbuf_putc(buf, ' ');
  else
    inspect_newline(buf, (opts->depth - depth) + 3);
  break_len -= ((opts->depth - depth) + 3) * 2;
  column = 0;

  for(i = 0; i < size; i++) {
    if(i == (size_t)opts->max_array_length)
      break;
    if(column + 3 >= break_len && opts->break_length != INT32_MAX) {
      if(compact)
        dbuf_putc(buf, ' ');
      else
        inspect_newline(buf, (opts->depth - depth) + 3);
      column = 0;
    }
    dbuf_printf(buf, column ? " %02x" : "%02x", ptr[i]);
    column += column ? 3 : 2;
  }
  if(i < size) {
    if(compact)
      dbuf_putc(buf, ' ');
    else
      inspect_newline(buf, (opts->depth - depth) + 3);

    dbuf_printf(buf, "... %zu more bytes", size - i);
  }
  if(compact)
    dbuf_putc(buf, ' ');
  else
    inspect_newline(buf, (opts->depth - depth) + 2);
  dbuf_putstr(buf, "]");
  if(compact)
    dbuf_putc(buf, ' ');
  else
    inspect_newline(buf, (opts->depth - depth) + 1);
  dbuf_putstr(buf, "}");
  return 0;
}

static int
js_inspect_print_regexp(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth) {
  const char* str;
  size_t len;
  str = JS_ToCStringLen(ctx, &len, value);
  if(opts->colors)
    dbuf_putstr(buf, COLOR_RED);
  dbuf_put_escaped_pred(buf, str, len, regexp_predicate);
  if(opts->colors)
    dbuf_putstr(buf, COLOR_NONE);
  js_cstring_free(ctx, str);
  return 0;
}

static int
js_inspect_print_number(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth) {
  int tag = JS_VALUE_GET_TAG(value);
  const char* str;
  size_t len;
  if(tag != JS_TAG_SYMBOL && opts->colors)
    dbuf_putstr(buf, COLOR_YELLOW);

  if(opts->number_base == 16 && tag != JS_TAG_FLOAT64) {
    int64_t num;
    char str[FMT_XLONG];
    JS_ToInt64(ctx, &num, value);
    dbuf_putstr(buf, "0x");
    dbuf_put(buf, str, fmt_xlonglong(str, num));

  } else {
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
      case 16: dbuf_putstr(buf, "0x"); break;
      case 2: dbuf_putstr(buf, "0b"); break;
      case 8: dbuf_putstr(buf, "0"); break;
    }

    dbuf_append(buf, (const uint8_t*)str, len);

    js_cstring_free(ctx, str);
  }

  if(tag <= JS_TAG_BIG_FLOAT)
    dbuf_putc(buf, tag == JS_TAG_BIG_DECIMAL ? 'm' : tag == JS_TAG_BIG_FLOAT ? 'l' : 'n');
  if(opts->colors)
    dbuf_putstr(buf, COLOR_NONE);
  return 0;
}

static int
js_inspect_print_string(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth) {
  int tag = JS_VALUE_GET_TAG(value);
  int compact = INSPECT_IS_COMPACT(opts);

  const char* str;
  size_t pos, len, max_len, limit, column_start = (INSPECT_LEVEL(opts) * 2);
  int col = dbuf_get_column(buf);
  str = JS_ToCStringLen(ctx, &len, value);
  max_len = min_size(opts->break_length - col - 10, len);

  if(tag != JS_TAG_SYMBOL && opts->colors)
    dbuf_putstr(buf, COLOR_GREEN);
  dbuf_putc(buf, tag == JS_TAG_SYMBOL ? '(' : '\'');
  limit = min_size(opts->max_string_length, len);

  for(pos = 0; pos < limit;) {
    size_t i, n, eol;
    n = limit - pos;
    if(pos > 0) {
      dbuf_putstr(buf, opts->colors ? "'" COLOR_NONE " +" : "' +");
      max_len = opts->break_length - column_start - 8;
      inspect_newline(buf, INSPECT_LEVEL(opts) + 1);
      dbuf_putstr(buf, opts->colors ? COLOR_GREEN "'" : "'");
    }
    if(compact) {
      n = ansi_truncate(&str[pos], n, max_len);
    } else if(opts->string_break_newline) {
      for(i = pos; i < limit; i += eol) {
        eol = byte_chr(&str[i], limit - i, '\n');
        if(str[i + eol] == '\n')
          eol++;

        if(i > pos && ansi_truncate(&str[pos], i + eol - pos, max_len) < i + eol - pos) {
          n = i - pos;
          break;
        }
      }
    }
    dbuf_put_escaped(buf, &str[pos], n);
    pos += n;
  }
  js_cstring_free(ctx, str);
  dbuf_putc(buf, tag == JS_TAG_SYMBOL ? ')' : '\'');

  if(opts->colors)
    dbuf_putstr(buf, COLOR_NONE);

  if(limit < len) {
    if(opts->break_length != INT32_MAX && dbuf_get_column(buf) + 26 > opts->break_length)
      inspect_newline(buf, INSPECT_LEVEL(opts) + 1);
    dbuf_printf(buf, "... %zu more characters", len - pos);
  }
  return 0;
}

static int
js_inspect_print_atom(JSContext* ctx, DynBuf* buf, JSAtom atom, inspect_options_t* opts, int32_t depth) {
  JSValue value;
  int r;
  value = JS_AtomToValue(ctx, atom);
  r = js_inspect_print_value(ctx, buf, value, opts, depth);
  JS_FreeValue(ctx, value);
  return r;
}

static int
js_inspect_print_module(JSContext* ctx, DynBuf* buf, JSModuleDef* def, inspect_options_t* opts, int32_t depth) {
  size_t pos;
  dbuf_putstr(buf, opts->colors ? COLOR_CYAN "[module" COLOR_NONE : "[module");
  if(def) {
    int index = js_module_indexof(ctx, def);
    assert(js_module_at(ctx, index) == def);
    dbuf_printf(buf, COLOR_WHITE " #%i" COLOR_NONE, index);

    dbuf_printf(buf, COLOR_YELLOW " %p" COLOR_NONE, def);
    dbuf_putc(buf, ' ');
    pos = buf->size;
    js_inspect_print_atom(ctx, buf, def->module_name, opts, depth - 1);

    while(pos < buf->size) {
      if(buf->buf[pos] == '\'')
        buf->buf[pos] = '"';
      else if(buf->buf[pos - 2] == '[' && buf->buf[pos - 1] == '3' && buf->buf[pos] == '2' && buf->buf[pos + 1] == 'm')
        buf->buf[pos] = '5';
      ++pos;
    }

    if(JS_IsFunction(ctx, def->func_obj)) {
      dbuf_putstr(buf, COLOR_RED " JS" COLOR_NONE);
    } else if(def->init_func) {
      dbuf_putstr(buf, COLOR_RED " NATIVE" COLOR_NONE);
    } else {
      dbuf_putstr(buf, COLOR_RED " BYTECODE" COLOR_NONE);
    }

    if(!def->resolved)
      dbuf_putstr(buf, COLOR_YELLOW " (not resolved)" COLOR_NONE);
    else if(!def->func_created)
      dbuf_putstr(buf, COLOR_YELLOW " (no function created)" COLOR_NONE);
    else if(!def->instantiated)
      dbuf_putstr(buf, COLOR_YELLOW " (not instantiated)" COLOR_NONE);
    else if(!def->evaluated)
      dbuf_putstr(buf, COLOR_YELLOW " (not evaluated)" COLOR_NONE);

    if(JS_IsFunction(ctx, def->func_obj)) {
      dbuf_putc(buf, ' ');
      js_inspect_print_value(ctx, buf, def->func_obj, opts, depth - 1);
    }
  }
  dbuf_putstr(buf, opts->colors ? COLOR_CYAN "]" COLOR_NONE : "]");
  return 0;
}

static int
js_inspect_print_error(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth) {

  char* class_name;
  const char* s;
  JSValue stack;

  class_name = js_object_classname(ctx, value);

  dbuf_putstr(buf, opts->colors ? COLOR_RED "[" COLOR_NONE : "[");
  dbuf_putstr(buf, class_name);
  if((s = js_get_propertystr_cstring(ctx, value, "message"))) {
    dbuf_putstr(buf, " \"");
    dbuf_putstr(buf, s);
    dbuf_putstr(buf, "\"");
    JS_FreeCString(ctx, s);
  }
  stack = JS_GetPropertyStr(ctx, value, "stack");

  if((s = JS_ToCString(ctx, stack))) {
    dbuf_putstr(buf, " STACK:");
    dbuf_putstr(buf, s);
    JS_FreeCString(ctx, s);
  }

  dbuf_putstr(buf, opts->colors ? COLOR_RED "]" COLOR_NONE : "]");
  return 0;
}

static int
js_inspect_print_object(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth) {
  BOOL is_array = 0, is_typedarray = 0, is_function = 0;
  uint32_t pos, len, limit;
  Vector propenum_tab;
  const char* s = 0;
  int compact = opts->compact;
  JSObject* obj = JS_VALUE_GET_OBJ(value);

  vector_init(&propenum_tab, ctx);

  if(!obj->prop || !obj->shape) {
    dbuf_printf(buf, "js_inspect_print_value Object prop = %p, shape = %p ", obj->prop, obj->shape);
    return -1;
  }

  if(opts->custom_inspect) {
    JSValue tmp = js_inspect_custom_call(ctx, value, opts, depth);
    if(JS_IsString(tmp)) {
      const char* s = JS_ToCString(ctx, tmp);
      dbuf_putstr(buf, s);
      js_cstring_free(ctx, s);
      return 0;
    }

    if(JS_IsException(tmp)) {
      JSValue exception = JS_GetException(ctx);

      // dbuf_printf(buf, "exception: %s\n", JS_ToCString(ctx, exception));
      // return 0;

    } else if(!JS_IsUndefined(tmp)) {
      if(!JS_IsObject(tmp))
        return js_inspect_print_value(ctx, buf, tmp, opts, depth + 1);

      value = tmp;
      compact++;
    }
  }

  if(opts->compact < 0) {
    compact = depth >= -opts->compact;
  } else if(INSPECT_INT32T_INRANGE(opts->compact) && opts->compact > 0) {
    int32_t deepest = 1;

    if(!js_is_arraybuffer(ctx, value))
      deepest = property_enumeration_deepest(ctx, value);
    const char* typestr = js_value_typestr(ctx, value);
    // printf("%s opts->compact = %d, deepest = %d, depth = %d\n", typestr ?
    // typestr :
    // "(null)", opts->compact, deepest, depth);

    compact = compact > deepest;
  }

  if(!(is_function = JS_IsFunction(ctx, value))) {
    is_array = js_is_array(ctx, value);
    is_typedarray = js_is_typedarray(value);

    if(!is_array && !is_typedarray) {
      if(js_is_arraybuffer(ctx, value) || js_is_sharedarraybuffer(ctx, value))
        return js_inspect_print_arraybuffer(ctx, buf, value, opts, depth + 1);

      if(js_is_map(ctx, value))
        return js_inspect_print_map(ctx, buf, value, opts, depth /*+ 1*/);
      if(js_is_set(ctx, value))
        return js_inspect_print_set(ctx, buf, value, opts, depth + 1);
      if(js_is_regexp(ctx, value))
        return js_inspect_print_regexp(ctx, buf, value, opts, depth + 1);
      /*if(JS_IsInstanceOf(ctx, value, proxy_ctor)) {
        dbuf_putstr(buf, "[Proxy]");
        return 0;
      }*/
    }

    if(js_object_tmpmark_isset(value)) {
      JS_ThrowTypeError(ctx, "circular reference");
      return -1;
    }

    if(js_is_generator(ctx, value)) {
      dbuf_putstr(buf, "Object [Generator] {}");
      js_cstring_free(ctx, s);
      return 0;
    }
  }

  if(!JS_IsArray(ctx, value) && !is_function) {
    if(s == 0)
      s = js_object_tostring(ctx, value);

    if(!strncmp(s, "[object ", 8)) {
      const char* e = strchr(s, ']');
      size_t slen = e - (s + 8);

      if(slen != 6 || memcmp(s + 8, "Object", 6)) {
        dbuf_putstr(buf, opts->colors ? COLOR_LIGHTRED : "[");
        // dbuf_putstr(buf, opts->colors ? COLOR_MARINE "[" : "[");
        dbuf_append(buf, (const uint8_t*)s + 8, e - (s + 8));
        // dbuf_putstr(buf, opts->colors ? "]" COLOR_NONE " " : "] ");
        dbuf_putstr(buf, opts->colors ? COLOR_NONE " " : "] ");
      }
    }
  }
  if(s)
    js_cstring_free(ctx, s);

  if(!(is_array || is_typedarray)) {
    BOOL show_hidden = opts->show_hidden;

    // printf("proto_chain: %i\n", opts->proto_chain);
    if((1 || opts->proto_chain
            ? js_object_getpropertynames_recursive
            : js_object_getpropertynames)(ctx,
                                          &propenum_tab,
                                          /* opts->proto_chain <= 0 ? JS_GetPrototype(ctx, value) : */ value,
                                          JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | (show_hidden ? 0 : JS_GPN_ENUM_ONLY)))
      return -1;
  }

  if(is_function) {
    JSValue name;
    dbuf_putstr(buf, opts->colors ? COLOR_MARINE "[Function" : "[Function");
    name = JS_GetPropertyStr(ctx, value, "name");
    if(!JS_IsUndefined(name)) {
      s = JS_ToCString(ctx, name);
      if(*s) {
        dbuf_putstr(buf, ": ");
        dbuf_putstr(buf, s);
      }
      js_cstring_free(ctx, s);
    }
    JS_FreeValue(ctx, name);
    dbuf_putstr(buf, opts->colors ? "]" COLOR_NONE : "]");
    /*  if(vector_size(&propenum_tab, sizeof(JSPropertyDescriptor)) && depth >= 0)
        dbuf_putc(buf, ' ');
      else*/
    goto end_obj;
  }
  if(depth < 0) {
    dbuf_put_colorstr(buf, is_array ? "[Array]" : "[Object]", COLOR_MARINE, opts->colors);
    goto end_obj;
  }

  js_object_tmpmark_set(value);

  if(is_array || is_typedarray) {
    len = js_array_length(ctx, value);
    dbuf_putstr(buf, compact && opts->break_length != INT32_MAX ? "[ " : "[");
    limit = min_size(opts->max_array_length, len);
    if(len && !compact && opts->break_length != INT32_MAX)
      inspect_newline(buf, INSPECT_LEVEL(opts) + 1);
    for(pos = 0; pos < len; pos++) {
      JSPropertyDescriptor desc;
      JSAtom prop;
      if(pos == limit)
        break;
      if(pos > 0) {
        dbuf_putstr(buf, ", ");
        // dbuf_putstr(buf, compact ? ", " : ",");
        if(!compact && opts->break_length != INT32_MAX)
          inspect_newline(buf, INSPECT_LEVEL(opts) + 1);
      }
      prop = JS_NewAtomUInt32(ctx, pos);
      memset(&desc, 0, sizeof(desc));
      desc.value = JS_UNDEFINED;
      JS_GetOwnProperty(ctx, &desc, value, prop);
      JS_FreeAtom(ctx, prop);

      if((desc.flags & JS_PROP_GETSET) && opts->getters) {
        int idx = (JS_IsUndefined(desc.getter) ? 0 : 1) | (JS_IsUndefined(desc.setter) ? 0 : 2);
        static const char* const strs[4] = {0, "[Getter]", "[Setter]", "[Getter/Setter]"};
        if(idx)
          dbuf_put_colorstr(buf, strs[idx], COLOR_MARINE, opts->colors);

      } else if(JS_HasProperty(ctx, value, JS_ATOM_TAG_INT | pos)) {
        /*  if(compact || opts->break_length == INT32_MAX)
            dbuf_putc(buf, ' ');*/
        js_inspect_print_value(ctx, buf, desc.value, opts, depth - 1);
      }
      js_propertydescriptor_free(ctx, &desc);
    }
    if(len && limit < len) {
      if(!compact && opts->break_length != INT32_MAX)
        inspect_newline(buf, INSPECT_LEVEL(opts) + 1);
      dbuf_printf(buf, "... %u more item", len - pos);
      if(pos + 1 < len)
        dbuf_putc(buf, 's');
    }
  }

  if(!is_array && !is_typedarray) {
    dbuf_putstr(buf, (compact && vector_size(&propenum_tab, sizeof(JSPropertyEnum))) ? "{ " : "{");
    len = 0;
  }

  for(pos = 0; pos < vector_size(&propenum_tab, sizeof(JSPropertyEnum)); pos++) {
    JSPropertyDescriptor desc;
    const char* name;
    JSPropertyEnum* propenum = (JSPropertyEnum*)vector_at(&propenum_tab, sizeof(JSPropertyEnum), pos);
    JSValue key = js_atom_tovalue(ctx, propenum->atom);
    name = JS_AtomToCString(ctx, propenum->atom);
    if((!JS_IsSymbol(key) && ((is_array || is_typedarray) && is_integer(name))) || inspect_options_hidden(opts, propenum->atom)) {
      JS_FreeValue(ctx, key);
      js_cstring_free(ctx, name);
      continue;
    }
    if(pos > 0)
      dbuf_putstr(buf, compact ? ", " : ",");
    if(!compact && opts->break_length != INT32_MAX)
      inspect_newline(buf, INSPECT_LEVEL(opts) + 1);
    if(!JS_IsSymbol(key) && (is_identifier(name) || is_integer(name))) {
      dbuf_putstr(buf, name);
    } else {
      if(!JS_IsString(key))
        dbuf_putc(buf, '[');
      js_inspect_print_value(ctx, buf, key, opts, depth - 1);
      if(!JS_IsString(key))
        dbuf_putc(buf, ']');
    }
    dbuf_putstr(buf, ": ");
    js_cstring_free(ctx, name);
    JS_FreeValue(ctx, key);

    if(js_get_propertydescriptor(ctx, &desc, value, propenum->atom) == TRUE) {
      if((desc.flags & JS_PROP_GETSET)) {
        if(!opts->getters) {
          // JSValue v = JS_GetProperty(ctx, value, propenum->atom);
          JSValue v = JS_Call(ctx, desc.getter, value, 0, 0);
          js_inspect_print_value(ctx, buf, v, opts, depth - 1);
          JS_FreeValue(ctx, v);
        } else
          dbuf_put_colorstr(buf,
                            JS_IsUndefined(desc.getter)   ? "[Setter]"
                            : JS_IsUndefined(desc.setter) ? "[Getter]"
                                                          : "[Getter/Setter]",
                            COLOR_MARINE,
                            opts->colors);
      } else {

        if(JS_IsObject(desc.value) && js_object_tmpmark_isset(desc.value))
          dbuf_putstr(buf, "\x1b[0;31m[Circular Reference]\x1b[0m");
        else
          js_inspect_print_value(ctx, buf, desc.value, opts, depth - 1);
      }
    }
    js_propertydescriptor_free(ctx, &desc);
    len++;
  }
  js_object_tmpmark_clear(value);

  if(!compact && len && opts->break_length != INT32_MAX)
    inspect_newline(buf, INSPECT_LEVEL(opts));
  dbuf_putstr(buf,
              (is_array || is_typedarray) ? ((compact || opts->break_length == INT32_MAX) && len ? " ]" : "]")
                                          : (compact && len ? " }" : "}"));

end_obj:
  if(!vector_empty(&propenum_tab))
    js_propertyenums_free(ctx, vector_begin(&propenum_tab), vector_size(&propenum_tab, sizeof(JSPropertyEnum)));

  return 0;
}

static int
js_inspect_print_value(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth) {
  int tag = JS_VALUE_GET_TAG(value);
  switch(tag) {
    case JS_TAG_FLOAT64:
    case JS_TAG_BIG_DECIMAL:
    case JS_TAG_BIG_INT:
    case JS_TAG_INT:
    case JS_TAG_BIG_FLOAT: {
      return js_inspect_print_number(ctx, buf, value, opts, depth);
    }

    case JS_TAG_BOOL: {
      if(opts->colors)
        dbuf_putstr(buf, COLOR_BROWN);
      dbuf_putstr(buf, JS_VALUE_GET_BOOL(value) ? "true" : "false");
      if(opts->colors)
        dbuf_putstr(buf, COLOR_NONE);
      break;
    }

    case JS_TAG_NULL: {
      dbuf_putstr(buf, opts->colors ? "\x1b[38;5;129mnull\x1b[m" : "null");
      break;
    }

    case JS_TAG_UNDEFINED: {
      dbuf_put_colorstr(buf, "undefined", COLOR_GRAY, opts->colors);
      break;
    }

    case JS_TAG_EXCEPTION: {
      const char* msg;
      JSValue message;
      dbuf_put_colorstr(buf, "[exception", COLOR_RED, opts->colors);

      if(JS_IsObject(ctx->rt->current_exception)) {
        message = JS_GetPropertyStr(ctx, ctx->rt->current_exception, "message");
        if((msg = JS_ToCString(ctx, message))) {
          dbuf_putstr(buf, " \"");
          dbuf_putstr(buf, msg);
          dbuf_putc(buf, '"');
          JS_FreeCString(ctx, msg);
        }
        JS_FreeValue(ctx, message);
      }
      dbuf_put_colorstr(buf, "]", COLOR_RED, opts->colors);
      break;
    }

    case JS_TAG_SYMBOL: {
      value = js_symbol_to_string(ctx, value);
      if(opts->colors)
        dbuf_putstr(buf, COLOR_GREEN);
      dbuf_putstr(buf, "Symbol");
      __attribute__((fallthrough));
    }

    case JS_TAG_STRING: {
      return js_inspect_print_string(ctx, buf, value, opts, depth);
    }

    case JS_TAG_OBJECT: {
      if(JS_IsError(ctx, value))
        return js_inspect_print_error(ctx, buf, value, opts, depth);

      return js_inspect_print_object(ctx, buf, value, opts, depth);
    }

    case JS_TAG_FUNCTION_BYTECODE: {
      dbuf_putstr(buf, opts->colors ? COLOR_LIGHTRED "[bytecode]" COLOR_NONE : "[bytecode]");
      break;
    }

    case JS_TAG_MODULE: {
      js_inspect_print_module(ctx, buf, JS_VALUE_GET_PTR(value), opts, depth - 1);
      break;
    }

    default: {
      JS_ThrowTypeError(ctx, "Unhandled value tag in js_inspect_print_value: %d\n", tag);
      return -1;
    }
  }
  return 0;
}

static JSValue
js_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  DynBuf dbuf;
  inspect_options_t options;
  int32_t level;
  int optind = 1;
  JSValue ret;

  js_dbuf_init(ctx, &dbuf);
  inspect_options_init(&options, ctx);

  if(argc > 1 && JS_IsNumber(argv[1]))
    optind++;

  if(optind < argc)
    inspect_options_get(&options, ctx, argv[optind]);

  if(optind > 1) {
    double d;
    JS_ToFloat64(ctx, &d, argv[1]);
    level = isinf(d) ? INT32_MAX : d;
  } else {
    level = 0;
  }

  /*printf("js_inspect break_length: %d, max_array_length: %d,
     max_string_length: %d\n", options.break_length, options.max_array_length,
     options.max_string_length);*/

  js_inspect_print_value(ctx, &dbuf, argv[0], &options, options.depth - level);

  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);

  dbuf_free(&dbuf);

  inspect_options_free(&options, ctx);

  return ret;
}

char*
js_inspect_tostring(JSContext* ctx, JSValueConst value) {
  DynBuf dbuf;
  inspect_options_t options;
  js_dbuf_init(ctx, &dbuf);
  inspect_options_init(&options, ctx);

  options.colors = FALSE;
  options.compact = 0;
  options.getters = TRUE;
  js_inspect_print_value(ctx, &dbuf, value, &options, options.depth);

  inspect_options_free(&options, ctx);

  dbuf_0(&dbuf);
  return (char*)dbuf.buf;
}

char*
js_inspect_atom(JSContext* ctx, JSAtom atom) {
  JSValue value;
  char* str;
  value = JS_AtomToValue(ctx, atom);

  str = js_inspect_tostring(ctx, value);
  JS_FreeValue(ctx, value);
  return str;
}

JSValue js_debugger_build_backtrace(JSContext* ctx, const uint8_t* cur_pc);

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
}

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

  inspect_custom_atom = js_inspect_custom_atom(ctx, "quickjs.inspect.custom");
  inspect_custom_atom_node = js_inspect_custom_atom(ctx, "nodejs.util.inspect.custom");

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
  m = JS_NewCModule(ctx, module_name, js_inspect_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_inspect_funcs, countof(js_inspect_funcs));
  JS_AddModuleExport(ctx, m, "default");
  return m;
}

/**
 * @}
 */
