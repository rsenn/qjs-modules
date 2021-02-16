#define _GNU_SOURCE
#include "quickjs.h"
#include "cutils.h"
#include "vector.h"
#include "property-enumeration.h"

#include <stdint.h>

#define WS 0x01
#define START 0x02
#define END 0x04
#define QUOTE 0x08
#define CLOSE 0x10
#define EQUAL 0x20
#define SPECIAL 0x40
#define SLASH 0x80
#define BACKSLASH 0x100
#define QUESTION 0x200
#define EXCLAM 0x400
#define HYPHEN 0x400

static int chars[256] = {0};

typedef struct {
  const uint8_t* x;
  size_t n;
  void (*free)(JSContext*, const char*);
} InputValue;

void
character_classes_init(int c[256]) {
  c[(unsigned char)' '] = WS;
  c[(unsigned char)'\t'] = WS;
  c[(unsigned char)'\r'] = WS;
  c[(unsigned char)'\n'] = WS;
  c[(unsigned char)'!'] = SPECIAL | EXCLAM;
  c[(unsigned char)'"'] = QUOTE;
  c[(unsigned char)'/'] = END | SLASH;
  c[(unsigned char)'<'] = START;
  c[(unsigned char)'='] = EQUAL;
  c[(unsigned char)'>'] = END | CLOSE;
  c[(unsigned char)'?'] = SPECIAL | QUESTION;
  c[(unsigned char)'\\'] = BACKSLASH;
  c[(unsigned char)'-'] = HYPHEN;
}

static void input_value_free_default(JSContext* ctx, const char* str){};

static InputValue
js_value_to_bytes(JSContext* ctx, JSValueConst value) {
  InputValue ret = {0, 0, input_value_free_default};

  if(JS_IsString(value)) {
    ret.x = (const uint8_t*)JS_ToCStringLen(ctx, &ret.n, value);
    ret.free = JS_FreeCString;
  } else {
    ret.x = JS_GetArrayBuffer(ctx, &ret.n, value);
  }
  return ret;
}

#define end()                                                                                                          \
  (vector_size(&st, sizeof(OutputValue)) >= 2                                                                          \
       ? (vector_pop(&st, sizeof(OutputValue)), out = vector_back(&st, sizeof(OutputValue)))                           \
       : 0)
#define next() ((c = *++ptr), ptr >= end ? done = TRUE : 0)
#define skip(cond)                                                                                                     \
  do {                                                                                                                 \
    c = *ptr;                                                                                                          \
    if(!(cond))                                                                                                        \
      break;                                                                                                           \
    if(++ptr >= end)                                                                                                   \
      done = TRUE;                                                                                                     \
  } while(!done)

#define skipws() skip(chars[c] & WS)

static void
js_set_attr_value(JSContext* ctx, JSValueConst obj, const uint8_t* attr, size_t alen, const uint8_t* str, size_t slen) {
  char* key;
  JSValue value;
  key = js_strndup(ctx, (const char*)attr, alen);
  value = JS_NewStringLen(ctx, (const char*)str, slen);
  JS_DefinePropertyValueStr(ctx, obj, key, value, JS_PROP_ENUMERABLE);
  js_free(ctx, key);
  // JS_FreeValue(ctx, value);
}

typedef struct {
  uint32_t idx;
  JSValue obj;
  const uint8_t* name;
  size_t namelen;
} OutputValue;

