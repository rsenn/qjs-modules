#include "defines.h"
#include <cutils.h>
#include "property-enumeration.h"
#include <quickjs.h>
#include "utils.h"
#include "vector.h"
#include "buffer-utils.h"
#include "stream-utils.h"
#include "location.h"
#include "debug.h"
#include "virtual-properties.h"
#include "quickjs-location.h"

#include <stdint.h>

char* js_inspect_tostring(JSContext* ctx, JSValueConst value);

/**
 * \defgroup quickjs-xml quickjs-xml: XML parser & printer
 * @{
 */
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

static const char* const default_self_closing_tags[] = {
    "area",
    "base",
    "br",
    "col",
    "embed",
    "hr",
    "img",
    "input",
    "link",
    "meta",
    "param",
    "source",
    "track",
    "wbr",
    0,
};

typedef struct {
  uint32_t idx;
  JSValue obj;
  const uint8_t* name;
  size_t namelen;
} OutputValue;

typedef struct {
  BOOL flat, tolerant, location;
  const char* const* self_closing_tags;
} ParseOptions;

void
character_classes_init(int c[256]) {
  c[' '] = WS;
  c['\t'] = WS;
  c['\r'] = WS;
  c['\n'] = WS;
  c['!'] = SPECIAL | EXCLAM;
  c['"'] = QUOTE;
  c['\''] = QUOTE;
  c['/'] = END | SLASH;
  c['<'] = START;
  c['='] = EQUAL;
  c['>'] = END | CLOSE;
  c['?'] = SPECIAL | QUESTION;
  c['\\'] = BACKSLASH;
  c['-'] = HYPHEN;
}

#ifdef DEBUG_OUTPUT
#define xml_debug(args...) printf(args)
#else
#define xml_debug(args...)
#endif

#define yield_push() \
  do { \
    xml_debug("push  [%" PRIu32 "] %.*s\n", vector_size(&st, sizeof(OutputValue)), (int)namelen, name); \
    out = vector_push(&st, ((OutputValue){0, JS_NewArray(ctx), name, namelen})); \
    JS_SetPropertyStr(ctx, element, "children", out->obj); \
  } while(0)

#define yield_pop() \
  do { \
    OutputValue* top = vector_back(&st, sizeof(OutputValue)); \
    xml_debug("pop    %.*s\n", (int)top->namelen, top->name); \
    if(vector_size(&st, sizeof(OutputValue)) >= 2 ?) { \
      vector_pop(&st, sizeof(OutputValue)); \
      out = vector_back(&st, sizeof(OutputValue))); \
    } \
    while(0)

#define yield_add(val) \
  do { \
    xml_debug("add   {%" PRIu32 "}\n", out->idx); \
    JS_SetPropertyUint32(ctx, out->obj, out->idx++, (val)); \
  } while(0)

#define yield_next() \
  do { \
    xml_debug("next  {%" PRIu32 "}\n", out->idx); \
    element = JS_NewObject(ctx); \
    JS_SetPropertyUint32(ctx, out->obj, out->idx++, element); \
    if(opts.location) \
      virtual_set(&vprop, ctx, element, locObj); \
  } while(0)

#define yield_return(index) \
  do { \
    if(index >= 1) { \
      xml_debug("return[%" PRId32 "] %" PRIu32 "\n", index, vector_size(&st, sizeof(OutputValue)) - index); \
      vector_shrink(&st, sizeof(OutputValue), index); \
      out = vector_back(&st, sizeof(OutputValue)); \
    } \
  } while(0)

#define parse_getc() \
  do { \
    parse_loc(); \
    c = *++ptr; \
    if(ptr >= end) \
      done = TRUE; \
  } while(0)

#define parse_loc() \
  if(*ptr == '\n') { \
    lineno++; \
    column = 1; \
  } else { \
    column++; \
  }

#define parse_skip(cond) \
  do { \
    c = *ptr; \
    if(!(cond)) \
      break; \
    parse_loc() if(++ptr >= end) done = TRUE; \
  } while(!done)

#define parse_until(cond) parse_skip(!(cond))
#define parse_skipspace() parse_skip(chars[c] & WS)
#define parse_is(c, classes) (chars[(c)] & (classes))
#define parse_inside(tag) (strlen((tag)) == out->namelen && !strncmp((const char*)out->name, (const char*)(tag), out->namelen))
#define parse_close() (ptr[0] == '<' && ptr[1] == '/' && !strncmp((const char*)&ptr[2], (const char*)out->name, out->namelen) && ptr[2 + out->namelen] == '>')

static int32_t
find_tag(Vector* st, const char* name, size_t namelen) {
  OutputValue* o;
  int32_t index = vector_size(st, sizeof(OutputValue));

  for(o = vector_back(st, sizeof(OutputValue)); o >= (OutputValue*)vector_begin(st); o--) {
    --index;

    if(o->namelen == namelen && !strncmp((const char*)o->name, name, namelen))
      return index;
  }

  return -1;
}

static BOOL
is_self_closing_tag(const char* name, size_t namelen, const ParseOptions* opts) {
  const char** v;

  for(v = (const char**)opts->self_closing_tags; *v; v++) {
    size_t n = strlen(*v);

    if(n == namelen && !strncasecmp(name, *v, namelen))
      return TRUE;
  }

  return FALSE;
}

static int32_t
xml_num_children(JSContext* ctx, JSValueConst element) {
  int64_t num_children = -1;
  JSValue children = JS_GetPropertyStr(ctx, element, "children");

  if(JS_IsArray(ctx, children))
    num_children = js_array_length(ctx, children);

  JS_FreeValue(ctx, children);
  return num_children;
}

static void
xml_set_attr_value(JSContext* ctx, JSValueConst obj, const char* attr, size_t alen, JSValue value) {
  JSAtom prop = JS_NewAtomLen(ctx, (const char*)attr, alen);

  JS_SetProperty(ctx, obj, prop, value);
  JS_FreeAtom(ctx, prop);
}

static inline void
xml_set_attr_bytes(JSContext* ctx, JSValueConst obj, const char* attr, size_t alen, const uint8_t* str, size_t slen) {
  xml_set_attr_value(ctx, obj, attr, alen, JS_NewStringLen(ctx, (const char*)str, slen));
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
      valuestr = JS_ToCString(ctx, value);

      dbuf_putstr(db, "=\"");
      dbuf_putstr(db, valuestr);

      JS_FreeCString(ctx, valuestr);
      dbuf_putc(db, '"');
    }

    JS_FreeCString(ctx, keystr);
    JS_FreeValue(ctx, value);
  }

  property_enumeration_reset(&props, JS_GetRuntime(ctx));
}

static inline void
xml_write_indent(DynBuf* db, int32_t depth) {
  while(depth-- > 0)
    dbuf_putstr(db, "  ");
}

