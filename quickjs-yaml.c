#include "defines.h"
#include "utils.h"
#include "buffer-utils.h"
#include "yaml/ywrite.h"
#include <quickjs.h>
#include <cutils.h>

/* quickjs-yaml.c: yaml module, parse(), stringify(), write().
 * depends on: cyaml (parse, when HAVE_LIBCYAML), yaml/ywrite.h (write, stringify).
 * rule: without cyaml the module still loads, only parse() is missing. */

/**
 * \defgroup quickjs-yaml quickjs-yaml: YAML parser and writer
 * @{
 */

#ifdef HAVE_LIBCYAML
#include <cyaml.h>
#include <string.h>
#include <stdlib.h>

#define YAML_MAX_DEPTH 1000

/* an anchored node already converted: an alias reuses the same JS value */
typedef struct {
  const cyaml_node_t* node;
  JSValue val;
} YamlAnchor;

typedef struct {
  JSContext* ctx;
  const cyaml_doc_t* doc;
  DynBuf anchors; /* YamlAnchor[], values owned */
} YamlParse;

static JSValue yaml_to_js(YamlParse*, const cyaml_node_t*, int);

/* a scalar that is not plain, or has a tag, is a string: "123", !!str 123 */
static JSValue
yaml_scalar_to_js(YamlParse* p, const cyaml_node_t* n) {
  JSContext* ctx = p->ctx;

  if(n->style == CYAML_PLAIN && !n->tag.len) {
    int64_t i;
    double d;
    bool b;

    switch(cyaml_scalar_kind(p->doc, n)) {
      case CYAML_KIND_NULL: return JS_NULL;
      case CYAML_KIND_BOOL:
        if(cyaml_as_bool(p->doc, n, &b))
          return JS_NewBool(ctx, b);

        break;
      case CYAML_KIND_INT:
        if(cyaml_as_int(p->doc, n, &i))
          return JS_NewInt64(ctx, i);
      /* fallthrough: too big for int64 */
      case CYAML_KIND_FLOAT:
        if(cyaml_as_float(p->doc, n, &d))
          return JS_NewFloat64(ctx, d);

        break;
      default: break;
    }
  }

  char* s = cyaml_scalar_str(p->doc, n);

  if(!s)
    return JS_ThrowOutOfMemory(ctx);

  JSValue ret = JS_NewString(ctx, s);

  free(s);
  return ret;
}

/* exception pending: a key that is not a scalar has no JS property name */
static char*
yaml_key_to_cstr(YamlParse* p, const cyaml_node_t* key) {
  if(key->type == CYAML_NULL)
    return strdup("null");

  if(key->type == CYAML_SCALAR)
    return cyaml_scalar_str(p->doc, key);

  JS_ThrowTypeError(p->ctx, "yaml.parse: mapping key must be a scalar");
  return NULL;
}

static JSValue
yaml_to_js(YamlParse* p, const cyaml_node_t* n, int depth) {
  JSContext* ctx = p->ctx;
  JSValue ret = JS_UNDEFINED;
  YamlAnchor* a = (YamlAnchor*)p->anchors.buf;
  size_t i, na = p->anchors.size / sizeof(YamlAnchor);

  if(!n)
    return JS_NULL;

  if(depth > YAML_MAX_DEPTH)
    return JS_ThrowRangeError(ctx, "yaml.parse: nesting too deep");

  if(n->type == CYAML_ALIAS)
    n = n->alias.target;

  if(!n)
    return JS_ThrowSyntaxError(ctx, "yaml.parse: unresolved alias");

  for(i = 0; i < na; i++)
    if(a[i].node == n)
      return JS_DupValue(ctx, a[i].val);

  switch(n->type) {
    case CYAML_NULL: return JS_NULL;
    case CYAML_SCALAR: ret = yaml_scalar_to_js(p, n); break;
    case CYAML_SEQ:
    case CYAML_MAP: {
      BOOL is_seq = n->type == CYAML_SEQ;
      uint32_t j, len = is_seq ? cyaml_seq_len(n) : cyaml_map_len(n);

      ret = is_seq ? JS_NewArray(ctx) : JS_NewObject(ctx);

      if(JS_IsException(ret))
        return ret;

      /* registered before the children, so an alias to an ancestor closes the cycle */
      if(n->anchor.len) {
        YamlAnchor anchor = {n, JS_DupValue(ctx, ret)};

        dbuf_put(&p->anchors, (const uint8_t*)&anchor, sizeof(anchor));
      }

      for(j = 0; j < len; j++) {
        JSValue child;
        int r;

        if(is_seq) {
          child = yaml_to_js(p, cyaml_seq_get(n, j), depth + 1);
          r = JS_IsException(child) ? -1 : JS_DefinePropertyValueUint32(ctx, ret, j, child, JS_PROP_C_W_E);
        } else {
          cyaml_pair_t* pair = cyaml_map_at(n, j);
          char* key = yaml_key_to_cstr(p, pair->key);

          if(!key) {
            JS_FreeValue(ctx, ret);
            return JS_EXCEPTION;
          }

          child = yaml_to_js(p, pair->val, depth + 1);
          r = JS_IsException(child) ? -1 : JS_DefinePropertyValueStr(ctx, ret, key, child, JS_PROP_C_W_E);
          free(key);
        }

        if(r < 0) {
          JS_FreeValue(ctx, ret);
          return JS_EXCEPTION;
        }
      }

      return ret;
    }
    default: return JS_ThrowSyntaxError(ctx, "yaml.parse: unsupported node");
  }

  /* an anchored scalar is registered too */
  if(!JS_IsException(ret) && n->anchor.len) {
    YamlAnchor anchor = {n, JS_DupValue(ctx, ret)};

    dbuf_put(&p->anchors, (const uint8_t*)&anchor, sizeof(anchor));
  }

  return ret;
}

