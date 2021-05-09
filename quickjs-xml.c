#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "cutils.h"
#include "property-enumeration.h"
#include "quickjs.h"
#include "utils.h"
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
  uint32_t idx;
  JSValue obj;
  const uint8_t* name;
  size_t namelen;
} OutputValue;

void
character_classes_init(int c[256]) {
  c[' '] = WS;
  c['\t'] = WS;
  c['\r'] = WS;
  c['\n'] = WS;
  c['!'] = SPECIAL | EXCLAM;
  c['"'] = QUOTE;
  c['/'] = END | SLASH;
  c['<'] = START;
  c['='] = EQUAL;
  c['>'] = END | CLOSE;
  c['?'] = SPECIAL | QUESTION;
  c['\\'] = BACKSLASH;
  c['-'] = HYPHEN;
}

#define pop()                                                                                                          \
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

#define skip_until(cond) skip(!(cond))
#define skip_ws() skip(chars[c] & WS)
#define char_is(c, classes) (chars[(c)] & (classes))

static void
xml_set_attr_value(JSContext* ctx, JSValueConst obj, const char* attr, size_t alen, JSValue value) {
  JSAtom prop;
  prop = JS_NewAtomLen(ctx, (const char*)attr, alen);
  JS_SetProperty(ctx, obj, prop, value);
  JS_FreeAtom(ctx, prop);
}

static void
xml_set_attr_bytes(JSContext* ctx, JSValueConst obj, const char* attr, size_t alen, const uint8_t* str, size_t slen) {
  JSValue value;
  value = JS_NewStringLen(ctx, (const char*)str, slen);
  xml_set_attr_value(ctx, obj, attr, alen, value);
  // JS_FreeValue(ctx, value);
}

static void
xml_write_attributes(JSContext* ctx, JSValueConst attributes, DynBuf* db) {
  size_t i;
  PropertyEnumeration props = {0};

  property_enumeration_init(&props, ctx, JS_DupValue(ctx, attributes), PROPENUM_DEFAULT_FLAGS);

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
      js_cstring_free(ctx, valuestr);
    }
    dbuf_putc(db, '"');
    js_cstring_free(ctx, keystr);
    JS_FreeValue(ctx, value);
  }

  property_enumeration_reset(&props, JS_GetRuntime(ctx));
}

static inline void
xml_write_indent(DynBuf* db, int32_t depth) {
  while(depth-- > 0) dbuf_putstr(db, "  ");
}

static void
xml_write_string(JSContext* ctx, const char* textStr, size_t textLen, DynBuf* db, int32_t depth) {
  const char* p;
  for(p = textStr;;) {
    size_t n;

    while(textLen) {
      if(byte_chr("\r\n \t", 4, *p) == 4)
        break;
      p++;
      textLen--;
    }
    n = byte_chr(p, textLen, '\n');
    dbuf_append(db, (const uint8_t*)p, n);
    if(n < textLen)
      n++;
    p += n;
    textLen -= n;

    /*while(textLen > 0 &&  (p[0] == ' ' ||  p[0] == '\t')) {
      textLen--;
      p++;
    }    */

    if(textLen == 0)
      break;
    dbuf_putc(db, '\n');

    xml_write_indent(db, depth);
  }
}

static void
xml_write_text(JSContext* ctx, JSValueConst text, DynBuf* db, int32_t depth) {
  const char* textStr;
  size_t textLen;
  textStr = JS_ToCStringLen(ctx, &textLen, text);
  xml_write_indent(db, depth);
  xml_write_string(ctx, textStr, textLen, db, depth);
  js_cstring_free(ctx, textStr);
  dbuf_putc(db, '\n');
}

static void
xml_write_element(JSContext* ctx, JSValueConst element, DynBuf* db, int32_t depth) {
  JSValue attributes = JS_GetPropertyStr(ctx, element, "attributes");
  JSValue children = JS_GetPropertyStr(ctx, element, "children");
  size_t tagLen;
  const char* tagName = js_get_propertystr_cstringlen(ctx, element, "tagName", &tagLen);
  BOOL isComment;
  assert(tagName);
  isComment = !strncmp(tagName, "!--", 3);

  xml_write_indent(db, depth);

  dbuf_putc(db, '<');

  if(isComment) {
    if(byte_chr(tagName, tagLen, '\n') < tagLen) {
      xml_write_string(ctx, tagName, tagLen - 2, db, depth + 1);
      dbuf_putc(db, '\n');
      xml_write_indent(db, depth);
      dbuf_putc(db, '-');
      dbuf_putc(db, '-');
    } else {
      xml_write_string(ctx, tagName, tagLen, db, depth + 1);
    }
  } else if(JS_IsObject(attributes)) {
    dbuf_putstr(db, tagName);
    xml_write_attributes(ctx, attributes, db);
  }

  dbuf_putstr(db,
              (JS_IsObject(children) || isComment) ? ">" : tagName[0] == '?' ? "?>" : tagName[0] == '!' ? "!>" : " />");
  dbuf_putc(db, '\n');

  js_cstring_free(ctx, tagName);
  JS_FreeValue(ctx, attributes);
  JS_FreeValue(ctx, children);
}

