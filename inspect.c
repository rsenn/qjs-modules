#include <quickjs.h>
#include <cutils.h>
#include <math.h>
#include <ctype.h>

#define COLOR_RED "\x1b[31m"
#define COLOR_GREEN "\x1b[32m"
#define COLOR_YELLOW "\x1b[33m"
#define COLOR_MARINE "\x1b[36m"
#define COLOR_GRAY "\x1b[1;30m"
#define COLOR_NONE "\x1b[m"
 
typedef struct {
  int colors : 1;
  int show_hidden : 1;
  int custom_inspect : 1;
  int show_proxy : 1;
  int getters : 1;
  int32_t depth;
  int32_t max_array_length;
  int32_t max_string_length;
  int32_t break_length;
  int32_t compact;

} inspect_options_t;

JSValue global_object;
// JSValue options_object;
JSAtom inspect_custom_atom;


static inline JSValue
new_number(JSContext* ctx, int32_t n) {
  if(n == INT32_MAX)
    return JS_NewFloat64(ctx, INFINITY);
  return JS_NewInt32(ctx, n);
}

static inline JSValue
new_bool_or_number(JSContext* ctx, int32_t n) {
  if(n == 0)
    return JS_NewBool(ctx, FALSE);
  return new_number(ctx, n);
}

static inline int
is_control_char(char c) {
  return c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v';
}

static inline int
is_escape_char(char c) {
  return is_control_char(c) || c == '\\' || c == '\'';
}

static inline char
escape_char(char c) {
  switch(c) {
    case '\t': return 't';
    case '\r': return 'r';
    case '\n': return 'n';
    case '\\': return '\\';
    case '\'': return '\'';
  }
}

static inline int
is_alphanumeric_char(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static inline int
is_digit_char(char c) {
  return c >= '0' && c <= '9';
}

static inline int
is_notdigit_char(char c) {
  return !is_digit_char(c);
}

static inline int
is_newline_char(char c) {
  return c == '\n';
}

static inline int
is_identifier_char(char c) {
  return is_alphanumeric_char(c) || c == '$' || c == '_';
}

static size_t
find_predicate(const char* s, size_t len, int (*pred)(char)) {
  size_t i;
  for(i = 0; i < len; i++) {
    if(pred(s[i]))
      break;
  }
  return i;
}

static size_t
nomatch_predicate(const char* s, size_t len, int (*pred)(char)) {
  size_t found = find_predicate(s, len, pred);
  return found == len;
}

static int
is_identifier(const char* s) {
  if(!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || *s == '$'))
    return 0;
  while(*++s) {
    if(!is_identifier_char(*s))
      return 0;
  }
  return 1;
}

static int
is_integer(const char* s) {
  if(!(*s >= '1' && *s <= '9') && !(*s == '0' && s[1] == '\0'))
    return 0;
  while(*++s) {
    if(!is_digit_char(*s))
      return 0;
  }
  return 1;
}

static size_t
skip_ansi(const char* s, size_t limit) {
  size_t i = 0;
  if(s[i] == 0x1b) {
    if(++i < limit && s[i] == '[') {
      while(++i < limit)
        if(is_alphanumeric_char(s[i]))
          break;
      if(++i < limit && s[i] == '~')
        ++i;
      return i;
    }
  }
  return 0;
}

static size_t
length_noansi(const char* str, size_t len) {
  size_t i, n = 0, p;
  for(i = 0; i < len; i++) {
    if((p = skip_ansi(&str[i], len - i)) > 0) {
      i += p;
      continue;
    }
    n++;
  }
  return n;
}

size_t
limit_noansi(const char* str, size_t len, size_t limit) {
  size_t i, n = 0, p;
  for(i = 0; i < len; i++) {
    if((p = skip_ansi(&str[i], len - i)) > 0) {
      i += p;
      continue;
    }
    n += is_escape_char(str[i]) ? 2 : 1;
    if(n > limit)
      break;
  }
  return i;
}

