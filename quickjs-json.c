#include "stream-utils.h"
#include "utils.h"
#include "virtual-properties.h"
#include "json.h"
#include "vector.h"
#include "property-enumeration.h"
#include "char-utils.h"
#include "quickjs-location.h"
#include "vector.h"
#include <math.h>
#define SJ_IMPL
#include "sj.h"
#include "jread.h"

#define REC_DEPTH(v) vector_size((v), sizeof(PropertyEnumeration))
#define REC_TOP(v) vector_back((v), sizeof(PropertyEnumeration))
#define REC_EMPLACE(v) vector_emplace((v), sizeof(PropertyEnumeration))
#define REC_POP(v) vector_pop((v), sizeof(PropertyEnumeration))

typedef struct {
  JSValue obj;
  sj_Value sj;
  uint32_t index;
  unsigned is_object : 1;
} ParseFrame;

VISIBLE JSClassID js_jsonparser_class_id = 0, js_jsonpushparser_class_id = 0, js_jsonserializer_class_id = 0;
static JSValue json_parser_proto, json_parser_ctor, json_pushparser_proto, json_pushparser_ctor, json_serializer_proto, json_serializer_ctor;

struct js_jsonparser_opaque {
  JSContext* ctx;
  void *parser, *obj;
};

static JSValue
js_json_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

static JSValue
parse_primitive(JSContext* ctx, sj_Value val) {
  switch(val.type) {
    case SJ_NUMBER: {
      double num;
      scan_double(val.start, &num);
      return JS_NewFloat64(ctx, num);
    }

    case SJ_STRING: return JS_NewStringLen(ctx, val.start, val.end - val.start);
    case SJ_NULL: return JS_NULL;
    case SJ_BOOL: return val.start[0] == 't' ? JS_TRUE : JS_FALSE;
  }

  return JS_UNDEFINED;
}

static JSValue
parse_make_container(JSContext* ctx, int type) {
  return type == SJ_OBJECT ? JS_NewObjectProto(ctx, JS_NULL) : JS_NewArray(ctx);
}

static JSValue
parse_throw(JSContext* ctx, sj_Reader* r) {
  int line, col;

  sj_location(r, &line, &col);

  return JS_ThrowInternalError(ctx, "error: %d:%d: %s\n", line, col, r->error ? r->error : "parse error");
}

static void
parse_stack_free(JSContext* ctx, Vector* stack) {
  ParseFrame* it;

  vector_foreach_t(stack, it) {
    JS_FreeValue(ctx, it->obj);
  }

  vector_free(stack);
}

static JSValue
parse_val(JSContext* ctx, sj_Reader* r, sj_Value root) {
  Vector stack;
  JSValue ret = JS_UNDEFINED;

  if(root.type == SJ_ERROR)
    return parse_throw(ctx, r);

  if(root.type != SJ_ARRAY && root.type != SJ_OBJECT)
    return parse_primitive(ctx, root);

  vector_init(&stack, ctx);

  JSValue container = parse_make_container(ctx, root.type);

  if(!vector_push(&stack, ((ParseFrame){container, root, 0, root.type == SJ_OBJECT}))) {
    JS_FreeValue(ctx, container);
    return JS_EXCEPTION;
  }

  while(!vector_empty(&stack)) {
    ParseFrame* top = vector_back(&stack, sizeof(ParseFrame));
    sj_Value k, v;
    BOOL more;

    if(top->is_object)
      more = sj_iter_object(r, top->sj, &k, &v);
    else
      more = sj_iter_array(r, top->sj, &v);

    if(!more) {
      if(r->error) {
        parse_stack_free(ctx, &stack);
        return parse_throw(ctx, r);
      }

      JSValue done = top->obj;
      vector_pop(&stack, sizeof(ParseFrame));

      if(vector_empty(&stack)) {
        ret = done;
        break;
      }

      JS_FreeValue(ctx, done);
      continue;
    }

    if(v.type == SJ_ERROR) {
      parse_stack_free(ctx, &stack);
      return parse_throw(ctx, r);
    }

    if(v.type == SJ_ARRAY || v.type == SJ_OBJECT) {
      JSValue child = parse_make_container(ctx, v.type);

      if(top->is_object) {
        JSAtom atom = JS_NewAtomLen(ctx, k.start, k.end - k.start);
        JS_SetProperty(ctx, top->obj, atom, JS_DupValue(ctx, child));
        JS_FreeAtom(ctx, atom);
      } else {
        JS_SetPropertyUint32(ctx, top->obj, top->index++, JS_DupValue(ctx, child));
      }

      if(!vector_push(&stack, ((ParseFrame){child, v, 0, v.type == SJ_OBJECT}))) {
        JS_FreeValue(ctx, child);
        parse_stack_free(ctx, &stack);
        return JS_EXCEPTION;
      }

    } else {
      JSValue prim = parse_primitive(ctx, v);

      if(top->is_object) {
        JSAtom atom = JS_NewAtomLen(ctx, k.start, k.end - k.start);
        JS_SetProperty(ctx, top->obj, atom, prim);
        JS_FreeAtom(ctx, atom);
      } else {
        JS_SetPropertyUint32(ctx, top->obj, top->index++, prim);
      }
    }
  }

  vector_free(&stack);
  return ret;
}

static JSValue
js_json_parse(JSContext* ctx, const uint8_t* buf, size_t len, const char* input_name) {
  sj_Reader r = sj_reader((char*)buf, len);
  JSValue ret = parse_val(ctx, &r, sj_read(&r));

  if(!JS_IsException(ret)) {
    while(r.cur < r.end && (*r.cur == ' ' || *r.cur == '\n' || *r.cur == '\r' || *r.cur == '\t'))
      r.cur++;

    if(r.cur != r.end) {
      JS_FreeValue(ctx, ret);
      r.error = "unexpected trailing data";
      return parse_throw(ctx, &r);
    }
  }

  return ret;
}

static JSValue
js_json_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  InputBuffer input = js_input_chars(ctx, argv[0]);
  const char* input_name = 0;

  if(input.data == 0 || input.size == 0) {
    JS_ThrowReferenceError(ctx, "json.read(): expecting buffer or string");
    return JS_EXCEPTION;
  }

  if(argc >= 2)
    input_name = JS_ToCString(ctx, argv[1]);

  ret = js_json_parse(ctx, input.data, input.size, input_name ? input_name : "<json>");

  if(input_name)
    JS_FreeCString(ctx, input_name);

  inputbuffer_free(&input, ctx);
  return ret;
}

/* Every write_*() helper below returns 1 on full success, 0 if the writer ran out of
 * room (a chunked destination signalling "retry later"), or -1 on a real error. Callers
 * must check the return value before mutating any traversal state, so a blocked write can
 * be retried later without having advanced past what was actually delivered.
 *
 * All multi-byte pieces (escapes, numbers, indentation, literals) are written one byte at
 * a time via write_all() rather than as a single bulk write: against a bounded destination
 * (see JsonSerializer's zero-copy .read(buffer)) a bulk write is all-or-nothing, so any
 * atomic unit wider than the caller's buffer could never be delivered at all. Byte-granular
 * writes guarantee forward progress as long as the destination has room for at least 1 byte. */
static ssize_t
write_all(Writer* wr, const void* buf, size_t len) {
  const uint8_t* p = buf;

  if(len == 0)
    return 1;

  for(size_t i = 0; i < len; i++) {
    ssize_t w = writer_putc(wr, p[i]);

    if(w < 0)
      return -1;
    if(w == 0)
      return 0;
  }

  return (ssize_t)len;
}

static int
write_json_string(Writer* wr, const char* s, size_t len) {
  ssize_t w;

  if((w = writer_putc(wr, '"')) <= 0)
    return (int)w;

  for(size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];

    switch(c) {
      case '"': w = write_all(wr, "\\\"", 2); break;
      case '\\': w = write_all(wr, "\\\\", 2); break;
      case '\b': w = write_all(wr, "\\b", 2); break;
      case '\f': w = write_all(wr, "\\f", 2); break;
      case '\n': w = write_all(wr, "\\n", 2); break;
      case '\r': w = write_all(wr, "\\r", 2); break;
      case '\t': w = write_all(wr, "\\t", 2); break;
      default:
        if(c < 0x20) {
          char buf[32];
          int n = snprintf(buf, sizeof(buf), "\\u%04x", c);
          w = write_all(wr, buf, n);
        } else {
          w = writer_putc(wr, c);
        }
        break;
    }

    if(w <= 0)
      return (int)w;
  }

  if((w = writer_putc(wr, '"')) <= 0)
    return (int)w;

  return 1;
}

static void
clear_pending_exception(JSContext* ctx) {
  JSValue exc = JS_GetException(ctx);

  if(!JS_IsNull(exc) && !JS_IsUndefined(exc))
    JS_FreeValue(ctx, exc);
}

static int
write_json_primitive(JSContext* ctx, Writer* wr, JSValueConst val) {
  if(JS_IsNull(val))
    return (int)write_all(wr, "null", 4);

  if(JS_IsUndefined(val) || JS_IsSymbol(val) || JS_IsFunction(ctx, val))
    return (int)write_all(wr, "null", 4);

  if(JS_IsBool(val)) {
    BOOL b = JS_ToBool(ctx, val);
    return (int)(b ? write_all(wr, "true", 4) : write_all(wr, "false", 5));
  }

  if(JS_IsString(val)) {
    size_t len;
    const char* s = JS_ToCStringLen(ctx, &len, val);
    int r;

    if(s) {
      r = write_json_string(wr, s, len);
      JS_FreeCString(ctx, s);
    } else {
      r = (int)write_all(wr, "null", 4);
      clear_pending_exception(ctx);
    }

    return r;
  }

  if(JS_IsNumber(val)) {
    double d;

    JS_ToFloat64(ctx, &d, val);

    if(isnan(d) || isinf(d))
      return (int)write_all(wr, "null", 4);
  }

  if(js_is_numeric(ctx, val)) {
    size_t len;
    const char* s = JS_ToCStringLen(ctx, &len, val);
    int r;

    if(s) {
      r = (int)write_all(wr, (const uint8_t*)s, len);
      JS_FreeCString(ctx, s);
    } else {
      r = (int)write_all(wr, "null", 4);
      clear_pending_exception(ctx);
    }

    return r;
  }

  /* Fallback: object-typed value that reached here (e.g. a circular
   * container that we refuse to recurse into). JS_ToCString invokes
   * the value's toString, which for arrays calls Array.prototype.join.
   * That can throw — typically InternalError "stack overflow" when the
   * referenced structure is deep, or TypeError when an element is a
   * Symbol. We write "null" and discard the pending exception so it
   * doesn't leak past the writer. */
  {
    size_t len;
    const char* s = JS_ToCStringLen(ctx, &len, val);
    int r;

    if(s) {
      r = write_json_string(wr, s, len);
      JS_FreeCString(ctx, s);
    } else {
      r = (int)write_all(wr, "null", 4);
      clear_pending_exception(ctx);
    }

    return r;
  }
}