/* Bun.YAML.parse(text): one document gives its value, several give an array.
 * An empty stream gives null. A document with anchors shares the aliased value.
 *
 *   parse('a: 1\nb: [x, true, ~]\n')   {a: 1, b: ['x', true, null]}
 *
 * throws SyntaxError with the position, TypeError if text is not a string. */
static JSValue
js_yaml_parse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  size_t len;
  const char* src;
  cyaml_stream_t* stream;
  cyaml_error_t err;
  cyaml_opts_t opts = CYAML_OPTS_DEFAULT;
  YamlParse p = {ctx, NULL, {0}};
  JSValue ret = JS_UNDEFINED;
  uint32_t i, count;

  if(argc < 1 || !JS_IsString(argv[0]))
    return JS_ThrowTypeError(ctx, "yaml.parse: argument 1 must be a string");

  if(!(src = JS_ToCStringLen(ctx, &len, argv[0])))
    return JS_EXCEPTION;

  if(!(stream = cyaml_parse_stream(src, len, &opts, &err))) {
    ret = JS_ThrowSyntaxError(ctx, "yaml.parse: %s (line %u, column %u)", err.msg, err.span.start_line,
                              err.span.start_col);
    goto end;
  }

  dbuf_init_ctx(ctx, &p.anchors);
  count = cyaml_stream_count(stream);

  if(count == 0) {
    ret = JS_NULL;
  } else if(count == 1) {
    p.doc = cyaml_stream_doc(stream, 0);
    ret = yaml_to_js(&p, cyaml_root(p.doc), 0);
  } else {
    ret = JS_NewArray(ctx);

    for(i = 0; i < count && !JS_IsException(ret); i++) {
      JSValue doc;

      p.doc = cyaml_stream_doc(stream, i);
      doc = yaml_to_js(&p, cyaml_root(p.doc), 0);

      if(JS_IsException(doc) || JS_DefinePropertyValueUint32(ctx, ret, i, doc, JS_PROP_C_W_E) < 0) {
        JS_FreeValue(ctx, ret);
        ret = JS_EXCEPTION;
      }

      /* anchors do not cross a document boundary */
      for(size_t k = 0; k < p.anchors.size / sizeof(YamlAnchor); k++)
        JS_FreeValue(ctx, ((YamlAnchor*)p.anchors.buf)[k].val);

      p.anchors.size = 0;
    }
  }

  for(size_t k = 0; k < p.anchors.size / sizeof(YamlAnchor); k++)
    JS_FreeValue(ctx, ((YamlAnchor*)p.anchors.buf)[k].val);

  dbuf_free(&p.anchors);
  cyaml_stream_free(stream);
end:
  JS_FreeCString(ctx, src);
  return ret;
}

#endif /* HAVE_LIBCYAML */

/* the text of val as YAML, always through ywrite: the cyaml emitter writes a boolean as a string */
static JSValue
js_yaml_dump(JSContext* ctx, JSValueConst val, int32_t indent) {
  DynBuf db;

  if(indent <= 0)
    indent = 2;

  dbuf_init_ctx(ctx, &db);

  if(ywrite_document(ctx, &db, val, indent)) {
    dbuf_free(&db);
    return JS_EXCEPTION;
  }

  return dbuf_tostring_free(&db, ctx);
}

/* write(value, indent?): value as a block YAML string, indent spaces per level, default 2.
 *
 *   write({a: [1, 2]})   'a:\n  - 1\n  - 2\n'
 *
 * throws TypeError for a function, symbol or bigint. */
static JSValue
js_yaml_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t indent = 2;

  if(argc > 1 && !JS_IsUndefined(argv[1]))
    JS_ToInt32(ctx, &indent, argv[1]);

  return js_yaml_dump(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, indent);
}

/* Bun.YAML.stringify(value, replacer, space): as write(), space is a number of spaces or a
 * string whose length is used. Always block style. A replacer is not supported.
 *
 * throws TypeError for a replacer other than null or undefined. */
static JSValue
js_yaml_stringify(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t indent = 2;

  if(argc > 1 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]))
    return JS_ThrowTypeError(ctx, "yaml.stringify: replacer is not supported");

  if(argc > 2) {
    if(JS_IsString(argv[2])) {
      size_t len;
      const char* s = JS_ToCStringLen(ctx, &len, argv[2]);

      if(!s)
        return JS_EXCEPTION;

      indent = len > 10 ? 10 : (int32_t)len;
      JS_FreeCString(ctx, s);
    } else if(JS_IsNumber(argv[2])) {
      JS_ToInt32(ctx, &indent, argv[2]);

      if(indent > 10)
        indent = 10;
    }
  }

  return js_yaml_dump(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, indent);
}

static const JSCFunctionListEntry js_yaml_funcs[] = {
#ifdef HAVE_LIBCYAML
    JS_CFUNC_DEF("parse", 1, js_yaml_parse),
#endif
    JS_CFUNC_DEF("stringify", 1, js_yaml_stringify),
    JS_CFUNC_DEF("write", 1, js_yaml_write),
};

static int
js_yaml_init(JSContext* ctx, JSModuleDef* m) {
  JS_SetModuleExportList(ctx, m, js_yaml_funcs, countof(js_yaml_funcs));
  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_yaml
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_yaml_init)))
    JS_AddModuleExportList(ctx, m, js_yaml_funcs, countof(js_yaml_funcs));

  return m;
}

/**
 * @}
 */
