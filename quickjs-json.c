#include "stream-utils.h"
#include "utils.h"
#include "virtual-properties.h"
#include "json.h"
#include "vector.h"
#include "property-enumeration.h"
#include "char-utils.h"
#include "quickjs-location.h"
#include <math.h>
#define SJ_IMPL
#include "sj.h"

#define REC_DEPTH(v) vector_size((v), sizeof(PropertyEnumeration))
#define REC_TOP(v) vector_back((v), sizeof(PropertyEnumeration))
#define REC_EMPLACE(v) vector_emplace((v), sizeof(PropertyEnumeration))
#define REC_POP(v) vector_pop((v), sizeof(PropertyEnumeration))

typedef struct {
  JSValue obj;
  sj_Value sj;
  uint32_t index;
  BOOL is_object;
} ParseFrame;

VISIBLE JSClassID js_json_parser_class_id = 0;
static JSValue json_parser_proto, json_parser_ctor;

VISIBLE JSClassID js_json_pushparser_class_id = 0;
static JSValue json_pushparser_proto, json_pushparser_ctor;

VISIBLE JSClassID js_json_serializer_class_id = 0;
static JSValue json_serializer_proto, json_serializer_ctor;

struct js_json_parser_opaque {
  JSContext* ctx;
  JSObject *parser, *obj;
};

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

  ParseFrame frame = (ParseFrame){parse_make_container(ctx, root.type), root, 0, root.type == SJ_OBJECT};

  if(!vector_put(&stack, &frame, sizeof(ParseFrame))) {
    JS_FreeValue(ctx, frame.obj);
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

      frame = (ParseFrame){child, v, 0, v.type == SJ_OBJECT};

      if(!vector_put(&stack, &frame, sizeof(ParseFrame))) {
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
    return 1; /* nothing to write is a trivial success, not the "blocked" 0 */

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

    if(oldsize != (size_t)count) {
      if(dbuf_realloc(ws, count))
        return -1;

      if((size_t)count > oldsize)
        memset(&ws->buf[oldsize], ' ', ws->allocated_size - oldsize);

      ws->size = count;
    }

    if((w = write_all(wr, ws->buf, count)) <= 0)
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

  dbuf_init2(&out, 0, 0);
  dbuf_init2(&space, 0, 0);

  Writer wr = writer_from_dynbuf(&out);

  if(!JS_IsObject(argv[0]) || JS_IsFunction(ctx, argv[0])) {
    write_json_primitive(ctx, &wr, argv[0]);
    JSValue ret = dbuf_tostring_free(&out, ctx);
    writer_free(&wr);
    return ret;
  }

  vector_init(&stack, ctx);

  if(write_push(&stack, ctx, JS_DupValue(ctx, argv[0]), flags)) {
    writer_free(&wr);
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
    } else {
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

static const JSCFunctionListEntry js_json_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_json_read),
    JS_CFUNC_DEF("write", 2, js_json_write),
};

/* ---------------------------------------------------------------------- */
/* JsonPushParser: incremental push (.write()) parser building .root,     */
/* tracking .path via a dedicated frame stack, and .location.             */
/* ---------------------------------------------------------------------- */

typedef enum {
  PP_TOK_NONE = 0,
  PP_TOK_STRING,
  PP_TOK_NUMBER,
  PP_TOK_TRUE,
  PP_TOK_FALSE,
  PP_TOK_NULL,
} PPTokKind;

typedef enum {
  PP_STR_NORMAL = 0,
  PP_STR_ESCAPE,
  PP_STR_UNICODE,
} PPStrState;

typedef enum {
  PP_EXPECT_VALUE = 0,
  PP_EXPECT_KEY,
  PP_EXPECT_COLON,
  PP_EXPECT_COMMA_OR_END,
  PP_DONE,
} PPState;

typedef struct {
  JSValue obj;
  BOOL is_array;
  uint32_t index; /* next array index, or count of committed object pairs */
  JSAtom key;      /* current object key atom; JS_ATOM_NULL for array frames / no key yet */
} PPFrame;

#define PPF_TOP(v) ((PPFrame*)vector_back((v), sizeof(PPFrame)))
#define PPF_EMPLACE(v) ((PPFrame*)vector_emplace((v), sizeof(PPFrame)))
#define PPF_POP(v) vector_pop((v), sizeof(PPFrame))

typedef struct {
  JSContext* ctx;
  Vector stack; /* PPFrame per open container */
  JSValue root;
  PPState state;
  BOOL done;
  BOOL had_error;
  Location* loc;

  PPTokKind tok_kind;
  DynBuf token;
  PPStrState str_state;
  uint32_t str_unicode_val;
  int str_unicode_count;
  uint32_t str_surrogate_hi;
  const char* literal_text;
  int literal_pos;

  BOOL resyncing;
  int32_t resync_local_depth;
  BOOL resync_in_string;
  BOOL resync_in_escape;
} JsonPushParser;

static JSValue
pp_path(JsonPushParser* pp, JSContext* ctx) {
  JSValue ret = JS_NewArray(ctx);
  PPFrame* it;
  uint32_t i = 0;

  vector_foreach_t(&pp->stack, it) {
    JSValue key = it->is_array ? JS_NewUint32(ctx, it->index) : (it->key != JS_ATOM_NULL ? JS_AtomToValue(ctx, it->key) : JS_UNDEFINED);
    JS_SetPropertyUint32(ctx, ret, i++, key);
  }

  return ret;
}

static int
pp_open_container(JsonPushParser* pp, JSContext* ctx, int sj_type) {
  JSValue container = parse_make_container(ctx, sj_type);
  PPFrame* fr;

  if(JS_IsException(container))
    return -1;

  if(!vector_empty(&pp->stack)) {
    PPFrame* parent = PPF_TOP(&pp->stack);

    if(parent->is_array)
      JS_SetPropertyUint32(ctx, parent->obj, parent->index, JS_DupValue(ctx, container));
    else if(parent->key != JS_ATOM_NULL)
      JS_SetProperty(ctx, parent->obj, parent->key, JS_DupValue(ctx, container));
  }

  if(!(fr = PPF_EMPLACE(&pp->stack))) {
    JS_FreeValue(ctx, container);
    return -1;
  }

  fr->obj = container;
  fr->is_array = (sj_type == SJ_ARRAY);
  fr->index = 0;
  fr->key = JS_ATOM_NULL;

  pp->state = fr->is_array ? PP_EXPECT_VALUE : PP_EXPECT_KEY;

  return 0;
}

static void
pp_frame_pop(JsonPushParser* pp, JSContext* ctx) {
  PPFrame* fr = PPF_TOP(&pp->stack);
  JSValue obj = fr->obj;

  if(fr->key != JS_ATOM_NULL)
    JS_FreeAtom(ctx, fr->key);

  PPF_POP(&pp->stack);

  if(vector_empty(&pp->stack)) {
    JS_FreeValue(ctx, pp->root);
    pp->root = obj;
    pp->done = TRUE;
    pp->state = PP_DONE;
  } else {
    JS_FreeValue(ctx, obj);
    pp->state = PP_EXPECT_COMMA_OR_END;
  }
}

static void
pp_emit_key(JsonPushParser* pp, JSContext* ctx, const uint8_t* bytes, size_t len) {
  PPFrame* parent = PPF_TOP(&pp->stack);

  if(parent->key != JS_ATOM_NULL)
    JS_FreeAtom(ctx, parent->key);

  parent->key = JS_NewAtomLen(ctx, (const char*)bytes, len);
  pp->state = PP_EXPECT_COLON;
}

static void
pp_emit_value(JsonPushParser* pp, JSContext* ctx, JSValue value) {
  PPFrame* parent;

  if(vector_empty(&pp->stack)) {
    JS_FreeValue(ctx, pp->root);
    pp->root = value;
    pp->done = TRUE;
    pp->state = PP_DONE;
    return;
  }

  parent = PPF_TOP(&pp->stack);

  if(parent->is_array) {
    JS_SetPropertyUint32(ctx, parent->obj, parent->index, value);
  } else if(parent->key != JS_ATOM_NULL) {
    JS_SetProperty(ctx, parent->obj, parent->key, value);
  } else {
    JS_FreeValue(ctx, value);
  }

  parent->index++;
  pp->state = PP_EXPECT_COMMA_OR_END;
}

static void
pp_finish_token(JsonPushParser* pp, JSContext* ctx) {
  JSValue value;

  switch(pp->tok_kind) {
    case PP_TOK_STRING:
      if(pp->state == PP_EXPECT_KEY) {
        pp_emit_key(pp, ctx, pp->token.buf, pp->token.size);
        pp->tok_kind = PP_TOK_NONE;
        dbuf_zero(&pp->token);
        pp->str_surrogate_hi = 0;
        return;
      }

      value = JS_NewStringLen(ctx, (const char*)pp->token.buf, pp->token.size);
      break;

    case PP_TOK_NUMBER: {
      double d = 0;
      dbuf_0(&pp->token);
      scan_double((const char*)pp->token.buf, &d);
      value = JS_NewFloat64(ctx, d);
      break;
    }

    case PP_TOK_TRUE: value = JS_TRUE; break;
    case PP_TOK_FALSE: value = JS_FALSE; break;
    case PP_TOK_NULL: value = JS_NULL; break;
    default: value = JS_UNDEFINED; break;
  }

  pp->tok_kind = PP_TOK_NONE;
  dbuf_zero(&pp->token);
  pp->str_surrogate_hi = 0;
  pp_emit_value(pp, ctx, value);
}

static void
pp_begin_resync(JsonPushParser* pp) {
  pp->resyncing = TRUE;
  pp->resync_local_depth = 0;
  pp->resync_in_string = FALSE;
  pp->resync_in_escape = FALSE;
  pp->tok_kind = PP_TOK_NONE;
  dbuf_zero(&pp->token);
  pp->str_surrogate_hi = 0;
}

static void
pp_throw(JsonPushParser* pp, JSContext* ctx, const char* msg) {
  char* loc = location_tostring(pp->loc, ctx);

  JS_ThrowSyntaxError(ctx, "%s%s%s", loc && *loc ? loc : "", loc && *loc ? ": " : "", msg);

  if(loc)
    js_free(ctx, loc);

  pp->had_error = TRUE;
  pp_begin_resync(pp);
}

static int
pp_string_char(JsonPushParser* pp, JSContext* ctx, uint8_t c) {
  if(pp->str_state == PP_STR_ESCAPE) {
    int uc;

    pp->str_state = PP_STR_NORMAL;

    if(c == 'u') {
      pp->str_state = PP_STR_UNICODE;
      pp->str_unicode_val = 0;
      pp->str_unicode_count = 0;
      return 0;
    }

    if(!(uc = is_quotable_char(c))) {
      pp_throw(pp, ctx, "invalid escape sequence in string");
      return 1;
    }

    dbuf_putc(&pp->token, uc);
    return 0;
  }

  if(pp->str_state == PP_STR_UNICODE) {
    int v;
    uint32_t cp;

    if(!is_xdigit_char(c)) {
      pp_throw(pp, ctx, "invalid unicode escape in string");
      return 1;
    }

    v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10;
    pp->str_unicode_val = (pp->str_unicode_val << 4) | (uint32_t)v;

    if(++pp->str_unicode_count < 4)
      return 0;

    pp->str_state = PP_STR_NORMAL;
    cp = pp->str_unicode_val;

    if(is_utf16_high_surrogate(cp)) {
      pp->str_surrogate_hi = cp;
    } else {
      uint8_t buf[UTF8_CHAR_LEN_MAX];
      int n;

      if(is_utf16_low_surrogate(cp) && pp->str_surrogate_hi)
        cp = 0x10000 + ((pp->str_surrogate_hi - 0xd800) << 10) + (cp - 0xdc00);

      pp->str_surrogate_hi = 0;
      n = unicode_to_utf8(buf, cp);
      dbuf_put(&pp->token, buf, n);
    }

    return 0;
  }

  if(c == '\\') {
    pp->str_state = PP_STR_ESCAPE;
    return 0;
  }

  if(c == '"') {
    pp_finish_token(pp, ctx);
    return 0;
  }

  dbuf_putc(&pp->token, c);
  return 0;
}

static int
pp_number_char(JsonPushParser* pp, JSContext* ctx, uint8_t c) {
  if(is_number_char((char)c)) {
    dbuf_putc(&pp->token, c);
    return 0;
  }

  pp_finish_token(pp, ctx);
  return 1;
}

static int
pp_literal_char(JsonPushParser* pp, JSContext* ctx, uint8_t c) {
  size_t len = strlen(pp->literal_text);

  if((uint8_t)pp->literal_text[pp->literal_pos] != c) {
    pp_throw(pp, ctx, "invalid literal");
    return 1;
  }

  if(++pp->literal_pos == (int)len)
    pp_finish_token(pp, ctx);

  return 0;
}

static int
pp_resync_char(JsonPushParser* pp, JSContext* ctx, uint8_t c) {
  if(pp->resync_in_string) {
    if(pp->resync_in_escape)
      pp->resync_in_escape = FALSE;
    else if(c == '\\')
      pp->resync_in_escape = TRUE;
    else if(c == '"')
      pp->resync_in_string = FALSE;

    return 0;
  }

  if(c == '"') {
    pp->resync_in_string = TRUE;
    return 0;
  }

  if(c == '{' || c == '[') {
    pp->resync_local_depth++;
    return 0;
  }

  if(c == '}' || c == ']') {
    if(pp->resync_local_depth > 0) {
      pp->resync_local_depth--;
      return 0;
    }

    pp->resyncing = FALSE;

    if(!vector_empty(&pp->stack))
      pp_frame_pop(pp, ctx);
    else
      pp->state = PP_EXPECT_VALUE;

    return 0;
  }

  if(c == ',' && pp->resync_local_depth == 0) {
    pp->resyncing = FALSE;

    if(!vector_empty(&pp->stack)) {
      PPFrame* top = PPF_TOP(&pp->stack);
      pp->state = top->is_array ? PP_EXPECT_VALUE : PP_EXPECT_KEY;
    } else {
      pp->state = PP_EXPECT_VALUE;
    }

    return 0;
  }

  return 0;
}

static int
pp_structural_char(JsonPushParser* pp, JSContext* ctx, uint8_t c) {
  if(is_whitespace_char((char)c))
    return 0;

  switch(pp->state) {
    case PP_DONE:
      pp_throw(pp, ctx, "unexpected data after end of input");
      return 1;

    case PP_EXPECT_COLON:
      if(c != ':') {
        pp_throw(pp, ctx, "expected ':'");
        return 1;
      }
      pp->state = PP_EXPECT_VALUE;
      return 0;

    case PP_EXPECT_COMMA_OR_END: {
      PPFrame* top = PPF_TOP(&pp->stack);
      BOOL is_array = top->is_array;

      if(c == ',') {
        pp->state = is_array ? PP_EXPECT_VALUE : PP_EXPECT_KEY;
        return 0;
      }

      if((c == '}' && !is_array) || (c == ']' && is_array)) {
        pp_frame_pop(pp, ctx);
        return 0;
      }

      pp_throw(pp, ctx, "expected ',' or closing bracket");
      return 1;
    }

    case PP_EXPECT_KEY: {
      PPFrame* top = PPF_TOP(&pp->stack);

      if(c == '}' && top->index == 0) {
        pp_frame_pop(pp, ctx);
        return 0;
      }

      if(c == '"') {
        pp->tok_kind = PP_TOK_STRING;
        pp->str_state = PP_STR_NORMAL;
        return 0;
      }

      pp_throw(pp, ctx, "expected string key");
      return 1;
    }

    case PP_EXPECT_VALUE: {
      PPFrame* top = vector_empty(&pp->stack) ? NULL : PPF_TOP(&pp->stack);

      if(c == '{') {
        if(pp_open_container(pp, ctx, SJ_OBJECT))
          pp->had_error = TRUE;
        return 0;
      }

      if(c == '[') {
        if(pp_open_container(pp, ctx, SJ_ARRAY))
          pp->had_error = TRUE;
        return 0;
      }

      if(c == ']' && top && top->is_array && top->index == 0) {
        pp_frame_pop(pp, ctx);
        return 0;
      }

      if(c == '"') {
        pp->tok_kind = PP_TOK_STRING;
        pp->str_state = PP_STR_NORMAL;
        return 0;
      }

      if(c == 't' || c == 'f' || c == 'n') {
        pp->tok_kind = c == 't' ? PP_TOK_TRUE : c == 'f' ? PP_TOK_FALSE : PP_TOK_NULL;
        pp->literal_text = c == 't' ? "true" : c == 'f' ? "false" : "null";
        pp->literal_pos = 1;
        return 0;
      }

      if(c == '-' || is_digit_char((char)c)) {
        pp->tok_kind = PP_TOK_NUMBER;
        dbuf_putc(&pp->token, c);
        return 0;
      }

      pp_throw(pp, ctx, "unexpected character, expected a value");
      return 1;
    }
  }

  return 0;
}

static int
pp_process_char(JsonPushParser* pp, JSContext* ctx, uint8_t c) {
  if(pp->resyncing)
    return pp_resync_char(pp, ctx, c);

  switch(pp->tok_kind) {
    case PP_TOK_STRING: return pp_string_char(pp, ctx, c);
    case PP_TOK_NUMBER: return pp_number_char(pp, ctx, c);
    case PP_TOK_TRUE:
    case PP_TOK_FALSE:
    case PP_TOK_NULL: return pp_literal_char(pp, ctx, c);
    default: break;
  }

  return pp_structural_char(pp, ctx, c);
}

static void
pp_write_bytes(JsonPushParser* pp, JSContext* ctx, const uint8_t* data, size_t len) {
  size_t i = 0;

  while(i < len) {
    if(pp_process_char(pp, ctx, data[i]))
      continue;

    i++;
  }
}

static JSValue
js_json_pushparser_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonPushParser* pp;
  InputBuffer input;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_json_pushparser_class_id)))
    return JS_EXCEPTION;

  input = js_input_chars(ctx, argv[0]);

  if(input.data == 0) {
    JS_ThrowReferenceError(ctx, "JsonPushParser.write(): expecting buffer or string");
    return JS_EXCEPTION;
  }

  location_count(pp->loc, input.data, input.size);

  pp->had_error = FALSE;
  pp_write_bytes(pp, ctx, input.data, input.size);

  inputbuffer_free(&input, ctx);

  return pp->had_error ? JS_EXCEPTION : JS_UNDEFINED;
}

