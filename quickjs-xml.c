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
#include "xml/xwrite.h"
#include "xml/xsink.h"
#include "xml/xlayout.h"
#include "xml/xtreewrite.h"
#include "xml/xread.h"
#include "xml/xbuilder.h"
#include "xml/xml_entities.h"
#include "xml/xml.h"

#include <stdint.h>

char* js_inspect_tostring(JSContext* ctx, JSValueConst value);

/**
 * \defgroup quickjs-xml quickjs-xml: XML parser & printer
 * @{
 */
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
  BOOL flat, tolerant, location;
  const char* const* self_closing_tags;
} ParseOptions;

#ifdef DEBUG_OUTPUT
#define xml_debug(args...) printf(args)
#else
#define xml_debug(args...)
#endif

static JSValue
make_tuple(JSContext* ctx, JSValue a, JSValue b) {
  JSValue ret = JS_NewArray(ctx);

  JS_SetPropertyUint32(ctx, ret, 0, a);
  JS_SetPropertyUint32(ctx, ret, 1, b);

  return ret;
}

/* the element tree or flat list of `buf`, driven by the event parser of src/xml/xml.c.
 *
 *   tree  [{tagName, attributes, children}], children only on elements that stay open
 *   flat  [{tagName, attributes}, 'text', {tagName: '/name'}, ...]
 *
 * returns the array (or [array, WeakMap] with location) or throws SyntaxError. */
/* js_xml_parse: the element tree or flat list of `buf`, driven by the event parser of
 * src/xml/xml.c and built by XmlBuilder (src/xml/xbuilder.c).
 *
 *   tree  [{tagName, attributes, children}], children only on elements that stay open
 *   flat  [{tagName, attributes}, 'text', {tagName: '/name'}, ...]
 *
 * returns the array (or [array, WeakMap] with location) or throws SyntaxError. */
static JSValue
js_xml_parse(JSContext* ctx, const uint8_t* buf, size_t len, const char* input_name, ParseOptions opts) {
  Reader reader = reader_from_bytes(buf, len);
  XMLParser xp;
  XmlBuilder xb;
  JSValue ret;
  VirtualProperties vprop = {0};
  xml_event_t ev;
  BOOL failed = FALSE;

  xml_parser_init(&xp, &reader);
  xml_parser_set_tolerant(&xp, opts.tolerant);
  xml_parser_set_flat(&xp, opts.flat);
  xp.self_closing_tags = opts.self_closing_tags;
  xbuild_init(&xb, ctx, XB_PLAIN | (opts.flat ? XB_FLAT : 0));

  if(opts.location)
    vprop = virtual_properties_map(ctx, js_object_new(ctx, "WeakMap", 0, 0));

  while((ev = xml_parser_run(&xp)) > XML_PARSE_OK || ev == XML_PARSE_AGAIN) {
    BOOL located = FALSE;

    switch(ev) {
      case XML_ELEMENT_START:
        xbuild_start(&xb, xp.event_name.data, xp.event_name.len);
        located = TRUE;
        break;

      case XML_ATTRIBUTE: xbuild_attribute(&xb, xp.event_name.data, xp.event_name.len, xp.event_has_value ? (xp.event_value.data ? xp.event_value.data : "") : NULL, xp.event_value.len); break;
      case XML_TEXT: xbuild_text(&xb, xp.event_value.data, xp.event_value.len); break;

      case XML_ELEMENT_END:
        xbuild_end(&xb, xp.event_name.data, xp.event_name.len, xp.self_closing);
        located = opts.flat;
        break;

      default: break;
    }

    /* the end of a flat list's comment or declaration makes no entry (last is undefined) */
    if(located && opts.location) {
      JSValue last = xbuild_last(&xb);

      if(!JS_IsUndefined(last))
        virtual_set(&vprop, ctx, last, make_tuple(ctx, JS_NewUint32(ctx, xp.tag_line), JS_NewUint32(ctx, xp.tag_column)));

      JS_FreeValue(ctx, last);
    }
  }

  if(ev == XML_PARSE_ERROR) {
    Location* at = location_new(ctx);

    location_set_filename(at, input_name, ctx);
    at->line = xp.tag_line - 1;
    at->column = xp.tag_column - 1;
    js_throw_syntax_error(ctx, at, xp.err_len, xp.err_code, "mismatch </%.*s>", (int)xp.event_name.len, xp.event_name.data);
    location_free(at, JS_GetRuntime(ctx));
    failed = TRUE;
  }

  /* input that ends inside open elements: they keep (empty) children */
  if(!failed)
    xbuild_finish(&xb);

  ret = xbuild_root(&xb);
  xbuild_free(&xb, JS_GetRuntime(ctx));
  xml_parser_free(&xp);
  reader_free(&reader);

  if(failed) {
    JS_FreeValue(ctx, ret);

    if(opts.location)
      JS_FreeValue(ctx, vprop.this_obj);

    return JS_EXCEPTION;
  }

  return opts.location ? make_tuple(ctx, ret, vprop.this_obj) : ret;
}

/* xml_throw_at: throws a SyntaxError for a failure at `pos` of the input called `name`.
 *
 *   const char*       name    input name for loc.file ("f.xml", "<xml>")
 *   const SourcePos*  pos     start of the offending token (1-based)
 *   size_t            length  characters it spans (0 at end of input)
 *   const char*       code    stable kebab-case name
 *
 * returns JS_EXCEPTION. */
static JSValue
xml_throw_at(JSContext* ctx, const char* name, const SourcePos* pos, size_t length, const char* code, const char* msg) {
  Location* at;
  JSValue ret;

  if(!(at = location_new(ctx)))
    return JS_EXCEPTION;

  location_set_filename(at, name, ctx);
  location_from_srcpos(at, pos);
  ret = js_throw_syntax_error(ctx, at, length, code, "%s", msg);
  location_free(at, JS_GetRuntime(ctx));
  return ret;
}