static void
xml_write_string(JSContext* ctx, const char* textStr, size_t textLen, DynBuf* db, int32_t depth) {
  for(const char* p = textStr;;) {
    size_t n;

    if(0) {
      while(textLen) {
        if(byte_chr("\r\n \t", 4, *p) == 4)
          break;

        p++;
        textLen--;
      }
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
    }*/

    if(textLen == 0)
      break;

    if(depth > 0) {
      dbuf_putc(db, '\n');
      xml_write_indent(db, depth + 1);
    }
  }
}

static void
xml_write_text(JSContext* ctx, JSValueConst text, DynBuf* db, int32_t depth, BOOL multiline) {
  const char* textStr;
  size_t textLen;

  textStr = JS_ToCStringLen(ctx, &textLen, text);

  if(multiline)
    xml_write_indent(db, depth);
  else
    while(db->size > 0 && is_whitespace_char(db->buf[db->size - 1]))
      db->size--;

  xml_write_string(ctx, textStr, textLen, db, multiline ? depth : 0);
  JS_FreeCString(ctx, textStr);

  if(multiline)
    dbuf_putc(db, '\n');
}

static void
xml_write_element(JSContext* ctx, JSValueConst element, DynBuf* db, int32_t depth, BOOL self_closing) {
  JSValue attributes = JS_GetPropertyStr(ctx, element, "attributes");
  int32_t num_children = -1;
  size_t tagLen;
  const char* tagName;
  BOOL isComment;

  if(!(tagName = js_get_propertystr_cstringlen(ctx, element, "tagName", &tagLen)) || !tagName[0])
    return;

  assert(tagName);
  isComment = !strncmp(tagName, "!--", 3);

  if(depth > 0)
    xml_write_indent(db, depth);

  if(tagName[0])
    dbuf_putc(db, '<');

  if(isComment) {
    if(TRUE || byte_chr(tagName, tagLen, '\n') < tagLen)
      dbuf_put(db, (const uint8_t*)tagName, tagLen);
    else
      xml_write_string(ctx, tagName, tagLen, db, depth - 1);
  } else if(tagName[0] == '!') {
    dbuf_putstr(db, tagName);
  } else {
    dbuf_putstr(db, tagName);

    if(JS_IsObject(attributes))
      xml_write_attributes(ctx, attributes, db);
  }

  if(!self_closing)
    num_children = xml_num_children(ctx, element);

  if(tagName[0])
    dbuf_putstr(db, tagName[0] == '?' ? "?>" : (self_closing || num_children <= 0) && !(tagName[0] == '!' || num_children > 0 || isComment) ? " />" : ">");

  dbuf_putc(db, '\n');

  JS_FreeCString(ctx, tagName);
  JS_FreeValue(ctx, attributes);
}

static void
xml_close_element(JSContext* ctx, JSValueConst element, DynBuf* db, int32_t depth) {
  int32_t num_children = xml_num_children(ctx, element);

  if(num_children > 0) {
    size_t tagLen;
    const char* tagName = js_get_propertystr_cstringlen(ctx, element, "tagName", &tagLen);

    if(tagName[0] != '?' && tagName[0]) {
      if(db->size > 0 && db->buf[db->size - 1] == '\n')
        xml_write_indent(db, depth);

      dbuf_putstr(db, "</");
      dbuf_append(db, (const uint8_t*)tagName, tagLen);
      dbuf_putstr(db, ">");
      dbuf_putc(db, '\n');
    }

    JS_FreeCString(ctx, tagName);
  }
}

static PropertyEnumeration*
xml_enumeration_next(Vector* vec, JSContext* ctx, DynBuf* db, int32_t max_depth) {
  PropertyEnumeration *it, *it2;
  JSValue value = JS_UNDEFINED, children;

  it = vector_back(vec, sizeof(PropertyEnumeration));
  value = property_enumeration_value(it, ctx);

  if(JS_IsObject(value)) {
    children = JS_GetPropertyStr(ctx, value, "children");
    JS_FreeValue(ctx, value);

    if(!JS_IsUndefined(children) && (max_depth == INT32_MAX || vector_size(vec, sizeof(PropertyEnumeration)) < (uint32_t)max_depth))
      if((it2 = property_recursion_push(vec, ctx, children, PROPENUM_DEFAULT_FLAGS)))
        if(property_enumeration_setpos(it2, 0))
          return it2;

  } else {
    JS_FreeValue(ctx, value);
  }

  while(!property_enumeration_next(it)) {
    int32_t depth;

    if((it = property_recursion_pop(vec, ctx)) == 0)
      break;

    value = property_enumeration_value(it, ctx);
    depth = property_recursion_depth(vec) - 1;
    depth = MAX_NUM(0, depth - 1);
    xml_close_element(ctx, value, db, depth);

    JS_FreeValue(ctx, value);
  }

  return it;
}

static JSValue
make_tuple(JSContext* ctx, JSValue a, JSValue b) {
  JSValue ret = JS_NewArray(ctx);

  JS_SetPropertyUint32(ctx, ret, 0, a);
  JS_SetPropertyUint32(ctx, ret, 1, b);

  return ret;
}

static JSValue
js_xml_parse_location(JSContext* ctx, uint32_t line, uint32_t column) {
  return make_tuple(ctx, JS_NewUint32(ctx, line), JS_NewUint32(ctx, column));
}