static JSValue
js_xml_parse(JSContext* ctx, const uint8_t* buf, size_t len) {

  BOOL done = FALSE;
  JSValue ret;
  const uint8_t *ptr, *end, *start;
  uint8_t c;
  size_t n;
  OutputValue* out;
  JSValue element = JS_UNDEFINED;
  vector st = VECTOR_INIT();
  ptr = buf;
  end = buf + len;

  ret = JS_NewArray(ctx);

  out = vector_push(&st, sizeof(OutputValue));
  out->obj = ret;
  out->idx = 0;

  while(!done) {
    skipws();
    start = ptr;
    skip(!(chars[c] & START));
    if(ptr > start) {
      JSValue str = JS_NewStringLen(ctx, start, ptr - start);
      JS_SetPropertyUint32(ctx, out->obj, out->idx++, str);
    }
    if(chars[c] & START) {
      const uint8_t* name;
      size_t namelen;
      BOOL closing = FALSE, self_closing = FALSE;
      next();
      if(chars[c] & SLASH) {
        closing = TRUE;
        next();
      }
      name = ptr;
      skip(!(chars[c] & (WS | END)));
      namelen = ptr - name;
      if(closing) {
        skipws();
        if(chars[c] & CLOSE)
          next();
        if(out->namelen == namelen && !memcmp(out->name, name, namelen)) {
          end();
          continue;
        }
      }
      element = JS_NewObject(ctx);
      js_set_attr_value(ctx, element, "tagName", 7, name, namelen);
      if(namelen && (chars[name[0]] & (QUESTION | EXCLAM)))
        self_closing = TRUE;
      if(n >= 3 && (chars[*start] & EXCLAM) && (chars[start[1]] & HYPHEN) && (chars[start[2]] & HYPHEN)) {
        while(!done) {
          next();
          if(end - ptr >= 3 && (chars[*start] & HYPHEN) && (chars[start[1]] & HYPHEN) && (chars[start[2]] & CLOSE)) {
            ptr += 3;
            break;
          }
        }
      } else {
        if(!closing) {
          const uint8_t *attr, *value;
          size_t alen, vlen;
          JSValue attributes = JS_NewObject(ctx);
          JS_SetPropertyStr(ctx, element, "attributes", attributes);
          while(!done) {
            skipws();
            if(chars[c] & END)
              break;
            attr = ptr;
            skip(!(chars[c] & (EQUAL | WS | SPECIAL | CLOSE)));
            if((alen = ptr - attr) == 0)
              break;
            if(chars[c] & (WS | CLOSE | SLASH)) {
              char* key;
              key = js_strndup(ctx, (const char*)attr, alen);
              JS_DefinePropertyValueStr(ctx, attributes, key, JS_NewBool(ctx, TRUE), JS_PROP_ENUMERABLE);
              js_free(ctx, key);
              continue;
            }
            if(chars[c] & EQUAL) {
              next();
              if(chars[c] & QUOTE)
                next();
              value = ptr;
              skip(!(chars[c] & QUOTE));
              vlen = ptr - value;
              if(chars[c] & QUOTE)
                next();
              js_set_attr_value(ctx, attributes, attr, alen, value, vlen);
            }
          }
          if(chars[c] & SLASH) {
            self_closing = TRUE;
            next();
          }
        }
        JS_SetPropertyUint32(ctx, out->obj, out->idx++, element);
        if(!closing && !self_closing) {
          out = vector_push(&st, sizeof(OutputValue));
          out->obj = JS_NewArray(ctx);
          out->idx = 0;
          out->name = name;
          out->namelen = namelen;
          JS_SetPropertyStr(ctx, element, "children", out->obj);
        }
        if(!closing) {
          if(chars[name[0]] & (QUESTION | EXCLAM)) {
            if(chars[c] == chars[name[0]])
              next();
            end();
          }
        }
        skipws();
        if(chars[c] & CLOSE)
          next();
      }
    }
  }
  return ret;
}

static JSValue
js_xml_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret;
  InputValue input = js_value_to_bytes(ctx, argv[0]);

  ret = js_xml_parse(ctx, input.x, input.n);

  input.free(ctx, input.x);
  return ret;
}

static const char*
js_get_property_string(JSContext* ctx, JSValueConst obj, JSAtom prop) {
  JSValue value;
  const char* str;
  value = JS_GetProperty(ctx, obj, prop);
  str = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);
  return str;
}

static void
xml_close_element(JSContext* ctx, JSValueConst element, DynBuf* db, int32_t depth) {
  JSAtom atagName = JS_NewAtom(ctx, "tagName");
  JSAtom achildren = JS_NewAtom(ctx, "children");

  JSValue tag = JS_GetProperty(ctx, element, atagName);
  JSValue childNodes = JS_GetProperty(ctx, element, achildren);

  if(JS_IsArray(ctx, childNodes)) {
    const char* tagStr;
    size_t tagLen;

    tagStr = JS_ToCStringLen(ctx, &tagLen, tag);

    while(depth-- > 0) dbuf_putstr(db, "  ");

    dbuf_putstr(db, "</");
    dbuf_put(db, tagStr, tagLen);
    dbuf_putstr(db, ">");
    dbuf_putc(db, '\n');
    JS_FreeCString(ctx, tagStr);
  }

  JS_FreeValue(ctx, tag);
  JS_FreeValue(ctx, childNodes);
  JS_FreeAtom(ctx, atagName);
  JS_FreeAtom(ctx, achildren);
}

static PropertyEnumeration*
xml_enumeration_next(vector* vec, JSContext* ctx, DynBuf* db) {
  PropertyEnumeration* it;
  JSValue value = JS_UNDEFINED, children;
  int32_t type;
  it = vector_back(vec, sizeof(PropertyEnumeration));
  for(;;) {
    value = property_enumeration_value(it, ctx);
    type = JS_VALUE_GET_TAG(value);
    JS_FreeValue(ctx, value);
    if(type == JS_TAG_OBJECT) {
      children = JS_GetPropertyStr(ctx, value, "children");
      if(!JS_IsUndefined(children)) {
        it = property_enumeration_push(vec, ctx, children, PROPERTY_ENUMERATION_DEFAULT_FLAGS);
        if(it && property_enumeration_setpos(it, 0))
          goto end;
      } else {
        if(property_enumeration_setpos(it, it->idx + 1))
          goto end;
      }
    }
    for(;;) {
      if((it = property_enumeration_pop(vec, ctx)) == 0)
        goto end;

      value = property_enumeration_value(it, ctx);
      xml_close_element(ctx, value, db, (int32_t)vector_size(vec, sizeof(PropertyEnumeration)) - 1);
      JS_FreeValue(ctx, value);

      if(property_enumeration_setpos(it, it->idx + 1))
        goto end;
    }
  end:
    if(!it)
      break;
    break;
  }
  return it;
}
static void
xml_write_value(JSContext* ctx, JSValueConst value, DynBuf* db) {
  const char* str;
  size_t len;
  str = JS_ToCStringLen(ctx, &len, value);
  dbuf_put(db, str, len);
  JS_FreeCString(ctx, str);
}