static JSValue
js_json_pushparser_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_json_pushparser_class_id)))
    return JS_EXCEPTION;

  if(pp->tok_kind == PP_TOK_NUMBER)
    pp_finish_token(pp, ctx);

  if(!pp->done) {
    char* loc = location_tostring(pp->loc, ctx);

    JS_ThrowSyntaxError(ctx, "%s%sunexpected end of input", loc && *loc ? loc : "", loc && *loc ? ": " : "");

    if(loc)
      js_free(ctx, loc);

    return JS_EXCEPTION;
  }

  return JS_UNDEFINED;
}

enum {
  JSON_PUSHPARSER_ROOT,
  JSON_PUSHPARSER_PATH,
  JSON_PUSHPARSER_LOCATION,
};

static JSValue
js_json_pushparser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonPushParser* pp;
  JSValue ret = JS_UNDEFINED;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_json_pushparser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_PUSHPARSER_ROOT: ret = JS_DupValue(ctx, pp->root); break;
    case JSON_PUSHPARSER_PATH: ret = pp_path(pp, ctx); break;
    case JSON_PUSHPARSER_LOCATION: ret = js_location_wrap(ctx, pp->loc); break;
  }

  return ret;
}

static JSValue
js_json_pushparser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  JsonPushParser* pp;

  if(!(pp = js_mallocz(ctx, sizeof(JsonPushParser))))
    return JS_EXCEPTION;

  pp->ctx = ctx;
  vector_init(&pp->stack, ctx);
  dbuf_init2(&pp->token, 0, 0);
  pp->root = JS_UNDEFINED;
  pp->state = PP_EXPECT_VALUE;

  if(!(pp->loc = location_new(ctx))) {
    dbuf_free(&pp->token);
    vector_free(&pp->stack);
    js_free(ctx, pp);
    return JS_EXCEPTION;
  }

  location_zero(pp->loc);

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, json_pushparser_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_json_pushparser_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    location_free(pp->loc, JS_GetRuntime(ctx));
    dbuf_free(&pp->token);
    vector_free(&pp->stack);
    js_free(ctx, pp);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, pp);
  return obj;
}