static JSValue
js_xml_parse(JSContext* ctx, const uint8_t* buf, size_t len, const char* input_name, ParseOptions opts) {
  BOOL done = FALSE;
  const uint8_t *ptr, *end, *start;
  uint32_t lineno = 1, column = 1;
  uint8_t c;
  OutputValue* out;
  JSValue ret, element = JS_UNDEFINED, locObj;
  Vector st = VECTOR(ctx);
  Location loc = LOCATION_FILE(JS_NewAtom(ctx, input_name));
  VirtualProperties vprop;

  ptr = buf;
  end = buf + len;

  if(opts.location)
    vprop = virtual_properties_map(ctx, js_object_new(ctx, "WeakMap", 0, 0));

  xml_debug("js_xml_parse input_name: %s flat: %s\n", input_name, opts.flat ? "TRUE" : "FALSE");

  ret = JS_NewArray(ctx);

  out = vector_emplace(&st, sizeof(OutputValue));
  out->obj = ret;
  out->idx = 0;

  while(!done) {
    start = ptr;

    BOOL inside_script = parse_inside("script");

    if(inside_script) {
      while(!parse_close()) {
        ++ptr;

        if((ptr += byte_chr(ptr, end - ptr, '<')) == end)
          break;
      }

    } else {
      parse_until(parse_is(c, START));
    }

    size_t leading_ws = scan_whitenskip((const char*)start, ptr - start);

    while(start < ptr) {
      size_t n, real_len;

      start += scan_whitenskip((const char*)start, leading_ws);
      real_len = n = ptr - start;

      if(inside_script) {
        if((real_len = byte_chr(start, n, '\n')) < n)
          real_len++;

        n = real_len;
      }

      if(!inside_script)
        while(n > 0 && is_whitespace_char(start[n - 1]))
          n--;

      if(n > 0) {
        JSValue str = JS_NewStringLen(ctx, (const char*)start, n);

        yield_add(str);
      }

      start += real_len;
    }

    if(done)
      break;

    start = ptr;
    c = *ptr;

    if(opts.location)
      locObj = js_xml_parse_location(ctx, lineno, column);

    if(parse_is(c, START)) {
      const uint8_t* name;
      size_t namelen;
      BOOL closing = FALSE, self_closing = FALSE;

      parse_getc();

      if(parse_is(c, SLASH)) {
        closing = TRUE;
        parse_getc();
      }

      name = ptr;

      if(parse_is(ptr[0], EXCLAM) && parse_is(ptr[1], HYPHEN) && parse_is(ptr[2], HYPHEN)) {
        parse_getc();
        parse_getc();
        parse_getc();
      } else {
        parse_until(parse_is(c, WS | END));
      }

      namelen = ptr - name;

      if(closing) {
        int32_t index = 0;

        parse_skipspace();

        if(parse_is(c, CLOSE))
          parse_getc();

        xml_debug("end-of [%" PRIu32 "] tagName: %s%.*s\n", index, closing ? "/" : "", (int)namelen, name);

        if(opts.flat) {
          yield_next();
          xml_set_attr_bytes(ctx, element, "tagName", 7, name - 1, namelen + 1);

        } else {
          if((index = find_tag(&st, (const char*)name, namelen)) == -1) {
            if(!opts.tolerant) {
              char* file;

              JS_FreeValue(ctx, ret);
              location_count(&loc, buf, start - buf);
              file = location_file(&loc, ctx);
              xml_debug(
                  "mismatch </%.*s> at %s:%u:%u (byte %zu/char %zu)", (int)namelen, name, file, loc.line + 1, loc.column + 1, loc.byte_offset, loc.char_offset);
              ret = JS_ThrowSyntaxError(ctx, "mismatch </%.*s> at %s:%u:%u", (int)namelen, name, file, loc.line + 1, loc.column + 1);

              if(file)
                js_free(ctx, file);

              return ret;
            }

            continue;
          }

          yield_return(index);
        }
      } else {
        yield_next();

        if(namelen && (parse_is(name[0], (/*QUESTION |*/ EXCLAM))))
          self_closing = TRUE;

        if(is_self_closing_tag((const char*)name, namelen, &opts))
          self_closing = TRUE;

        if(namelen >= 3 && parse_is(name[0], EXCLAM) && parse_is(name[1], HYPHEN) && parse_is(name[2], HYPHEN)) {
          while(!done) {
            parse_getc();

            if(end - ptr >= 3 && parse_is(ptr[0], HYPHEN) && parse_is(ptr[1], HYPHEN) && parse_is(ptr[2], CLOSE)) {
              ptr += 2;
              break;
            }
          }

          namelen = ptr - name;

        } else if(namelen && parse_is(name[0], EXCLAM)) {
          parse_until(parse_is(c, CLOSE));
          namelen = ptr - name;
        }

        xml_set_attr_bytes(ctx, element, "tagName", 7, name, namelen);

        if(namelen && parse_is(name[0], EXCLAM)) {
          parse_getc();
          continue;
        }

        /* Parse attributes if not a closing tag */
        const uint8_t *attr, *value;
        size_t alen, vlen;
        JSValue attributes = JS_NewObject(ctx);

        JS_SetPropertyStr(ctx, element, "attributes", attributes);

        while(!done) {
          parse_skipspace();

          if(parse_is(c, END))
            break;

          attr = ptr;
          parse_until(parse_is(c, EQUAL | WS | SPECIAL | CLOSE));

          if((alen = ptr - attr) == 0)
            break;

          if(parse_is(c, WS | CLOSE | SLASH)) {
            xml_set_attr_value(ctx, attributes, (const char*)attr, alen, JS_NewBool(ctx, TRUE));
            continue;
          }

          if(parse_is(c, EQUAL)) {
            char quote = 0;

            parse_getc();

            if(parse_is(c, QUOTE)) {
              quote = c;
              parse_getc();
            }

            value = ptr;

            if(quote)
              parse_until(c == quote);
            else
              parse_until(parse_is(c, (WS | CLOSE)));

            vlen = ptr - value;

            if(quote && parse_is(c, QUOTE))
              parse_getc();

            xml_set_attr_bytes(ctx, attributes, (const char*)attr, alen, value, vlen);
          }
        }

        if(parse_is(c, SLASH)) {
          self_closing = TRUE;
          parse_getc();
        }

        if(parse_is(name[0], QUESTION | EXCLAM))
          if(chars[c] == chars[name[0]])
            parse_getc();

        if(!opts.flat && !self_closing)
          yield_push();
      }

      if(self_closing && opts.flat) {
        char* tagName = js_mallocz(ctx, namelen + 2);

        tagName[0] = '/';
        str_copyn(&tagName[1], (const char*)name, namelen);

        yield_next();
        xml_set_attr_bytes(ctx, element, "tagName", 7, (const uint8_t*)tagName, namelen + 1);
        js_free(ctx, tagName);
      }

      parse_skipspace();

      if(parse_is(c, CLOSE))
        parse_getc();
    }
  }

  JS_FreeAtom(ctx, loc.file);

  if(opts.location)
    return make_tuple(ctx, ret, vprop.this_obj);

  return ret;
}

static JSValue
js_xml_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  InputBuffer input = js_input_chars(ctx, argv[0]);
  const char* input_name = 0;
  ParseOptions opts = {
      .flat = FALSE,
      .tolerant = FALSE,
      .location = FALSE,
      .self_closing_tags = default_self_closing_tags,
  };

  if(input.data == 0 || input.size == 0) {
    JS_ThrowReferenceError(ctx, "xml.read(): expecting buffer or string");
    return JS_EXCEPTION;
  }

  if(argc >= 2)
    input_name = JS_ToCString(ctx, argv[1]);

  if(argc >= 3) {
    if(JS_IsObject(argv[2])) {
      JSValue tags = JS_UNDEFINED;

      if(js_has_propertystr(ctx, argv[2], "flat"))
        opts.flat = js_get_propertystr_bool(ctx, argv[2], "flat");

      if(js_has_propertystr(ctx, argv[2], "tolerant"))
        opts.tolerant = js_get_propertystr_bool(ctx, argv[2], "tolerant");

      if(js_has_propertystr(ctx, argv[2], "location"))
        opts.location = js_get_propertystr_bool(ctx, argv[2], "location");

      if(js_has_propertystr(ctx, argv[2], "selfClosingTags"))
        tags = JS_GetPropertyStr(ctx, argv[2], "selfClosingTags");

      if(JS_IsArray(ctx, tags)) {
        size_t ac;
        opts.self_closing_tags = (const char* const*)js_array_to_argv(ctx, &ac, tags);
      }

      JS_FreeValue(ctx, tags);

    } else {
      opts.flat = JS_ToBool(ctx, argv[2]);

      if(argc >= 4)
        opts.tolerant = JS_ToBool(ctx, argv[3]);
    }
  }

  ret = js_xml_parse(ctx, input.data, input.size, input_name ? input_name : "<xml>", opts);

  if(input_name)
    JS_FreeCString(ctx, input_name);

  inputbuffer_free(&input, ctx);
  return ret;
}

