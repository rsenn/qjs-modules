/* xwrite-golden.c: runs XmlWriter (src/xml/xwrite.c) over the XMLWriter and XMLTreeSerializer cases of
 * tests/fixtures/xml-write-golden.json and compares with the recorded output.
 *
 * ```sh
 * B=build/$(cc -dumpmachine)
 * cc -I include -I /usr/local/include/quickjs tests/tools/xwrite-golden.c $B/libmodules.a \
 *    /usr/local/lib/$(cc -dumpmachine)/libquickjs.a -lm -lpthread -ldl -o /tmp/xwrite-golden
 * /tmp/xwrite-golden tests/fixtures/xml-write-golden.json
 * ```
 *
 * known differences (the visible changes of TODO.md Tier 20 step 3c) are counted, not failed:
 * escaping in `special`, value-less `boolattr`, and the newline XMLWriter writes at level 0
 * with a negative indent. exit status 1 on any other difference. */

#include "xml/xwrite.h"
#include <quickjs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char*
S(JSContext* c, JSValueConst v, size_t* n) {
  return JS_ToCStringLen(c, n, v);
}

static int
walk(JSContext* ctx, XmlWriter* wr, JSValueConst list) {
  uint32_t len;
  JSValue lv = JS_GetPropertyStr(ctx, list, "length");
  JS_ToUint32(ctx, &len, lv);
  JS_FreeValue(ctx, lv);
  for(uint32_t i = 0; i < len; i++) {
    JSValue v = JS_GetPropertyUint32(ctx, list, i);
    if(JS_IsString(v)) {
      size_t n;
      const char* s = S(ctx, v, &n);
      if(xwrite_text(wr, s, n) < 0)
        return -1;
      JS_FreeCString(ctx, s);
    } else if(JS_IsObject(v)) {
      size_t tn;
      JSValue t = JS_GetPropertyStr(ctx, v, "tagName");
      const char* tag = JS_IsString(t) ? S(ctx, t, &tn) : 0;
      if(tag && tn) {
        if(tag[0] == '!' || tag[0] == '?')
          xwrite_markup(wr, tag, tn);
        else {
          xwrite_element_start(wr, tag, tn);
          JSValue at = JS_GetPropertyStr(ctx, v, "attributes");
          if(JS_IsObject(at)) {
            JSPropertyEnum* tab;
            uint32_t cnt;
            if(!JS_GetOwnPropertyNames(ctx, &tab, &cnt, at, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
              for(uint32_t k = 0; k < cnt; k++) {
                size_t kn, vn;
                const char* key = JS_AtomToCStringLen(ctx, &kn, tab[k].atom);
                JSValue val = JS_GetProperty(ctx, at, tab[k].atom);
                if(JS_IsBool(val) && JS_ToBool(ctx, val))
                  xwrite_attribute(wr, key, kn, 0, 0);
                else {
                  const char* vs = S(ctx, val, &vn);
                  xwrite_attribute(wr, key, kn, vs, vn);
                  JS_FreeCString(ctx, vs);
                }
                JS_FreeValue(ctx, val);
                JS_FreeCString(ctx, key);
                JS_FreeAtom(ctx, tab[k].atom);
              }
              js_free(ctx, tab);
            }
          }
          JS_FreeValue(ctx, at);
          JSValue ch = JS_GetPropertyStr(ctx, v, "children");
          if(JS_IsArray(ctx, ch))
            walk(ctx, wr, ch);
          xwrite_element_end(wr, tag, tn);
          JS_FreeValue(ctx, ch);
        }
      }
      if(tag)
        JS_FreeCString(ctx, tag);
      JS_FreeValue(ctx, t);
    }
    JS_FreeValue(ctx, v);
  }
  return 0;
}

int
main(int argc, char** argv) {
  JSRuntime* rt = JS_NewRuntime();
  JSContext* ctx = JS_NewContext(rt);
  FILE* f = fopen(argv[1], "rb");
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  rewind(f);
  char* buf = malloc(n + 1);
  fread(buf, 1, n, f);
  buf[n] = 0;
  fclose(f);
  JSValue root = JS_ParseJSON(ctx, buf, n, argv[1]);
  JSValue cases = JS_GetPropertyStr(ctx, root, "cases");
  uint32_t cnt;
  JSValue lv = JS_GetPropertyStr(ctx, cases, "length");
  JS_ToUint32(ctx, &cnt, lv);
  int ok = 0, bad = 0, known = 0;
  for(uint32_t i = 0; i < cnt; i++) {
    JSValue c = JS_GetPropertyUint32(ctx, cases, i);
    JSValue kind = JS_GetPropertyStr(ctx, c, "kind"), name = JS_GetPropertyStr(ctx, c, "name"), input = JS_GetPropertyStr(ctx, c, "input"),
            opts = JS_GetPropertyStr(ctx, c, "opts"), ex = JS_GetPropertyStr(ctx, c, "expect");
    const char *k = JS_ToCString(ctx, kind), *nm = JS_ToCString(ctx, name);
    if(strcmp(k, "write") && JS_IsString(ex)) {
      DynBuf db;
      dbuf_init(&db);
      XmlWriter wr = {0};
      wr.writer = writer_from_dynbuf(&db);
      xwrite_init(&wr, NULL, NULL);
      wr.opts.layout = XW_COMPACT;
      if(!strcmp(k, "writer")) {
        JSValue ind = JS_GetPropertyStr(ctx, opts, "indent");
        int32_t indent = 0;
        JS_ToInt32(ctx, &indent, ind);
        JS_FreeValue(ctx, ind);
        if(indent >= 0) {
          wr.opts.layout = XW_PRETTY;
          wr.opts.indent = indent;
        }
        JSValue ops = JS_GetPropertyStr(ctx, input, "ops");
        uint32_t on;
        JSValue ol = JS_GetPropertyStr(ctx, ops, "length");
        JS_ToUint32(ctx, &on, ol);
        for(uint32_t j = 0; j < on; j++) {
          JSValue op = JS_GetPropertyUint32(ctx, ops, j), o0 = JS_GetPropertyUint32(ctx, op, 0), o1 = JS_GetPropertyUint32(ctx, op, 1),
                  o2 = JS_GetPropertyUint32(ctx, op, 2);
          size_t n1 = 0, n2 = 0;
          const char *o = JS_ToCString(ctx, o0), *a = JS_IsUndefined(o1) ? 0 : S(ctx, o1, &n1);
          if(!strcmp(o, "start"))
            xwrite_element_start(&wr, a, n1);
          else if(!strcmp(o, "end"))
            xwrite_element_end(&wr, a, n1);
          else if(!strcmp(o, "text"))
            xwrite_text(&wr, a, n1);
          else if(JS_IsUndefined(o2) || (JS_IsBool(o2) && JS_ToBool(ctx, o2)))
            xwrite_attribute(&wr, a, n1, 0, 0);
          else {
            const char* b = S(ctx, o2, &n2);
            xwrite_attribute(&wr, a, n1, b, n2);
            JS_FreeCString(ctx, b);
          }
          JS_FreeCString(ctx, o);
          if(a)
            JS_FreeCString(ctx, a);
          JS_FreeValue(ctx, op);
          JS_FreeValue(ctx, o0);
          JS_FreeValue(ctx, o1);
          JS_FreeValue(ctx, o2);
        }
      } else {
        JSValue tree = JS_GetPropertyStr(ctx, input, "tree");
        walk(ctx, &wr, tree);
        JS_FreeValue(ctx, tree);
      }
      size_t en;
      const char* exs = S(ctx, ex, &en);
      int same = en == db.size && !memcmp(exs, db.buf, en);
      if(same)
        ok++;
      else if(strstr(nm, "special") || strstr(nm, "boolattr") || strstr(nm, "indent -1")) {
        known++;
      } else {
        bad++;
        printf("DIFF %s\n  want %.*s\n  got  %.*s\n", nm, (int)en, exs, (int)db.size, db.buf);
      }
      JS_FreeCString(ctx, exs);
    }
    JS_FreeCString(ctx, k);
    JS_FreeCString(ctx, nm);
  }
  printf("ok %d, known 3c differences %d, bad %d\n", ok, known, bad);
  return bad != 0;
}
