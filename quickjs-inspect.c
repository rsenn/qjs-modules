#define _GNU_SOURCE

#include "cutils.h"
#include "iteration.h"
#include "list.h"
#include "property-enumeration.h"
#include "quickjs-internal.h"
#include "quickjs.h"
#include "utils.h"

#include <ctype.h>
#include <math.h>
#include <string.h>
#include <sys/ioctl.h>

#define INSPECT_INT32T_INRANGE(i) ((i) > INT32_MIN && (i) < INT32_MAX)
#define INSPECT_LEVEL(opts) ((opts)->depth - (depth))
#define INSPECT_IS_COMPACT(opts)                                                                                       \
  ((opts)->compact == INT32_MAX                                                                                        \
       ? TRUE                                                                                                          \
       : INSPECT_INT32T_INRANGE((opts)->compact) ? INSPECT_LEVEL(opts) >= (opts)->compact : 0)

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
  struct list_head hide_keys;
} inspect_options_t;

struct prop_key;

typedef struct prop_key {
  struct list_head link;
  const char* name;
  JSAtom atom;
} prop_key_t;

static int js_inspect_print(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth);

static JSValueConst global_object, object_ctor, object_proto, array_buffer_ctor, shared_array_buffer_ctor, map_ctor,
    set_ctor, regexp_ctor, proxy_ctor;

static void
inspect_options_init(inspect_options_t* opts) {
  opts->colors = TRUE;
  opts->show_hidden = FALSE;
  opts->custom_inspect = TRUE;
  opts->show_proxy = FALSE;
  opts->getters = FALSE;
  opts->string_break_newline = TRUE;
  opts->depth = INT32_MAX;
  opts->max_array_length = 100;
  opts->max_string_length = INT32_MAX;
  opts->break_length = 80;
  opts->compact = 5;
  opts->proto_chain = 5;
  init_list_head(&opts->hide_keys);
}

static void
inspect_options_free(inspect_options_t* opts, JSContext* ctx) {
  union {
    struct list_head* link;
    prop_key_t* key;
  } item, next;

  list_for_each_safe(item.link, next.link, &opts->hide_keys) {
    JS_FreeAtom(ctx, item.key->atom);
    js_cstring_free(ctx, item.key->name);
    js_free(ctx, item.key);
  }
  memset(&opts->hide_keys, 0, sizeof(opts->hide_keys));
}