static JSValue
js_xml_write_tree(JSContext* ctx, JSValueConst obj, int max_depth, DynBuf* output) {
  Vector enumerations = VECTOR(ctx);
  JSValue str, value = JS_UNDEFINED;
  PropertyEnumeration* it = property_recursion_push(&enumerations, ctx, JS_DupValue(ctx, obj), PROPENUM_DEFAULT_FLAGS);

  do {
    int32_t depth = vector_size(&enumerations, sizeof(PropertyEnumeration)) - 1;

    depth = MAX_NUM(0, depth - 1);
    value = property_enumeration_value(it, ctx);

    if(JS_IsString(value)) {
      xml_write_text(ctx, value, output, depth, it->tab_atom_len > 1);
    } else if(JS_IsObject(value) && !JS_IsArray(ctx, value)) {
      int32_t num_children = xml_num_children(ctx, value);

      xml_write_element(ctx, value, output, depth, num_children <= 0);
    }

    JS_FreeValue(ctx, value);
  } while((it = xml_enumeration_next(&enumerations, ctx, output, max_depth)));

  while(output->size > 0 && (output->buf[output->size - 1] == '\0' || byte_chr("\r\n\t ", 4, output->buf[output->size - 1]) < 4))
    output->size--;

  dbuf_putc(output, '\0');

  str = JS_NewString(ctx, (const char*)output->buf);

  vector_foreach_t(&enumerations, it) {
    property_enumeration_reset(it, JS_GetRuntime(ctx));
  }
  vector_free(&enumerations);
  return str;
}

static JSValue
js_xml_write_list(JSContext* ctx, JSValueConst obj, size_t len, DynBuf* output) {
  int32_t depth = 0;
  BOOL single_line = FALSE;
  JSValue value = JS_UNDEFINED, next = JS_GetPropertyUint32(ctx, obj, 0);
  const char *tagName = 0, *nextTag = JS_IsObject(next) ? js_get_propertystr_cstring(ctx, next, "tagName") : 0;

  for(size_t i = 0; i < len; i++) {
    JS_FreeValue(ctx, value);
    value = next;
    next = JS_GetPropertyUint32(ctx, obj, i + 1);

    tagName = nextTag;
    nextTag = JS_IsObject(next) ? js_get_propertystr_cstring(ctx, next, "tagName") : 0;

    if(JS_IsString(value)) {
      const char* s = JS_ToCString(ctx, value);
      uint32_t newlines = str_count(s, '\n');

      JS_FreeCString(ctx, s);
      single_line = newlines == 0;

      xml_write_text(ctx, value, output, depth, !single_line);
    } else if(JS_IsObject(value) && !JS_IsArray(ctx, value)) {
      const char* tag;

      if((tag = js_get_propertystr_cstring(ctx, value, "tagName"))) {
        BOOL self_closing = nextTag && nextTag[0] == '/' && !strcmp(tag, &nextTag[1]);

        if(tag[0] == '/')
          depth--;

        xml_write_element(ctx, value, output, single_line ? 0 : depth, self_closing);

        if(self_closing)
          next = JS_GetPropertyUint32(ctx, obj, ++i + 1);
        else if(tag[0] != '/' && tag[0] != '?' && tag[0] != '!' && !strcasecmp(tag, "dt"))
          depth++;

        JS_FreeCString(ctx, tag);
      }

      single_line = FALSE;
    }

    if(tagName)
      JS_FreeCString(ctx, tagName);
  }

  return JS_NewStringLen(ctx, (const char*)output->buf, output->size);
}

static JSValue
js_xml_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  DynBuf output = {0};
  JSValueConst obj = argc > 0 ? argv[0] : JS_UNDEFINED;
  JSValue ret, last, children = JS_UNDEFINED, arr = JS_UNDEFINED;
  int32_t max_depth = INT32_MAX;
  size_t len;
  BOOL flat = TRUE;

  dbuf_init_ctx(ctx, &output);

  if(argc >= 2)
    JS_ToInt32(ctx, &max_depth, argv[1]);

  if(!JS_IsArray(ctx, obj)) {
    arr = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, arr, 0, JS_DupValue(ctx, obj));
    obj = arr;
  }

  len = js_array_length(ctx, obj);
  last = JS_GetPropertyUint32(ctx, obj, len - 1);

  if(JS_IsObject(last)) {
    children = JS_GetPropertyStr(ctx, last, "children");

    if(JS_IsArray(ctx, children))
      flat = FALSE;
  }

  xml_debug("js_xml_write len=%zu, children=%s, flat=%d\n", len, JS_ToCString(ctx, children), flat);

  if(flat)
    ret = js_xml_write_list(ctx, obj, len, &output);
  else
    ret = js_xml_write_tree(ctx, obj, max_depth, &output);

  dbuf_free(&output);

  if(!JS_IsUndefined(arr))
    JS_FreeValue(ctx, arr);

  return ret;
}

static const JSCFunctionListEntry js_xml_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_xml_read),
    JS_CFUNC_DEF("write", 2, js_xml_write),
};

/**
 * XMLSerializer: a pull serializer, modeled on JsonSerializer (quickjs-json.c). The
 * constructor is given an object tree (root); read(buf, offset, length) writes as much
 * serialized XML text as fits into the caller's buffer, resuming on the next call
 * exactly where the last one left off. Traversal state (the open-element stack) is
 * tracked in the XMLSerializer struct via property_recursion_push()/pop(), and is only
 * ever pushed for a node's "children" array (list frames) or, transiently, its
 * "attributes" object (xs->attrs) - never the generic, any-property
 * property_recursion_next().
 */
static JSClassID js_xmlserializer_class_id;
static JSValue xmlserializer_proto, xmlserializer_ctor;

typedef struct {
  uint8_t* dst;
  size_t cap;
  size_t pos;
} XMLCappedBuf;

/* All-or-nothing writer over a fixed caller-supplied buffer: a write either fits
 * entirely (returns len) or is refused whole (returns 0, "no room yet") - the same
 * zero-copy destination scheme as JsonSerializer.read(buffer)/write_capped(). */
static ssize_t
xml_write_capped(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  XMLCappedBuf* c = (XMLCappedBuf*)fd;

  if(c->pos + len > c->cap)
    return 0;

  memcpy(c->dst + c->pos, buf, len);
  c->pos += len;
  return (ssize_t)len;
}