static int
write_push(Vector* stack, JSContext* ctx, JSValue obj, int flags) {
  PropertyEnumeration* it;
  JSPropertyEnum* tmp;
  uint32_t len = 0;

  if(JS_GetOwnPropertyNames(ctx, &tmp, &len, obj, flags)) {
    JS_FreeValue(ctx, obj);
    return -1;
  }

  if(!(it = REC_EMPLACE(stack))) {
    js_propertyenums_free(ctx, tmp, len);
    JS_FreeValue(ctx, obj);
    return -1;
  }

  *it = (PropertyEnumeration)PROPENUM_INIT();
  it->obj = obj;
  it->tab_atom_len = len;

  if(len > 0) {
    if(!(it->tab_atom = js_malloc(ctx, sizeof(JSAtom) * len))) {
      js_propertyenums_free(ctx, tmp, len);
      JS_FreeValue(ctx, obj);
      REC_POP(stack);
      return -1;
    }

    for(uint32_t i = 0; i < len; i++)
      it->tab_atom[i] = JS_DupAtom(ctx, tmp[i].atom);
  }

  js_propertyenums_free(ctx, tmp, len);
  return 0;
}

static int
write_indent(Writer* wr, int indent, int n, DynBuf* ws) {
  if(indent) {
    int count = indent * n;
    size_t oldsize = ws->size;
    ssize_t w;

    if((w = writer_putc(wr, '\n')) <= 0)
      return (int)w;

    if(oldsize < (size_t)count) {
      if(dbuf_claim(ws, count - ws->size))
        return -1;

      if((size_t)count > oldsize)
        memset(&ws->buf[oldsize], ' ', ws->allocated_size - oldsize);

      ws->size = count;
    }

    w = writer_write(wr, ws->buf, count);
    return (int)w;
  }

  return 1;
}

static JSValue
js_json_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  DynBuf out, space;
  Vector stack;
  const int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
  int32_t indent = 0;

  if(argc > 1)
    JS_ToInt32(ctx, &indent, argv[1]);

  dbuf_init_ctx(ctx, &out);
  dbuf_init_ctx(ctx, &space);
  Writer wr = writer_from_dynbuf(&out);

  if(!JS_IsObject(argv[0]) || JS_IsFunction(ctx, argv[0])) {
    write_json_primitive(ctx, &wr, argv[0]);
    JSValue ret = dbuf_tostring_free(&out, ctx);
    writer_free(&wr);
    dbuf_free(&space);
    return ret;
  }

  vector_init(&stack, ctx);

  if(write_push(&stack, ctx, JS_DupValue(ctx, argv[0]), flags)) {
    writer_free(&wr);
    dbuf_free(&space);
    vector_free(&stack);
    return JS_EXCEPTION;
  }

  writer_putc(&wr, JS_IsArray(ctx, argv[0]) ? '[' : '{');
  write_indent(&wr, indent, REC_DEPTH(&stack), &space);

  while(!vector_empty(&stack)) {
    PropertyEnumeration* top = REC_TOP(&stack);
    BOOL is_array = JS_IsArray(ctx, top->obj);

    if(top->idx >= top->tab_atom_len) {
      write_indent(&wr, indent, REC_DEPTH(&stack) - 1, &space);
      writer_putc(&wr, is_array ? ']' : '}');
      property_enumeration_reset(top, JS_GetRuntime(ctx));
      REC_POP(&stack);
      continue;
    }

    if(top->idx > 0) {
      writer_putc(&wr, ',');
      if(indent)
        write_indent(&wr, indent, REC_DEPTH(&stack), &space);
    }

    if(!is_array) {
      size_t klen;
      const char* kstr = js_atom_to_cstringlen(ctx, &klen, top->tab_atom[top->idx]);

      if(kstr) {
        write_json_string(&wr, kstr, klen);
        JS_FreeCString(ctx, kstr);
      } else {
        writer_puts(&wr, "\"\"");
      }

      writer_putc(&wr, ':');
      if(indent)
        writer_putc(&wr, ' ');
    }

    JSValue val = property_enumeration_value(top, ctx);
    BOOL is_container = JS_IsObject(val) && !JS_IsFunction(ctx, val);

    if(is_container && !property_recursion_circular(&stack, val)) {
      writer_putc(&wr, JS_IsArray(ctx, val) ? '[' : '{');

      top->idx++;

      if(write_push(&stack, ctx, val, flags)) {
        property_recursion_free(&stack, JS_GetRuntime(ctx));
        writer_free(&wr);
        dbuf_free(&space);
        vector_free(&stack);
        return JS_EXCEPTION;
      }

      write_indent(&wr, indent, REC_DEPTH(&stack), &space);

    } else {
      write_json_primitive(ctx, &wr, val);
      JS_FreeValue(ctx, val);
      top->idx++;
    }
  }

  JSValue ret = dbuf_tostring_free(&out, ctx);
  writer_free(&wr);
  dbuf_free(&space);
  vector_free(&stack);
  return ret;
}

/* JSONL: Bun's `JSONL` namespace, values separated by newlines.
 *
 * ```js
 * JSONL.parse('1\n{"a":2}\n[3');     // [1, {a: 2}]
 * JSONL.parseChunk('1\n2\n[3');      // {values: [1, 2], read: 3, done: false, error: null}
 * ```
 *
 *   string|Uint8Array  input   text, or its UTF-8 bytes
 *   number             start   first unit to read (chars for a string, bytes otherwise)
 *   number             end     unit to stop at
 *
 *   returns parse():      the values read; a trailing incomplete value is ignored
 *           parseChunk(): `read` is the unit after the last value, `done` is true when
 *                         the rest is only whitespace, `error` the SyntaxError of a bad value
 *
 * throws parse(): SyntaxError when the first value is bad; TypeError for a bad input.
 * wired in: JS_CFUNC_MAGIC_DEF("parse"/"parseChunk") on the `JSONL` export.
 */
static BOOL
jsonl_is_ws(uint8_t c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* end of the value starting at b[i], or 0 when the input ends inside it */
static size_t
jsonl_value_end(const uint8_t* b, size_t n, size_t i) {
  int depth = 0;

  if(b[i] == '{' || b[i] == '[' || b[i] == '"') {
    for(BOOL in_str = FALSE; i < n; i++) {
      uint8_t c = b[i];

      if(in_str) {
        if(c == '\\')
          i++;
        else if(c == '"') {
          in_str = FALSE;
          if(!depth)
            return i + 1;
        }
      } else if(c == '"') {
        in_str = TRUE;
      } else if(c == '{' || c == '[') {
        depth++;
      } else if((c == '}' || c == ']') && --depth == 0) {
        return i + 1;
      }
    }

    return 0;
  }

  while(i < n && !jsonl_is_ws(b[i]))
    i++;

  return i;
}

/* byte offset of UTF-16 unit `units` in the UTF-8 buffer `b` */
static size_t
jsonl_unit_to_byte(const uint8_t* b, size_t n, size_t units) {
  size_t i = 0;

  while(i < n && units > 0) {
    uint8_t c = b[i];

    i += c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
    units -= c >= 0xf0 && units >= 2 ? 2 : 1;
  }

  return i > n ? n : i;
}

/* UTF-16 unit index of byte offset `end` in the UTF-8 buffer `b` */
static size_t
jsonl_byte_to_unit(const uint8_t* b, size_t end) {
  size_t units = 0;

  for(size_t i = 0; i < end; i++)
    if((b[i] & 0xc0) != 0x80)
      units += b[i] >= 0xf0 ? 2 : 1;

  return units;
}

static JSValue
js_jsonl_parse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  BOOL is_str = argc > 0 && JS_IsString(argv[0]);
  InputBuffer input = js_input_chars(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
  JSValue values, error = JS_NULL, ret = JS_EXCEPTION;
  int64_t start = 0, end = -1;
  size_t i, n, read, nvalues = 0;
  BOOL done = FALSE;

  if(!input.data) {
    JS_ThrowTypeError(ctx, "%s: expecting string or Uint8Array", magic ? "JSONL.parseChunk()" : "JSONL.parse()");
    return JS_EXCEPTION;
  }

  if(magic) {
    if(argc > 1 && !JS_IsUndefined(argv[1]))
      JS_ToInt64(ctx, &start, argv[1]);
    if(argc > 2 && !JS_IsUndefined(argv[2]))
      JS_ToInt64(ctx, &end, argv[2]);
  }

  if(start < 0)
    start = 0;

  i = is_str ? jsonl_unit_to_byte(input.data, input.size, start) : MIN_NUM((size_t)start, input.size);
  n = end < 0 ? input.size : is_str ? jsonl_unit_to_byte(input.data, input.size, end) : MIN_NUM((size_t)end, input.size);
  read = i;
  values = JS_NewArray(ctx);

  for(;;) {
    size_t e, vs;
    char* text;
    JSValue v;

    while(i < n && jsonl_is_ws(input.data[i]))
      i++;

    if(i >= n) {
      done = TRUE;
      break;
    }

    vs = i;

    if(!(e = jsonl_value_end(input.data, n, vs)))
      break;

    if(!(text = js_malloc(ctx, e - vs + 1)))
      goto fail;

    memcpy(text, input.data + vs, e - vs);
    text[e - vs] = 0;
    v = JS_ParseJSON(ctx, text, e - vs, "<jsonl>");
    js_free(ctx, text);

    if(JS_IsException(v)) {
      /* a bare token cut off by the end of input may be completed by more data */
      if(e == n && input.data[vs] != '{' && input.data[vs] != '[' && input.data[vs] != '"') {
        JS_FreeValue(ctx, JS_GetException(ctx));
        break;
      }

      error = JS_GetException(ctx);
      break;
    }

    JS_SetPropertyUint32(ctx, values, nvalues++, v);
    read = i = e;

    while(i < n && (input.data[i] == ' ' || input.data[i] == '\t' || input.data[i] == '\r'))
      i++;

    if(i < n && input.data[i] != '\n') {
      JS_ThrowSyntaxError(ctx, "JSONL: expected newline after value");
      error = JS_GetException(ctx);
      break;
    }
  }

  if(!magic) {
    if(nvalues == 0 && !JS_IsNull(error)) {
      JS_Throw(ctx, error);
      error = JS_NULL;
      goto fail;
    }

    ret = values;
    values = JS_UNDEFINED;
  } else {
    ret = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, ret, "values", values);
    JS_SetPropertyStr(ctx, ret, "read", JS_NewInt64(ctx, is_str ? (int64_t)jsonl_byte_to_unit(input.data, read) : (int64_t)read));
    JS_SetPropertyStr(ctx, ret, "done", JS_NewBool(ctx, done));
    JS_SetPropertyStr(ctx, ret, "error", error);
    values = error = JS_UNDEFINED;
  }

fail:
  JS_FreeValue(ctx, values);
  JS_FreeValue(ctx, error);
  inputbuffer_free(&input, ctx);
  return ret;
}