static void
js_json_pushparser_finalizer(JSRuntime* rt, JSValue val) {
  JsonPushParser* pp;

  if((pp = JS_GetOpaque(val, js_json_pushparser_class_id))) {
    PPFrame* it;

    vector_foreach_t(&pp->stack, it) {
      if(it->key != JS_ATOM_NULL)
        JS_FreeAtomRT(rt, it->key);
      JS_FreeValueRT(rt, it->obj);
    }

    vector_free(&pp->stack);
    dbuf_free(&pp->token);
    JS_FreeValueRT(rt, pp->root);

    if(pp->loc)
      location_free(pp->loc, rt);

    js_free_rt(rt, pp);
  }
}

static const JSCFunctionListEntry js_json_pushparser_proto_funcs[] = {
    JS_CFUNC_DEF("write", 1, js_json_pushparser_write),
    JS_CFUNC_DEF("close", 0, js_json_pushparser_close),
    JS_CGETSET_MAGIC_FLAGS_DEF("root", js_json_pushparser_get, 0, JSON_PUSHPARSER_ROOT, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("path", js_json_pushparser_get, 0, JSON_PUSHPARSER_PATH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("location", js_json_pushparser_get, 0, JSON_PUSHPARSER_LOCATION, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonPushParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_json_pushparser_class = {
    .class_name = "JsonPushParser",
    .finalizer = js_json_pushparser_finalizer,
};

/* ---------------------------------------------------------------------- */
/* JsonSerializer: pull (.read(n)) serializer traversing via              */
/* property_recursion_*(), producing only as much text as requested.      */
/* ---------------------------------------------------------------------- */

typedef struct {
  uint8_t* dst;
  size_t cap;
  size_t pos;
} CappedBuf;

/* All-or-nothing writer over a fixed caller-supplied buffer: a write either fits
 * entirely (returns len) or is refused whole (returns 0, "no room yet"). Used as the
 * zero-copy destination for JsonSerializer.read(buffer): bytes go straight from
 * stringification into the caller's buffer, no intermediate copy. */
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
  DynBuf out;   /* internal buffer, only used as the destination for .read(n) (string mode) */
  DynBuf space;
  size_t out_pos;
  int32_t indent;
  BOOL finished;
  BOOL started;  /* root's opening bracket/primitive already emitted */
  BOOL is_primitive;
  BOOL error;    /* fatal (e.g. OOM) */
  BOOL blocked;  /* destination ran out of room this attempt; retry later */
  size_t skip;   /* bytes of the (deterministic) replay to discard: already delivered previously */
  size_t delivered; /* bytes actually forwarded to the destination during the current step attempt */
  JSValue root;
  Location* loc; /* advanced explicitly via location_count() on bytes actually delivered, not via a wrapping Writer:
                    write_location() (stream-utils.c) treats any non-full write from its parent as a hard error,
                    which would turn a benign "destination full, retry" (0) signal into a fatal one */
  Writer out_writer;  /* wraps js->out, used for .read(n) */
  Writer dest_writer;  /* swappable slot: out_writer for .read(n), a capped writer for .read(buf) */
  Writer skip_writer;  /* outermost: discards the first `skip` bytes of a step's replay */
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

/* Checked wrappers: on a blocked/error write, set js->blocked/js->error and return FALSE
 * so the caller can bail out before mutating any traversal state. */

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
  BOOL is_array;
  JSValue val;
  BOOL is_container;

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

/* Wraps json_serializer_step_inner() with the skip/replay bookkeeping: a step's writes are
 * deterministic given unchanged traversal state, so on a blocked attempt we fold whatever was
 * newly delivered into js->skip (discarded on the next replay) instead of losing or duplicating
 * it; a clean step clears js->skip since it now applies to whatever step comes next. */
static void
json_serializer_step(JsonSerializer* js, JSContext* ctx) {
  size_t skip_before = js->skip; /* write_skip() decrements js->skip live as it discards the
                                     replayed prefix, so by the end of the attempt it no longer
                                     reflects what was already-delivered before this attempt */

  js->delivered = 0;
  json_serializer_step_inner(js, ctx);

  if(js->blocked)
    js->skip = skip_before + js->delivered;
  else if(!js->error)
    js->skip = 0;
}

static JSValue
js_json_serializer_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonSerializer* js;
  BOOL is_buf;

  if(!(js = JS_GetOpaque2(ctx, this_val, js_json_serializer_class_id)))
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

  {
    int64_t n;

    if(JS_ToInt64(ctx, &n, argc > 0 ? argv[0] : JS_UNDEFINED))
      return JS_EXCEPTION;

    if(n < 0)
      return JS_ThrowRangeError(ctx, "size must not be negative");

    js->dest_writer = js->out_writer;

    while(!js->finished && !js->error && !js->blocked && (int64_t)(js->out.size - js->out_pos) < n)
      json_serializer_step(js, ctx);

    if(js->error) {
      property_recursion_free(&js->stack, JS_GetRuntime(ctx));
      return JS_EXCEPTION;
    }

    {
      size_t avail = js->out.size - js->out_pos;
      size_t take = (size_t)n < avail ? (size_t)n : avail;
      JSValue ret = JS_NewStringLen(ctx, (const char*)js->out.buf + js->out_pos, take);

      location_count(js->loc, js->out.buf + js->out_pos, take);
      js->out_pos += take;

      if(js->out_pos == js->out.size) {
        js->out.size = 0;
        js->out_pos = 0;
      } else if(js->out_pos > 0) {
        memmove(js->out.buf, js->out.buf + js->out_pos, js->out.size - js->out_pos);
        js->out.size -= js->out_pos;
        js->out_pos = 0;
      }

      return ret;
    }
  }
}

enum {
  JSON_SERIALIZER_LOCATION,
};

static JSValue
js_json_serializer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonSerializer* js;
  JSValue ret = JS_UNDEFINED;

  if(!(js = JS_GetOpaque2(ctx, this_val, js_json_serializer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_SERIALIZER_LOCATION: ret = js_location_wrap(ctx, js->loc); break;
  }

  return ret;
}

static JSValue
js_json_serializer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  JsonSerializer* js;
  JSValueConst root = argc > 0 ? argv[0] : JS_UNDEFINED;

  if(!(js = js_mallocz(ctx, sizeof(JsonSerializer))))
    return JS_EXCEPTION;

  js->ctx = ctx;
  vector_init(&js->stack, ctx);
  dbuf_init2(&js->out, 0, 0);
  dbuf_init2(&js->space, 0, 0);

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

  obj = JS_NewObjectProtoClass(ctx, proto, js_json_serializer_class_id);
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

static void
js_json_serializer_finalizer(JSRuntime* rt, JSValue val) {
  JsonSerializer* js;

  if((js = JS_GetOpaque(val, js_json_serializer_class_id))) {
    property_recursion_free(&js->stack, rt);
    dbuf_free(&js->out);
    dbuf_free(&js->space);
    JS_FreeValueRT(rt, js->root);

    if(js->loc)
      location_free(js->loc, rt);

    js_free_rt(rt, js);
  }
}

static const JSCFunctionListEntry js_json_serializer_proto_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_json_serializer_read),
    JS_CGETSET_MAGIC_FLAGS_DEF("location", js_json_serializer_get, 0, JSON_SERIALIZER_LOCATION, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonSerializer", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_json_serializer_class = {
    .class_name = "JsonSerializer",
    .finalizer = js_json_serializer_finalizer,
};

static JSValue
js_json_parser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  JsonParser* parser;
  JSValueConst input = argc > 0 ? argv[0] : JS_UNDEFINED;
  Reader reader;
  const char* filename = 0;

  /* input is either a buffer (string/ArrayBuffer/TypedArray), a pull function
   * called as fn(buf, len) -> bytesRead, or an object exposing such a function
   * as its "read" method (called with the object as `this`). */
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
  } else {
    reader = reader_from_jsbuf(ctx, input);
  }

  if(argc > 1)
    filename = JS_ToCString(ctx, argv[1]);

  parser = json_new(reader, filename, ctx);

  if(filename)
    JS_FreeCString(ctx, filename);

  if(!parser) {
    reader_free(&reader);
    return JS_EXCEPTION;
  }

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, json_parser_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_json_parser_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    json_free(parser, JS_GetRuntime(ctx));
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, parser);

  return obj;
}