typedef struct {
  Vector stack;              /* Vector<PropertyEnumeration>: the root array, or some element's
                                "children" array - one frame per open element. */
  Vector owners;             /* Vector<JSValue>, 1:1 with `stack`: the element each frame's
                                "children" belongs to (JS_UNDEFINED for the root frame) - used to
                                emit the matching closing tag once a frame is exhausted. */
  PropertyEnumeration attrs; /* transient: the "attributes" of the element currently
                                 being opened, valid only while in_attrs is set. */
  JSValue cur_element;
  BOOL in_attrs;
  BOOL started;
  BOOL finished;
  BOOL error;
  BOOL blocked;
  size_t skip;      /* bytes of the (deterministic) replay to discard: already delivered previously */
  size_t delivered; /* bytes actually forwarded to the destination during the current step attempt */
  JSValue root;
  Writer dest_writer; /* the capped writer for the current read(buf, offset, length) call */
  Writer skip_writer; /* outermost: discards the first `skip` bytes of a step's replay */
  XMLCappedBuf capped;
} XMLSerializer;

static ssize_t
xml_write_skip(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  XMLSerializer* xs = (XMLSerializer*)fd;
  ssize_t w;

  if(xs->skip >= len) {
    xs->skip -= len;
    return (ssize_t)len;
  }

  if(xs->skip > 0) {
    size_t skip = xs->skip;
    size_t remain = len - skip;

    if((w = writer_write(&xs->dest_writer, (const uint8_t*)buf + skip, remain)) <= 0)
      return w;

    xs->skip = 0;
    xs->delivered += (size_t)w;
    return (ssize_t)len;
  }

  w = writer_write(&xs->dest_writer, buf, len);

  if(w > 0)
    xs->delivered += (size_t)w;

  return w;
}

/* Checked wrappers: on a blocked/error write, set xs->blocked/xs->error and return
 * FALSE so the caller can bail out before mutating any traversal state. */

static BOOL
xsw_write(XMLSerializer* xs, const void* buf, size_t len) {
  ssize_t w = writer_write(&xs->skip_writer, buf, len);

  if(w < 0) {
    xs->error = TRUE;
    return FALSE;
  }
  if(w == 0) {
    xs->blocked = TRUE;
    return FALSE;
  }
  return TRUE;
}

static BOOL
xsw_putc(XMLSerializer* xs, int c) {
  ssize_t w = writer_putc(&xs->skip_writer, c);

  if(w < 0) {
    xs->error = TRUE;
    return FALSE;
  }
  if(w == 0) {
    xs->blocked = TRUE;
    return FALSE;
  }
  return TRUE;
}

static BOOL
xsw_puts(XMLSerializer* xs, const char* s) {
  return xsw_write(xs, s, strlen(s));
}

/* Unlike xsw_write()/xsw_puts() (fine for short, fixed literals), this writes
 * arbitrary-length, runtime-supplied content (tag names, text, attribute keys/values)
 * one byte at a time - so a destination buffer far smaller than the content (even a
 * single byte, as with .read(buf, offset, 1)) can still make forward progress, one
 * xsw_putc() at a time, the same granularity write_json_string() uses in quickjs-json.c. */
static BOOL
xsw_raw(XMLSerializer* xs, const char* s, size_t len) {
  size_t i;

  for(i = 0; i < len; i++)
    if(!xsw_putc(xs, (unsigned char)s[i]))
      return FALSE;

  return TRUE;
}

static JSValue
xmlserializer_wrap_root(JSContext* ctx, JSValueConst root) {
  if(JS_IsArray(ctx, root))
    return JS_DupValue(ctx, root);

  JSValue arr = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, arr, 0, JS_DupValue(ctx, root));
  return arr;
}

/* Attributes of xs->cur_element are fully emitted (or it had none): decide
 * self-closing vs. pushing a "children" frame, then hand control back to whichever
 * list frame is (still) on top of xs->stack. */
static void
xmlserializer_finish_open(XMLSerializer* xs, JSContext* ctx) {
  JSValue children = JS_GetPropertyStr(ctx, xs->cur_element, "children");
  BOOL has_children = JS_IsArray(ctx, children) && js_array_length(ctx, children) > 0;

  if(has_children) {
    if(!xsw_putc(xs, '>')) {
      JS_FreeValue(ctx, children);
      return;
    }

    PropertyEnumeration* parent = vector_back(&xs->stack, sizeof(PropertyEnumeration));
    parent->idx++;

    if(!property_recursion_push(&xs->stack, ctx, children, PROPENUM_DEFAULT_FLAGS)) {
      xs->error = TRUE;
      return;
    }

    vector_push(&xs->owners, xs->cur_element);
    xs->cur_element = JS_UNDEFINED;
  } else {
    JS_FreeValue(ctx, children);

    if(!xsw_puts(xs, " />"))
      return;

    JS_FreeValue(ctx, xs->cur_element);
    xs->cur_element = JS_UNDEFINED;

    PropertyEnumeration* top = vector_back(&xs->stack, sizeof(PropertyEnumeration));
    top->idx++;
  }
}