static const JSCFunctionListEntry js_jsonl_funcs[] = {
    JS_CFUNC_MAGIC_DEF("parse", 1, js_jsonl_parse, 0),
    JS_CFUNC_MAGIC_DEF("parseChunk", 1, js_jsonl_parse, 1),
};

static const JSCFunctionListEntry js_json_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_json_read),
    JS_CFUNC_DEF("write", 2, js_json_write),
};

/* ---------------------------------------------------------------------- */
/* JsonBuilder / JsonPushParser                                           */
/* ---------------------------------------------------------------------- */

typedef struct JsonBuilderFrame {
  struct JsonBuilderFrame* parent;
  JSValue obj;
  uint32_t index, my_index;
  char *current_key, *my_key;
  unsigned is_object : 1;
} JsonBuilderFrame;

typedef struct JsonBuilder {
  JSContext* ctx;
  JsonBuilderFrame* top;
  JSValue root;
  unsigned has_root : 1;
} JsonBuilder;

static void
json_builder_init(JsonBuilder* b, JSContext* ctx) {
  b->ctx = ctx;
  b->top = NULL;
  b->root = JS_UNDEFINED;
  b->has_root = FALSE;
}

static void
json_builder_push(JsonBuilder* b, jr_type_t type) {
  JSContext* ctx = b->ctx;
  BOOL is_object = (type == jr_type_object_start);
  JSValue container = is_object ? JS_NewObjectProto(ctx, JS_NULL) : JS_NewArray(ctx);

  char* child_key = NULL;
  uint32_t child_index = 0;

  if(!b->top) {
    b->root = JS_DupValue(ctx, container);
    b->has_root = TRUE;
  } else {
    JsonBuilderFrame* parent = b->top;

    if(parent->is_object) {
      if(parent->current_key) {
        child_key = parent->current_key;
        parent->current_key = NULL;

        JSAtom atom = JS_NewAtomLen(ctx, child_key, strlen(child_key));
        JS_SetProperty(ctx, parent->obj, atom, JS_DupValue(ctx, container));
        JS_FreeAtom(ctx, atom);
      }
    } else {
      child_index = parent->index++;
      JS_SetPropertyUint32(ctx, parent->obj, child_index, JS_DupValue(ctx, container));
    }
  }

  JsonBuilderFrame* frame = js_mallocz(ctx, sizeof(JsonBuilderFrame));
  frame->parent = b->top;
  frame->obj = container;
  frame->is_object = is_object;
  frame->index = 0;
  frame->current_key = NULL;
  frame->my_key = child_key;
  frame->my_index = child_index;
  b->top = frame;
}

static void
json_builder_pop(JsonBuilder* b) {
  JsonBuilderFrame* frame;

  if(!(frame = b->top))
    return;

  b->top = frame->parent;

  if(frame->current_key)
    js_free(b->ctx, frame->current_key);
  if(frame->my_key)
    js_free(b->ctx, frame->my_key);

  JS_FreeValue(b->ctx, frame->obj);
  js_free(b->ctx, frame);
}

static void
json_builder_key(JsonBuilder* b, const char* name, size_t len) {
  JSContext* ctx = b->ctx;

  if(!b->top)
    return;

  if(b->top->current_key)
    js_free(ctx, b->top->current_key);

  if((b->top->current_key = js_malloc(ctx, len + 1))) {
    memcpy(b->top->current_key, name, len);
    b->top->current_key[len] = '\0';
  }
}

static void
json_builder_value(JsonBuilder* b, jr_type_t type, const char* data, size_t len) {
  JSContext* ctx = b->ctx;
  JSValue val = JS_UNDEFINED;

  switch(type) {
    case jr_type_null: val = JS_NULL; break;
    case jr_type_true: val = JS_TRUE; break;
    case jr_type_false: val = JS_FALSE; break;
    case jr_type_number: {
      double num = 0;

      if(data) {
        char* buf;

        if((buf = js_malloc(ctx, len + 1))) {
          memcpy(buf, data, len);
          buf[len] = '\0';
          scan_double(buf, &num);
          js_free(ctx, buf);
        }
      }

      val = JS_NewFloat64(ctx, num);
      break;
    }

    case jr_type_string: {
      val = data ? JS_NewStringLen(ctx, data, len) : JS_NewString(ctx, "");
      break;
    }

    default: return;
  }

  if(!b->top) {
    b->root = val;
    b->has_root = TRUE;
  } else {
    JsonBuilderFrame* parent = b->top;

    if(parent->is_object) {
      if(parent->current_key) {
        JSAtom atom = JS_NewAtomLen(ctx, parent->current_key, strlen(parent->current_key));
        JS_SetProperty(ctx, parent->obj, atom, val);
        JS_FreeAtom(ctx, atom);
        js_free(ctx, parent->current_key);
        parent->current_key = NULL;
      } else {
        JS_FreeValue(ctx, val);
      }
    } else {
      JS_SetPropertyUint32(ctx, parent->obj, parent->index++, val);
    }
  }
}

static JSValue
json_builder_path(JsonBuilder* b) {
  JSContext* ctx = b->ctx;
  JSValue ret = JS_NewArray(ctx);
  int count = 0;
  JsonBuilderFrame* f;

  for(f = b->top; f; f = f->parent)
    count++;

  BOOL has_current = FALSE;

  if(b->top) {
    if(b->top->is_object && b->top->current_key)
      has_current = TRUE;
    else if(!b->top->is_object)
      has_current = TRUE;
  }

  int total_len = count - 1 + (has_current ? 1 : 0);

  if(total_len < 0)
    total_len = 0;

  int index_to_set = total_len - 1;

  if(has_current && b->top) {
    JSValue val;

    if(b->top->is_object)
      val = JS_NewString(ctx, b->top->current_key);
    else
      val = JS_NewUint32(ctx, b->top->index);

    JS_SetPropertyUint32(ctx, ret, index_to_set--, val);
  }

  for(f = b->top; f && f->parent; f = f->parent) {
    JSValue val;

    if(f->parent->is_object)
      val = f->my_key ? JS_NewString(ctx, f->my_key) : JS_NewString(ctx, "");
    else
      val = JS_NewUint32(ctx, f->my_index);

    JS_SetPropertyUint32(ctx, ret, index_to_set--, val);
  }

  return ret;
}

static JSValue
json_builder_root(JsonBuilder* b) {
  return b->has_root ? JS_DupValue(b->ctx, b->root) : JS_UNDEFINED;
}

static void
json_builder_free(JsonBuilder* b, JSRuntime* rt) {
  JsonBuilderFrame* frame = b->top;

  while(frame) {
    JsonBuilderFrame* parent = frame->parent;

    if(frame->current_key)
      js_free_rt(rt, frame->current_key);
    if(frame->my_key)
      js_free_rt(rt, frame->my_key);

    JS_FreeValueRT(rt, frame->obj);
    js_free_rt(rt, frame);
    frame = parent;
  }

  b->top = NULL;
  JS_FreeValueRT(rt, b->root);
  b->root = JS_UNDEFINED;
  b->has_root = FALSE;
}

typedef struct PushParser {
  JSContext* ctx;
  jr_state_t jrs;
  JsonBuilder builder;
  JSValue callback_fn, callbacks_obj, callbacks[jr_type_key + 1 - jr_type_error];
  unsigned use_builder : 1;
  Location* loc;
} JsonPushParser;

static JSValue
json_pushparser_tojs(JSContext* ctx, jr_type_t type, const jr_str_t* data) {
  switch(type) {
    case jr_type_null: return JS_NULL;
    case jr_type_true: return JS_TRUE;
    case jr_type_false: return JS_FALSE;
    case jr_type_number: {
      double num = 0;

      if(data && data->cstr) {
        char* buf = js_malloc(ctx, data->len + 1);

        if(buf) {
          memcpy(buf, data->cstr, data->len);
          buf[data->len] = '\0';
          scan_double(buf, &num);
          js_free(ctx, buf);
        }
      }

      return JS_NewFloat64(ctx, num);
    }

    case jr_type_string:
    case jr_type_key:
    case jr_type_error: return (data && data->cstr) ? JS_NewStringLen(ctx, data->cstr, data->len) : JS_NewString(ctx, "");

    case jr_type_array_start:
    case jr_type_array_end:
    case jr_type_object_start:
    case jr_type_object_end: break;
  }

  return JS_UNDEFINED;
}