static JSValue
js_xml_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  InputBuffer input;
  const char* input_name = 0;
  ParseOptions opts = {
      .flat = FALSE,
      .tolerant = FALSE,
      .location = FALSE,
      .self_closing_tags = default_self_closing_tags,
  };

  if(!(argc > 0 && js_is_bytes_or_string(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "xml.read(): expecting buffer or string");

  input = js_input_chars(ctx, argv[0]);

  if(input.size == 0) {
    SourcePos start;

    inputbuffer_free(&input, ctx);
    srcpos_init(&start, 0);
    return xml_throw_at(ctx, "<xml>", &start, 0, "unexpected-end", "xml.read(): empty input");
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

/* xml_write_attributes: the own enumerable properties of `attributes` as xlay_attribute() calls.
 * `true` is a bare attribute, anything else its string value. */
static int
xml_write_attributes(JSContext* ctx, JSValueConst attributes, XmlLayout* lay) {
  PropertyEnumeration props = {0};
  int ret = 0;

  property_enumeration_init(&props, ctx, JS_DupValue(ctx, attributes), PROPENUM_DEFAULT_FLAGS);

  for(size_t i = 0; ret == 0 && i < props.tab_atom_len; i++) {
    const char* keystr;
    JSValue value;

    property_enumeration_setpos(&props, i);
    keystr = property_enumeration_keystr(&props, ctx);
    value = property_enumeration_value(&props, ctx);

    if(JS_IsBool(value) && JS_ToBool(ctx, value)) {
      ret = xlay_attribute(lay, keystr, strlen(keystr), 0, 0);
    } else {
      size_t vlen;
      const char* valuestr = JS_ToCStringLen(ctx, &vlen, value);

      ret = valuestr ? xlay_attribute(lay, keystr, strlen(keystr), valuestr, vlen) : -1;
      JS_FreeCString(ctx, valuestr);
    }

    JS_FreeCString(ctx, keystr);
    JS_FreeValue(ctx, value);
  }

  property_enumeration_reset(&props, JS_GetRuntime(ctx));
  return ret;
}

/* js_xml_write_tree: the layout of an element tree; `obj` is an array of elements and texts.
 *
 * returns the string, or JS_EXCEPTION. */
static JSValue
js_xml_write_tree(JSContext* ctx, JSValueConst obj, int max_depth, DynBuf* output) {
  if(xwrite_tree(ctx, obj, max_depth, writer_from_dynbuf(output)) < 0)
    return JS_EXCEPTION;

  return JS_NewStringLen(ctx, (const char*)output->buf, output->size);
}

/* xml_write_flat_element: one entry of a flat list as layout calls; `{tagName: '/x'}` is an end marker.
 *
 * returns 0, or -1 with an exception pending. */
static int
xml_write_flat_element(JSContext* ctx, JSValueConst element, XmlLayout* lay, int depth, BOOL self_closing) {
  size_t len;
  const char* tag = js_get_propertystr_cstringlen(ctx, element, "tagName", &len);
  int r = 0;

  if(!tag)
    return -1;

  if(len == 0) {
    JS_FreeCString(ctx, tag);
    return 0;
  }

  if(tag[0] == '/') {
    r = xlay_end_marker(lay, depth, tag + 1, len - 1);
  } else {
    JSValue attributes = JS_GetPropertyStr(ctx, element, "attributes");

    r = xlay_start(lay, depth, tag, len);

    if(r == 0 && JS_IsObject(attributes))
      r = xml_write_attributes(ctx, attributes, lay);

    if(r == 0)
      r = xlay_open_end(lay, self_closing);

    JS_FreeValue(ctx, attributes);
  }

  JS_FreeCString(ctx, tag);
  return r;
}

static JSValue
js_xml_write_list(JSContext* ctx, JSValueConst obj, size_t len, DynBuf* output) {
  XmlLayout lay;
  int32_t depth = 0;
  BOOL single_line = FALSE;
  JSValue str = JS_EXCEPTION, value = JS_UNDEFINED, next = JS_GetPropertyUint32(ctx, obj, 0);
  const char *tagName = 0, *nextTag = JS_IsObject(next) ? js_get_propertystr_cstring(ctx, next, "tagName") : 0;
  int r = 0;

  xlay_init(&lay, writer_from_dynbuf(output), 0, 0);

  for(size_t i = 0; r == 0 && i < len; i++) {
    JS_FreeValue(ctx, value);
    value = next;
    next = JS_GetPropertyUint32(ctx, obj, i + 1);

    tagName = nextTag;
    nextTag = JS_IsObject(next) ? js_get_propertystr_cstring(ctx, next, "tagName") : 0;

    if(JS_IsString(value)) {
      size_t slen;
      const char* s = JS_ToCStringLen(ctx, &slen, value);

      if(!s) {
        r = -1;
      } else {
        single_line = str_count(s, '\n') == 0;
        r = xlay_text(&lay, depth, !single_line, s, slen);
        JS_FreeCString(ctx, s);
      }
    } else if(JS_IsObject(value) && !JS_IsArray(ctx, value)) {
      const char* tag;

      if((tag = js_get_propertystr_cstring(ctx, value, "tagName"))) {
        /* self-closing: the next entry is this element's own end marker, or this is the
         * last entry; a following text does not count */
        BOOL self_closing = (nextTag && nextTag[0] == '/' && !strcmp(tag, &nextTag[1])) || (tag[0] != '/' && i + 1 >= len);

        if(tag[0] == '/')
          depth--;

        r = xml_write_flat_element(ctx, value, &lay, single_line ? 0 : depth, self_closing);

        if(self_closing) {
          JS_FreeValue(ctx, next);
          next = JS_GetPropertyUint32(ctx, obj, ++i + 1);
        } else if(tag[0] != '/' && tag[0] != '?' && tag[0] != '!' && !strcasecmp(tag, "dt"))
          depth++;

        JS_FreeCString(ctx, tag);
      }

      single_line = FALSE;
    }

    if(tagName)
      JS_FreeCString(ctx, tagName);
  }

  JS_FreeValue(ctx, value);
  JS_FreeValue(ctx, next);

  if(nextTag)
    JS_FreeCString(ctx, nextTag);

  if(r == 0 && xlay_finish(&lay, 0) == 0)
    str = JS_NewStringLen(ctx, (const char*)output->buf, output->size);
  else if(!JS_HasException(ctx))
    JS_ThrowInternalError(ctx, "xml.write(): out of memory");

  xlay_free(&lay);
  return str;
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

  xml_debug("js_xml_write len=%"PRIu64", children=%s, flat=%d\n", (uint64_t)len, JS_ToCString(ctx, children), flat);

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
 * XMLTreeSerializer: a pull serializer, modeled on JsonSerializer (quickjs-json.c). The
 * constructor is given an object tree (root); read(buf, offset, length) writes as much
 * serialized XML text as fits into the caller's buffer, resuming on the next call
 * exactly where the last one left off. Traversal state (the open-element stack) is
 * tracked in the XMLTreeSerializer struct via property_recursion_push()/pop(), and is only
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
  DynBuf out; /* internal buffer, only used as the destination for .read(n) (string mode) */
  size_t out_pos;
  Writer out_writer;  /* wraps xs->out, used for .read(n) */
  Writer dest_writer; /* swappable slot: out_writer for .read(n), the capped writer for .read(buf) */
  Writer skip_writer; /* outermost: discards the first `skip` bytes of a step's replay */
  XMLCappedBuf capped;
} XMLTreeSerializer;

static ssize_t
xml_write_skip(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  XMLTreeSerializer* xs = (XMLTreeSerializer*)fd;
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
xsw_write(XMLTreeSerializer* xs, const void* buf, size_t len) {
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
xsw_putc(XMLTreeSerializer* xs, int c) {
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
xsw_puts(XMLTreeSerializer* xs, const char* s) {
  return xsw_write(xs, s, strlen(s));
}

/* Unlike xsw_write()/xsw_puts() (fine for short, fixed literals), this writes
 * arbitrary-length, runtime-supplied content (tag names, text, attribute keys/values)
 * one byte at a time - so a destination buffer far smaller than the content (even a
 * single byte, as with .read(buf, offset, 1)) can still make forward progress, one
 * xsw_putc() at a time, the same granularity write_json_string() uses in quickjs-json.c. */
static BOOL
xsw_raw(XMLTreeSerializer* xs, const char* s, size_t len) {
  size_t i;

  for(i = 0; i < len; i++)
    if(!xsw_putc(xs, (unsigned char)s[i]))
      return FALSE;

  return TRUE;
}

/* Same escaping as dbuf_put_escaped_xml() (quickjs-xml.c's other, non-resumable
 * writer, shared by xml.write()) - '&'/'<'/'>' always, '"' only in an attribute
 * value - but through xsw_putc() one byte (of the *escaped* output) at a time,
 * same as xsw_raw(), so a blocked write can't lose or duplicate part of a
 * multi-character entity: resuming a step replays the exact same input from the
 * top and xs->skip discards whatever was already delivered, so as long as the
 * output is a deterministic function of the input - which this is - partial
 * progress through an entity is handled the same way xsw_raw() handles partial
 * progress through any other string. Before this existed, xmlserializer_step_inner()
 * wrote raw_attribute/text bytes straight through xsw_raw(), so any value
 * containing one of these characters produced XML that doesn't parse back to the
 * same tree (or doesn't parse at all). */
static BOOL
xsw_raw_escaped(XMLTreeSerializer* xs, const char* s, size_t len, BOOL is_attr) {
  size_t i;

  for(i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    const char* esc = 0;

    if(c == '&')
      esc = "&amp;";
    else if(c == '<')
      esc = "&lt;";
    else if(c == '>')
      esc = "&gt;";
    else if(c == '"' && is_attr)
      esc = "&quot;";

    if(esc) {
      for(; *esc; esc++)
        if(!xsw_putc(xs, (unsigned char)*esc))
          return FALSE;
    } else if(!xsw_putc(xs, c)) {
      return FALSE;
    }
  }

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
xmlserializer_finish_open(XMLTreeSerializer* xs, JSContext* ctx) {
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
xmlserializer_step_inner(XMLTreeSerializer* xs, JSContext* ctx) {
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

      ok = xsw_puts(xs, "=\"") && xsw_raw_escaped(xs, valuestr, valuelen, TRUE) && xsw_putc(xs, '"');

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
    BOOL ok = xsw_raw_escaped(xs, s, slen, FALSE);

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
xmlserializer_step(XMLTreeSerializer* xs, JSContext* ctx) {
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
  XMLTreeSerializer* xs;
  BOOL is_buf;

  if(!(xs = JS_GetOpaque2(ctx, this_val, js_xmlserializer_class_id)))
    return JS_EXCEPTION;

  xs->error = FALSE;
  xs->blocked = FALSE;

  /* Two calling conventions, like JsonSerializer.read() (quickjs-json.c):
   * .read(buffer) writes zero-copy into a caller-supplied ArrayBuffer/TypedArray
   * and returns the byte count; .read(n) returns up to n bytes as a string.
   * Before this fix, every call (including a plain number) went through
   * js_input_args() as if it were a destination buffer - for a number, that
   * stringifies it and then memcpy()s serialized output into the resulting
   * C string's storage, which was never meant to be written to. */
  is_buf = argc > 0 && (js_is_arraybuffer(ctx, argv[0]) || js_is_sharedarraybuffer(ctx, argv[0]) || js_is_typedarray(ctx, argv[0]));

  if(is_buf) {
    InputBuffer buf = js_input_args(ctx, argc, argv);

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

  {
    int64_t n;

    if(JS_ToInt64(ctx, &n, argc > 0 ? argv[0] : JS_UNDEFINED))
      return JS_EXCEPTION;

    if(n < 0)
      return JS_ThrowRangeError(ctx, "size must not be negative");

    xs->dest_writer = xs->out_writer;

    while(!xs->finished && !xs->error && !xs->blocked && (int64_t)(xs->out.size - xs->out_pos) < n)
      xmlserializer_step(xs, ctx);

    if(xs->error)
      return JS_EXCEPTION;

    {
      size_t avail = xs->out.size - xs->out_pos;
      size_t take = (size_t)n < avail ? (size_t)n : avail;
      JSValue ret = JS_NewStringLen(ctx, (const char*)xs->out.buf + xs->out_pos, take);

      xs->out_pos += take;

      if(xs->out_pos == xs->out.size) {
        xs->out.size = 0;
        xs->out_pos = 0;
      } else if(xs->out_pos > 0) {
        memmove(xs->out.buf, xs->out.buf + xs->out_pos, xs->out.size - xs->out_pos);
        xs->out.size -= xs->out_pos;
        xs->out_pos = 0;
      }

      return ret;
    }
  }
}

static JSValue
js_xmlserializer_get_root(JSContext* ctx, JSValueConst this_val) {
  XMLTreeSerializer* xs;

  if(!(xs = JS_GetOpaque2(ctx, this_val, js_xmlserializer_class_id)))
    return JS_EXCEPTION;

  return JS_DupValue(ctx, xs->root);
}

static JSValue
js_xmlserializer_get_finished(JSContext* ctx, JSValueConst this_val) {
  XMLTreeSerializer* xs;

  if(!(xs = JS_GetOpaque2(ctx, this_val, js_xmlserializer_class_id)))
    return JS_EXCEPTION;

  return JS_NewBool(ctx, xs->finished);
}

static JSValue
js_xmlserializer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  XMLTreeSerializer* xs;
  JSValueConst root = argc > 0 ? argv[0] : JS_UNDEFINED;

  if(!(xs = js_mallocz(ctx, sizeof(XMLTreeSerializer))))
    return JS_EXCEPTION;

  xs->stack = VECTOR(ctx);
  xs->owners = VECTOR(ctx);
  xs->root = JS_DupValue(ctx, root);
  xs->cur_element = JS_UNDEFINED;
  dbuf_init_ctx(ctx, &xs->out);
  xs->out_writer = writer_from_dynbuf(&xs->out);
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
  dbuf_free(&xs->out);
  js_free(ctx, xs);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_xmlserializer_finalizer(JSRuntime* rt, JSValue val) {
  XMLTreeSerializer* xs;

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
    dbuf_free(&xs->out);
    orig_js_free_rt(rt, xs);
  }
}

static JSClassDef js_xmlserializer_class = {
    .class_name = "XMLTreeSerializer",
    .finalizer = js_xmlserializer_finalizer,
};

static const JSCFunctionListEntry js_xmlserializer_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_xmlserializer_read),
    JS_CGETSET_DEF("root", js_xmlserializer_get_root, 0),
    JS_CGETSET_DEF("finished", js_xmlserializer_get_finished, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLTreeSerializer", JS_PROP_CONFIGURABLE),
};


typedef struct PushParser {
  JSContext* ctx;
  JSValue this_obj;
  struct xr_state xrs;
  JSValue attribute, element_start, element_end, error, text;
  JSValue on_parse_error; /* onParseError(err): receives the SyntaxError instead of a throw */
  JSValue last_error;     /* SyntaxError of the latest error, or JS_UNDEFINED */
  XmlBuilder builder;
  XmlEventSink sink; /* xbuild_sink(&builder) */
  BOOL use_builder; /* build {tagName, attributes, children} tree
                       instead of forwarding events */
} XMLPushParser;

/* xml_pushparser_report: builds the SyntaxError for a failure at `pos`, keeps it as
 * `last_error` and passes it to `onParseError(err)`; throws nothing.
 *
 * returns TRUE when onParseError exists (the error is handled). */
static BOOL
xml_pushparser_report(XMLPushParser* pp, const SourcePos* pos, size_t length, const char* code, const char* msg) {
  JSContext* ctx = pp->ctx;
  Location* at;
  JSValue err;

  if(!(at = location_new(ctx)))
    return FALSE;

  location_set_filename(at, "<xml>", ctx);
  location_from_srcpos(at, pos);
  err = js_syntax_error_new(ctx, at, length, code, "%s", msg);
  location_free(at, JS_GetRuntime(ctx));

  if(JS_IsException(err)) {
    JS_FreeValue(ctx, JS_GetException(ctx));
    return FALSE;
  }

  JS_FreeValue(ctx, pp->last_error);
  pp->last_error = JS_DupValue(ctx, err);

  if(JS_IsFunction(ctx, pp->on_parse_error)) {
    JSValue ret = JS_Call(ctx, pp->on_parse_error, pp->this_obj, 1, &err);

    JS_FreeValue(ctx, ret); /* an exception from the callback is dropped */
    JS_FreeValue(ctx, JS_GetException(ctx));
  }

  JS_FreeValue(ctx, err);
  return JS_IsFunction(ctx, pp->on_parse_error);
}

static void
xread_callback_build(xr_type_t type, const xr_str_t* name, const xr_str_t* value, void* user_data) {
  XMLPushParser* pp = user_data;
  XmlEvent ev = {0};

  switch(type) {
    /* value == 0 for a boolean attribute */
    case xr_type_attribute:
      ev = (XmlEvent){.type = XE_ATTRIBUTE, .name = name->cstr, .nlen = name->len, .value = value ? value->cstr : 0, .vlen = value ? value->len : 0};
      break;
    case xr_type_element_start: ev = (XmlEvent){.type = XE_ELEMENT_START, .name = name->cstr, .nlen = name->len}; break;
    case xr_type_element_end: ev = (XmlEvent){.type = XE_ELEMENT_END}; break;
    case xr_type_text: ev = (XmlEvent){.type = XE_TEXT, .name = value->cstr, .nlen = value->len}; break;
    case xr_type_error: xml_pushparser_report(pp, &pp->xrs.err_pos, pp->xrs.err_len, xr_error_name(pp->xrs.err_code), "XMLPushParser: malformed input"); return;
  }

  xevent_send(&pp->sink, &ev);
}

static void
xread_callback(xr_type_t type, const xr_str_t* name, const xr_str_t* value, void* user_data) {
  XMLPushParser* pp = user_data;
  JSValue args[] = {
      name ? JS_NewStringLen(pp->ctx, name->cstr, name->len) : JS_UNDEFINED,
      value ? JS_NewStringLen(pp->ctx, value->cstr, value->len) : JS_UNDEFINED,
  };
  JSValue* cb = 0;

  switch(type) {
    case xr_type_attribute:
      cb = &pp->attribute;
      if(value == 0)
        args[1] = JS_TRUE;
      break;
    case xr_type_element_start: cb = &pp->element_start; break;
    case xr_type_element_end: cb = &pp->element_end; break;
    case xr_type_error:
      cb = &pp->error;
      xml_pushparser_report(pp, &pp->xrs.err_pos, pp->xrs.err_len, xr_error_name(pp->xrs.err_code), "XMLPushParser: malformed input");
      break;
    /* text(name, value): name is always undefined (xr_type_text has no name), value
     * is the text content - kept as a (name, value) pair for consistency with every
     * other callback here rather than a single-argument special case. */
    case xr_type_text: cb = &pp->text; break;
  }

  /* the options object passed to the constructor need not define every callback -
   * silently skip whichever ones it left out, rather than trying to JS_Call() a
   * non-function and leaving a pending exception behind. */
  if(cb && JS_IsFunction(pp->ctx, *cb))
    JS_FreeValue(pp->ctx, JS_Call(pp->ctx, *cb, pp->this_obj, countof(args), args));

  JS_FreeValue(pp->ctx, args[0]);
  JS_FreeValue(pp->ctx, args[1]);
}

static JSClassID js_xml_pushparser_class_id;
static JSValue xml_pushparser_proto, xml_pushparser_ctor;

static JSValue
js_xml_pushparser_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XMLPushParser* pp;
  InputBuffer input;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  if(!(argc > 0 && js_is_bytes_or_string(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "XMLPushParser.write(): expecting buffer or string");

  input = js_input_chars(ctx, argv[0]);

  if(input.data == 0)
    return JS_EXCEPTION;

  xr_read(pp->use_builder ? xread_callback_build : xread_callback, inputbuffer_data(&input), inputbuffer_length(&input), pp, &pp->xrs);
  inputbuffer_free(&input, ctx);

  /* xr_read() sets xrs.error and goes silently inert (every further xr_read()/
   * xr_finish() call becomes a no-op) rather than reporting anything itself - most
   * notably on any non-whitespace text between tags, which its grammar doesn't
   * accept at all (see BUGS: xread-no-text-content). With custom callbacks, the
   * caller's own error() callback already ran (xread_callback() dispatches
   * xr_type_error to it) and owns handling this, so only throw here in the
   * builder fallback, which otherwise has no way to report it at all - .root
   * would just silently stop growing. */
  if(pp->xrs.error && pp->use_builder && !JS_IsFunction(ctx, pp->on_parse_error))
    return xml_throw_at(ctx, "<xml>", &pp->xrs.err_pos, pp->xrs.err_len, xr_error_name(pp->xrs.err_code), "XMLPushParser: malformed input");

  return JS_UNDEFINED;
}

static JSValue
js_xml_pushparser_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XMLPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  xr_finish(pp->use_builder ? xread_callback_build : xread_callback, pp, &pp->xrs);

  /* xr_finish()'s own !at_root check (which sets xrs.error) only catches ending
   * mid-tag, e.g. "<a><b" with no closing '>' - src/xml/xread.c's l_tag_end sets
   * at_root=1 after *every* '>', nested or not, so it does NOT mean every opened
   * element was also closed. When building a tree, check the frame stack
   * directly instead, to also catch e.g. "<a><b/>" (well-formed lexically, but
   * <a> itself was never closed). Only in the builder fallback - see write()'s
   * comment above for why custom-callback mode doesn't also throw here. */
  if(pp->xrs.error && pp->use_builder && !JS_IsFunction(ctx, pp->on_parse_error))
    return xml_throw_at(ctx, "<xml>", &pp->xrs.err_pos, pp->xrs.err_len, xr_error_name(pp->xrs.err_code), "XMLPushParser: malformed or truncated input");

  if(pp->xrs.error) /* the error callback already ran; nothing left open to report */
    return JS_UNDEFINED;

  if(pp->use_builder && xbuild_depth(&pp->builder)) {
    if(xml_pushparser_report(pp, &pp->xrs.pos, 0, "unexpected-end", "XMLPushParser: unexpected end of input (unclosed element)"))
      return JS_UNDEFINED;

    return xml_throw_at(ctx, "<xml>", &pp->xrs.pos, 0, "unexpected-end", "XMLPushParser: unexpected end of input (unclosed element)");
  }

  return JS_UNDEFINED;
}

static JSValue
js_xml_pushparser_get_root(JSContext* ctx, JSValueConst this_val) {
  XMLPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  return xbuild_root(&pp->builder);
}

static JSValue
js_xml_pushparser_get_error(JSContext* ctx, JSValueConst this_val) {
  XMLPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  return JS_IsUndefined(pp->last_error) ? JS_NULL : JS_DupValue(ctx, pp->last_error);
}

/* Array of tagName strings from the document root down to the currently-open
 * element, root-first - mirrors JsonPushParser's .path. Only reflects reality
 * when the builder is actually being fed (pp->use_builder); with custom
 * callbacks given instead, xbuild_start()/xbuild_end() are never called, so this
 * stays empty. */
static JSValue
js_xml_pushparser_get_path(JSContext* ctx, JSValueConst this_val) {
  XMLPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  return xbuild_path(&pp->builder);
}

static JSValue
js_xml_pushparser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  XMLPushParser* pp;
  JSValueConst options = argc > 0 ? argv[0] : JS_UNDEFINED;

  if(!(pp = js_mallocz(ctx, sizeof(XMLPushParser))))
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
  pp->text = JS_IsObject(options) ? JS_GetPropertyStr(ctx, options, "text") : JS_UNDEFINED;
  pp->on_parse_error = JS_IsObject(options) ? JS_GetPropertyStr(ctx, options, "onParseError") : JS_UNDEFINED;
  pp->last_error = JS_UNDEFINED;

  /* no real callback given at all: build a tree (retrievable via .root) instead of
   * forwarding events that would just be dropped on the floor. */
  pp->use_builder = !JS_IsFunction(ctx, pp->attribute) && !JS_IsFunction(ctx, pp->element_start) && !JS_IsFunction(ctx, pp->element_end) &&
                    !JS_IsFunction(ctx, pp->error) && !JS_IsFunction(ctx, pp->text);

  xr_state_init(&pp->xrs);

  xbuild_init(&pp->builder, ctx, 0);
  pp->sink = xbuild_sink(&pp->builder);

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
  XMLPushParser* pp;

  if((pp = JS_GetOpaque(val, js_xml_pushparser_class_id))) {
    xr_state_free(&pp->xrs);
    xbuild_free(&pp->builder, rt);
    JS_FreeValueRT(rt, pp->this_obj);
    JS_FreeValueRT(rt, pp->attribute);
    JS_FreeValueRT(rt, pp->element_start);
    JS_FreeValueRT(rt, pp->element_end);
    JS_FreeValueRT(rt, pp->error);
    JS_FreeValueRT(rt, pp->text);
    JS_FreeValueRT(rt, pp->on_parse_error);
    JS_FreeValueRT(rt, pp->last_error);
    js_free_rt(rt, pp);
  }
}

static const JSCFunctionListEntry js_xml_pushparser_proto_funcs[] = {
    JS_CFUNC_DEF("write", 1, js_xml_pushparser_write),
    JS_CFUNC_DEF("close", 0, js_xml_pushparser_close),
    JS_CGETSET_DEF("root", js_xml_pushparser_get_root, 0),
    JS_CGETSET_DEF("path", js_xml_pushparser_get_path, 0),
    JS_CGETSET_DEF("error", js_xml_pushparser_get_error, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLPushParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_xml_pushparser_class = {
    .class_name = "XMLPushParser",
    .finalizer = js_xml_pushparser_finalizer,
};

/* ----------------------------------------------------------------------
   XMLParser: pull (.parse()) parser over include/xml/xml.h's xml_parser_run(),
   modeled on JsonParser (quickjs-json.c) - a constructor taking a reader-
   like input, .parse() advancing one token at a time, and getters for the
   current token/position - except .parse() returns the raw xml_event_t
   token id (an int) rather than a string, and, like XMLPushParser above,
   every element/attribute/text event is also fed into an XmlBuilder as it
   happens, so .root optionally gives the tree built so far without the
   caller having to do that bookkeeping itself.
   ---------------------------------------------------------------------- */

/* xml_parser_error_new: the SyntaxError for the XML_PARSE_ERROR `xp` just returned, at the
 * start of the offending tag; `base` supplies the input name.
 *
 * returns the error object (refcount: a new reference), or JS_EXCEPTION. */
static JSValue
xml_parser_error_new(JSContext* ctx, const XMLParser* xp, const Location* base) {
  Location* at;
  JSValue ret;

  if(!(at = location_clone(base, ctx)))
    return JS_EXCEPTION;

  at->line = xp->tag_line - 1;
  at->column = xp->tag_column - 1;
  at->char_offset = at->byte_offset = -1;
  ret = js_syntax_error_new(ctx, at, xp->err_len, xp->err_code, "mismatch </%.*s>", (int)xp->event_name.len, xp->event_name.data);
  location_free(at, JS_GetRuntime(ctx));
  return ret;
}

typedef struct {
  Reader reader;
  XMLParser xp;
  Location* loc;
  XmlBuilder builder;
  XmlEventSink sink; /* xbuild_sink(&builder) */
  BOOL use_builder;
  JSValue element_start, element_end, attribute, text, error;
  JSValue this_obj;
  JSValue last_error; /* SyntaxError after parse() returned PARSE_ERROR, else undefined */
} XmlParser;

static JSClassID js_xml_parser_class_id;
static JSValue xml_parser_proto, xml_parser_ctor;

static uint32_t
xml_parser_depth(XmlParser* p) {
  uint32_t n = 0;
  XMLFrame* f;

  for(f = p->xp.top; f; f = f->parent)
    n++;

  return n;
}

/* p->loc (a ref-counted, js_malloc'd Location, wrapped as-is by .location) tracks
 * xp.loc (a plain embedded value struct xml_parser_run() advances - see
 * include/xml/xml.h) by copying the fields js_location_wrap()'s consumers care about
 * after every .parse() call; the two can't just be the same object since xp.loc
 * isn't heap-allocated the way location_free() requires. */
static void
xml_parser_sync_location(XmlParser* p) {
  p->loc->line = p->xp.loc.line;
  p->loc->column = p->xp.loc.column;
  p->loc->char_offset = p->xp.loc.char_offset;
  p->loc->byte_offset = p->xp.loc.byte_offset;
}

static void
xml_parser_build(XmlParser* p, xml_event_t ev) {
  XmlEvent e = {0};

  switch(ev) {
    case XML_ELEMENT_START: e = (XmlEvent){.type = XE_ELEMENT_START, .name = p->xp.event_name.data, .nlen = p->xp.event_name.len}; break;
    case XML_ELEMENT_END: xbuild_end(&p->builder, p->xp.event_name.data, p->xp.event_name.len, 0); return;
    case XML_ATTRIBUTE:
      /* a valueless attribute is stored as "" here, `true` in XMLPushParser */
      e = (XmlEvent){.type = XE_ATTRIBUTE,
                     .name = p->xp.event_name.data,
                     .nlen = p->xp.event_name.len,
                     .value = p->xp.event_has_value ? p->xp.event_value.data : "",
                     .vlen = p->xp.event_has_value ? p->xp.event_value.len : 0};
      break;
    case XML_TEXT:
      if(!p->xp.event_has_value)
        return;
      e = (XmlEvent){.type = XE_TEXT, .name = p->xp.event_value.data, .nlen = p->xp.event_value.len};
      break;
    default: return;
  }

  xevent_send(&p->sink, &e);
}

static void
xml_parser_callback(XmlParser* p, xml_event_t ev, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  switch(ev) {
    case XML_ELEMENT_START: {
      JSValue name = JS_NewStringLen(ctx, p->xp.event_name.data, p->xp.event_name.len);
      ret = (JS_Call(ctx, p->element_start, p->this_obj, 1, &name));
      JS_FreeValue(ctx, name);
      break;
    }
    case XML_ATTRIBUTE: {
      JSValue argv[] = {
          JS_NewStringLen(ctx, p->xp.event_name.data, p->xp.event_name.len),
          p->xp.event_has_value ? JS_NewStringLen(ctx, p->xp.event_value.data, p->xp.event_value.len) : JS_UNDEFINED,
      };
      ret = (JS_Call(ctx, p->attribute, p->this_obj, countof(argv), argv));
      JS_FreeValue(ctx, argv[0]);
      JS_FreeValue(ctx, argv[1]);
      break;
    }
    case XML_ELEMENT_END: {
      JSValue name = JS_NewStringLen(ctx, p->xp.event_name.data, p->xp.event_name.len);
      ret = (JS_Call(ctx, p->element_end, p->this_obj, 1, &name));
      JS_FreeValue(ctx, name);
      break;
    }
    case XML_TEXT: {
      JSValue value = p->xp.event_has_value ? JS_NewStringLen(ctx, p->xp.event_value.data, p->xp.event_value.len) : JS_UNDEFINED;
      ret = (JS_Call(ctx, p->text, p->this_obj, 1, &value));
      JS_FreeValue(ctx, value);
      break;
    }

    default: break;
  }

  if(JS_IsException(ret))
    JS_GetException(ctx);
}

static JSValue
js_xml_parser_parse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XmlParser* p;
  xml_event_t ev;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_parser_class_id)))
    return JS_EXCEPTION;

  if(!p->reader.read)
    return JS_ThrowTypeError(ctx, "XMLParser: no input to parse");

  ev = xml_parser_run(&p->xp);

  if(ev == XML_PARSE_ERROR) {
    JSValue err = xml_parser_error_new(ctx, &p->xp, p->loc);

    if(JS_IsException(err))
      return err;

    JS_FreeValue(ctx, p->last_error);
    p->last_error = err;
  }

  if(p->use_builder)
    xml_parser_build(p, ev);
  else
    xml_parser_callback(p, ev, ctx);

  xml_parser_sync_location(p);

  return JS_NewInt32(ctx, ev);
}

enum {
  XML_PARSER_EVENT_NAME,
  XML_PARSER_EVENT_VALUE,
  XML_PARSER_HAS_VALUE,
  XML_PARSER_DEPTH,
  XML_PARSER_LOCATION,
  XML_PARSER_TOLERANT,
  XML_PARSER_ROOT,
  XML_PARSER_ERROR,
};

static JSValue
js_xml_parser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  XmlParser* p;
  JSValue ret = JS_UNDEFINED;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_parser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case XML_PARSER_EVENT_NAME: {
      if(p->xp.event_name.data && p->xp.event_name.len > 0)
        ret = JS_NewStringLen(ctx, p->xp.event_name.data, p->xp.event_name.len);
      break;
    }
    case XML_PARSER_EVENT_VALUE: {
      if(p->xp.event_has_value)
        ret = JS_NewStringLen(ctx, p->xp.event_value.data, p->xp.event_value.len);
      break;
    }
    case XML_PARSER_HAS_VALUE: {
      ret = JS_NewBool(ctx, p->xp.event_has_value);
      break;
    }
    case XML_PARSER_DEPTH: {
      ret = JS_NewUint32(ctx, xml_parser_depth(p));
      break;
    }
    case XML_PARSER_LOCATION: {
      ret = js_location_wrap(ctx, p->loc);
      break;
    }
    case XML_PARSER_ERROR: {
      ret = JS_IsUndefined(p->last_error) ? JS_NULL : JS_DupValue(ctx, p->last_error);
      break;
    }
    case XML_PARSER_TOLERANT: {
      ret = JS_NewBool(ctx, p->xp.tolerant);
      break;
    }
    case XML_PARSER_ROOT: {
      ret = xbuild_root(&p->builder);
      break;
    }
  }

  return ret;
}

static JSValue
js_xml_parser_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  XmlParser* p;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_parser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case XML_PARSER_TOLERANT: {
      xml_parser_set_tolerant(&p->xp, JS_ToBool(ctx, value));
      break;
    }
  }

  return JS_UNDEFINED;
}

static JSValue
js_xml_parser_callback(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  XmlParser* p;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_parser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case XML_ELEMENT_START: {
      JS_FreeValue(ctx, p->element_start);
      p->element_start = JS_DupValue(ctx, value);
      break;
    }
    case XML_ELEMENT_END: {
      JS_FreeValue(ctx, p->element_end);
      p->element_end = JS_DupValue(ctx, value);
      break;
    }
    case XML_ATTRIBUTE: {
      JS_FreeValue(ctx, p->attribute);
      p->attribute = JS_DupValue(ctx, value);
      break;
    }
    case XML_TEXT: {
      JS_FreeValue(ctx, p->text);
      p->text = JS_DupValue(ctx, value);
      break;
    }
  }

  /* Assigning a real callback post-construction is an explicit request to switch out of
   * builder mode - matches the "no real callback given at all" default computed in
   * js_xml_parser_constructor(), which only defaults to use_builder=TRUE because none of
   * these were set yet. Without this, use_builder stays whatever it was at construction
   * time forever, and callbacks assigned afterward (the only way to assign them without
   * passing an options object to the constructor) are silently never invoked. */
  if(JS_IsFunction(ctx, value))
    p->use_builder = FALSE;

  return JS_UNDEFINED;
}