static void
put_string(DynBuf* buf, const char* str, size_t len) {
  size_t i = 0, j;
  while(i < len) {
    j = find_predicate(&str[i], len - i, is_escape_char);
    if(j)
      dbuf_put(buf, &str[i], j);
    if((i += j) == len)
      break;
    dbuf_putc(buf, '\\');
    dbuf_putc(buf, escape_char(str[i]));
    i++;
  }
}

static inline const char*
last_line(DynBuf* buf, size_t* len) {
  size_t i;
  for(i = buf->size; i > 0; i--)
    if(buf->buf[i - 1] == '\n')
      break;
  if(len)
    *len = buf->size - i;
  return &buf->buf[i];
}

static size_t
get_column(DynBuf* buf) {
  size_t len;
  const char* s;
  s = last_line(buf, &len);
  return length_noansi(s, len);
}

static JSValue
invoke_symbol_static_method(JSContext* ctx, const char* name, JSValueConst arg) {

  JSAtom method_name = JS_NewAtom(ctx, name);
  JSValue symbol_ctor = JS_GetPropertyStr(ctx, global_object, "Symbol");
  return JS_Invoke(ctx, symbol_ctor, method_name, 1, &arg);
}

static JSAtom
get_inspect_custom_atom(JSContext* ctx) {
  static JSValue symbol_value = {.u = {.int32 = 0}, .tag = JS_TAG_UNDEFINED};

  if(JS_IsUndefined(symbol_value)) {
    JSValue key = JS_NewString(ctx, "nodejs.util.inspect.custom");
    symbol_value = invoke_symbol_static_method(ctx, "for", key);
    JS_FreeValue(ctx, key);
    inspect_custom_atom = JS_ValueToAtom(ctx, symbol_value);
  }
  return inspect_custom_atom;
}

static void
js_inspect_options_init(inspect_options_t* opts) {
  opts->colors = TRUE;
  opts->show_hidden = FALSE;
  opts->custom_inspect = TRUE;
  opts->show_proxy = FALSE;
  opts->getters = FALSE;
  opts->depth = 2;
  opts->max_array_length = 100;
  opts->max_string_length = INT32_MAX;
  opts->break_length = 80;
  opts->compact = 3;
}

static void
js_inspect_options_get(JSContext* ctx, JSValueConst object, inspect_options_t* opts) {
  JSValue value;
  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "colors"))))
    opts->colors = JS_ToBool(ctx, value);

  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "showHidden"))))
    opts->show_hidden = JS_ToBool(ctx, value);

  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "customInspect"))))
    opts->custom_inspect = JS_ToBool(ctx, value);

  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "showProxy"))))
    opts->show_proxy = JS_ToBool(ctx, value);

  if(!JS_IsUndefined((value = JS_GetPropertyStr(ctx, object, "getters"))))
    opts->getters = JS_ToBool(ctx, value);

  value = JS_GetPropertyStr(ctx, object, "depth");
  if(!JS_IsUndefined(value)) {
    if(isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->depth = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->depth, value);
  }

  value = JS_GetPropertyStr(ctx, object, "maxArrayLength");
  if(!JS_IsUndefined(value)) {
    if(isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->max_array_length = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->max_array_length, value);
  }

  value = JS_GetPropertyStr(ctx, object, "maxStringLength");
  if(!JS_IsUndefined(value)) {
    if(isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->max_string_length = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->max_string_length, value);
  }

  value = JS_GetPropertyStr(ctx, object, "breakLength");
  if(!JS_IsUndefined(value)) {
    if(isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->break_length = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->break_length, value);
  }

  value = JS_GetPropertyStr(ctx, object, "compact");
  if(!JS_IsUndefined(value)) {
    if(isinf(JS_VALUE_GET_FLOAT64(value)))
      opts->compact = INT32_MAX;
    else
      JS_ToInt32(ctx, &opts->compact, value);
  }
}