static void
xml_write_attributes(JSContext* ctx, JSValueConst attributes, DynBuf* db) {
  size_t i;
  PropertyEnumeration props = {0};
  property_enumeration_init(&props, ctx, attributes, PROPERTY_ENUMERATION_DEFAULT_FLAGS);
  for(i = 0; i < props.tab_atom_len; i++) {
    const char *keystr, *valuestr;
    JSValue value;
    property_enumeration_setpos(&props, i);
    keystr = property_enumeration_keystr(&props, ctx);
    value = property_enumeration_value(&props, ctx);
    dbuf_putc(db, ' ');
    dbuf_putstr(db, keystr);
    if(!(JS_IsBool(value) && JS_ToBool(ctx, value))) {
      valuestr = property_enumeration_valuestr(&props, ctx);
      dbuf_putstr(db, "=\"");
      dbuf_putstr(db, valuestr);
      JS_FreeCString(ctx, valuestr);
    }
    dbuf_putc(db, '"');
    JS_FreeCString(ctx, keystr);
    JS_FreeValue(ctx, value);
  }
}

static void
xml_write_element(JSContext* ctx, JSValueConst element, DynBuf* db, int32_t depth) {
  JSAtom atagName = JS_NewAtom(ctx, "tagName");
  JSAtom aattributes = JS_NewAtom(ctx, "attributes");
  JSAtom achildren = JS_NewAtom(ctx, "children");

  JSValue tag = JS_GetProperty(ctx, element, atagName);
  JSValue attrs = JS_GetProperty(ctx, element, aattributes);
  JSValue childNodes = JS_GetProperty(ctx, element, achildren);

  const char* tagStr = JS_ToCString(ctx, tag);

  while(depth-- > 0) dbuf_putstr(db, "  ");

  dbuf_putc(db, '<');
  dbuf_putstr(db, tagStr);

  if(JS_IsObject(attrs))
    xml_write_attributes(ctx, attrs, db);
  dbuf_putstr(db, JS_IsObject(childNodes) ? ">" : tagStr[0] == '?' ? "?>" : tagStr[0] == '!' ? "!>" : " />");
  dbuf_putc(db, '\n');

  JS_FreeCString(ctx, tagStr);

  JS_FreeValue(ctx, tag);
  JS_FreeValue(ctx, attrs);
  JS_FreeValue(ctx, childNodes);

  JS_FreeAtom(ctx, atagName);
  JS_FreeAtom(ctx, aattributes);
  JS_FreeAtom(ctx, achildren);
}

static JSValue
js_xml_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {

  vector enumerations = VECTOR_INIT();
  DynBuf output = {0};

  JSValueConst obj = argc > 0 ? argv[0] : JS_UNDEFINED;
  PropertyEnumeration* it;
  JSValue value = JS_UNDEFINED;
  JSValue str;
  it = property_enumeration_push(&enumerations, ctx, obj, PROPERTY_ENUMERATION_DEFAULT_FLAGS);
  dbuf_init(&output);

  do {
    int32_t depth = vector_size(&enumerations, sizeof(PropertyEnumeration)) - 1;
    value = property_enumeration_value(it, ctx);

    if(JS_IsObject(value)) {
      xml_write_element(ctx, value, &output, depth);
    }

    JS_FreeValue(ctx, value);
  } while((it = xml_enumeration_next(&enumerations, ctx, &output)));

  dbuf_putc(&output, '\0');

  str = JS_NewString(ctx, output.buf);

  dbuf_free(&output);

  vector_foreach_t(&enumerations, it) { property_enumeration_free(it, JS_GetRuntime(ctx)); }
  vector_free(&enumerations);

  return str;
}

static const JSCFunctionListEntry js_xml_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_xml_read),
    JS_CFUNC_DEF("write", 2, js_xml_write),
};

static int
js_xml_init(JSContext* ctx, JSModuleDef* m) {

  character_classes_init(chars);

  return JS_SetModuleExportList(ctx, m, js_xml_funcs, countof(js_xml_funcs));
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_xml
#endif

JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_xml_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_xml_funcs, countof(js_xml_funcs));
  return m;
}