static void
jread_callback_build(jr_type_t type, const jr_str_t* data, void* user_data) {
  JsonPushParser* pp = user_data;

  switch(type) {
    case jr_type_object_start:
    case jr_type_array_start: json_builder_push(&pp->builder, type); break;

    case jr_type_object_end:
    case jr_type_array_end: json_builder_pop(&pp->builder); break;

    case jr_type_key:
      if(data)
        json_builder_key(&pp->builder, data->cstr, data->len);
      break;

    case jr_type_null:
    case jr_type_true:
    case jr_type_false:
    case jr_type_number:
    case jr_type_string: json_builder_value(&pp->builder, type, data ? data->cstr : NULL, data ? data->len : 0); break;

    case jr_type_error: break;
  }
}

static void
jread_callback(jr_type_t type, const jr_str_t* data, void* user_data) {
  JsonPushParser* pp = user_data;
  JSContext* ctx = pp->ctx;

  jread_callback_build(type, data, user_data);

  if(!JS_IsUndefined(pp->callback_fn)) {
    JSValue args[2] = {
        JS_NewInt32(ctx, type),
        json_pushparser_tojs(ctx, type, data),
    };
    JSValue ret = JS_Call(ctx, pp->callback_fn, JS_UNDEFINED, countof(args), args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);

    if(!JS_IsException(ret))
      JS_FreeValue(ctx, ret);
    else
      clear_pending_exception(ctx);
  }

  if(type >= jr_type_error && type <= jr_type_key) {
    JSValue cb = pp->callbacks[type - jr_type_error];

    if(!JS_IsUndefined(cb) && JS_IsFunction(ctx, cb)) {
      JSValue val = json_pushparser_tojs(ctx, type, data);
      JSValue ret = JS_Call(ctx, cb, pp->callbacks_obj, 1, &val);
      JS_FreeValue(ctx, val);

      if(!JS_IsException(ret))
        JS_FreeValue(ctx, ret);
      else
        clear_pending_exception(ctx);
    }
  }
}

static JSValue
js_jsonpushparser_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonPushParser* pp;
  InputBuffer input;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_jsonpushparser_class_id)))
    return JS_EXCEPTION;

  input = js_input_chars(ctx, argv[0]);

  if(input.data == 0) {
    JS_ThrowReferenceError(ctx, "JsonPushParser.write(): expecting buffer or string");
    return JS_EXCEPTION;
  }

  jr_read(pp->use_builder ? &jread_callback_build : &jread_callback, inputbuffer_data(&input), inputbuffer_length(&input), pp, &pp->jrs);

  if(pp->loc)
    location_count(pp->loc, inputbuffer_data(&input), inputbuffer_length(&input));

  inputbuffer_free(&input, ctx);

  /* just_erred (not the sticky error) - a resynced parser must not keep throwing on every
   * future write() call just because some earlier call hit a (already-reported) error. */
  return pp->jrs.just_erred ? JS_ThrowSyntaxError(ctx, "parse error") : JS_UNDEFINED;
}

static JSValue
js_jsonpushparser_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_jsonpushparser_class_id)))
    return JS_EXCEPTION;

  jr_finish(pp->use_builder ? &jread_callback_build : &jread_callback, pp, &pp->jrs);

  if(!pp->jrs.done)
    return JS_ThrowSyntaxError(ctx, "unexpected end of input");

  if(pp->jrs.error)
    return JS_ThrowSyntaxError(ctx, "parse error");

  return JS_UNDEFINED;
}

enum {
  JSON_PUSHPARSER_ROOT,
  JSON_PUSHPARSER_PATH,
  JSON_PUSHPARSER_LOCATION,
};

static JSValue
js_jsonpushparser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonPushParser* pp;
  JSValue ret = JS_UNDEFINED;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_jsonpushparser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_PUSHPARSER_ROOT: ret = json_builder_root(&pp->builder); break;
    case JSON_PUSHPARSER_PATH: ret = json_builder_path(&pp->builder); break;
    case JSON_PUSHPARSER_LOCATION: ret = pp->loc ? js_location_wrap(ctx, pp->loc) : JS_UNDEFINED; break;
  }

  return ret;
}

static JSValue
js_jsonpushparser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  JsonPushParser* pp;

  if(!(pp = js_mallocz(ctx, sizeof(JsonPushParser))))
    return JS_EXCEPTION;

  pp->ctx = ctx;
  jr_state_init(&pp->jrs);
  json_builder_init(&pp->builder, ctx);

  if(!(pp->loc = location_new(ctx))) {
    jr_state_free(&pp->jrs);
    json_builder_free(&pp->builder, JS_GetRuntime(ctx));
    js_free(ctx, pp);
    return JS_EXCEPTION;
  }

  location_zero(pp->loc);

  pp->callback_fn = JS_UNDEFINED;
  pp->callbacks_obj = JS_UNDEFINED;

  for(int i = 0; i < jr_type_key + 1 - jr_type_error; i++)
    pp->callbacks[i] = JS_UNDEFINED;

  if(argc > 0) {
    if(JS_IsFunction(ctx, argv[0])) {
      pp->callback_fn = JS_DupValue(ctx, argv[0]);
    } else if(JS_IsObject(argv[0])) {
      pp->callbacks_obj = JS_DupValue(ctx, argv[0]);

      for(int t = jr_type_error; t <= jr_type_key; t++) {
        const char* prop_name = NULL;

        switch((jr_type_t)t) {
          case jr_type_error: prop_name = "error"; break;
          case jr_type_null:
          case jr_type_true:
          case jr_type_false:
          case jr_type_number:
          case jr_type_string: prop_name = "value"; break;
          case jr_type_object_start: prop_name = "objectStart"; break;
          case jr_type_object_end: prop_name = "objectEnd"; break;
          case jr_type_array_start: prop_name = "arrayStart"; break;
          case jr_type_array_end: prop_name = "arrayEnd"; break;
          case jr_type_key: prop_name = "key"; break;
        }

        if(prop_name) {
          pp->callbacks[t - jr_type_error] = JS_GetPropertyStr(ctx, argv[0], prop_name);
        }
      }
    }
  }

  BOOL all_callbacks_present = TRUE;

  for(int t = jr_type_null; t <= jr_type_key; t++) {
    if(!JS_IsFunction(ctx, pp->callbacks[t - jr_type_error])) {
      all_callbacks_present = FALSE;
      break;
    }
  }

  if(!JS_IsUndefined(pp->callback_fn) || all_callbacks_present)
    pp->use_builder = FALSE;
  else
    pp->use_builder = TRUE;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, json_pushparser_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_jsonpushparser_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    jr_state_free(&pp->jrs);
    json_builder_free(&pp->builder, JS_GetRuntime(ctx));
    JS_FreeValue(ctx, pp->callback_fn);
    JS_FreeValue(ctx, pp->callbacks_obj);

    for(int i = 0; i < jr_type_key + 1 - jr_type_error; i++)
      JS_FreeValue(ctx, pp->callbacks[i]);

    js_free(ctx, pp);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, pp);
  return obj;
}

static void
js_jsonpushparser_finalizer(JSRuntime* rt, JSValue val) {
  JsonPushParser* pp;

  if((pp = JS_GetOpaque(val, js_jsonpushparser_class_id))) {
    jr_state_free(&pp->jrs);
    json_builder_free(&pp->builder, rt);

    if(pp->loc)
      location_free(pp->loc, rt);

    JS_FreeValueRT(rt, pp->callback_fn);
    JS_FreeValueRT(rt, pp->callbacks_obj);

    for(int i = 0; i < jr_type_key + 1 - jr_type_error; i++)
      JS_FreeValueRT(rt, pp->callbacks[i]);

    js_free_rt(rt, pp);
  }
}