static JSValue
js_inspect_options_object(JSContext* ctx, inspect_options_t* opts) {
  JSValue ret = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, ret, "colors", JS_NewBool(ctx, opts->colors));
  JS_SetPropertyStr(ctx, ret, "showHidden", JS_NewBool(ctx, opts->show_hidden));
  JS_SetPropertyStr(ctx, ret, "customInspect", JS_NewBool(ctx, opts->custom_inspect));
  JS_SetPropertyStr(ctx, ret, "showProxy", JS_NewBool(ctx, opts->show_proxy));
  JS_SetPropertyStr(ctx, ret, "getters", JS_NewBool(ctx, opts->getters));

  JS_SetPropertyStr(ctx, ret, "depth", new_number(ctx, opts->depth));
  JS_SetPropertyStr(ctx, ret, "maxArrayLength", new_number(ctx, opts->max_array_length));
  JS_SetPropertyStr(ctx, ret, "maxStringLength", new_number(ctx, opts->max_string_length));
  JS_SetPropertyStr(ctx, ret, "breakLength", new_number(ctx, opts->break_length));
  JS_SetPropertyStr(ctx, ret, "compact", new_bool_or_number(ctx, opts->compact));

  return ret;
}

static const char*
js_inspect_call_custom(JSContext* ctx, JSValueConst obj, inspect_options_t* opts, int32_t depth) {
  JSValue inspect;

  inspect = JS_GetProperty(ctx, obj, get_inspect_custom_atom(ctx));
  if(JS_IsUndefined(inspect))
    inspect = JS_GetPropertyStr(ctx, obj, "inspect");

  if(JS_IsFunction(ctx, inspect)) {
    JSValueConst args[2];
    args[0] = JS_NewInt32(ctx, depth);
    args[1] = js_inspect_options_object(ctx, opts);

    return JS_ToCString(ctx, JS_Call(ctx, inspect, obj, 2, args));
  }
  return 0;
}

static int
js_inspect_newline(DynBuf* buf, int32_t depth) {
  dbuf_putc(buf, '\n');

  while(depth-- > 0) dbuf_putstr(buf, "  ");
}