static void
xmlserializer_step_inner(XMLSerializer* xs, JSContext* ctx) {
  if(!xs->started) {
    JSValue undef = JS_UNDEFINED;
    JSValue rootArr = xmlserializer_wrap_root(ctx, xs->root);

    xs->started = TRUE;

    /* an empty root list has nothing to serialize; property_recursion_push() on a
     * zero-property object spuriously reports failure (see the property_enumeration_init()
     * comment below), so short-circuit rather than mistaking this for a real error. */
    if(js_array_length(ctx, rootArr) == 0) {
      JS_FreeValue(ctx, rootArr);
      xs->finished = TRUE;
      return;
    }

    PropertyEnumeration* it = property_recursion_push(&xs->stack, ctx, rootArr, PROPENUM_DEFAULT_FLAGS);

    if(!it) {
      xs->error = TRUE;
      return;
    }

    vector_push(&xs->owners, undef);
    return;
  }

  if(xs->in_attrs) {
    if(xs->attrs.idx >= xs->attrs.tab_atom_len) {
      xmlserializer_finish_open(xs, ctx);

      /* only leave in_attrs once finish_open() actually got all its bytes out - if it
       * blocked partway (e.g. " />" not fitting), xs->attrs must stay alive so the next
       * read() call retries finish_open() instead of falling through to the "list frame"
       * code below and reopening this same element's tag from scratch. */
      if(!xs->blocked && !xs->error) {
        property_enumeration_reset(&xs->attrs, JS_GetRuntime(ctx));
        xs->in_attrs = FALSE;
      }

      return;
    }

    size_t keylen;
    const char* keystr = property_enumeration_keystrlen(&xs->attrs, &keylen, ctx);
    JSValue value = property_enumeration_value(&xs->attrs, ctx);
    BOOL boolTrue = JS_IsBool(value) && JS_ToBool(ctx, value);
    BOOL ok = xsw_putc(xs, ' ') && xsw_raw(xs, keystr, keylen);

    if(ok && !boolTrue) {
      size_t valuelen;
      const char* valuestr = JS_ToCStringLen(ctx, &valuelen, value);

      ok = xsw_puts(xs, "=\"") && xsw_raw(xs, valuestr, valuelen) && xsw_putc(xs, '"');

      JS_FreeCString(ctx, valuestr);
    }

    JS_FreeCString(ctx, keystr);
    JS_FreeValue(ctx, value);

    if(ok)
      xs->attrs.idx++;

    return;
  }

  PropertyEnumeration* top = vector_back(&xs->stack, sizeof(PropertyEnumeration));

  if(top->idx >= top->tab_atom_len) {
    JSValue* ownerp = vector_back(&xs->owners, sizeof(JSValue));

    if(JS_IsUndefined(*ownerp)) {
      xs->finished = TRUE;
      return;
    }

    size_t tagLen;
    const char* tagName = js_get_propertystr_cstringlen(ctx, *ownerp, "tagName", &tagLen);
    BOOL ok = TRUE;

    if(tagName && tagName[0] && tagName[0] != '!' && tagName[0] != '?')
      ok = xsw_putc(xs, '<') && xsw_putc(xs, '/') && xsw_raw(xs, tagName, tagLen) && xsw_putc(xs, '>');

    if(tagName)
      JS_FreeCString(ctx, tagName);

    if(!ok)
      return;

    property_enumeration_reset(top, JS_GetRuntime(ctx));
    vector_pop(&xs->stack, sizeof(PropertyEnumeration));

    JS_FreeValue(ctx, *ownerp);
    vector_pop(&xs->owners, sizeof(JSValue));
    return;
  }

  JSValue val = property_enumeration_value(top, ctx);

  if(JS_IsString(val)) {
    size_t slen;
    const char* s = JS_ToCStringLen(ctx, &slen, val);
    BOOL ok = xsw_raw(xs, s, slen);

    JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, val);

    if(ok)
      top->idx++;

    return;
  }

  if(!JS_IsObject(val)) {
    JS_FreeValue(ctx, val);
    top->idx++;
    return;
  }

  size_t tagLen;
  const char* tagName = js_get_propertystr_cstringlen(ctx, val, "tagName", &tagLen);

  if(!tagName || !tagName[0]) {
    if(tagName)
      JS_FreeCString(ctx, tagName);

    JS_FreeValue(ctx, val);
    top->idx++;
    return;
  }

  BOOL isComment = tagLen >= 3 && !strncmp(tagName, "!--", 3);
  BOOL isPI = tagName[0] == '?';
  BOOL isDecl = !isComment && tagName[0] == '!';

  if(isComment || isDecl || isPI) {
    BOOL ok = xsw_putc(xs, '<') && xsw_raw(xs, tagName, tagLen) && xsw_puts(xs, isPI ? "?>" : ">");

    JS_FreeCString(ctx, tagName);

    if(!ok) {
      JS_FreeValue(ctx, val);
      return;
    }

    JS_FreeValue(ctx, val);
    top->idx++;
    return;
  }

  BOOL ok = xsw_putc(xs, '<') && xsw_raw(xs, tagName, tagLen);

  JS_FreeCString(ctx, tagName);

  if(!ok) {
    JS_FreeValue(ctx, val);
    return;
  }

  JSValue attributes = JS_GetPropertyStr(ctx, val, "attributes");

  if(!JS_IsObject(attributes)) {
    JS_FreeValue(ctx, attributes);
    attributes = JS_NewObject(ctx);
  }

  /* return value deliberately unchecked: property_enumeration_init() reports -1 even for
   * the harmless "object has zero enumerable properties" case (js_object_properties()'s
   * realloc-to-0 can return NULL there), matching xml_write_attributes()'s convention of
   * ignoring it - xs->attrs.tab_atom_len is 0 either way, which the attrs-draining step
   * below handles like any other empty attribute set. */
  property_enumeration_init(&xs->attrs, ctx, attributes, PROPENUM_DEFAULT_FLAGS);

  xs->cur_element = val;
  xs->in_attrs = TRUE;
}

/* Wraps xmlserializer_step_inner() with the skip/replay bookkeeping: a step's writes
 * are deterministic given unchanged traversal state, so on a blocked attempt we fold
 * whatever was newly delivered into xs->skip (discarded on the next replay) instead of
 * losing or duplicating it; a clean step clears xs->skip since it now applies to
 * whatever step comes next. */
static void
xmlserializer_step(XMLSerializer* xs, JSContext* ctx) {
  size_t skip_before = xs->skip;

  xs->delivered = 0;
  xmlserializer_step_inner(xs, ctx);

  if(xs->blocked)
    xs->skip = skip_before + xs->delivered;
  else if(!xs->error)
    xs->skip = 0;
}

static JSValue
js_xmlserializer_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XMLSerializer* xs;
  InputBuffer buf;

  if(!(xs = JS_GetOpaque2(ctx, this_val, js_xmlserializer_class_id)))
    return JS_EXCEPTION;

  xs->error = FALSE;
  xs->blocked = FALSE;

  buf = js_input_args(ctx, argc, argv);

  xs->capped.dst = (uint8_t*)inputbuffer_data(&buf);
  xs->capped.cap = inputbuffer_length(&buf);
  xs->capped.pos = 0;
  xs->dest_writer = (Writer){&xml_write_capped, &xs->capped, NULL};

  while(!xs->finished && !xs->error && !xs->blocked && xs->capped.pos < xs->capped.cap)
    xmlserializer_step(xs, ctx);

  inputbuffer_free(&buf, ctx);

  if(xs->error)
    return JS_EXCEPTION;

  return JS_NewInt64(ctx, (int64_t)xs->capped.pos);
}

static JSValue
js_xmlserializer_get_root(JSContext* ctx, JSValueConst this_val) {
  XMLSerializer* xs;

  if(!(xs = JS_GetOpaque2(ctx, this_val, js_xmlserializer_class_id)))
    return JS_EXCEPTION;

  return JS_DupValue(ctx, xs->root);
}

static JSValue
js_xmlserializer_get_finished(JSContext* ctx, JSValueConst this_val) {
  XMLSerializer* xs;

  if(!(xs = JS_GetOpaque2(ctx, this_val, js_xmlserializer_class_id)))
    return JS_EXCEPTION;

  return JS_NewBool(ctx, xs->finished);
}

static JSValue
js_xmlserializer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  XMLSerializer* xs;
  JSValueConst root = argc > 0 ? argv[0] : JS_UNDEFINED;

  if(!(xs = js_mallocz(ctx, sizeof(XMLSerializer))))
    return JS_EXCEPTION;

  xs->stack = VECTOR(ctx);
  xs->owners = VECTOR(ctx);
  xs->root = JS_DupValue(ctx, root);
  xs->cur_element = JS_UNDEFINED;
  xs->skip_writer = (Writer){&xml_write_skip, xs, NULL};

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_xmlserializer_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, xs);
  return obj;