static JSValue
js_xml_parser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  XmlParser* p;
  int i = 0;

  if(!(p = js_mallocz(ctx, sizeof(XmlParser))))
    return JS_EXCEPTION;

  if(reader_from_js(ctx, argv[i], &p->reader)) {
    i++;
  }

  JSValue options = i == argc ? argv[0] : argv[1];

  if(!(p->loc = location_new(ctx))) {
    reader_free(&p->reader);
    js_free(ctx, p);
    return JS_EXCEPTION;
  }

  JSValue filename = argc > 1 && JS_IsString(argv[1]) ? JS_DupValue(ctx, argv[1]) : JS_GetPropertyStr(ctx, options, "filename");

  char* file = js_tostring_free(ctx, filename);
  if(file) {
    location_set_filename(p->loc, file, ctx);
    js_free(ctx, file);
  }

  p->attribute = JS_GetPropertyStr(ctx, options, "attribute");
  p->element_start = JS_GetPropertyStr(ctx, options, "elementStart");
  p->element_end = JS_GetPropertyStr(ctx, options, "elementEnd");
  p->error = JS_GetPropertyStr(ctx, options, "error");
  p->text = JS_GetPropertyStr(ctx, options, "text");
  p->last_error = JS_UNDEFINED;

  p->this_obj = JS_DupValue(ctx, options);

  if(js_has_propertystr(ctx, options, "builder"))
    p->use_builder = js_tobool_free(ctx, JS_GetPropertyStr(ctx, options, "builder"));
  else
    /* no real callback given at all: build a tree (retrievable via .root) instead of
     * forwarding events that would just be dropped on the floor. */
    p->use_builder = !JS_IsFunction(ctx, p->attribute) && !JS_IsFunction(ctx, p->element_start) && !JS_IsFunction(ctx, p->element_end) &&
                     !JS_IsFunction(ctx, p->error) && !JS_IsFunction(ctx, p->text);

  /* p->xp.reader is a borrowed pointer (include/xml/xml.h doesn't own/copy the Reader
   * it's given), valid as long as p->reader is - i.e. for XmlParser's whole
   * lifetime, since both live in this one allocation and are freed together in
   * the finalizer below. */
  xml_parser_init(&p->xp, &p->reader);
  xbuild_init(&p->builder, ctx, 0);
  p->sink = xbuild_sink(&p->builder);

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, xml_parser_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_xml_parser_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    xml_parser_free(&p->xp);
    reader_free(&p->reader);
    location_free(p->loc, JS_GetRuntime(ctx));
    xbuild_free(&p->builder, JS_GetRuntime(ctx));
    js_free(ctx, p);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, p);
  return obj;
}

