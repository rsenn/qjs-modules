#define _GNU_SOURCE
#include "quickjs.h"
#include "cutils.h"
#include "vector.h"

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
}

typedef struct {
  uint32_t idx;
  JSValue obj;
} OutputValue;

static JSValue
js_xml_parse(JSContext* ctx, const uint8_t* buf, size_t len) {

  BOOL done = FALSE;
  JSValue ret;
  const uint8_t *ptr, *end, *start;
  uint8_t c;
  size_t n;
  OutputValue* out;
  vector st = VECTOR_INIT();
  ptr = buf;
  end = buf + len;

  ret = JS_NewArray(ctx);

  out = vector_push(&st, sizeof(OutputValue));
out->obj = ret;
out->idx = 0;

  while(!done) {
    start = ptr;
    skip(!(chars[c] & START));

    if(ptr > start) {
      JSValue str = JS_NewStringLen(ctx, start, ptr - start);
      JS_SetPropertyUint32(ctx, out->obj, out->idx++, str);
    }

    if(chars[c] & START) {
      const uint8_t* name;
      size_t namelen;
      BOOL closing = FALSE;
      JSValue element;

      next();
      if(chars[c] & SLASH) {
        closing = TRUE;
        next();
      }
      name = ptr;
      skip(!(chars[c] & (WS | END)));
      namelen = ptr - name;
      element = JS_NewObject(ctx);
      JS_SetPropertyUint32(ctx, out->obj, out->idx++, element);
      js_set_attr_value(ctx, element, "tagName", 7, name, namelen);

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
            alen = ptr - attr;
            if(alen == 0)
              break;

            if(chars[c = *ptr] & EQUAL) {
              next();
              if(chars[c = *ptr] & QUOTE)
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
            closing = TRUE;
            next();
          }
        }
        if(closing) {
          JS_DefinePropertyValueStr(ctx, element, "closing", JS_NewBool(ctx, closing), JS_PROP_ENUMERABLE);
        } else {
          out = vector_push(&st, sizeof(OutputValue));
          out->obj = JS_NewArray(ctx);
          out->idx = 0;
          JS_SetPropertyStr(ctx, element, "children", out->obj);
        }
        if(!closing) {
          if(chars[name[0]] & EXCLAM) {
            // end(str(name));
          } else if((chars[name[0]] & QUESTION) && (chars[c] & QUESTION)) {
            next();
          } else if(chars[c] & SLASH) {
            next();
            // end();
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

static JSValue
js_xml_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {}

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