static const JSCFunctionListEntry js_jsonpushparser_proto_funcs[] = {
    JS_CFUNC_DEF("write", 1, js_jsonpushparser_write),
    JS_CFUNC_DEF("close", 0, js_jsonpushparser_close),
    JS_CGETSET_MAGIC_DEF("root", js_jsonpushparser_get, 0, JSON_PUSHPARSER_ROOT),
    JS_CGETSET_MAGIC_DEF("path", js_jsonpushparser_get, 0, JSON_PUSHPARSER_PATH),
    JS_CGETSET_MAGIC_DEF("location", js_jsonpushparser_get, 0, JSON_PUSHPARSER_LOCATION),
    JS_PROP_INT32_DEF("TYPE_ERROR", jr_type_error, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_NULL", jr_type_null, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_TRUE", jr_type_true, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_FALSE", jr_type_false, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_NUMBER", jr_type_number, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_STRING", jr_type_string, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_OBJECT", jr_type_object_start, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_OBJECT_START", jr_type_object_start, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_OBJECT_END", jr_type_object_end, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ARRAY", jr_type_array_start, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ARRAY_START", jr_type_array_start, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ARRAY_END", jr_type_array_end, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_KEY", jr_type_key, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonPushParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_jsonpushparser_class = {
    .class_name = "JsonPushParser",
    .finalizer = js_jsonpushparser_finalizer,
};

/* ---------------------------------------------------------------------- */
/* JsonSerializer: pull (.read(n)) serializer traversing via              */
/* property_recursion_*(), producing only as much text as requested.      */
/* ---------------------------------------------------------------------- */

typedef struct {
  uint8_t* dst;
  size_t cap, pos;
} CappedBuf;

static ssize_t
write_capped(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  CappedBuf* c = (CappedBuf*)fd;

  if(c->pos + len > c->cap)
    return 0;

  memcpy(c->dst + c->pos, buf, len);
  c->pos += len;
  return (ssize_t)len;
}

typedef struct {
  JSContext* ctx;
  Vector stack;
  DynBuf out, space;
  size_t out_pos, skip, delivered;
  int32_t indent;
  unsigned finished : 1, started : 1, is_primitive : 1, error : 1, blocked : 1;
  JSValue root;
  Location* loc;
  Writer out_writer, dest_writer, skip_writer;
  CappedBuf capped;
} JsonSerializer;

static ssize_t
write_skip(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  JsonSerializer* js = (JsonSerializer*)fd;
  ssize_t w;

  if(js->skip >= len) {
    js->skip -= len;
    return (ssize_t)len;
  }

  if(js->skip > 0) {
    size_t skip = js->skip;
    size_t remain = len - skip;

    if((w = writer_write(&js->dest_writer, (const uint8_t*)buf + skip, remain)) <= 0)
      return w;

    js->skip = 0;
    js->delivered += (size_t)w;
    return (ssize_t)len;
  }

  w = writer_write(&js->dest_writer, buf, len);

  if(w > 0)
    js->delivered += (size_t)w;

  return w;
}

static BOOL
sw_putc(JsonSerializer* js, int c) {
  ssize_t w = writer_putc(&js->skip_writer, c);

  if(w < 0) {
    js->error = TRUE;
    return FALSE;
  }

  if(w == 0) {
    js->blocked = TRUE;
    return FALSE;
  }

  return TRUE;
}

static BOOL
sw_puts(JsonSerializer* js, const char* s) {
  ssize_t w = writer_puts(&js->skip_writer, s);

  if(w < 0) {
    js->error = TRUE;
    return FALSE;
  }

  if(w == 0) {
    js->blocked = TRUE;
    return FALSE;
  }

  return TRUE;
}

static BOOL
sw_indent(JsonSerializer* js, int n) {
  int r = write_indent(&js->skip_writer, js->indent, n, &js->space);

  if(r < 0) {
    js->error = TRUE;
    return FALSE;
  }

  if(r == 0) {
    js->blocked = TRUE;
    return FALSE;
  }

  return TRUE;
}

static BOOL
sw_string(JsonSerializer* js, const char* s, size_t len) {
  int r = write_json_string(&js->skip_writer, s, len);

  if(r < 0) {
    js->error = TRUE;
    return FALSE;
  }

  if(r == 0) {
    js->blocked = TRUE;
    return FALSE;
  }

  return TRUE;
}

static BOOL
sw_primitive(JsonSerializer* js, JSContext* ctx, JSValueConst val) {
  int r = write_json_primitive(ctx, &js->skip_writer, val);

  if(r < 0) {
    js->error = TRUE;
    return FALSE;
  }

  if(r == 0) {
    js->blocked = TRUE;
    return FALSE;
  }

  return TRUE;
}

static void
json_serializer_step_inner(JsonSerializer* js, JSContext* ctx) {
  const int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
  PropertyEnumeration* top;
  BOOL is_array, is_container;
  JSValue val;

  if(!js->started) {
    if(js->is_primitive) {
      if(!sw_primitive(js, ctx, js->root))
        return;

      js->started = TRUE;
      js->finished = TRUE;
      return;
    }

    if(!sw_putc(js, JS_IsArray(ctx, js->root) ? '[' : '{'))
      return;

    if(!sw_indent(js, 1))
      return;

    if(write_push(&js->stack, ctx, JS_DupValue(ctx, js->root), flags)) {
      js->error = TRUE;
      return;
    }

    js->started = TRUE;
    return;
  }

  top = REC_TOP(&js->stack);
  is_array = JS_IsArray(ctx, top->obj);

  if(top->idx >= top->tab_atom_len) {
    if(!sw_indent(js, REC_DEPTH(&js->stack) - 1))
      return;

    if(!sw_putc(js, is_array ? ']' : '}'))
      return;

    property_enumeration_reset(top, JS_GetRuntime(ctx));
    REC_POP(&js->stack);

    if(vector_empty(&js->stack))
      js->finished = TRUE;

    return;
  }

  if(top->idx > 0) {
    if(!sw_putc(js, ','))
      return;

    if(js->indent && !sw_indent(js, REC_DEPTH(&js->stack)))
      return;
  }

  if(!is_array) {
    size_t klen;
    const char* kstr = js_atom_to_cstringlen(ctx, &klen, top->tab_atom[top->idx]);
    BOOL ok = kstr ? sw_string(js, kstr, klen) : sw_puts(js, "\"\"");

    if(kstr)
      JS_FreeCString(ctx, kstr);

    if(!ok)
      return;

    if(!sw_putc(js, ':'))
      return;

    if(js->indent && !sw_putc(js, ' '))
      return;
  }

  val = property_enumeration_value(top, ctx);
  is_container = JS_IsObject(val) && !JS_IsFunction(ctx, val);

  if(is_container && !property_recursion_circular(&js->stack, val)) {
    if(!sw_putc(js, JS_IsArray(ctx, val) ? '[' : '{')) {
      JS_FreeValue(ctx, val);
      return;
    }

    if(!sw_indent(js, REC_DEPTH(&js->stack) + 1)) {
      JS_FreeValue(ctx, val);
      return;
    }

    top->idx++;

    if(write_push(&js->stack, ctx, val, flags)) {
      js->error = TRUE;
      return;
    }
  } else {
    if(!sw_primitive(js, ctx, val)) {
      JS_FreeValue(ctx, val);
      return;
    }

    JS_FreeValue(ctx, val);
    top->idx++;
  }
}

static void
json_serializer_step(JsonSerializer* js, JSContext* ctx) {
  size_t skip_before = js->skip;

  js->delivered = 0;
  json_serializer_step_inner(js, ctx);

  if(js->blocked)
    js->skip = skip_before + js->delivered;
  else if(!js->error)
    js->skip = 0;
}

static JSValue
js_jsonserializer_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonSerializer* js;
  BOOL is_buf;
  int64_t n = -1;
  JSValue ret = JS_UNDEFINED;

  if(!(js = JS_GetOpaque2(ctx, this_val, js_jsonserializer_class_id)))
    return JS_EXCEPTION;

  js->error = FALSE;
  js->blocked = FALSE;
  is_buf = argc > 0 && (js_is_arraybuffer(ctx, argv[0]) || js_is_sharedarraybuffer(ctx, argv[0]) || js_is_typedarray(ctx, argv[0]));

  if(is_buf) {
    InputBuffer buf = js_input_args(ctx, argc, argv);

    js->capped.dst = (uint8_t*)inputbuffer_data(&buf);
    js->capped.cap = inputbuffer_length(&buf);
    js->capped.pos = 0;
    js->dest_writer = (Writer){&write_capped, &js->capped, NULL};

    while(!js->finished && !js->error && !js->blocked && js->capped.pos < js->capped.cap)
      json_serializer_step(js, ctx);

    inputbuffer_free(&buf, ctx);

    if(js->error) {
      property_recursion_free(&js->stack, JS_GetRuntime(ctx));
      return JS_EXCEPTION;
    }

    location_count(js->loc, js->capped.dst, js->capped.pos);

    return JS_NewInt64(ctx, (int64_t)js->capped.pos);
  }

  if(argc > 0 && JS_ToInt64(ctx, &n, argv[0]))
    return JS_EXCEPTION;

  if(n == -1)
    n = INT64_MAX;
  else if(n < 0)
    return JS_ThrowRangeError(ctx, "size must not be negative");

  js->dest_writer = js->out_writer;

  while(!js->finished && !js->error && !js->blocked && (int64_t)(js->out.size - js->out_pos) < n)
    json_serializer_step(js, ctx);

  if(js->error) {
    property_recursion_free(&js->stack, JS_GetRuntime(ctx));
    return JS_EXCEPTION;
  }

  const uint8_t* start = js->out.buf + js->out_pos;
  size_t avail = js->out.size - js->out_pos;
  size_t take = MIN_NUM((size_t)n, utf8_strlen(start, avail));

  if(take == 0)
    return ret;

  size_t offs = utf8_byteoffset(start, avail, take);
  ret = JS_NewStringLen(ctx, (const char*)start, offs);

  location_count(js->loc, start, offs);
  js->out_pos += offs;

  if(js->out_pos > 0) {
    dbuf_advance(&js->out, js->out_pos);
    js->out_pos = 0;
  }

  return ret;
}

enum {
  JSON_SERIALIZER_LOCATION,
  JSON_SERIALIZER_ROOT,
  JSON_SERIALIZER_INDENT,
  JSON_SERIALIZER_PATH,
  JSON_SERIALIZER_FINISHED,
  JSON_SERIALIZER_STARTED,
  JSON_SERIALIZER_ERROR,
  JSON_SERIALIZER_BLOCKED,
};

static JSValue
js_jsonserializer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonSerializer* js;
  JSValue ret = JS_UNDEFINED;

  if(!(js = JS_GetOpaque2(ctx, this_val, js_jsonserializer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_SERIALIZER_LOCATION: ret = js_location_wrap(ctx, js->loc); break;
    case JSON_SERIALIZER_ROOT: ret = JS_DupValue(ctx, js->root); break;
    case JSON_SERIALIZER_INDENT: ret = JS_NewInt32(ctx, js->indent); break;
    case JSON_SERIALIZER_PATH: ret = property_recursion_path(&js->stack, ctx); break;
    case JSON_SERIALIZER_FINISHED: ret = JS_NewBool(ctx, js->finished); break;
    case JSON_SERIALIZER_STARTED: ret = JS_NewBool(ctx, js->started); break;
    case JSON_SERIALIZER_ERROR: ret = JS_NewBool(ctx, js->error); break;
    case JSON_SERIALIZER_BLOCKED: ret = JS_NewBool(ctx, js->blocked); break;
  }

  return ret;
}

static JSValue
js_jsonserializer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JsonSerializer* js;
  JSValue ret = JS_UNDEFINED;

  if(!(js = JS_GetOpaque2(ctx, this_val, js_jsonserializer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_SERIALIZER_INDENT: JS_ToInt32(ctx, &js->indent, value); break;
  }

  return ret;
}

static JSValue
js_jsonserializer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  JsonSerializer* js;
  JSValueConst root = argc > 0 ? argv[0] : JS_UNDEFINED;

  if(!(js = js_mallocz(ctx, sizeof(JsonSerializer))))
    return JS_EXCEPTION;

  js->ctx = ctx;
  vector_init(&js->stack, ctx);
  dbuf_init_ctx(ctx, &js->out);
  dbuf_init_ctx(ctx, &js->space);
  if(argc > 1)
    JS_ToInt32(ctx, &js->indent, argv[1]);

  if(!(js->loc = location_new(ctx))) {
    dbuf_free(&js->out);
    dbuf_free(&js->space);
    vector_free(&js->stack);
    js_free(ctx, js);
    return JS_EXCEPTION;
  }

  location_zero(js->loc);

  js->root = JS_DupValue(ctx, root);
  js->is_primitive = !JS_IsObject(root) || JS_IsFunction(ctx, root);
  js->out_writer = writer_from_dynbuf(&js->out);
  js->skip_writer = (Writer){&write_skip, js, NULL};

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, json_serializer_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_jsonserializer_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    JS_FreeValue(ctx, js->root);
    location_free(js->loc, JS_GetRuntime(ctx));
    dbuf_free(&js->out);
    dbuf_free(&js->space);
    vector_free(&js->stack);
    js_free(ctx, js);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, js);
  return obj;
}

static JSValue
js_jsonserializer_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  JSValue args[] = {argc > 0 ? argv[0] : JS_NewUint32(ctx, 1024)};
  JSValue ret = js_jsonserializer_read(ctx, this_val, countof(args), args);

  *pdone = FALSE;

  if(JS_IsException(ret)) {
    *pdone = TRUE;
    ret = JS_GetException(ctx);
  }

  return ret;
}

