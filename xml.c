#include "quickjs.h"
#include "cutils.h"

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

#define next()                                                                                                         \
  do {                                                                                                                 \
    c = *ptr++;                                                                                                        \
    if(ptr >= end)                                                                                                     \
      done = TRUE;                                                                                                     \
  } while(0)
#define skip(cond)                                                                                                     \
  do {                                                                                                                 \
    c = *ptr;                                                                                                          \
    if(!(cond))                                                                                                        \
      break;                                                                                                           \
    if(++ptr >= end)                                                                                                   \
      done = TRUE;                                                                                                     \
  } while(!done)

#define skipws() skip(chars[c] & WS)

static JSValue
js_xml_parse(JSContext* ctx, const uint8_t* buf, size_t len) {

  BOOL done = FALSE;
  JSValue tag;
  const uint8_t *ptr, *end, *start;
  uint8_t c;
  size_t n;
  ptr = buf;
  end = buf + len;
  while(!done) {
    start = ptr;
    skip(!(chars[c] & START));

    if(ptr > start) {}

    if(chars[c] & START) {
      BOOL closing = FALSE;
      skipws();
      next();

      if(chars[c] & SLASH) {
        closing = TRUE;
        skipws();
        next();
      }
      start = ptr;
      skip(!(chars[c] & (WS | END)));

      n = ptr - start;
      //  tag = JS_NewStringLen(ctx, start, ptr - start);

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
          const char *attr, *value;
          size_t alen, vlen;
          while(!done) {
            skipws();
            if(chars[c] & END)
              break;
            attr = ptr;
            skip((chars[c] & (EQUAL | WS | SPECIAL | CLOSE)) == 0);
            alen = ptr - attr;
            if(alen == 0)
              break;

            if(chars[c] & EQUAL) {
              next();
              if(chars[c] & QUOTE) {
                next();
                value = ptr;
                skip((chars[c] & QUOTE) == 0);
                vlen = ptr - value;

                if(chars[c] & QUOTE)
                  next();
              }
            }
          }
        }
      }
    }

    static JSValue js_xml_read(JSContext * ctx, JSValueConst this_val, int argc, JSValueConst* argv) {

      InputValue input = js_value_to_bytes(ctx, argv[0]);

      js_xml_parse(ctx, input.x, input.n);

      input.free(ctx, input.x);
    }

    static JSValue js_xml_write(JSContext * ctx, JSValueConst this_val, int argc, JSValueConst* argv) {}

    static const JSCFunctionListEntry js_xml_funcs[] = {
        JS_CFUNC_DEF("read", 1, js_xml_read),
        JS_CFUNC_DEF("write", 2, js_xml_write),
    };

    static int js_xml_init(JSContext * ctx, JSModuleDef * m) {

      character_classes_init(chars);

      return JS_SetModuleExportList(ctx, m, js_xml_funcs, countof(js_xml_funcs));
    }

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_xml
#endif

    JSModuleDef* JS_INIT_MODULE(JSContext * ctx, const char* module_name) {
      JSModuleDef* m;
      m = JS_NewCModule(ctx, module_name, js_xml_init);
      if(!m)
        return NULL;
      JS_AddModuleExportList(ctx, m, js_xml_funcs, countof(js_xml_funcs));
      return m;
    }
