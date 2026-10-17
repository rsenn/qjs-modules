/* xbuilder.c: XmlBuilder, xbuild_*(), xbuild_sink().
 * depends on: jbuilder.h (frames, attaching), xevent.h (the sink table).
 * rule: an element is {tagName, attributes, children} in that key order; `attributes` is open
 * until the first child or the end, then `children` is. */

#include "xml/xbuilder.h"

/* what the innermost element has open */
enum { XB_NONE, XB_ATTRS, XB_CHILDREN, XB_BANG };

/* names beginning with `!` or `-` (the parser's EXCLAM and HYPHEN classes share a bit). */
static int
xbuild_is_bang(const char* name, size_t len) {
  return len && (name[0] == '!' || name[0] == '-');
}

void
xbuild_init(XmlBuilder* b, JSContext* ctx, unsigned flags) {
  jbuild_init(&b->json, ctx);
  b->json.plain_objects = !!(flags & XB_PLAIN);
  b->names = JS_NewArray(ctx);
  b->last = JS_UNDEFINED;
  b->depth = 0;
  b->state = XB_NONE;
  b->flat = !!(flags & XB_FLAT);
  b->skip_end = 0;
  b->open_flat = 0;
  jbuild_push(&b->json, jr_type_array_start); /* the root array */
}

/* a child or text is coming: closes `attributes`, opens `children`, once per element */
static void
xbuild_open_children(XmlBuilder* b) {
  if(b->state == XB_ATTRS)
    jbuild_pop(&b->json);

  if(b->state == XB_ATTRS || b->state == XB_BANG) {
    jbuild_key(&b->json, "children", 8);
    jbuild_push(&b->json, jr_type_array_start);
    b->state = XB_CHILDREN;
  }
}

/* flat: the previous element is complete once anything but an attribute follows */
static void
xbuild_flat_close(XmlBuilder* b) {
  if(!b->open_flat)
    return;

  if(b->state == XB_ATTRS)
    jbuild_pop(&b->json);

  jbuild_pop(&b->json); /* the element */
  b->open_flat = 0;
  b->state = XB_NONE;
}

void
xbuild_start(XmlBuilder* b, const char* name, size_t len) {
  JSContext* ctx = b->json.ctx;
  int bang = xbuild_is_bang(name, len);

  b->skip_end = bang;

  if(b->flat)
    xbuild_flat_close(b);
  else if(b->depth)
    xbuild_open_children(b);

  jbuild_push(&b->json, jr_type_object_start);
  JS_FreeValue(ctx, b->last);
  b->last = jbuild_top(&b->json);
  jbuild_key(&b->json, "tagName", 7);
  jbuild_value(&b->json, jr_type_string, name, len);

  if(!bang) {
    jbuild_key(&b->json, "attributes", 10);
    jbuild_push(&b->json, jr_type_object_start);
  }

  b->state = bang ? XB_BANG : XB_ATTRS;

  if(b->flat) {
    b->open_flat = 1;
    return;
  }

  JS_SetPropertyUint32(ctx, b->names, b->depth++, JS_NewStringLen(ctx, name, len));
}

void
xbuild_attribute(XmlBuilder* b, const char* name, size_t nlen, const char* value, size_t vlen) {
  if(b->state != XB_ATTRS || (b->flat ? !b->open_flat : !b->depth))
    return;

  jbuild_key(&b->json, name, nlen);

  if(value)
    jbuild_value(&b->json, jr_type_string, value, vlen);
  else
    jbuild_value(&b->json, jr_type_true, NULL, 0);
}

void
xbuild_text(XmlBuilder* b, const char* str, size_t len) {
  if(b->flat)
    xbuild_flat_close(b);
  else if(b->depth)
    xbuild_open_children(b);

  jbuild_value(&b->json, jr_type_string, str, len);
}