fail:
  JS_FreeValue(ctx, xs->root);
  vector_free(&xs->stack);
  vector_free(&xs->owners);
  js_free(ctx, xs);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_xmlserializer_finalizer(JSRuntime* rt, JSValue val) {
  XMLSerializer* xs;

  if((xs = JS_GetOpaque(val, js_xmlserializer_class_id))) {
    JSValue* ownerp;

    property_recursion_free(&xs->stack, rt);

    vector_foreach_t(&xs->owners, ownerp) {
      JS_FreeValueRT(rt, *ownerp);
    }
    vector_free(&xs->owners);

    if(xs->in_attrs)
      property_enumeration_reset(&xs->attrs, rt);

    JS_FreeValueRT(rt, xs->cur_element);
    JS_FreeValueRT(rt, xs->root);
    orig_js_free_rt(rt, xs);
  }
}

static JSClassDef js_xmlserializer_class = {
    .class_name = "XMLSerializer",
    .finalizer = js_xmlserializer_finalizer,
};

static const JSCFunctionListEntry js_xmlserializer_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_xmlserializer_read),
    JS_CGETSET_DEF("root", js_xmlserializer_get_root, 0),
    JS_CGETSET_DEF("finished", js_xmlserializer_get_finished, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLSerializer", JS_PROP_CONFIGURABLE),
};

#include "include/xread.h"

/*
 * XmlBuilder: builds a {tagName, attributes, children}-shaped JSValue tree from a
 * stream of element_start/attribute/element_end calls (e.g. driven by xr_read()'s
 * callback), independent of any particular parser's own bookkeeping.
 *
 * The path from the document root down to the currently-open element is a plain
 * singly-linked list of XmlBuilderFrame, one per open element plus a synthetic root
 * frame (element/attributes == JS_UNDEFINED) whose `children` is the top-level result
 * array. xml_builder_push() pushes a new frame (and appends its `element`
 * into the *parent* frame's `children`); xml_builder_pop() pops back to
 * frame->parent.
 */
typedef struct XmlBuilderFrame {
  struct XmlBuilderFrame* parent;
  JSValue element;
  JSValue attributes;
  JSValue children;
} XmlBuilderFrame;

typedef struct XmlBuilder {
  JSContext* ctx;
  XmlBuilderFrame* top;
} XmlBuilder;

static void
xml_builder_init(XmlBuilder* b, JSContext* ctx) {
  XmlBuilderFrame* root = js_mallocz(ctx, sizeof(XmlBuilderFrame));

  root->parent = 0;
  root->element = JS_UNDEFINED;
  root->attributes = JS_UNDEFINED;
  root->children = JS_NewArray(ctx);

  b->ctx = ctx;
  b->top = root;
}

/* The top-level array of root elements/text built so far (a new reference). Typically
 * called once the document is complete, before xml_builder_free(). */
static JSValue
xml_builder_root(XmlBuilder* b) {
  XmlBuilderFrame* frame = b->top;

  while(frame->parent)
    frame = frame->parent;

  return JS_DupValue(b->ctx, frame->children);
}

/* Releases every frame still on the stack, root included - so it's safe to call on an
 * incomplete tree (e.g. after a truncated/errored parse whose element_end calls never
 * caught up with its element_start calls). Callers that want the built tree must
 * xml_builder_root() (or otherwise dup what they need out of it) first.
 *
 * Takes an explicit JSRuntime* (rather than using b->ctx, like the rest of XmlBuilder)
 * so it's safe to call from a JSClassFinalizer, which only gets a JSRuntime* - the
 * JSContext a builder was initialized with is not guaranteed to still be alive by
 * then. */
static void
xml_builder_free(XmlBuilder* b, JSRuntime* rt) {
  XmlBuilderFrame* frame = b->top;

  while(frame) {
    XmlBuilderFrame* parent = frame->parent;

    JS_FreeValueRT(rt, frame->element);
    JS_FreeValueRT(rt, frame->attributes);
    JS_FreeValueRT(rt, frame->children);
    js_free_rt(rt, frame);

    frame = parent;
  }

  b->top = 0;
}

static void
xml_builder_push(XmlBuilder* b, const char* name, size_t namelen) {
  JSContext* ctx = b->ctx;
  XmlBuilderFrame* frame = js_mallocz(ctx, sizeof(XmlBuilderFrame));
  JSValue element = JS_NewObjectProto(ctx, JS_NULL);
  JSValue attributes = JS_NewObjectProto(ctx, JS_NULL);
  JSValue children = JS_NewArray(ctx);
  JSValue ret;

  JS_SetPropertyStr(ctx, element, "tagName", JS_NewStringLen(ctx, name, namelen));
  JS_SetPropertyStr(ctx, element, "attributes", JS_DupValue(ctx, attributes));
  JS_SetPropertyStr(ctx, element, "children", JS_DupValue(ctx, children));

  ret = js_invoke(ctx, b->top->children, "push", 1, &element);
  JS_FreeValue(ctx, ret);

  frame->parent = b->top;
  frame->element = element;
  frame->attributes = attributes;
  frame->children = children;

  b->top = frame;
}

static void
xml_builder_attribute(XmlBuilder* b, const char* name, size_t namelen, const char* value, size_t valuelen) {
  JSContext* ctx = b->ctx;
  JSAtom prop = JS_NewAtomLen(ctx, name, namelen);

  JS_SetProperty(ctx, b->top->attributes, prop, JS_NewStringLen(ctx, value, valuelen));
  JS_FreeAtom(ctx, prop);
}

/* Pops the currently-open element; a no-op at the (already-closed) root frame. */
static void
xml_builder_pop(XmlBuilder* b) {
  XmlBuilderFrame* frame = b->top;

  if(!frame->parent)
    return;

  b->top = frame->parent;

  JS_FreeValue(b->ctx, frame->element);
  JS_FreeValue(b->ctx, frame->attributes);
  JS_FreeValue(b->ctx, frame->children);
  js_free(b->ctx, frame);
}

typedef struct PushParser {
  JSContext* ctx;
  JSValue this_obj;
  struct xr_state xrs;
  JSValue attribute, element_start, element_end, error;
  XmlBuilder builder;
} XmlPushParser;

static void
xread_callback_build(xr_type_t type, const xr_str_t* name, const xr_str_t* value, void* user_data) {
  XmlPushParser* pp = user_data;

  switch(type) {
    case xr_type_attribute:
      xml_builder_attribute(&pp->builder, name->cstr, name->len, value->cstr, value->len);
      break;
    case xr_type_element_start:
      xml_builder_push(&pp->builder, name->cstr, name->len);
      break;
    case xr_type_element_end:
      xml_builder_pop(&pp->builder);
      break;
    case xr_type_error:
      break;
  }
}