static void
inspect_options_get(inspect_options_t* opts, JSContext* ctx, JSValueConst object) {
  JSValue value;
  value = JS_GetPropertyStr(ctx, object, "colors");
  if(!JS_IsException(value) && !JS_IsUndefined(value))
    opts->colors = JS_ToBool(ctx, value);
  js_value_free(ctx, value);

  value = JS_GetPropertyStr(ctx, object, "showHidden");
  if(!JS_IsException(value) && !JS_IsUndefined(value))
    opts->show_hidden = JS_ToBool(ctx, value);
  js_value_free(ctx, value);

  value = JS_GetPropertyStr(ctx, object, "customInspect");
  if(!JS_IsException(value) && !JS_IsUndefined(value))
    opts->custom_inspect = JS_ToBool(ctx, value);
  js_value_free(ctx, value);

  value = JS_GetPropertyStr(ctx, object, "showProxy");
  if(!JS_IsException(value) && !JS_IsUndefined(value))
    opts->show_proxy = JS_ToBool(ctx, value);
  js_value_free(ctx, value);

  value = JS_GetPropertyStr(ctx, object, "getters");
  if(!JS_IsException(value) && !JS_IsUndefined(value))
    opts->getters = JS_ToBool(ctx, value);
  js_value_free(ctx, value);

  value = JS_GetPropertyStr(ctx, object, "stringBreakNewline");
  if(!JS_IsException(value) && !JS_IsUndefined(value))
    opts->string_break_newline = JS_ToBool(ctx, value);
  js_value_free(ctx, value);

  value = JS_GetPropertyStr(ctx, object, "depth");
  if(!JS_IsException(value) && !JS_IsUndefined(value)) {
    if(isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->depth = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->depth, value);
    js_value_free(ctx, value);
  }
  value = JS_GetPropertyStr(ctx, object, "maxArrayLength");
  if(!JS_IsUndefined(value)) {
    if(isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->max_array_length = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->max_array_length, value);
    js_value_free(ctx, value);
  }
  value = JS_GetPropertyStr(ctx, object, "maxStringLength");
  if(!JS_IsUndefined(value)) {
    if(isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->max_string_length = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->max_string_length, value);
    js_value_free(ctx, value);
  }
  value = JS_GetPropertyStr(ctx, object, "breakLength");
  if(!JS_IsUndefined(value)) {
    if(isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->break_length = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->break_length, value);
    js_value_free(ctx, value);
  }
  value = JS_GetPropertyStr(ctx, object, "compact");
  if(!JS_IsUndefined(value)) {
    if(JS_VALUE_GET_TAG(value) == JS_TAG_BOOL && JS_VALUE_GET_BOOL(value) == 0)
      opts->compact = INT32_MIN;
    else if(JS_VALUE_GET_TAG(value) == JS_TAG_FLOAT64 && isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->compact = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->compact, value);
    js_value_free(ctx, value);
  }
  value = JS_GetPropertyStr(ctx, object, "hideKeys");
  if(JS_IsArray(ctx, value)) {
    int64_t len, pos;
    len = js_array_length(ctx, value);

    for(pos = 0; pos < len; pos++) {
      JSValue item = JS_GetPropertyUint32(ctx, value, pos);
      prop_key_t* key = js_mallocz(ctx, sizeof(prop_key_t));
      key->name = JS_ToCString(ctx, item);
      key->atom = JS_ValueToAtom(ctx, item);
      list_add(&key->link, &opts->hide_keys);
      js_value_free(ctx, item);
    }
    js_value_free(ctx, value);
  }
  value = JS_GetPropertyStr(ctx, object, "protoChain");
  if(JS_IsNumber(value))
    JS_ToInt32(ctx, &opts->proto_chain, value);
  js_value_free(ctx, value);
}

static JSValue
inspect_options_object(inspect_options_t* opts, JSContext* ctx) {
  JSValue arr, ret = JS_NewObject(ctx);
  uint32_t n;
  struct list_head* el;

  JS_SetPropertyStr(ctx, ret, "colors", JS_NewBool(ctx, opts->colors));
  JS_SetPropertyStr(ctx, ret, "showHidden", JS_NewBool(ctx, opts->show_hidden));
  JS_SetPropertyStr(ctx, ret, "customInspect", JS_NewBool(ctx, opts->custom_inspect));
  JS_SetPropertyStr(ctx, ret, "showProxy", JS_NewBool(ctx, opts->show_proxy));
  JS_SetPropertyStr(ctx, ret, "getters", JS_NewBool(ctx, opts->getters));
  JS_SetPropertyStr(ctx, ret, "stringBreakNewline", JS_NewBool(ctx, opts->string_break_newline));
  JS_SetPropertyStr(ctx, ret, "depth", js_new_number(ctx, opts->depth));
  JS_SetPropertyStr(ctx, ret, "maxArrayLength", js_new_number(ctx, opts->max_array_length));
  JS_SetPropertyStr(ctx, ret, "maxStringLength", js_new_number(ctx, opts->max_string_length));
  JS_SetPropertyStr(ctx, ret, "breakLength", js_new_number(ctx, opts->break_length));
  JS_SetPropertyStr(ctx, ret, "compact", js_new_bool_or_number(ctx, opts->compact));
  if(opts->proto_chain)
    JS_SetPropertyStr(ctx, ret, "protoChain", js_new_number(ctx, opts->proto_chain));
  arr = JS_NewArray(ctx);
  n = 0;
  list_for_each(el, &opts->hide_keys) {
    prop_key_t* key = list_entry(el, prop_key_t, link);
    JS_SetPropertyUint32(ctx, arr, n++, js_atom_tovalue(ctx, key->atom));
  }
  JS_SetPropertyStr(ctx, ret, "hideKeys", arr);
  return ret;
}