static int
js_inspect_print(JSContext* ctx, DynBuf* buf, JSValueConst value, inspect_options_t* opts, int32_t depth) {

  int tag = JS_VALUE_GET_TAG(value);
  int level = opts->depth - depth;
  int compact = (level - opts->compact) > 0;

  switch(tag) {
    case JS_TAG_BIG_DECIMAL:
    case JS_TAG_BIG_INT:
    case JS_TAG_BIG_FLOAT: {
      const char* str;
      size_t len;
      str = JS_ToCStringLen(ctx, &len, value);
      if(tag != JS_TAG_SYMBOL && opts->colors)
        dbuf_putstr(buf, COLOR_YELLOW);
      dbuf_put(buf, (const uint8_t*)str, len);
      dbuf_putc(buf, tag == JS_TAG_BIG_DECIMAL ? 'm' : tag == JS_TAG_BIG_FLOAT ? 'l' : 'n');
      if(opts->colors)
        dbuf_putstr(buf, COLOR_NONE);
      break;
    }
    case JS_TAG_SYMBOL: {
      value = invoke_symbol_static_method(ctx, "keyFor", value);
      if(opts->colors)
        dbuf_putstr(buf, COLOR_GREEN);
      dbuf_putstr(buf, "Symbol");
    }
    case JS_TAG_STRING: {
      const char* str;
      size_t pos, len, max_len, limit;
      str = JS_ToCStringLen(ctx, &len, value);
      max_len = min_uint32(opts->break_length - get_column(buf) - 4, len);

      if(tag != JS_TAG_SYMBOL && opts->colors)
        dbuf_putstr(buf, COLOR_GREEN);
      dbuf_putc(buf, tag == JS_TAG_SYMBOL ? '(' : '\'');
      limit = min_uint32(opts->max_string_length, len);
      for(pos = 0; pos < limit;) {
        size_t n, eol;
        eol = find_predicate(&str[pos], limit - pos, is_newline_char);
        if(str[pos + eol] == '\n')
          eol++;

        n = min_uint32(limit_noansi(&str[pos], limit - pos, max_len), eol);

        if(pos > 0) {
          dbuf_putstr(buf, opts->colors ? "'" COLOR_NONE " +" : "' +");
          js_inspect_newline(buf, level + 1);
          dbuf_putstr(buf, opts->colors ? COLOR_GREEN "'" : "'");
        } else {
          max_len = opts->break_length - get_column(buf) - 4;
        }

        put_string(buf, &str[pos], n);
        pos += n;
      }
      dbuf_putc(buf, tag == JS_TAG_SYMBOL ? ')' : '\'');
      if(opts->colors)
        dbuf_putstr(buf, COLOR_NONE);
      if(limit < len) {
        if(get_column(buf) + 26 > opts->break_length)
          js_inspect_newline(buf, level + 1);
        dbuf_printf(buf, "... %zu more characters", len - pos);
      }
      break;
    }
    case JS_TAG_OBJECT: {
      int is_array = JS_IsArray(ctx, value);
      uint32_t nprops, pos, len, limit;
      JSPropertyEnum* props;
      const char* s;

      if((s = js_inspect_call_custom(ctx, value, opts, depth))) {
        dbuf_putstr(buf, s);
        return 0;
      }

      if(JS_GetOwnPropertyNames(ctx, &props, &nprops, value, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | (opts->show_hidden ? 0 : JS_GPN_ENUM_ONLY)))
        return -1;

      if(JS_IsFunction(ctx, value)) {
        JSValue name;

        dbuf_putstr(buf, opts->colors ? COLOR_MARINE "[Function" : "[Function");
        name = JS_GetPropertyStr(ctx, value, "name");
        if(!JS_IsUndefined(name)) {
          s = JS_ToCString(ctx, name);
          dbuf_putstr(buf, ": ");
          dbuf_putstr(buf, s);
          JS_FreeCString(ctx, s);
        }

        dbuf_putstr(buf, opts->colors ? "]" COLOR_NONE : "]");

        compact = 1;
        if(nprops)
          dbuf_putc(buf, ' ');
        else
          return 0;
      }

      if(depth < 0) {
        dbuf_putstr(buf,
                    is_array ? (opts->colors ? COLOR_MARINE "[Array]" COLOR_NONE : "[Array]")
                             : (opts->colors ? COLOR_MARINE "[Object]" COLOR_NONE : "[Object]"));
        return 0;
      }

      if(is_array) {
        JS_ToUint32(ctx, &len, JS_GetPropertyStr(ctx, value, "length"));
        dbuf_putstr(buf, compact ? "[ " : "[");
        if(!compact)
          js_inspect_newline(buf, level + 1);

        limit = min_uint32(opts->max_array_length, len);

        for(pos = 0; pos < len; pos++) {
          if(pos > 0) {
            dbuf_putstr(buf, compact ? ", " : ",");
            if(!compact)
              js_inspect_newline(buf, level + 1);
          }
          if(pos == limit)
            break;
          js_inspect_print(ctx, buf, JS_GetPropertyUint32(ctx, value, pos), opts, depth - 1);
        }
        if(limit < len) {
          if(get_column(buf) + 20 > opts->break_length)
            js_inspect_newline(buf, level + 1);
          dbuf_printf(buf, "... %u more item", len - pos);
          if(pos + 1 < len)
            dbuf_putc(buf, 's');
        }

        if(nprops <= len) {
          if(!compact)
            js_inspect_newline(buf, level);
          dbuf_putstr(buf, compact ? " ]" : "]");
          return 0;
        }
      }

      if(!is_array) {
        dbuf_putstr(buf, compact ? "{ " : "{");
        if(!compact)
          js_inspect_newline(buf, level + 1);
      }

      for(pos = 0; pos < nprops; pos++) {
        JSPropertyDescriptor desc;
        const char* name;
        name = JS_AtomToCString(ctx, props[pos].atom);

        if(is_array && is_integer(name)) {
          JS_FreeCString(ctx, name);
          continue;
        }

        if(pos > 0) {
          dbuf_putstr(buf, compact ? ", " : ",");
          if(!compact)
            js_inspect_newline(buf, level + 1);
        }

        if(is_identifier(name))
          dbuf_putstr(buf, name);
        else
          js_inspect_print(ctx, buf, JS_AtomToValue(ctx, props[pos].atom), opts, depth - 1);
        dbuf_putstr(buf, ": ");
        JS_FreeCString(ctx, name);

        JS_GetOwnProperty(ctx, &desc, value, props[pos].atom);

        if(desc.flags & JS_PROP_GETSET)
          dbuf_putstr(buf,
                      JS_IsUndefined(desc.getter)
                          ? COLOR_MARINE "[Setter]" COLOR_NONE
                          : JS_IsUndefined(desc.setter) ? COLOR_MARINE "[Getter]" COLOR_NONE : COLOR_MARINE "[Getter/Setter]" COLOR_NONE);
        else
          js_inspect_print(ctx, buf, desc.value, opts, depth - 1);
      }
      if(!compact)
        js_inspect_newline(buf, level);
      dbuf_putstr(buf, is_array ? (compact ? " ]" : "]") : (compact ? " }" : "}"));
      break;
    }
    case JS_TAG_INT: {
      int i = JS_VALUE_GET_INT(value);
      if(opts->colors)
        dbuf_putstr(buf, COLOR_YELLOW);
      if(i < 0) {
        dbuf_putc(buf, '-');
        i = -i;
      }
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
      dbuf_putstr(buf, "null");
      break;
    }
    case JS_TAG_UNDEFINED: {
      dbuf_putstr(buf, opts->colors ? COLOR_GRAY "undefined" COLOR_NONE : "undefined");
      break;
    }

    case JS_TAG_EXCEPTION: {
      dbuf_putstr(buf, opts->colors ? COLOR_RED "[exception]" COLOR_NONE : "[exception]");
      break;
    }
    case JS_TAG_FLOAT64: {
      double d = JS_VALUE_GET_FLOAT64(value);
      if(opts->colors)
        dbuf_putstr(buf, COLOR_YELLOW);
      if(d == -INFINITY)
        dbuf_putstr(buf, "-Infinity");
      else if(d == INFINITY)
        dbuf_putstr(buf, "Infinity");
      else if(JS_VALUE_IS_NAN(value))
        dbuf_putstr(buf, "NaN");
      else
        dbuf_printf(buf, "%lg", d);
      if(opts->colors)
        dbuf_putstr(buf, COLOR_NONE);
      break;
    }
    default: {
      return INT32_MAX;
    }
  }
}

static JSValue
js_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  DynBuf dbuf;
  inspect_options_t options;

  dbuf_init(&dbuf);

  js_inspect_options_init(&options);

  if(argc > 1)
    js_inspect_options_get(ctx, argv[1], &options);

  js_inspect_print(ctx, &dbuf, argv[0], &options, options.depth);

  return JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
}

static const JSCFunctionListEntry js_inspect_funcs[] = {JS_CFUNC_DEF("inspect", 1, js_inspect)};

static int
js_inspect_init(JSContext* ctx, JSModuleDef* m) {
  JSValue inspect;

  JS_SetModuleExportList(ctx, m, js_inspect_funcs, countof(js_inspect_funcs));

  inspect = JS_NewCFunction(ctx, js_inspect, "inspect", 2);
  JS_SetModuleExport(ctx, m, "default", inspect);

  global_object = JS_GetGlobalObject(ctx);
  // options_object = JS_UNDEFINED;
  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_inspect
#endif

JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_inspect_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_inspect_funcs, countof(js_inspect_funcs));
  JS_AddModuleExport(ctx, m, "default");
  return m;
}