static void
js_xml_parser_finalizer(JSRuntime* rt, JSValue val) {
  XmlParser* p;

  if((p = JS_GetOpaque(val, js_xml_parser_class_id))) {
    xml_parser_free(&p->xp);
    reader_free(&p->reader);
    xbuild_free(&p->builder, rt);

    if(p->loc)
      location_free(p->loc, rt);

    JS_FreeValueRT(rt, p->this_obj);
    JS_FreeValueRT(rt, p->last_error);
    js_free_rt(rt, p);
  }
}

static const JSCFunctionListEntry js_xml_parser_proto_funcs[] = {
    JS_CFUNC_DEF("parse", 0, js_xml_parser_parse),
    JS_CGETSET_MAGIC_FLAGS_DEF("eventName", js_xml_parser_get, 0, XML_PARSER_EVENT_NAME, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("eventValue", js_xml_parser_get, 0, XML_PARSER_EVENT_VALUE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("hasValue", js_xml_parser_get, 0, XML_PARSER_HAS_VALUE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("depth", js_xml_parser_get, 0, XML_PARSER_DEPTH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("location", js_xml_parser_get, 0, XML_PARSER_LOCATION, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("tolerant", js_xml_parser_get, js_xml_parser_set, XML_PARSER_TOLERANT),
    JS_CGETSET_MAGIC_FLAGS_DEF("root", js_xml_parser_get, 0, XML_PARSER_ROOT, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("error", js_xml_parser_get, 0, XML_PARSER_ERROR, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("onelementstart", 0, js_xml_parser_callback, XML_ELEMENT_START, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("onelementend", 0, js_xml_parser_callback, XML_ELEMENT_END, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("onattribute", 0, js_xml_parser_callback, XML_ATTRIBUTE, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("ontext", 0, js_xml_parser_callback, XML_TEXT, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_xml_parser_class = {
    .class_name = "XMLParser",
    .finalizer = js_xml_parser_finalizer,
};

/* XMLNodeParser: pull (.parse()) parser that yields one "Flat List" node
   per call. Returns JS strings for text, and JS objects for start/end
   tags (with end tags having a tagName prefixed with '/').
   ---------------------------------------------------------------------- */

static JSClassID js_xml_nodeparser_class_id;
static JSValue xml_nodeparser_proto, xml_nodeparser_ctor;

typedef struct {
  Reader reader;
  XMLParser xp;
  Location* loc;

  /* Flat state machine */
  BOOL has_pending_node;
  JSValue pending_node;
  JSValue pending_attrs;
  xml_event_t buffered_ev;
  BOOL has_buffered_ev;

  uint32_t depth;
  JSValue last_error; /* SyntaxError after parse() returned PARSE_ERROR, else undefined */
} XMLNodeParser;

static int
xml_nodeparser_record_error(JSContext* ctx, XMLNodeParser* p) {
  JSValue err = xml_parser_error_new(ctx, &p->xp, p->loc);

  if(JS_IsException(err))
    return -1;

  JS_FreeValue(ctx, p->last_error);
  p->last_error = err;
  return 0;
}

static void
xml_nodeparser_sync_location(XMLNodeParser* p) {
  p->loc->line = p->xp.loc.line;
  p->loc->column = p->xp.loc.column;
  p->loc->char_offset = p->xp.loc.char_offset;
  p->loc->byte_offset = p->xp.loc.byte_offset;
}

static JSValue
js_xml_nodeparser_parse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XMLNodeParser* p;
  xml_event_t ev;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_nodeparser_class_id)))
    return JS_EXCEPTION;

  /* If we don't have a pending start tag, fetch the next event */
  if(!p->has_pending_node) {
    if(p->has_buffered_ev) {
      ev = p->buffered_ev;
      p->has_buffered_ev = FALSE;
      /* event_name/event_value still point into p->xp.ev_name/ev_value,
       * which haven't been touched since the event was buffered. */
    } else {
      ev = xml_parser_run(&p->xp);
      xml_nodeparser_sync_location(p);

      if(ev == XML_PARSE_ERROR && xml_nodeparser_record_error(ctx, p))
        return JS_EXCEPTION;
    }

    if(ev <= XML_PARSE_OK)
      return JS_NewInt32(ctx, ev);

    if(ev == XML_ELEMENT_START) {
      JSValue node = JS_NewObjectProto(ctx, JS_NULL);
      JS_SetPropertyStr(ctx, node, "tagName", JS_NewStringLen(ctx, p->xp.event_name.data, p->xp.event_name.len));
      JSValue attrs = JS_NewObjectProto(ctx, JS_NULL);
      JS_SetPropertyStr(ctx, node, "attributes", attrs);

      p->pending_node = node;
      p->pending_attrs = attrs;
      p->has_pending_node = TRUE;
      p->depth++;
    } else if(ev == XML_ELEMENT_END) {
      if(p->depth > 0)
        p->depth--;

      JSValue node = JS_NewObjectProto(ctx, JS_NULL);
      char* closeStr = js_malloc(ctx, p->xp.event_name.len + 2);
      closeStr[0] = '/';
      memcpy(closeStr + 1, p->xp.event_name.data, p->xp.event_name.len);
      closeStr[p->xp.event_name.len + 1] = '\0';
      JS_SetPropertyStr(ctx, node, "tagName", JS_NewString(ctx, closeStr));
      js_free(ctx, closeStr);
      return node;

    } else if(ev == XML_TEXT) {
      if(p->xp.event_has_value)
        return JS_NewStringLen(ctx, p->xp.event_value.data, p->xp.event_value.len);

      return JS_NewString(ctx, "");

    } else {
      return JS_NewInt32(ctx, ev);
    }
  }

  /* Consume Attributes for the pending start tag */
  while(1) {
    xml_event_t next_ev;

    if(p->has_buffered_ev) {
      next_ev = p->buffered_ev;
      p->has_buffered_ev = FALSE;
    } else {
      next_ev = xml_parser_run(&p->xp);
      xml_nodeparser_sync_location(p);

      if(next_ev == XML_PARSE_ERROR && xml_nodeparser_record_error(ctx, p))
        return JS_EXCEPTION;
    }

    /* Stream paused waiting for data; preserve state and return */
    if(next_ev == XML_PARSE_AGAIN)
      return JS_NewInt32(ctx, XML_PARSE_AGAIN);

    if(next_ev == XML_PARSE_ERROR || next_ev == XML_PARSE_OK) {
      JS_FreeValue(ctx, p->pending_node);
      p->has_pending_node = FALSE;
      return JS_NewInt32(ctx, next_ev);
    }

    if(next_ev == XML_ATTRIBUTE) {
      JSValue val = p->xp.event_has_value ? JS_NewStringLen(ctx, p->xp.event_value.data, p->xp.event_value.len) : JS_TRUE;
      JSAtom prop = JS_NewAtomLen(ctx, p->xp.event_name.data, p->xp.event_name.len);
      JS_SetProperty(ctx, p->pending_attrs, prop, val);
      JS_FreeAtom(ctx, prop);
    } else {
      /* Attributes finished. Buffer the non-attribute event for the NEXT .parse() call.
       * event_name/event_value remain valid since they point into p->xp.ev_name/ev_value. */
      p->buffered_ev = next_ev;
      p->has_buffered_ev = TRUE;

      JSValue ret = p->pending_node;
      p->has_pending_node = FALSE;
      return ret;
    }
  }
}

enum {
  XML_NODEPARSER_DEPTH,
  XML_NODEPARSER_LOCATION,
  XML_NODEPARSER_ERROR,
};

static JSValue
js_xml_nodeparser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  XMLNodeParser* p;
  JSValue ret = JS_UNDEFINED;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_nodeparser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case XML_NODEPARSER_DEPTH: ret = JS_NewUint32(ctx, p->depth); break;
    case XML_NODEPARSER_LOCATION: ret = js_location_wrap(ctx, p->loc); break;
    case XML_NODEPARSER_ERROR: ret = JS_IsUndefined(p->last_error) ? JS_NULL : JS_DupValue(ctx, p->last_error); break;
  }

  return ret;
}

static JSValue
js_xml_nodeparser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  XMLNodeParser* p;
  int i = 0;

  if(!(p = js_mallocz(ctx, sizeof(XMLNodeParser))))
    return JS_EXCEPTION;

  if(reader_from_js(ctx, argv[i], &p->reader)) {
    i++;
  }

  if(!(p->loc = location_new(ctx))) {
    reader_free(&p->reader);
    js_free(ctx, p);
    return JS_EXCEPTION;
  }

  JSValue options = i == argc ? JS_UNDEFINED : argv[i];

  if(JS_IsObject(options)) {
    JSValue filename = JS_GetPropertyStr(ctx, options, "filename");
    char* file;

    if((file = js_tostring_free(ctx, filename))) {
      location_set_filename(p->loc, file, ctx);
      js_free(ctx, file);
    }
  }

  p->has_pending_node = FALSE;
  p->has_buffered_ev = FALSE;
  p->pending_node = JS_UNDEFINED;
  p->pending_attrs = JS_UNDEFINED;
  p->last_error = JS_UNDEFINED;
  p->depth = 0;

  xml_parser_init(&p->xp, &p->reader);

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, xml_nodeparser_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_xml_nodeparser_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    xml_parser_free(&p->xp);
    reader_free(&p->reader);
    location_free(p->loc, JS_GetRuntime(ctx));
    js_free(ctx, p);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, p);
  return obj;
}

static void
js_xml_nodeparser_finalizer(JSRuntime* rt, JSValue val) {
  XMLNodeParser* p;

  if((p = JS_GetOpaque(val, js_xml_nodeparser_class_id))) {
    xml_parser_free(&p->xp);
    reader_free(&p->reader);

    if(p->loc)
      location_free(p->loc, rt);

    if(p->has_pending_node)
      JS_FreeValueRT(rt, p->pending_node);

    JS_FreeValueRT(rt, p->last_error);
    js_free_rt(rt, p);
  }
}

static const JSCFunctionListEntry js_xml_nodeparser_proto_funcs[] = {
    JS_CFUNC_DEF("parse", 0, js_xml_nodeparser_parse),
    JS_CGETSET_MAGIC_FLAGS_DEF("depth", js_xml_nodeparser_get, 0, XML_NODEPARSER_DEPTH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("location", js_xml_nodeparser_get, 0, XML_NODEPARSER_LOCATION, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("error", js_xml_nodeparser_get, 0, XML_NODEPARSER_ERROR, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLNodeParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_xml_nodeparser_class = {
    .class_name = "XMLNodeParser",
    .finalizer = js_xml_nodeparser_finalizer,
};

typedef struct {
  XmlWriter xw;
  DynBuf buf;     /* backs `xw.writer` when no output was given to the constructor */
  int32_t indent; /* as given; negative: compact */
  int event;      /* XML_* of the last call, 0 before the first */
} XMLWriter;

static JSClassID js_xmlwriter_class_id;
static JSValue xmlwriter_proto, xmlwriter_ctor;

/* xmlwriter_set_indent: remembers `indent` as given (the getter returns it) and sets the layout. */
static void
xmlwriter_set_indent(XMLWriter* wr, int32_t indent) {
  wr->indent = indent;
  xwrite_set_indent(&wr->xw, indent);
}

/* xmlwriter_throw: maps a failed xwrite event (-r) to a JS exception.
 *
 *   code                         throws
 *   XWRITE_E_MISMATCH            TypeError, code mismatched-end-tag
 *   XWRITE_E_UNMATCHED           TypeError, code unmatched-end-tag
 *   XWRITE_E_NOT_IN_TAG          TypeError, code attribute-outside-tag
 *   XWRITE_E_WRITE               Error, code write-failed
 *   XWRITE_E_NOMEM               out of memory
 *
 * returns JS_EXCEPTION. */
static JSValue
xmlwriter_throw(JSContext* ctx, ssize_t r) {
  const char* text = xwrite_error_message(-r);
  char msg[96];
  JSValue err;

  if(-r == XWRITE_E_NOMEM)
    return JS_ThrowOutOfMemory(ctx);

  snprintf(msg, sizeof(msg), "XMLWriter: %s", text ? text : "write failed");

  if(-r == XWRITE_E_WRITE) {
    err = JS_NewError(ctx);
    JS_DefinePropertyValueStr(ctx, err, "message", JS_NewString(ctx, msg), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
  } else {
    JS_ThrowTypeError(ctx, "%s", msg);
    err = JS_GetException(ctx);
  }

  JS_DefinePropertyValueStr(ctx, err, "code", JS_NewString(ctx, xwrite_error_code(-r)), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
  return JS_Throw(ctx, err);
}

/* XMLWriter is an XmlEventSink object: elementStart, attribute, elementEnd, text, comment,
 * declaration, instruction, cdata and error come from js_xmlsink_define() (xml/xsink.h) and
 * write through xwrite.h; failures are thrown by xmlwriter_throw(). */
static XmlEventSink
js_xmlwriter_sink(void* opaque) {
  return xwrite_sink(&((XMLWriter*)opaque)->xw);
}

static void
js_xmlwriter_note(void* opaque, int type) {
  ((XMLWriter*)opaque)->event = type;
}

enum {
  WRITER_STATE,
  WRITER_WRITTEN,
  WRITER_INDENT,
};

static JSValue
js_xmlwriter_get(JSContext* ctx, JSValueConst this_val, int magic) {
  XMLWriter* wr;
  JSValue ret = JS_UNDEFINED;

  if(!(wr = JS_GetOpaque2(ctx, this_val, js_xmlwriter_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITER_STATE: {
      ret = JS_NewInt32(ctx, wr->event);
      break;
    }
    case WRITER_WRITTEN: {
      ret = JS_NewInt64(ctx, wr->xw.written);
      break;
    }
    case WRITER_INDENT: {
      ret = JS_NewInt32(ctx, wr->indent);
      break;
    }
  }

  return ret;
}

static JSValue
js_xmlwriter_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  XMLWriter* wr;
  JSValue ret = JS_UNDEFINED;

  if(!(wr = JS_GetOpaque2(ctx, this_val, js_xmlwriter_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITER_INDENT: {
      xmlwriter_set_indent(wr, js_toint32(ctx, value));
      break;
    }
  }

  return ret;
}

static JSValue
js_xmlwriter_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  XMLWriter* wr;
  int i = 0;

  if(!(wr = js_mallocz(ctx, sizeof(XMLWriter))))
    return JS_EXCEPTION;

  if(argc > 0 && writer_from_js(ctx, argv[i], &wr->xw.writer)) {
    i++;
  } else {
    /* No output given (or argv[i] isn't a recognized writer target): fall back to an
     * internal buffer, as documented - leaving `wr->writer` zero-initialized instead
     * would dispatch every write through a null function pointer and crash on the
     * first elementStart()/attribute()/text() call. */
    dbuf_init_ctx(ctx, &wr->buf);
    wr->xw.writer = writer_from_dynbuf(&wr->buf);
  }

  JSValue options = i < argc ? argv[i] : argc > 0 ? argv[0] : JS_UNDEFINED;

  if(JS_IsNumber(options))
    xmlwriter_set_indent(wr, js_toint32(ctx, options));
  else if(JS_IsObject(options) && js_has_propertystr(ctx, options, "indent"))
    xmlwriter_set_indent(wr, js_toint32_free(ctx, JS_GetPropertyStr(ctx, options, "indent")));
  else
    xmlwriter_set_indent(wr, 2);

  xwrite_init(&wr->xw, NULL, NULL);

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_xmlwriter_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, wr);
  return obj;

fail:
  xwrite_free(&wr->xw);
  js_free(ctx, wr);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_xmlwriter_finalizer(JSRuntime* rt, JSValue val) {
  XMLWriter* wr;

  if((wr = JS_GetOpaque(val, js_xmlwriter_class_id))) {
    xwrite_free(&wr->xw); /* also frees the writer */
    js_free_rt(rt, wr);
  }
}

static JSClassDef js_xmlwriter_class = {
    .class_name = "XMLWriter",
    .finalizer = js_xmlwriter_finalizer,
};

static const JSCFunctionListEntry js_xmlwriter_funcs[] = {
    JS_CGETSET_MAGIC_DEF("state", js_xmlwriter_get, 0, WRITER_STATE),
    JS_CGETSET_MAGIC_DEF("written", js_xmlwriter_get, 0, WRITER_WRITTEN),
    JS_CGETSET_MAGIC_DEF("indent", js_xmlwriter_get, js_xmlwriter_set, WRITER_INDENT),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLWriter", JS_PROP_CONFIGURABLE),
};

static int
js_xml_init(JSContext* ctx, JSModuleDef* m) {
  if(js_location_class_id == 0)
    js_location_init(ctx, 0);

  JS_SetModuleExportList(ctx, m, js_xml_funcs, countof(js_xml_funcs));

  JSValue defaultObj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, defaultObj, "read", JS_NewCFunction(ctx, js_xml_read, "read", 1));
  JS_SetPropertyStr(ctx, defaultObj, "write", JS_NewCFunction(ctx, js_xml_write, "write", 2));
  JS_SetModuleExport(ctx, m, "default", defaultObj);

  JS_NewClassID(&js_xmlserializer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xmlserializer_class_id, &js_xmlserializer_class);

  xmlserializer_ctor = JS_NewCFunction2(ctx, js_xmlserializer_constructor, "XMLTreeSerializer", 1, JS_CFUNC_constructor, 0);
  xmlserializer_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, xmlserializer_proto, js_xmlserializer_funcs, countof(js_xmlserializer_funcs));
  JS_SetClassProto(ctx, js_xmlserializer_class_id, xmlserializer_proto);
  JS_SetConstructor(ctx, xmlserializer_ctor, xmlserializer_proto);

  JS_SetModuleExport(ctx, m, "XMLTreeSerializer", xmlserializer_ctor);

  JS_NewClassID(&js_xml_pushparser_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xml_pushparser_class_id, &js_xml_pushparser_class);

  xml_pushparser_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, xml_pushparser_proto, js_xml_pushparser_proto_funcs, countof(js_xml_pushparser_proto_funcs));

  xml_pushparser_ctor = JS_NewCFunction2(ctx, js_xml_pushparser_constructor, "XMLPushParser", 1, JS_CFUNC_constructor, 0);
  JS_SetClassProto(ctx, js_xml_pushparser_class_id, xml_pushparser_proto);
  JS_SetConstructor(ctx, xml_pushparser_ctor, xml_pushparser_proto);

  JS_SetModuleExport(ctx, m, "XMLPushParser", xml_pushparser_ctor);

  JS_NewClassID(&js_xml_parser_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xml_parser_class_id, &js_xml_parser_class);

  xml_parser_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, xml_parser_proto, js_xml_parser_proto_funcs, countof(js_xml_parser_proto_funcs));

  xml_parser_ctor = JS_NewCFunction2(ctx, js_xml_parser_constructor, "XMLParser", 1, JS_CFUNC_constructor, 0);
  JS_SetClassProto(ctx, js_xml_parser_class_id, xml_parser_proto);
  JS_SetConstructor(ctx, xml_parser_ctor, xml_parser_proto);

  /* .parse()'s return value: named constants for the raw xml_event_t token ids
   * (include/xml/xml.h), so callers don't have to hardcode -2..4. */
  JS_DefinePropertyValueStr(ctx, xml_parser_ctor, "PARSE_AGAIN", JS_NewInt32(ctx, XML_PARSE_AGAIN), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_parser_ctor, "PARSE_ERROR", JS_NewInt32(ctx, XML_PARSE_ERROR), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_parser_ctor, "PARSE_OK", JS_NewInt32(ctx, XML_PARSE_OK), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_parser_ctor, "ELEMENT_START", JS_NewInt32(ctx, XML_ELEMENT_START), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_parser_ctor, "ATTRIBUTE", JS_NewInt32(ctx, XML_ATTRIBUTE), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_parser_ctor, "ELEMENT_END", JS_NewInt32(ctx, XML_ELEMENT_END), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_parser_ctor, "TEXT", JS_NewInt32(ctx, XML_TEXT), JS_PROP_ENUMERABLE);

  JS_SetModuleExport(ctx, m, "XMLParser", xml_parser_ctor);

  JS_NewClassID(&js_xml_nodeparser_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xml_nodeparser_class_id, &js_xml_nodeparser_class);

  xml_nodeparser_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, xml_nodeparser_proto, js_xml_nodeparser_proto_funcs, countof(js_xml_nodeparser_proto_funcs));

  xml_nodeparser_ctor = JS_NewCFunction2(ctx, js_xml_nodeparser_constructor, "XMLNodeParser", 1, JS_CFUNC_constructor, 0);
  JS_SetClassProto(ctx, js_xml_nodeparser_class_id, xml_nodeparser_proto);
  JS_SetConstructor(ctx, xml_nodeparser_ctor, xml_nodeparser_proto);

  JS_SetModuleExport(ctx, m, "XMLNodeParser", xml_nodeparser_ctor);

  JS_NewClassID(&js_xmlwriter_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xmlwriter_class_id, &js_xmlwriter_class);

  xmlwriter_ctor = JS_NewCFunction2(ctx, js_xmlwriter_constructor, "XMLWriter", 1, JS_CFUNC_constructor, 0);
  xmlwriter_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, xmlwriter_proto, js_xmlwriter_funcs, countof(js_xmlwriter_funcs));
  js_xmlsink_register(js_xmlwriter_class_id, js_xmlwriter_sink, xmlwriter_throw, js_xmlwriter_note);
  js_xmlsink_define(ctx, xmlwriter_proto);
  JS_SetClassProto(ctx, js_xmlwriter_class_id, xmlwriter_proto);
  JS_SetConstructor(ctx, xmlwriter_ctor, xmlwriter_proto);

  JS_SetModuleExport(ctx, m, "XMLWriter", xmlwriter_ctor);

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
    JS_AddModuleExport(ctx, m, "XMLTreeSerializer");
    JS_AddModuleExport(ctx, m, "XMLPushParser");
    JS_AddModuleExport(ctx, m, "XMLParser");
    JS_AddModuleExport(ctx, m, "XMLNodeParser");
    JS_AddModuleExport(ctx, m, "XMLWriter");
  }

  return m;
}