static void
xml_close_element(JSContext* ctx, JSValueConst element, DynBuf* db, int32_t depth) {
  JSValue childNodes = JS_GetPropertyStr(ctx, element, "children");

  if(JS_IsArray(ctx, childNodes)) {
    size_t tagLen;
    const char* tagName = js_get_propertystr_cstringlen(ctx, element, "tagName", &tagLen);

    xml_write_indent(db, depth);

    dbuf_putstr(db, "</");
    dbuf_append(db, (const uint8_t*)tagName, tagLen);
    dbuf_putstr(db, ">");
    dbuf_putc(db, '\n');
    js_cstring_free(ctx, tagName);
  }

  JS_FreeValue(ctx, childNodes);
}

static PropertyEnumeration*
xml_enumeration_next(Vector* vec, JSContext* ctx, DynBuf* db) {
  PropertyEnumeration* it;
  JSValue value = JS_UNDEFINED, children;

  it = vector_back(vec, sizeof(PropertyEnumeration));
  value = property_enumeration_value(it, ctx);

  if(JS_IsObject(value)) {
    children = JS_GetPropertyStr(ctx, value, "children");
    JS_FreeValue(ctx, value);
    if(!JS_IsUndefined(children)) {
      if((it = property_enumeration_push(vec, ctx, children, PROPENUM_DEFAULT_FLAGS)))
        if(property_enumeration_setpos(it, 0))
          return it;
    }
  } else {
    JS_FreeValue(ctx, value);
  }

  for(;;) {
    if(property_enumeration_setpos(it, it->idx + 1))
      break;

    if((it = property_enumeration_pop(vec, ctx)) == 0)
      break;

    value = property_enumeration_value(it, ctx);
    xml_close_element(ctx, value, db, (int32_t)vector_size(vec, sizeof(PropertyEnumeration)) - 1);
    JS_FreeValue(ctx, value);
  }

  return it;
}