static void
js_jsonserializer_finalizer(JSRuntime* rt, JSValue val) {
  JsonSerializer* js;

  if((js = JS_GetOpaque(val, js_jsonserializer_class_id))) {
    property_recursion_free(&js->stack, rt);
    dbuf_free(&js->out);
    dbuf_free(&js->space);
    JS_FreeValueRT(rt, js->root);

    if(js->loc)
      location_free(js->loc, rt);

    js_free_rt(rt, js);
  }
}

static const JSCFunctionListEntry js_jsonserializer_proto_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_jsonserializer_read),
    JS_ITERATOR_NEXT_DEF("next", 0, js_jsonserializer_next, 0),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_json_iterator),
    JS_CGETSET_MAGIC_DEF("location", js_jsonserializer_get, 0, JSON_SERIALIZER_LOCATION),
    JS_CGETSET_MAGIC_DEF("root", js_jsonserializer_get, 0, JSON_SERIALIZER_ROOT),
    JS_CGETSET_MAGIC_DEF("indent", js_jsonserializer_get, js_jsonserializer_set, JSON_SERIALIZER_INDENT),
    JS_CGETSET_MAGIC_DEF("path", js_jsonserializer_get, 0, JSON_SERIALIZER_PATH),
    JS_CGETSET_MAGIC_DEF("finished", js_jsonserializer_get, 0, JSON_SERIALIZER_FINISHED),
    JS_CGETSET_MAGIC_DEF("started", js_jsonserializer_get, 0, JSON_SERIALIZER_STARTED),
    JS_CGETSET_MAGIC_DEF("error", js_jsonserializer_get, 0, JSON_SERIALIZER_ERROR),
    JS_CGETSET_MAGIC_DEF("blocked", js_jsonserializer_get, 0, JSON_SERIALIZER_BLOCKED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonSerializer", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_jsonserializer_class = {
    .class_name = "JsonSerializer",
    .finalizer = js_jsonserializer_finalizer,
};

/* ---------------------------------------------------------------------- */
/* JsonWriter: push-based incremental JSON writer                         */
/* ---------------------------------------------------------------------- */

typedef struct {
  unsigned is_object : 1, expecting_value : 1;
  uint32_t count;
} JsonWriterFrame;

typedef struct {
  Writer writer;
  size_t written;
  int32_t indent;
  Vector stack;
  DynBuf ibuf;
} JsonWriter;

static JSClassID js_jsonwriter_class_id = 0;
static JSValue jsonwriter_proto, jsonwriter_ctor;

static ssize_t
json_writer_putc(JsonWriter* wr, int c) {
  ssize_t res = writer_putc(&wr->writer, c);

  if(res > 0)
    wr->written += res;

  return res;
}

/*static ssize_t
json_writer_write(JsonWriter* wr, const void* buf, size_t len) {
  ssize_t res = write_all(&wr->writer, buf, len);

  if(res > 0)
    wr->written += res;

  return res;
}*/

static ssize_t
json_writer_indent(JsonWriter* wr) {
  ssize_t w = 0;

  if(wr->indent > 0) {
    size_t len = wr->indent * vector_size(&wr->stack, sizeof(JsonWriterFrame)) + 1;
    DynBuf* ib = &wr->ibuf;

    if(ib->size < len) {
      size_t oldlen = ib->size;
      dbuf_claim(ib, len - ib->size);
      memset(&ib->buf[oldlen], ' ', len - oldlen);
    }

    ib->buf[0] = '\n';
    w = writer_write(&wr->writer, ib->buf, len);
  }

  return w;
}

static ssize_t
json_writer_comma_indent(JsonWriter* wr, uint32_t count) {
  ssize_t res, w = 0;

  if(count > 0) {
    if((res = json_writer_putc(wr, ',')) <= 0)
      return -1;
    w += res;
  }

  if((res = json_writer_indent(wr)) < 0)
    return -1;
  w += res;
  return w;
}

/* Bookkeeping before writing an object/array/primitive value into the
 * current container: validates key/value ordering, and for array
 * containers emits the comma + indentation between items. */
static ssize_t
json_writer_before_value(JsonWriter* wr, JSContext* ctx) {
  if(vector_empty(&wr->stack))
    return 0;

  JsonWriterFrame* top = vector_back(&wr->stack, sizeof(JsonWriterFrame));

  if(top->is_object) {
    if(!top->expecting_value) {
      JS_ThrowTypeError(ctx, "JsonWriter: expected key");
      return -1;
    }

    top->expecting_value = FALSE;
    return 0;
  }

  ssize_t w;
  if((w = json_writer_comma_indent(wr, top->count)) < 0)
    return -1;

  top->count++;
  return w;
}

/* After a value is written, an enclosing object no longer expects a
 * value (its next token must be a key or objectEnd). */
static void
json_writer_after_value(JsonWriter* wr) {
  if(!vector_empty(&wr->stack)) {
    JsonWriterFrame* parent = vector_back(&wr->stack, sizeof(JsonWriterFrame));

    if(parent->is_object)
      parent->expecting_value = FALSE;
  }
}

static ssize_t
json_writer_key(JsonWriter* wr, JSContext* ctx, JSValueConst key_val) {
  ssize_t res, w;

  if(vector_empty(&wr->stack)) {
    JS_ThrowTypeError(ctx, "JsonWriter: key cannot be at root level");
    return -1;
  }

  JsonWriterFrame* top = vector_back(&wr->stack, sizeof(JsonWriterFrame));

  if(!top->is_object) {
    JS_ThrowTypeError(ctx, "JsonWriter: key cannot be used inside an array");
    return -1;
  }

  if(top->expecting_value) {
    JS_ThrowTypeError(ctx, "JsonWriter: expected value for previous key");
    return -1;
  }

  if((w = json_writer_comma_indent(wr, top->count)) < 0)
    return -1;

  size_t klen;
  const char* kstr;
  if(!(kstr = JS_ToCStringLen(ctx, &klen, key_val)))
    return -1;

  DynBuf db;
  dbuf_init_ctx(ctx, &db);
  Writer temp_wr = writer_from_dynbuf(&db);
  write_json_string(&temp_wr, kstr, klen);

  res = writer_write(&wr->writer, db.buf, db.size);
  writer_free(&temp_wr);
  JS_FreeCString(ctx, kstr);

  if(res <= 0)
    return -1;
  w += res;

  if((res = json_writer_putc(wr, ':')) <= 0)
    return -1;
  w += res;

  if(wr->indent > 0) {
    if((res = json_writer_putc(wr, ' ')) <= 0)
      return -1;
    w += res;
  }

  top->expecting_value = TRUE;
  top->count++;
  return w;
}

static ssize_t
json_writer_object_start(JsonWriter* wr, JSContext* ctx) {
  ssize_t res, w;
  if((w = json_writer_before_value(wr, ctx)) < 0)
    return -1;

  if((res = json_writer_putc(wr, '{')) <= 0)
    return -1;
  w += res;

  if(!vector_push(&wr->stack, ((JsonWriterFrame){TRUE, 0, FALSE})))
    return -1;

  return w;
}

static ssize_t
json_writer_array_start(JsonWriter* wr, JSContext* ctx) {
  ssize_t res, w;
  if((w = json_writer_before_value(wr, ctx)) < 0)
    return -1;

  if((res = json_writer_putc(wr, '[')) <= 0)
    return -1;
  w += res;

  if(!vector_push(&wr->stack, ((JsonWriterFrame){FALSE, 0, FALSE})))
    return -1;

  return w;
}

static ssize_t
json_writer_object_end(JsonWriter* wr, JSContext* ctx) {
  ssize_t res, w = 0;

  if(vector_empty(&wr->stack)) {
    JS_ThrowTypeError(ctx, "JsonWriter: unmatched objectEnd");
    return -1;
  }

  JsonWriterFrame* top = vector_back(&wr->stack, sizeof(JsonWriterFrame));
  if(!top->is_object) {
    JS_ThrowTypeError(ctx, "JsonWriter: expected arrayEnd, got objectEnd");
    return -1;
  }
  if(top->expecting_value) {
    JS_ThrowTypeError(ctx, "JsonWriter: expected value for key");
    return -1;
  }

  vector_pop(&wr->stack, sizeof(JsonWriterFrame));

  if(top->count > 0) {
    if((res = json_writer_indent(wr)) < 0)
      return -1;
    w += res;
  }

  if((res = json_writer_putc(wr, '}')) <= 0)
    return -1;
  w += res;

  json_writer_after_value(wr);
  return w;
}