enum {
  JSON_PARSER_PARSE,
};

static JSValue
js_json_parser_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JsonParser* parser;
  JSValue ret = JS_UNDEFINED;

  if(!(parser = JS_GetOpaque2(ctx, this_val, js_json_parser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_PARSER_PARSE: {
      int type = json_parse(parser);

      if(type == JSON_ERROR) {
        char* loc = location_tostring(parser->loc, ctx);

        JS_ThrowSyntaxError(ctx, "%s%s%s", loc && *loc ? loc : "", loc && *loc ? ": " : "", parser->error ? parser->error : "parse error");

        if(loc)
          js_free(ctx, loc);

        return JS_EXCEPTION;
      }

      ret = JS_NewString(ctx,
                         (const char* const[]){
                             "NEED_DATA",
                             "NONE",
                             "OBJECT",
                             "OBJECT_END",
                             "ARRAY",
                             "ARRAY_END",
                             "KEY",
                             "STRING",
                             "TRUE",
                             "FALSE",
                             "NULL",
                             "NUMBER",
                         }[type + 2]);
      break;
    }
  }

  return ret;
}

enum {
  JSON_PARSER_CALLBACK,
  JSON_PARSER_POS,
  JSON_PARSER_TOKEN,
  JSON_PARSER_STATE,
  JSON_PARSER_DEPTH,
  JSON_PARSER_LOCATION,
};

static JSValue
js_json_parser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonParser* parser;
  JSValue ret = JS_UNDEFINED;

  if(!(parser = JS_GetOpaque2(ctx, this_val, js_json_parser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_PARSER_CALLBACK: {
      ret = js_value_mkobj2(ctx, parser->opaque);
      break;
    }

    case JSON_PARSER_POS: {
      ret = JS_NewUint32(ctx, parser->pos);
      break;
    }

    case JSON_PARSER_TOKEN: {
      ret = dbuf_tostring(&parser->token, ctx);
      break;
    }

    case JSON_PARSER_STATE: {
      ret = JS_NewInt32(ctx, parser->state);
      break;
    }

    case JSON_PARSER_DEPTH: {
      ret = JS_NewUint32(ctx, parser->stack.len);
      break;
    }

    case JSON_PARSER_LOCATION: {
      ret = js_location_wrap(ctx, parser->loc);
      break;
    }
  }

  return ret;
}

static void
js_json_parser_callback(JsonParser* parser, JsonValueType type, void* ptr) {
  struct js_json_parser_opaque* op = parser->opaque;
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
js_json_parser_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JsonParser* parser;
  JSValue ret = JS_UNDEFINED;

  if(!(parser = JS_GetOpaque2(ctx, this_val, js_json_parser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_PARSER_CALLBACK: {
      struct js_json_parser_opaque* op;

      if(!JS_IsFunction(ctx, value))
        return JS_ThrowTypeError(ctx, "value must be a function");

      if(parser->opaque) {
        op = parser->opaque;
        js_freeobj(ctx, op->obj);
      }

      op = parser->opaque ? parser->opaque : js_malloc(ctx, sizeof(struct js_json_parser_opaque));

      if(op) {
        *op = (struct js_json_parser_opaque){ctx, js_value_obj(this_val), js_value_obj2(ctx, value)};

        parser->callback = js_json_parser_callback;
        parser->opaque = op;
      }

      break;
    }
  }

  return ret;
}

static void
js_json_parser_finalizer(JSRuntime* rt, JSValue obj) {
  JsonParser* parser;

  if((parser = JS_GetOpaque(obj, js_json_parser_class_id)))
    json_free(parser, rt);
}

static const JSCFunctionListEntry js_json_parser_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("parse", 0, js_json_parser_method, JSON_PARSER_PARSE),
    JS_CGETSET_MAGIC_FLAGS_DEF("pos", js_json_parser_get, 0, JSON_PARSER_POS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("token", js_json_parser_get, 0, JSON_PARSER_TOKEN, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("state", js_json_parser_get, 0, JSON_PARSER_STATE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("depth", js_json_parser_get, 0, JSON_PARSER_DEPTH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("location", js_json_parser_get, 0, JSON_PARSER_LOCATION, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("callback", js_json_parser_get, js_json_parser_set, JSON_PARSER_CALLBACK),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_json_parser_class = {
    .class_name = "JsonParser",
    .finalizer = js_json_parser_finalizer,
};

static int
js_json_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_json_parser_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_json_parser_class_id, &js_json_parser_class);

  json_parser_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, json_parser_proto, js_json_parser_proto_funcs, countof(js_json_parser_proto_funcs));

  json_parser_ctor = JS_NewCFunction2(ctx, js_json_parser_constructor, "JsonParser", 1, JS_CFUNC_constructor, 0);
  JS_SetClassProto(ctx, js_json_parser_class_id, json_parser_proto);
  JS_SetConstructor(ctx, json_parser_ctor, json_parser_proto);

  if(js_location_class_id == 0)
    js_location_init(ctx, 0);

  JS_NewClassID(&js_json_pushparser_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_json_pushparser_class_id, &js_json_pushparser_class);

  json_pushparser_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, json_pushparser_proto, js_json_pushparser_proto_funcs, countof(js_json_pushparser_proto_funcs));

  json_pushparser_ctor = JS_NewCFunction2(ctx, js_json_pushparser_constructor, "JsonPushParser", 0, JS_CFUNC_constructor, 0);
  JS_SetClassProto(ctx, js_json_pushparser_class_id, json_pushparser_proto);
  JS_SetConstructor(ctx, json_pushparser_ctor, json_pushparser_proto);

  JS_NewClassID(&js_json_serializer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_json_serializer_class_id, &js_json_serializer_class);

  json_serializer_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, json_serializer_proto, js_json_serializer_proto_funcs, countof(js_json_serializer_proto_funcs));

  json_serializer_ctor = JS_NewCFunction2(ctx, js_json_serializer_constructor, "JsonSerializer", 1, JS_CFUNC_constructor, 0);
  JS_SetClassProto(ctx, js_json_serializer_class_id, json_serializer_proto);
  JS_SetConstructor(ctx, json_serializer_ctor, json_serializer_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "JsonParser", json_parser_ctor);
    JS_SetModuleExport(ctx, m, "JsonPushParser", json_pushparser_ctor);
    JS_SetModuleExport(ctx, m, "JsonSerializer", json_serializer_ctor);
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
    JS_AddModuleExport(ctx, m, "JsonParser");
    JS_AddModuleExport(ctx, m, "JsonPushParser");
    JS_AddModuleExport(ctx, m, "JsonSerializer");
    JS_AddModuleExportList(ctx, m, js_json_funcs, countof(js_json_funcs));
  }

  return m;
}