static void
xread_callback(xr_type_t type, const xr_str_t* name, const xr_str_t* value, void* user_data) {
  XmlPushParser* pp = user_data;
  JSValue args[] = {
      name ? JS_NewStringLen(pp->ctx, name->cstr, name->len) : JS_UNDEFINED,
      value ? JS_NewStringLen(pp->ctx, value->cstr, value->len) : JS_UNDEFINED,
  };
  JSValue* cb = 0;

  switch(type) {
    case xr_type_attribute: cb = &pp->attribute; break;
    case xr_type_element_start: cb = &pp->element_start; break;
    case xr_type_element_end: cb = &pp->element_end; break;
    case xr_type_error: cb = &pp->error; break;
  }

  /* the options object passed to the constructor need not define every callback -
   * silently skip whichever ones it left out, rather than trying to JS_Call() a
   * non-function and leaving a pending exception behind. */
  if(cb && JS_IsFunction(pp->ctx, *cb)) {
    JSValue ret = JS_Call(pp->ctx, *cb, pp->this_obj, countof(args), args);
    JS_FreeValue(pp->ctx, ret);
  }

  JS_FreeValue(pp->ctx, args[0]);
  JS_FreeValue(pp->ctx, args[1]);
}

static JSClassID js_xml_pushparser_class_id;
static JSValue xml_pushparser_proto, xml_pushparser_ctor;

static JSValue
js_xml_pushparser_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XmlPushParser* pp;
  InputBuffer input;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  input = js_input_chars(ctx, argv[0]);

  if(input.data == 0) {
    JS_ThrowReferenceError(ctx, "XmlPushParser.write(): expecting buffer or string");
    return JS_EXCEPTION;
  }

  xr_read(xread_callback, inputbuffer_data(&input), inputbuffer_length(&input), pp, &pp->xrs);
  inputbuffer_free(&input, ctx);

  return JS_UNDEFINED;
}

static JSValue
js_xml_pushparser_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XmlPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  xr_finish(xread_callback, pp, &pp->xrs);

  return JS_UNDEFINED;
}

static JSValue
js_xml_pushparser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  XmlPushParser* pp;
  JSValueConst options = argc > 0 ? argv[0] : JS_UNDEFINED;

  if(!(pp = js_mallocz(ctx, sizeof(XmlPushParser))))
    return JS_EXCEPTION;

  pp->ctx = ctx;

  /* options is also this_obj: the object callbacks are invoked with, so an
   * elementStart() etc. method can be defined directly on the options object passed
   * to the constructor and use `this` to reach the others (or the parser itself, via
   * a self-reference the caller stashes on it) without a separate thisArg. */
  pp->this_obj = JS_DupValue(ctx, options);
  pp->attribute = JS_IsObject(options) ? JS_GetPropertyStr(ctx, options, "attribute") : JS_UNDEFINED;
  pp->element_start = JS_IsObject(options) ? JS_GetPropertyStr(ctx, options, "elementStart") : JS_UNDEFINED;
  pp->element_end = JS_IsObject(options) ? JS_GetPropertyStr(ctx, options, "elementEnd") : JS_UNDEFINED;
  pp->error = JS_IsObject(options) ? JS_GetPropertyStr(ctx, options, "error") : JS_UNDEFINED;

  xr_state_init(&pp->xrs);

  xml_builder_init(&pp->builder, ctx);

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, xml_pushparser_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_xml_pushparser_class_id);
  JS_FreeValue(ctx, proto);

  JS_SetOpaque(obj, pp);
  return obj;
}

static void
js_xml_pushparser_finalizer(JSRuntime* rt, JSValue val) {
  XmlPushParser* pp;

  if((pp = JS_GetOpaque(val, js_xml_pushparser_class_id))) {
    xr_state_free(&pp->xrs);
    xml_builder_free(&pp->builder, rt);
    JS_FreeValueRT(rt, pp->this_obj);
    JS_FreeValueRT(rt, pp->attribute);
    JS_FreeValueRT(rt, pp->element_start);
    JS_FreeValueRT(rt, pp->element_end);
    JS_FreeValueRT(rt, pp->error);
    js_free_rt(rt, pp);
  }
}

static const JSCFunctionListEntry js_xml_pushparser_proto_funcs[] = {
    JS_CFUNC_DEF("write", 1, js_xml_pushparser_write),
    JS_CFUNC_DEF("close", 0, js_xml_pushparser_close),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLPushParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_xml_pushparser_class = {
    .class_name = "XMLPushParser",
    .finalizer = js_xml_pushparser_finalizer,
};

static int
js_xml_init(JSContext* ctx, JSModuleDef* m) {
  character_classes_init(chars);

  if(js_location_class_id == 0)
    js_location_init(ctx, 0);

  JS_SetModuleExportList(ctx, m, js_xml_funcs, countof(js_xml_funcs));

  JSValue defaultObj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, defaultObj, "read", JS_NewCFunction(ctx, js_xml_read, "read", 1));
  JS_SetPropertyStr(ctx, defaultObj, "write", JS_NewCFunction(ctx, js_xml_write, "write", 2));
  JS_SetModuleExport(ctx, m, "default", defaultObj);

  JS_NewClassID(&js_xmlserializer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xmlserializer_class_id, &js_xmlserializer_class);

  xmlserializer_ctor = JS_NewCFunction2(ctx, js_xmlserializer_constructor, "XMLSerializer", 1, JS_CFUNC_constructor, 0);
  xmlserializer_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, xmlserializer_proto, js_xmlserializer_funcs, countof(js_xmlserializer_funcs));
  JS_SetClassProto(ctx, js_xmlserializer_class_id, xmlserializer_proto);
  JS_SetConstructor(ctx, xmlserializer_ctor, xmlserializer_proto);

  JS_SetModuleExport(ctx, m, "XMLSerializer", xmlserializer_ctor);

  JS_NewClassID(&js_xml_pushparser_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xml_pushparser_class_id, &js_xml_pushparser_class);

  xml_pushparser_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, xml_pushparser_proto, js_xml_pushparser_proto_funcs, countof(js_xml_pushparser_proto_funcs));

  xml_pushparser_ctor = JS_NewCFunction2(ctx, js_xml_pushparser_constructor, "XMLPushParser", 1, JS_CFUNC_constructor, 0);
  JS_SetClassProto(ctx, js_xml_pushparser_class_id, xml_pushparser_proto);
  JS_SetConstructor(ctx, xml_pushparser_ctor, xml_pushparser_proto);

  JS_SetModuleExport(ctx, m, "XMLPushParser", xml_pushparser_ctor);
  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_xml
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_xml_init))) {
    JS_AddModuleExportList(ctx, m, js_xml_funcs, countof(js_xml_funcs));
    JS_AddModuleExport(ctx, m, "default");
    JS_AddModuleExport(ctx, m, "XMLSerializer");
    JS_AddModuleExport(ctx, m, "XMLPushParser");
  }

  return m;
}