static JSValue
js_xml_parse(JSContext* ctx, const uint8_t* buf, size_t len) {
  BOOL done = FALSE;
  JSValue ret;
  const uint8_t *ptr, *end, *start;
  uint8_t c;
  OutputValue* out;
  JSValue element = JS_UNDEFINED;
  Vector st = VECTOR(ctx);
  ptr = buf;
  end = buf + len;

  ret = JS_NewArray(ctx);

  out = vector_emplace(&st, sizeof(OutputValue));
  out->obj = ret;
  out->idx = 0;

  while(!done) {
    skip_ws();
    start = ptr;
    skip_until(char_is(c, START));

    if(done)
      break;
    if(ptr > start) {
      size_t len;
      //  while(start < ptr && is_whitespace_char(*start)) start++;
      len = ptr - start;
      while(len > 0 && is_whitespace_char(start[len - 1])) len--;

      JSValue str = JS_NewStringLen(ctx, (const char*)start, len);
      JS_SetPropertyUint32(ctx, out->obj, out->idx++, str);
    }
    if(char_is(c, START)) {
      const uint8_t* name;
      size_t namelen;
      BOOL closing = FALSE, self_closing = FALSE;
      next();
      if(char_is(c, SLASH)) {
        closing = TRUE;
        next();
      }
      name = ptr;
      skip_until(char_is(c, WS | END));
      namelen = ptr - name;
      if(closing) {
        skip_ws();
        if(char_is(c, CLOSE))
          next();
        if(out->namelen == namelen && !memcmp(out->name, name, namelen)) {
          pop();
          continue;
        }
      }
      element = JS_NewObject(ctx);
      JS_SetPropertyUint32(ctx, out->obj, out->idx++, element);
      if(namelen && (char_is(name[0], (QUESTION | EXCLAM))))
        self_closing = TRUE;

      if(namelen >= 3 && char_is(start[0], EXCLAM) && char_is(start[1], HYPHEN) && char_is(start[2], HYPHEN)) {
        /*  next();
          next();*/
        while(!done) {
          next();
          if(end - ptr >= 3 && char_is(ptr[0], HYPHEN) && char_is(ptr[1], HYPHEN) && char_is(ptr[2], CLOSE)) {
            ptr += 2;
            break;
          }
        }
        namelen = ptr - name;

      } else if(namelen && char_is(name[0], EXCLAM)) {
        skip_until(char_is(c, CLOSE));
        namelen = ptr - name;
      }
      xml_set_attr_bytes(ctx, element, "tagName", 7, name, namelen);

      if(namelen && char_is(name[0], EXCLAM)) {
        next();
        continue;
      }
      if(!closing) {
        const uint8_t *attr, *value;
        size_t alen, vlen;
        JSValue attributes = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, element, "attributes", attributes);
        while(!done) {
          skip_ws();
          if(char_is(c, END))
            break;
          attr = ptr;
          skip_until(char_is(c, EQUAL | WS | SPECIAL | CLOSE));
          if((alen = ptr - attr) == 0)
            break;
          if(char_is(c, WS | CLOSE | SLASH)) {
            xml_set_attr_value(ctx, attributes, (const char*)attr, alen, JS_NewBool(ctx, TRUE));
            continue;
          }
          if(char_is(c, EQUAL)) {
            next();
            if(char_is(c, QUOTE))
              next();
            value = ptr;
            skip_until(char_is(c, QUOTE));
            vlen = ptr - value;
            if(char_is(c, QUOTE))
              next();
            xml_set_attr_bytes(ctx, attributes, (const char*)attr, alen, value, vlen);
          }
        }
        if(char_is(c, SLASH)) {
          self_closing = TRUE;
          next();
        }

        if(char_is(name[0], QUESTION | EXCLAM)) {
          if(chars[c] == chars[name[0]])
            next();
          pop();
        } else if(!self_closing) {
          out = vector_emplace(&st, sizeof(OutputValue));
          out->obj = JS_NewArray(ctx);
          out->idx = 0;
          out->name = name;
          out->namelen = namelen;
          JS_SetPropertyStr(ctx, element, "children", out->obj);
        }
      }

      skip_ws();
      if(char_is(c, CLOSE))
        next();
    }
  }
  return ret;
}

static JSValue
js_xml_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret;
  InputBuffer input = js_input_buffer(ctx, argv[0]);

  if(input.data == 0 || input.size == 0) {
    JS_ThrowReferenceError(ctx, "xml.read(): expecting buffer or string");
    return JS_EXCEPTION;
  }

  ret = js_xml_parse(ctx, input.data, input.size);

  input_buffer_free(&input, ctx);
  return ret;
}

static JSValue
js_xml_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Vector enumerations = VECTOR(ctx);
  DynBuf output = {0};
  JSValueConst obj = argc > 0 ? argv[0] : JS_UNDEFINED;
  PropertyEnumeration* it;
  JSValue value = JS_UNDEFINED;
  JSValue str;
  it = property_enumeration_push(&enumerations, ctx, JS_DupValue(ctx, obj), PROPENUM_DEFAULT_FLAGS);
  //  dbuf_init(&output);
  js_dbuf_init(ctx, &output);

  do {
    int32_t depth = vector_size(&enumerations, sizeof(PropertyEnumeration)) - 1;
    value = property_enumeration_value(it, ctx);
    if(JS_IsObject(value) && !JS_IsArray(ctx, value))
      xml_write_element(ctx, value, &output, depth);
    else if(JS_IsString(value))
      xml_write_text(ctx, value, &output, depth);
    JS_FreeValue(ctx, value);
  } while((it = xml_enumeration_next(&enumerations, ctx, &output)));
  while(output.size > 0 &&
        (output.buf[output.size - 1] == '\0' || byte_chr("\r\n\t ", 4, output.buf[output.size - 1]) < 4))
    output.size--;
  dbuf_putc(&output, '\0');

  str = JS_NewString(ctx, (const char*)output.buf);
  // str = JS_NewStringLen(ctx, output.buf, output.size);

  dbuf_free(&output);

  vector_foreach_t(&enumerations, it) { property_enumeration_reset(it, JS_GetRuntime(ctx)); }
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

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_xml_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_xml_funcs, countof(js_xml_funcs));
  return m;
}