void
xbuild_end(XmlBuilder* b, const char* name, size_t len, int self_closing) {
  JSContext* ctx = b->json.ctx;

  if(b->flat) {
    xbuild_flat_close(b);

    if(b->skip_end) { /* the end that comes with a comment or declaration makes no entry */
      b->skip_end = 0;
      JS_FreeValue(ctx, b->last);
      b->last = JS_UNDEFINED;
      return;
    }

    jbuild_push(&b->json, jr_type_object_start);
    JS_FreeValue(ctx, b->last);
    b->last = jbuild_top(&b->json);
    jbuild_key(&b->json, "tagName", 7);

    {
      char* tag = js_malloc(ctx, len + 1);

      if(tag) {
        tag[0] = '/';
        memcpy(tag + 1, name, len);
        jbuild_value(&b->json, jr_type_string, tag, len + 1);
        js_free(ctx, tag);
      }
    }

    jbuild_pop(&b->json);
    return;
  }

  if(!b->depth)
    return;

  if(b->state == XB_ATTRS) {
    jbuild_pop(&b->json);

    if(!self_closing) { /* an element left open still gets its (empty) children */
      jbuild_key(&b->json, "children", 8);
      jbuild_push(&b->json, jr_type_array_start);
      jbuild_pop(&b->json);
    }
  } else if(b->state == XB_CHILDREN) {
    jbuild_pop(&b->json);
  }

  jbuild_pop(&b->json); /* the element */
  b->depth--;
  b->state = b->depth ? XB_CHILDREN : XB_NONE;
  JS_SetPropertyStr(ctx, b->names, "length", JS_NewUint32(ctx, b->depth));
}

void
xbuild_finish(XmlBuilder* b) {
  if(b->flat) {
    xbuild_flat_close(b);
    return;
  }

  while(b->depth)
    xbuild_end(b, NULL, 0, 0);
}

JSValue
xbuild_last(XmlBuilder* b) {
  return JS_DupValue(b->json.ctx, b->last);
}

uint32_t
xbuild_depth(const XmlBuilder* b) {
  return b->depth;
}

JSValue
xbuild_root(XmlBuilder* b) {
  return jbuild_root(&b->json);
}

JSValue
xbuild_path(XmlBuilder* b) {
  JSContext* ctx = b->json.ctx;
  JSValue slice = JS_GetPropertyStr(ctx, b->names, "slice");
  JSValue ret = JS_Call(ctx, slice, b->names, 0, NULL);

  JS_FreeValue(ctx, slice);
  return ret;
}

void
xbuild_free(XmlBuilder* b, JSRuntime* rt) {
  jbuild_free(&b->json, rt);
  JS_FreeValueRT(rt, b->names);
  JS_FreeValueRT(rt, b->last);
  b->names = b->last = JS_UNDEFINED;
  b->depth = 0;
}

/* the XmlEventSink members: each casts `opaque` back to the builder */
static ssize_t
xbuild_if_ignore(void* b, const char* str, size_t len) {
  return 0;
}
static ssize_t
xbuild_if_element_start(void* b, const char* name, size_t len) {
  xbuild_start(b, name, len);
  return 0;
}
static ssize_t
xbuild_if_attribute(void* b, const char* name, size_t nlen, const char* value, size_t vlen) {
  xbuild_attribute(b, name, nlen, value, vlen);
  return 0;
}
static ssize_t
xbuild_if_element_end(void* b, const char* name, size_t len) {
  xbuild_end(b, name, len, 0);
  return 0;
}
static ssize_t
xbuild_if_text(void* b, const char* str, size_t len) {
  xbuild_text(b, str, len);
  return 0;
}

XmlEventSink
xbuild_sink(XmlBuilder* b) {
  return (XmlEventSink){
      b,
      xbuild_if_ignore,
      xbuild_if_element_start,
      xbuild_if_attribute,
      xbuild_if_element_end,
      xbuild_if_text,
      xbuild_if_ignore,
      xbuild_if_ignore,
      xbuild_if_ignore,
      xbuild_if_ignore,
  };
}