static ssize_t
json_writer_array_end(JsonWriter* wr, JSContext* ctx) {
  ssize_t res, w = 0;

  if(vector_empty(&wr->stack)) {
    JS_ThrowTypeError(ctx, "JsonWriter: unmatched arrayEnd");
    return -1;
  }

  JsonWriterFrame* top = vector_back(&wr->stack, sizeof(JsonWriterFrame));
  if(top->is_object) {
    JS_ThrowTypeError(ctx, "JsonWriter: expected objectEnd, got arrayEnd");
    return -1;
  }

  vector_pop(&wr->stack, sizeof(JsonWriterFrame));

  if(top->count > 0) {
    if((res = json_writer_indent(wr)) < 0)
      return -1;
    w += res;
  }

  if((res = json_writer_putc(wr, ']')) <= 0)
    return -1;
  w += res;

  json_writer_after_value(wr);
  return w;
}

static ssize_t
json_writer_value(JsonWriter* wr, JSContext* ctx, JSValueConst val) {
  ssize_t res, w;
  if((w = json_writer_before_value(wr, ctx)) < 0)
    return -1;

  DynBuf db;
  dbuf_init_ctx(ctx, &db);
  Writer temp_wr = writer_from_dynbuf(&db);
  write_json_primitive(ctx, &temp_wr, val);

  res = writer_write(&wr->writer, db.buf, db.size);
  writer_free(&temp_wr);

  if(res <= 0)
    return -1;
  w += res;

  json_writer_after_value(wr);
  return w;
}

enum {
  JSON_WRITER_OBJECT_START,
  JSON_WRITER_OBJECT_END,
  JSON_WRITER_ARRAY_START,
  JSON_WRITER_ARRAY_END,
  JSON_WRITER_KEY,
  JSON_WRITER_VALUE,
};

static JSValue
js_jsonwriter_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JsonWriter* wr;
  ssize_t w = 0;

  if(!(wr = JS_GetOpaque2(ctx, this_val, js_jsonwriter_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_WRITER_OBJECT_START: w = json_writer_object_start(wr, ctx); break;
    case JSON_WRITER_OBJECT_END: w = json_writer_object_end(wr, ctx); break;
    case JSON_WRITER_ARRAY_START: w = json_writer_array_start(wr, ctx); break;
    case JSON_WRITER_ARRAY_END: w = json_writer_array_end(wr, ctx); break;

    case JSON_WRITER_KEY: {
      if(argc < 1)
        return JS_ThrowTypeError(ctx, "JsonWriter.key() requires an argument");

      w = json_writer_key(wr, ctx, argv[0]);
      break;
    }

    case JSON_WRITER_VALUE: {
      if(argc < 1)
        return JS_ThrowTypeError(ctx, "JsonWriter.value() requires an argument");

      w = json_writer_value(wr, ctx, argv[0]);
      break;
    }
  }

  if(w < 0)
    return JS_EXCEPTION;

  return JS_NewInt64(ctx, w);
}

enum {
  JSON_WRITER_WRITTEN,
  JSON_WRITER_INDENT,
  JSON_WRITER_LEVEL,
  JSON_WRITER_STACK,
};

static JSValue
js_jsonwriter_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonWriter* wr;
  JSValue ret = JS_UNDEFINED;

  if(!(wr = JS_GetOpaque2(ctx, this_val, js_jsonwriter_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_WRITER_WRITTEN: ret = JS_NewInt64(ctx, wr->written); break;
    case JSON_WRITER_INDENT: ret = JS_NewInt32(ctx, wr->indent); break;
    case JSON_WRITER_LEVEL: ret = JS_NewInt32(ctx, vector_size(&wr->stack, sizeof(JsonWriterFrame))); break;
    case JSON_WRITER_STACK: {
      ret = JS_NewArray(ctx);

      JsonWriterFrame* frame;
      uint32_t i = 0;
      vector_foreach_t(&wr->stack, frame) {
        JSValue item = JS_NewObjectProto(ctx, JS_NULL);
        JS_SetPropertyStr(ctx, item, "isObject", JS_NewBool(ctx, frame->is_object));
        JS_SetPropertyStr(ctx, item, "expectingValue", JS_NewBool(ctx, frame->expecting_value));
        JS_SetPropertyStr(ctx, item, "count", JS_NewUint32(ctx, frame->count));
        JS_SetPropertyUint32(ctx, ret, i++, item);
      }

      break;
    }
  }

  return ret;
}

static JSValue
js_jsonwriter_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JsonWriter* wr;
  JSValue ret = JS_UNDEFINED;

  if(!(wr = JS_GetOpaque2(ctx, this_val, js_jsonwriter_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_WRITER_INDENT: wr->indent = js_toint32(ctx, value); break;
  }

  return ret;
}

static JSValue
js_jsonwriter_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  JsonWriter* wr;
  int i = 0;

  if(!(wr = js_mallocz(ctx, sizeof(JsonWriter))))
    return JS_EXCEPTION;

  if(i < argc && writer_from_js(ctx, argv[i], &wr->writer))
    i++;

  vector_init(&wr->stack, ctx);

  JSValue options = i < argc ? argv[i] : (argc > 0 ? argv[0] : JS_UNDEFINED);

  if(JS_IsNumber(options))
    wr->indent = js_toint32(ctx, options);
  else if(js_has_propertystr(ctx, options, "indent"))
    wr->indent = js_toint32_free(ctx, JS_GetPropertyStr(ctx, options, "indent"));
  else
    wr->indent = 0;

  dbuf_init_ctx(ctx, &wr->ibuf);

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_jsonwriter_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, wr);
  return obj;

fail:
  writer_free(&wr->writer);
  vector_free(&wr->stack);
  dbuf_free(&wr->ibuf);
  js_free(ctx, wr);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_jsonwriter_finalizer(JSRuntime* rt, JSValue val) {
  JsonWriter* wr;

  if((wr = JS_GetOpaque(val, js_jsonwriter_class_id))) {
    writer_free(&wr->writer);
    vector_free(&wr->stack);
    dbuf_free(&wr->ibuf);
    js_free_rt(rt, wr);
  }
}

static JSClassDef js_jsonwriter_class = {
    .class_name = "JsonWriter",
    .finalizer = js_jsonwriter_finalizer,
};

static const JSCFunctionListEntry js_jsonwriter_funcs[] = {
    JS_CFUNC_MAGIC_DEF("value", 1, js_jsonwriter_method, JSON_WRITER_VALUE),
    JS_CFUNC_MAGIC_DEF("objectStart", 0, js_jsonwriter_method, JSON_WRITER_OBJECT_START),
    JS_CFUNC_MAGIC_DEF("objectEnd", 0, js_jsonwriter_method, JSON_WRITER_OBJECT_END),
    JS_CFUNC_MAGIC_DEF("arrayStart", 0, js_jsonwriter_method, JSON_WRITER_ARRAY_START),
    JS_CFUNC_MAGIC_DEF("arrayEnd", 0, js_jsonwriter_method, JSON_WRITER_ARRAY_END),
    JS_CFUNC_MAGIC_DEF("key", 1, js_jsonwriter_method, JSON_WRITER_KEY),
    JS_CGETSET_MAGIC_DEF("written", js_jsonwriter_get, 0, JSON_WRITER_WRITTEN),
    JS_CGETSET_MAGIC_DEF("indent", js_jsonwriter_get, js_jsonwriter_set, JSON_WRITER_INDENT),
    JS_CGETSET_MAGIC_DEF("level", js_jsonwriter_get, 0, JSON_WRITER_LEVEL),
    JS_CGETSET_MAGIC_DEF("stack", js_jsonwriter_get, 0, JSON_WRITER_STACK),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonWriter", JS_PROP_CONFIGURABLE),
};

static JSValue
js_jsonparser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  JsonParser* p;
  JSValueConst input = argc > 0 ? argv[0] : JS_UNDEFINED;
  Reader reader;
  const char* filename = 0;

  if(JS_IsFunction(ctx, input)) {
    reader = reader_from_jsfunction(ctx, input);
  } else if(JS_IsObject(input)) {
    JSValue read_fn = JS_GetPropertyStr(ctx, input, "read");

    if(JS_IsException(read_fn))
      return JS_EXCEPTION;

    if(JS_IsFunction(ctx, read_fn))
      reader = reader_from_jsmethod(ctx, read_fn, input);
    else
      reader = reader_from_jsbuf(ctx, input);

    JS_FreeValue(ctx, read_fn);
  } else if(JS_IsNumber(input)) {
    reader = reader_from_fd(js_toint64(ctx, input), FALSE);
  } else {
    reader = reader_from_jsbuf(ctx, input);
  }

  if(argc > 1)
    filename = JS_ToCString(ctx, argv[1]);

  p = json_new(reader, filename, ctx);

  if(filename)
    JS_FreeCString(ctx, filename);

  if(!p) {
    reader_free(&reader);
    return JS_EXCEPTION;
  }

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, json_parser_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_jsonparser_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    json_free(p, JS_GetRuntime(ctx));
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, p);

  return obj;
}

enum {
  JSON_PARSER_PARSE,
};