static int
inspect_options_hidden(inspect_options_t* opts, JSAtom atom) {
  struct list_head* el;
  list_for_each(el, &opts->hide_keys) {
    prop_key_t* key = list_entry(el, prop_key_t, link);
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

static inline int
inspect_screen_width(void) {
  struct winsize w = {.ws_col = -1, .ws_row = -1};
  ioctl(1, TIOCGWINSZ, &w);
  return w.ws_col;
}

static int
js_object_getpropertynames(JSContext* ctx, union Vector* propenum_tab, JSValueConst obj, int flags) {
  int ret;
  JSPropertyEnum* tmp_tab;
  uint32_t tmp_len;
  ret = JS_GetOwnPropertyNames(ctx, &tmp_tab, &tmp_len, obj, flags);
  vector_put(propenum_tab, tmp_tab, sizeof(JSPropertyEnum) * tmp_len);
  js_free(ctx, tmp_tab);
  return ret;
}

static void
js_inspect_constructors_get(JSContext* ctx) {
  global_object = JS_GetGlobalObject(ctx);
  object_ctor = JS_GetPropertyStr(ctx, global_object, "Object");
  array_buffer_ctor = JS_GetPropertyStr(ctx, global_object, "ArrayBuffer");
  shared_array_buffer_ctor = JS_GetPropertyStr(ctx, global_object, "SharedArrayBuffer");
  map_ctor = JS_GetPropertyStr(ctx, global_object, "Map");
  set_ctor = JS_GetPropertyStr(ctx, global_object, "Set");
  regexp_ctor = JS_GetPropertyStr(ctx, global_object, "RegExp");
  proxy_ctor = JS_GetPropertyStr(ctx, global_object, "Proxy");

  if(!JS_IsConstructor(ctx, array_buffer_ctor))
    JS_ThrowTypeError(ctx, "ArrayBuffer is not a constructor");
  if(!JS_IsConstructor(ctx, shared_array_buffer_ctor))
    JS_ThrowTypeError(ctx, "SharedArrayBuffer is not a constructor");
  if(!JS_IsConstructor(ctx, map_ctor))
    JS_ThrowTypeError(ctx, "Map is not a constructor");
  if(!JS_IsConstructor(ctx, set_ctor))
    JS_ThrowTypeError(ctx, "Set is not a constructor");
  if(!JS_IsConstructor(ctx, regexp_ctor))
    JS_ThrowTypeError(ctx, "RegExp is not a constructor");
  if(!JS_IsConstructor(ctx, proxy_ctor))
    JS_ThrowTypeError(ctx, "Proxy is not a constructor");

  object_proto = JS_GetPropertyStr(ctx, object_ctor, "prototype");
}

static void
js_inspect_constructors_free(JSContext* ctx) {
  js_value_free(ctx, object_ctor);
  js_value_free(ctx, object_proto);
  js_value_free(ctx, array_buffer_ctor);
  js_value_free(ctx, shared_array_buffer_ctor);
  js_value_free(ctx, map_ctor);
  js_value_free(ctx, set_ctor);
  js_value_free(ctx, regexp_ctor);
  js_value_free(ctx, global_object);
}

static JSAtom
js_inspect_custom_atom(JSContext* ctx) {
  JSValue key, sym;
  JSAtom atom;
  key = JS_NewString(ctx, "nodejs.util.inspect.custom");
  sym = js_symbol_invoke_static(ctx, "for", key);
  js_value_free(ctx, key);
  atom = JS_ValueToAtom(ctx, sym);
  js_value_free(ctx, sym);

  return atom;
}

static const char*
js_inspect_custom_call(JSContext* ctx, JSValueConst obj, inspect_options_t* opts, int32_t depth) {
  JSValue inspect;
  JSAtom inspect_custom;
  const char* str = 0;
  inspect_custom = js_inspect_custom_atom(ctx);
  inspect = JS_GetProperty(ctx, obj, inspect_custom);
  JS_FreeAtom(ctx, inspect_custom);
  if(!JS_IsFunction(ctx, inspect)) {
    js_value_free(ctx, inspect);
    inspect = JS_GetPropertyStr(ctx, obj, "inspect");
  }
  /*printf("js_inspect_custom_call ");
  js_value_print(ctx, inspect);*/
  if(JS_IsFunction(ctx, inspect)) {
    JSValueConst args[2];
    JSValue ret;
    args[0] = js_new_number(ctx, INSPECT_LEVEL(opts));
    args[1] = inspect_options_object(opts, ctx);
    ret = JS_Call(ctx, inspect, obj, 2, args);
    js_value_free(ctx, args[0]);
    js_value_free(ctx, args[1]);
    str = JS_ToCString(ctx, ret);
    js_value_free(ctx, ret);
  }
  js_value_free(ctx, inspect);
  return str;
}

static int
js_inspect_map(JSContext* ctx, DynBuf* buf, JSValueConst obj, inspect_options_t* opts, int32_t depth) {
  BOOL ret, finish = FALSE;
  size_t i = 0;
  int compact = INSPECT_IS_COMPACT(opts);
  // printf("js_inspect_map level=%u opts->depth=%" PRId32 " depth=%" PRId32 " compact=%i\n",
  // INSPECT_LEVEL(opts), opts->depth, depth, compact);
  JSValue data, key, value;
  Iteration it;
  if(!(ret = iteration_method_symbol(&it, ctx, obj, "iterator"))) {
    JS_ThrowTypeError(ctx, "js_inspect_map tag=%d\n", JS_VALUE_GET_TAG(obj));
    return 0;
  }
  dbuf_putstr(buf, "Map {");
  if(!compact)
    inspect_newline(buf, INSPECT_LEVEL(opts));
  for(i = 0; !(finish = iteration_next(&it, ctx)); i++) {
    if(!finish) {
      data = iteration_value(&it, ctx);
      if(i) {
        dbuf_putstr(buf, ",");
        if(!compact)
          inspect_newline(buf, INSPECT_LEVEL(opts));
      }
      dbuf_putstr(buf, compact ? " " : "  ");
      key = JS_GetPropertyUint32(ctx, data, 0);
      js_inspect_print(ctx, buf, key, opts, depth - 1);
      dbuf_putstr(buf, " => ");
      value = JS_GetPropertyUint32(ctx, data, 1);
      js_inspect_print(ctx, buf, value, opts, depth - 1);
      js_value_free(ctx, key);
      js_value_free(ctx, value);
      js_value_free(ctx, data);
    }
  }
  if(!compact)
    inspect_newline(buf, INSPECT_LEVEL(opts));
  dbuf_putstr(buf, compact ? " }" : "}");
  iteration_reset(&it, JS_GetRuntime(ctx));
  return 0;
}

static int
js_inspect_set(JSContext* ctx, DynBuf* buf, JSValueConst obj, inspect_options_t* opts, int32_t depth) {
  BOOL ret, finish = FALSE;
  size_t i = 0;
  int compact = INSPECT_IS_COMPACT(opts);
  JSValue value;
  Iteration it;

  if(!(ret = iteration_method_symbol(&it, ctx, obj, "iterator"))) {
    JS_ThrowTypeError(ctx, "js_inspect_map tag=%d\n", JS_VALUE_GET_TAG(obj));
    return 0;
  }
  dbuf_putstr(buf, "Set [");
  if(!compact)
    inspect_newline(buf, INSPECT_LEVEL(opts));
  for(i = 0; !(finish = iteration_next(&it, ctx)); i++) {
    if(!finish) {
      value = iteration_value(&it, ctx);
      if(i) {
        dbuf_putstr(buf, ",");
        if(!compact)
          inspect_newline(buf, INSPECT_LEVEL(opts));
      }
      dbuf_putstr(buf, compact ? " " : "  ");
      js_inspect_print(ctx, buf, value, opts, depth);
      js_value_free(ctx, value);
    }
  }
  if(!compact)
    inspect_newline(buf, INSPECT_LEVEL(opts));
  dbuf_putstr(buf, compact ? " ]" : "]");
  iteration_reset(&it, JS_GetRuntime(ctx));
  return 0;
}

static int
js_inspect_arraybuffer(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth) {
  const char *str, *str2;
  uint8_t* ptr;
  size_t i, slen, size;
  int break_len = inspect_screen_width();
  int column = dbuf_get_column(buf);
  JSValue proto;
  if(break_len > opts->break_length)
    break_len = opts->break_length;
  ptr = JS_GetArrayBuffer(ctx, &size, value);
  // printf("maxArrayLength: %i\n", opts->max_array_length);
  proto = JS_GetPrototype(ctx, value);
  str = js_object_tostring(ctx, proto);
  js_value_free(ctx, proto);

  if(str && (str2 = strstr(str, "ArrayBuffer"))) {
    while(str2 > str && !isspace(*--str2))
      ;
    slen = byte_chr(str2, strlen(str2), ']');
    dbuf_append(buf, (const uint8_t*)str2, slen);
  } else {
    if(JS_IsInstanceOf(ctx, value, array_buffer_ctor))
      dbuf_putstr(buf, "ArrayBuffer");
    if(JS_IsInstanceOf(ctx, value, shared_array_buffer_ctor))
      dbuf_putstr(buf, "SharedArrayBuffer");
  }
  if(str)
    js_cstring_free(ctx, str);

  dbuf_printf(buf, " { byteLength: %zu [", size);
  for(i = 0; i < size; i++) {
    if(i == (size_t)opts->max_array_length)
      break;
    if(column == break_len) {
      inspect_newline(buf, (opts->depth - depth) + 1);
      column = 0;
    } else {
      column += 3;
    }
    dbuf_printf(buf, " %02x", ptr[i]);
  }
  if(i < size)
    dbuf_printf(buf, "... %zu more bytes", size - i);
  dbuf_putstr(buf, " ] }");
  return 0;
}

static int
js_inspect_regexp(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth) {
  const char* str;
  str = JS_ToCString(ctx, value);
  if(opts->colors)
    dbuf_putstr(buf, "\x1b[0;31m");
  dbuf_putstr(buf, str);
  if(opts->colors)
    dbuf_putstr(buf, "\x1b[m");
  js_cstring_free(ctx, str);
  return 0;
}

static int
js_inspect_number(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth) {
  int tag = JS_VALUE_GET_TAG(value);
  const char* str;
  size_t len;
  str = JS_ToCStringLen(ctx, &len, value);
  if(tag != JS_TAG_SYMBOL && opts->colors)
    dbuf_putstr(buf, COLOR_YELLOW);
  dbuf_append(buf, (const uint8_t*)str, len);
  js_cstring_free(ctx, str);
  if(tag <= JS_TAG_BIG_FLOAT)
    dbuf_putc(buf, tag == JS_TAG_BIG_DECIMAL ? 'm' : tag == JS_TAG_BIG_FLOAT ? 'l' : 'n');
  if(opts->colors)
    dbuf_putstr(buf, COLOR_NONE);
  return 0;
}

static int
js_inspect_string(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth) {
  int tag = JS_VALUE_GET_TAG(value);
  int compact = INSPECT_IS_COMPACT(opts);

  const char* str;
  size_t pos, len, max_len, limit;
  str = JS_ToCStringLen(ctx, &len, value);
  max_len = min_size(opts->break_length - dbuf_get_column(buf) - 4, len);

  if(tag != JS_TAG_SYMBOL && opts->colors)
    dbuf_putstr(buf, COLOR_GREEN);
  dbuf_putc(buf, tag == JS_TAG_SYMBOL ? '(' : '\'');
  limit = min_size(opts->max_string_length, len);

  for(pos = 0; pos < limit;) {
    size_t n, eol;
    n = limit - pos;
    if(pos > 0) {
      dbuf_putstr(buf, opts->colors ? "'" COLOR_NONE " +" : "' +");
      inspect_newline(buf, INSPECT_LEVEL(opts) + 1);
      max_len = opts->break_length - (INSPECT_LEVEL(opts) * 2) - 4;
      dbuf_putstr(buf, opts->colors ? COLOR_GREEN "'" : "'");
    }
    if(!compact && opts->string_break_newline) {
      eol = byte_chr(&str[pos], n, '\n');
      if(str[pos + eol] == '\n') {
        eol++;
        n = ansi_truncate(&str[pos], eol, max_len);
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
    if(dbuf_get_column(buf) + 26 > opts->break_length)
      inspect_newline(buf, INSPECT_LEVEL(opts) + 1);
    dbuf_printf(buf, "... %zu more characters", len - pos);
  }
  return 0;
}

static int
js_inspect_print(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth) {
  int tag = JS_VALUE_GET_TAG(value);
  int compact = INSPECT_IS_COMPACT(opts);
  // printf("js_inspect_print level: %d\n", INSPECT_LEVEL(opts));
  switch(tag) {
    case JS_TAG_FLOAT64:
    case JS_TAG_BIG_DECIMAL:
    case JS_TAG_BIG_INT:
    case JS_TAG_BIG_FLOAT: {
      return js_inspect_number(ctx, buf, value, opts, depth);
    }

    case JS_TAG_INT: {
      int i = JS_VALUE_GET_INT(value);
      if(opts->colors)
        dbuf_putstr(buf, COLOR_YELLOW);
      dbuf_printf(buf, "%i", i);
      if(opts->colors)
        dbuf_putstr(buf, COLOR_NONE);
      break;
    }

    case JS_TAG_BOOL: {
      if(opts->colors)
        dbuf_putstr(buf, COLOR_YELLOW);
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
      dbuf_put_colorstr(buf, "[exception]", COLOR_RED, opts->colors);
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
      return js_inspect_string(ctx, buf, value, opts, depth);
    }

    case JS_TAG_OBJECT: {
      BOOL is_array = 0, is_typedarray = 0, is_function = 0;
      uint32_t pos, len, limit;
      Vector propenum_tab;
      const char* s;
      // compact = FALSE;

      if(INSPECT_INT32T_INRANGE(opts->compact)) {
        int32_t depth = property_enumeration_depth(ctx, value);
        // printf("opts->compact %d depth = %d\n", opts->compact, depth);

        compact = opts->compact >= depth;
      }

      if(opts->custom_inspect && (s = js_inspect_custom_call(ctx, value, opts, depth))) {
        dbuf_putstr(buf, s);
        js_cstring_free(ctx, s);
        return 0;
      }
      if(!(is_function = JS_IsFunction(ctx, value))) {
        if(JS_IsInstanceOf(ctx, value, array_buffer_ctor) || JS_IsInstanceOf(ctx, value, shared_array_buffer_ctor))
          return js_inspect_arraybuffer(ctx, buf, value, opts, depth + 1);
        if(JS_IsInstanceOf(ctx, value, map_ctor))
          return js_inspect_map(ctx, buf, value, opts, depth /*+ 1*/);
        if(JS_IsInstanceOf(ctx, value, set_ctor))
          return js_inspect_set(ctx, buf, value, opts, depth + 1);
        if(JS_IsInstanceOf(ctx, value, regexp_ctor))
          return js_inspect_regexp(ctx, buf, value, opts, depth + 1);
        /*if(JS_IsInstanceOf(ctx, value, proxy_ctor)) {
          dbuf_putstr(buf, "[Proxy]");
          return 0;
        }*/

        if(js_object_tmpmark_isset(value)) {
          JS_ThrowTypeError(ctx, "circular reference");
          return -1;
        }

        s = js_object_tostring(ctx, value);
        if(!strcmp(s, "[object Generator]")) {
          dbuf_putstr(buf, "Object [Generator] {}");
          js_cstring_free(ctx, s);
          return 0;
        }
        if(!(is_array = JS_IsArray(ctx, value)))
          is_typedarray = js_is_typedarray(ctx, value);
      }

      if(!is_array && !is_function && !strncmp(s, "[object ", 8)) {
        const char* e = strchr(s, ']');
        size_t slen = e - (s + 8);

        if(slen != 6 || memcmp(s + 8, "Object", 6)) {
          dbuf_putstr(buf, opts->colors ? COLOR_MARINE "[" : "[");
          dbuf_append(buf, (const uint8_t*)s + 8, e - (s + 8));
          dbuf_putstr(buf, opts->colors ? "]" COLOR_NONE " " : "] ");
        }
      }
      js_cstring_free(ctx, s);

      vector_init(&propenum_tab, ctx);

      if(js_object_getpropertynames(ctx,
                                    &propenum_tab,
                                    opts->proto_chain ? JS_GetPrototype(ctx, value) : value,
                                    JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK |
                                        (opts->show_hidden ? 0 : JS_GPN_ENUM_ONLY)))
        return -1;

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
        js_value_free(ctx, name);
        dbuf_putstr(buf, opts->colors ? "]" COLOR_NONE : "]");
        if(vector_size(&propenum_tab, sizeof(JSPropertyDescriptor)) && depth >= 0)
          dbuf_putc(buf, ' ');
        else
          goto end_obj;
      }
      if(depth < 0) {
        dbuf_put_colorstr(buf, is_array ? "[Array]" : "[Object]", COLOR_MARINE, opts->colors);
        goto end_obj;
      }

      js_object_tmpmark_set(value);

      if(is_array || is_typedarray) {
        len = js_array_length(ctx, value);
        dbuf_putstr(buf, compact ? "[ " : "[");
        limit = min_size(opts->max_array_length, len);
        if(len && !compact)
          inspect_newline(buf, INSPECT_LEVEL(opts) + 1);
        for(pos = 0; pos < len; pos++) {
          JSPropertyDescriptor desc;
          JSAtom prop;
          if(pos == limit)
            break;
          if(pos > 0) {
            dbuf_putc(buf, ',');
            //            dbuf_putstr(buf, compact ? ", " : ",");
            if(!compact)
              inspect_newline(buf, INSPECT_LEVEL(opts) + 1);
          }
          prop = JS_NewAtomUInt32(ctx, pos);
          memset(&desc, 0, sizeof(desc));
          desc.value = JS_UNDEFINED;
          JS_GetOwnProperty(ctx, &desc, value, prop);
          JS_FreeAtom(ctx, prop);

          if(desc.flags & JS_PROP_GETSET) {
            int idx = (JS_IsUndefined(desc.getter) ? 0 : 1) | (JS_IsUndefined(desc.setter) ? 0 : 2);
            static const char* const strs[4] = {0, "[Getter]", "[Setter]", "[Getter/Setter]"};
            if(idx)
              dbuf_put_colorstr(buf, strs[idx], COLOR_MARINE, opts->colors);
          } else if(!JS_IsUndefined(desc.value)) {
            /* if(!compact)*/ dbuf_putc(buf, ' ');
            js_inspect_print(ctx, buf, desc.value, opts, depth - 1);
          }
          js_propertydescriptor_free(ctx, &desc);
        }
        if(len && limit < len) {
          if(!compact)
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
        if(!JS_IsSymbol(key)) {
          if(((is_array || is_typedarray) && is_integer(name)) || inspect_options_hidden(opts, propenum->atom)) {
            js_value_free(ctx, key);
            js_cstring_free(ctx, name);
            continue;
          }
        }
        if(pos > 0)
          dbuf_putstr(buf, compact ? ", " : ",");
        if(!compact)
          inspect_newline(buf, INSPECT_LEVEL(opts) + 1);
        if(!JS_IsSymbol(key) && (is_identifier(name) || is_integer(name))) {
          dbuf_putstr(buf, name);
        } else {
          if(!JS_IsString(key))
            dbuf_putc(buf, '[');
          js_inspect_print(ctx, buf, key, opts, depth - 1);
          if(!JS_IsString(key))
            dbuf_putc(buf, ']');
        }
        dbuf_putstr(buf, ": ");
        js_cstring_free(ctx, name);
        js_value_free(ctx, key);
        JS_GetOwnProperty(ctx, &desc, value, propenum->atom);
        if(desc.flags & JS_PROP_GETSET)
          dbuf_put_colorstr(buf,
                            JS_IsUndefined(desc.getter) ? "[Setter]"
                                                        : JS_IsUndefined(desc.setter) ? "[Getter]" : "[Getter/Setter]",
                            COLOR_MARINE,
                            opts->colors);
        else
          js_inspect_print(ctx, buf, desc.value, opts, depth - 1);
        js_propertydescriptor_free(ctx, &desc);
        len++;
      }
      js_object_tmpmark_clear(value);

      if(!compact && len)
        inspect_newline(buf, INSPECT_LEVEL(opts));
      dbuf_putstr(buf, (is_array || is_typedarray) ? (compact && len ? " ]" : "]") : (compact && len ? " }" : "}"));

    end_obj:
      if(!vector_empty(&propenum_tab))
        js_propertyenums_free(ctx, vector_begin(&propenum_tab), vector_size(&propenum_tab, sizeof(JSPropertyEnum)));
      break;
    }

    default: {
      JS_ThrowTypeError(ctx, "Unhandled value tag in js_inspect_print: %d\n", tag);
      return -1;
    }
  }
  return 0;
}

static JSValue
js_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  DynBuf dbuf;
  inspect_options_t options;
  int32_t level;
  int optsind = 1;
  JSValue ret;

  js_inspect_constructors_get(ctx);
  js_dbuf_init(ctx, &dbuf);
  inspect_options_init(&options);

  if(argc > 1 && JS_IsNumber(argv[1]))
    optsind++;

  if(optsind < argc)
    inspect_options_get(&options, ctx, argv[optsind]);

  if(optsind > 1) {
    double d;
    JS_ToFloat64(ctx, &d, argv[1]);
    level = isinf(d) ? INT32_MAX : d;
  } else
    level = 0;

  // if(level)
  // printf("js_inspect level: %d\n", level);

  js_inspect_print(ctx, &dbuf, argv[0], &options, options.depth - level);

  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);

  dbuf_free(&dbuf);

  inspect_options_free(&options, ctx);
  js_inspect_constructors_free(ctx);

  return ret;
}

static const JSCFunctionListEntry js_inspect_funcs[] = {
    JS_CFUNC_DEF("inspect", 1, js_inspect),
};

static int
js_inspect_init(JSContext* ctx, JSModuleDef* m) {
  JSValue inspect;

  inspect = JS_NewCFunction(ctx, js_inspect, "inspect", 2);

  if(m) {
    JS_SetModuleExportList(ctx, m, js_inspect_funcs, countof(js_inspect_funcs));
    JS_SetModuleExport(ctx, m, "default", inspect);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
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