static JSValue
js_jsonparser_parse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonParser* p;
  JSValue ret = JS_UNDEFINED;
  BOOL skip = argc > 0 && JS_ToBool(ctx, argv[0]);

  if(!(p = JS_GetOpaque2(ctx, this_val, js_jsonparser_class_id)))
    return JS_EXCEPTION;

  int type = json_parse(p);

  if(type == JSON_ERROR) {
    char* loc = location_tostring(p->loc, ctx);

    JS_ThrowSyntaxError(ctx, "%s%s%s", loc && *loc ? loc : "", loc && *loc ? ": " : "", p->error ? p->error : "parse error");

    if(loc)
      js_free(ctx, loc);

    return JS_EXCEPTION;
  }

  /* If skipping is requested and the current token starts a container (Object or Array) */
  if(skip && (type == JSON_TYPE_OBJECT || type == JSON_TYPE_ARRAY)) {
    int depth = 1;

    while(depth > 0) {
      int next_type = json_parse(p);

      if(next_type == JSON_ERROR) {
        char* loc = location_tostring(p->loc, ctx);

        JS_ThrowSyntaxError(ctx, "%s%s%s", loc && *loc ? loc : "", loc && *loc ? ": " : "", p->error ? p->error : "parse error");

        if(loc)
          js_free(ctx, loc);

        return JS_EXCEPTION;
      }

      if(next_type == JSON_TYPE_OBJECT || next_type == JSON_TYPE_ARRAY)
        depth++;
      else if(next_type == JSON_TYPE_OBJECT_END || next_type == JSON_TYPE_ARRAY_END)
        depth--;

      type = next_type;
    }
  }

  return JS_NewInt32(ctx, type);
}

enum {
  JSON_PARSER_CALLBACK,
  JSON_PARSER_POS,
  JSON_PARSER_TOKEN,
  JSON_PARSER_STATE,
  JSON_PARSER_DEPTH,
  JSON_PARSER_LOCATION,
  JSON_PARSER_COMMENTS,
};

static JSValue
js_jsonparser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonParser* p;
  JSValue ret = JS_UNDEFINED;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_jsonparser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_PARSER_CALLBACK: {
      ret = js_value_mkobj2(ctx, p->opaque);
      break;
    }

    case JSON_PARSER_POS: {
      ret = JS_NewUint32(ctx, p->pos);
      break;
    }

    case JSON_PARSER_COMMENTS: {
      ret = JS_NewBool(ctx, p->comments);
      break;
    }

    case JSON_PARSER_TOKEN: {
      ret = dbuf_tostring(&p->token, ctx);
      break;
    }

    case JSON_PARSER_STATE: {
      ret = JS_NewInt32(ctx, p->state);
      break;
    }

    case JSON_PARSER_DEPTH: {
      ret = JS_NewUint32(ctx, p->stack.len);
      break;
    }

    case JSON_PARSER_LOCATION: {
      ret = js_location_wrap(ctx, p->loc);
      break;
    }
  }

  return ret;
}

static void
js_jsonparser_callback(JsonParser* p, JsonValueType type, void* ptr) {
  struct js_jsonparser_opaque* op = p->opaque;
  JSContext* ctx = op->ctx;
  JSValue fn = js_value_mkobj(op->obj);
  JSValue args[] = {
      js_value_mkobj(op->parser),
      JS_NewInt32(ctx, type),
      ptr ? JS_NewString(ctx, ptr) : JS_UNDEFINED,
  };
  JSValue ret = JS_Call(ctx, fn, JS_UNDEFINED, countof(args), args);
  JS_FreeValue(ctx, ret);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, args[2]);
}

static JSValue
js_jsonparser_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JsonParser* p;
  JSValue ret = JS_UNDEFINED;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_jsonparser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_PARSER_COMMENTS: {
      p->comments = JS_ToBool(ctx, value);
      break;
    }

    case JSON_PARSER_CALLBACK: {
      struct js_jsonparser_opaque* op;

      if(!JS_IsFunction(ctx, value))
        return JS_ThrowTypeError(ctx, "value must be a function");

      if(p->opaque) {
        op = p->opaque;
        js_freeobj(ctx, op->obj);
      }

      op = p->opaque ? p->opaque : js_malloc(ctx, sizeof(struct js_jsonparser_opaque));

      if(op) {
        *op = (struct js_jsonparser_opaque){ctx, js_value_obj(this_val), js_value_obj2(ctx, value)};

        p->callback = js_jsonparser_callback;
        p->opaque = op;
      }

      break;
    }
  }

  return ret;
}

static JSValue
js_jsonparser_iterator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  JSValue ret = js_jsonparser_parse(ctx, this_val, argc, argv);

  *pdone = FALSE;

  if(JS_IsException(ret)) {
    *pdone = TRUE;
    ret = JS_GetException(ctx);
  } else if(js_toint32(ctx, ret) == JSON_NEED_DATA) {
    *pdone = TRUE;
    ret = JS_UNDEFINED;
  }

  return ret;
}

static void
js_jsonparser_finalizer(JSRuntime* rt, JSValue obj) {
  JsonParser* p;

  if((p = JS_GetOpaque(obj, js_jsonparser_class_id)))
    json_free(p, rt);
}

static const JSCFunctionListEntry js_jsonparser_proto_funcs[] = {
    JS_CFUNC_DEF("parse", 0, js_jsonparser_parse),
    JS_CGETSET_MAGIC_FLAGS_DEF("pos", js_jsonparser_get, 0, JSON_PARSER_POS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("token", js_jsonparser_get, 0, JSON_PARSER_TOKEN, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("state", js_jsonparser_get, 0, JSON_PARSER_STATE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("depth", js_jsonparser_get, 0, JSON_PARSER_DEPTH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("location", js_jsonparser_get, 0, JSON_PARSER_LOCATION, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("callback", js_jsonparser_get, js_jsonparser_set, JSON_PARSER_CALLBACK),
    JS_CGETSET_MAGIC_DEF("comments", js_jsonparser_get, js_jsonparser_set, JSON_PARSER_COMMENTS),
    JS_ITERATOR_NEXT_DEF("next", 0, js_jsonparser_iterator_next, 0),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_json_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonParser", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_jsonparser_static_props[] = {
    JS_PROP_INT32_DEF("NEED_DATA", -2, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("NONE", -1, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("OBJECT", 0, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("OBJECT_END", 1, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ARRAY", 2, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ARRAY_END", 3, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("KEY", 4, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("STRING", 5, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TRUE", 6, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("FALSE", 7, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("NULL", 8, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("NUMBER", 9, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("COMMENT", 10, JS_PROP_ENUMERABLE),
};

static JSClassDef js_jsonparser_class = {
    .class_name = "JsonParser",
    .finalizer = js_jsonparser_finalizer,
};

static int
js_json_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_jsonparser_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_jsonparser_class_id, &js_jsonparser_class);

  json_parser_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, json_parser_proto, js_jsonparser_proto_funcs, countof(js_jsonparser_proto_funcs));
  JS_SetPropertyFunctionList(ctx, json_parser_proto, js_jsonparser_static_props, countof(js_jsonparser_static_props));

  json_parser_ctor = JS_NewCFunction2(ctx, js_jsonparser_constructor, "JsonParser", 1, JS_CFUNC_constructor, 0);
  JS_SetPropertyFunctionList(ctx, json_parser_ctor, js_jsonparser_static_props, countof(js_jsonparser_static_props));

  JS_SetClassProto(ctx, js_jsonparser_class_id, json_parser_proto);
  JS_SetConstructor(ctx, json_parser_ctor, json_parser_proto);

  if(js_location_class_id == 0)
    js_location_init(ctx, 0);

  JS_NewClassID(&js_jsonpushparser_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_jsonpushparser_class_id, &js_jsonpushparser_class);

  json_pushparser_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, json_pushparser_proto, js_jsonpushparser_proto_funcs, countof(js_jsonpushparser_proto_funcs));

  json_pushparser_ctor = JS_NewCFunction2(ctx, js_jsonpushparser_constructor, "JsonPushParser", 0, JS_CFUNC_constructor, 0);
  JS_SetClassProto(ctx, js_jsonpushparser_class_id, json_pushparser_proto);
  JS_SetConstructor(ctx, json_pushparser_ctor, json_pushparser_proto);

  JS_NewClassID(&js_jsonserializer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_jsonserializer_class_id, &js_jsonserializer_class);

  json_serializer_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, json_serializer_proto, js_jsonserializer_proto_funcs, countof(js_jsonserializer_proto_funcs));

  json_serializer_ctor = JS_NewCFunction2(ctx, js_jsonserializer_constructor, "JsonSerializer", 1, JS_CFUNC_constructor, 0);
  JS_SetClassProto(ctx, js_jsonserializer_class_id, json_serializer_proto);
  JS_SetConstructor(ctx, json_serializer_ctor, json_serializer_proto);

  JS_NewClassID(&js_jsonwriter_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_jsonwriter_class_id, &js_jsonwriter_class);

  jsonwriter_ctor = JS_NewCFunction2(ctx, js_jsonwriter_constructor, "JsonWriter", 1, JS_CFUNC_constructor, 0);
  jsonwriter_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, jsonwriter_proto, js_jsonwriter_funcs, countof(js_jsonwriter_funcs));
  JS_SetClassProto(ctx, js_jsonwriter_class_id, jsonwriter_proto);
  JS_SetConstructor(ctx, jsonwriter_ctor, jsonwriter_proto);

  if(m) {
    JSValue jsonl = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, jsonl, js_jsonl_funcs, countof(js_jsonl_funcs));
    JS_SetModuleExport(ctx, m, "JSONL", jsonl);
    JS_SetModuleExport(ctx, m, "JsonParser", json_parser_ctor);
    JS_SetModuleExport(ctx, m, "JsonPushParser", json_pushparser_ctor);
    JS_SetModuleExport(ctx, m, "JsonSerializer", json_serializer_ctor);
    JS_SetModuleExport(ctx, m, "JsonWriter", jsonwriter_ctor);
  }

  JS_SetModuleExportList(ctx, m, js_json_funcs, countof(js_json_funcs));
  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_json
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_json_init))) {
    JS_AddModuleExport(ctx, m, "JSONL");
    JS_AddModuleExport(ctx, m, "JsonParser");
    JS_AddModuleExport(ctx, m, "JsonPushParser");
    JS_AddModuleExport(ctx, m, "JsonSerializer");
    JS_AddModuleExport(ctx, m, "JsonWriter");
    JS_AddModuleExportList(ctx, m, js_json_funcs, countof(js_json_funcs));
  }

  return m;
}
