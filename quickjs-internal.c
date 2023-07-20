#include "utils.h"
#include "char-utils.h"
#include "vector.h"
#include "quickjs-internal.h"

JSValue
js_std_file(JSContext* ctx, FILE* f) {
  JSClassID class_id = js_class_find(ctx, "FILE");
  JSValue obj, proto = JS_GetClassProto(ctx, class_id);
  JSSTDFile* file;

  file = js_malloc(ctx, sizeof(JSSTDFile));
  *file = (JSSTDFile){0, TRUE, FALSE};

  file->f = f;

  obj = JS_NewObjectProtoClass(ctx, proto, class_id);

  JS_SetOpaque(obj, file);

  return obj;
}

struct list_head*
js_modules_list(JSContext* ctx) {
  return &ctx->loaded_modules;
}

JSModuleDef**
js_modules_vector(JSContext* ctx) {
  struct list_head* el;
  Vector vec = VECTOR(ctx);
  JSModuleDef* m;

  list_for_each(el, js_modules_list(ctx)) {
    m = list_entry(el, JSModuleDef, link);

    vector_push(&vec, m);
  }

  m = NULL;
  vector_push(&vec, m);

  return vector_begin(&vec);
}

JSValue
js_modules_entries(JSContext* ctx, JSValueConst this_val, int magic) {
  struct list_head* el;
  JSValue ret = JS_NewArray(ctx);
  uint32_t i = 0;
  list_for_each(el, &ctx->loaded_modules) {
    JSModuleDef* m = list_entry(el, JSModuleDef, link);
    // const char* name = module_namecstr(ctx, m);
    JSValue entry = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, entry, 0, JS_AtomToValue(ctx, m->module_name));
    JS_SetPropertyUint32(ctx, entry, 1, magic ? module_entry(ctx, m) : module_value(ctx, m));

    if(1 /*str[0] != '<'*/)
      JS_SetPropertyUint32(ctx, ret, i++, entry);
    else
      JS_FreeValue(ctx, entry);

    // JS_FreeCString(ctx, name);
  }

  return ret;
}

JSValue
js_modules_object(JSContext* ctx, JSValueConst this_val, int magic) {
  struct list_head* it;
  JSValue obj = JS_NewObject(ctx);

  list_for_each(it, &ctx->loaded_modules) {
    JSModuleDef* m = list_entry(it, JSModuleDef, link);
    const char* name = module_namecstr(ctx, m);
    JSValue entry = magic ? module_entry(ctx, m) : module_value(ctx, m);

    if(1 /*str[0] != '<'*/)
      JS_SetPropertyStr(ctx, obj, basename(name), entry);
    else
      JS_FreeValue(ctx, entry);

    JS_FreeCString(ctx, name);
  }

  return obj;
}

JSModuleDef*
js_module_find_fwd(JSContext* ctx, const char* name, JSModuleDef* start) {
  struct list_head* el;

  for(el = start ? &start->link : ctx->loaded_modules.next; el != &ctx->loaded_modules; el = el->next)
  /*list_for_each(el, &ctx->loaded_modules)*/ {
    JSModuleDef* m = list_entry(el, JSModuleDef, link);
    const char* str = module_namecstr(ctx, m);
    BOOL match = !strcmp(str, name);
    JS_FreeCString(ctx, str);

    if(match)
      return m;
  }

  return 0;
}

int
js_module_index(JSContext* ctx, JSModuleDef* m) {
  struct list_head* el;
  int i = 0;

  list_for_each(el, &ctx->loaded_modules) {

    if(m == list_entry(el, JSModuleDef, link))
      return i;
    ++i;
  }

  return -1;
}

JSModuleDef*
js_module_find_rev(JSContext* ctx, const char* name, JSModuleDef* start) {
  struct list_head* el;

  for(el = start ? &start->link : ctx->loaded_modules.prev; el != &ctx->loaded_modules; el = el->prev) /*list_for_each_prev(el, &ctx->loaded_modules)*/ {
    JSModuleDef* m = list_entry(el, JSModuleDef, link);
    const char* str = module_namecstr(ctx, m);
    BOOL match = !strcmp(str, name);
    JS_FreeCString(ctx, str);

    if(match)
      return m;
  }

  return 0;
}

int
js_module_indexof(JSContext* ctx, JSModuleDef* def) {
  int i = 0;
  struct list_head* el;

  list_for_each(el, &ctx->loaded_modules) {
    JSModuleDef* m = list_entry(el, JSModuleDef, link);

    if(m == def)
      return i;

    ++i;
  }

  return -1;
}

JSModuleDef*
js_module_at(JSContext* ctx, int index) {
  int i = 0;
  struct list_head* el;

  if(index >= 0) {
    list_for_each(el, &ctx->loaded_modules) {
      JSModuleDef* m = list_entry(el, JSModuleDef, link);

      if(index == i)
        return m;

      ++i;
    }
  } else {
    index = -(index + 1);

    list_for_each_prev(el, &ctx->loaded_modules) {
      JSModuleDef* m = list_entry(el, JSModuleDef, link);

      if(index == i)
        return m;

      ++i;
    }
  }

  return 0;
}

void
module_make_object(JSContext* ctx, JSModuleDef* m, JSValueConst obj) {
  JSValue tmp;
  char buf[FMT_XLONG + 2];
  strcpy(buf, "0x");

  if(!js_has_propertystr(ctx, obj, "name"))
    JS_SetPropertyStr(ctx, obj, "name", module_name(ctx, m));

  JS_DefinePropertyValueStr(ctx, obj, "resolved", JS_NewBool(ctx, m->resolved), 0);
  JS_DefinePropertyValueStr(ctx, obj, "funcCreated", JS_NewBool(ctx, m->func_created), 0);
  JS_DefinePropertyValueStr(ctx, obj, "instantiated", JS_NewBool(ctx, m->instantiated), 0);
  JS_DefinePropertyValueStr(ctx, obj, "evaluated", JS_NewBool(ctx, m->evaluated), 0);

  if(!JS_IsUndefined((tmp = module_ns(ctx, m))))
    JS_DefinePropertyValueStr(ctx, obj, "ns", tmp, 0);

  if(!JS_IsUndefined((tmp = module_exports(ctx, m))))
    JS_DefinePropertyValueStr(ctx, obj, "exports", tmp, 0);

  if(!JS_IsUndefined((tmp = module_imports(ctx, m))))
    JS_DefinePropertyValueStr(ctx, obj, "imports", tmp, 0);

  if(!JS_IsUndefined((tmp = module_reqmodules(ctx, m))))
    JS_SetPropertyStr(ctx, obj, "reqModules", tmp);

  if(m->init_func) {
    JS_SetPropertyStr(ctx, obj, "native", JS_NewBool(ctx, m->init_func != NULL));
  }

  if(!JS_IsUndefined((tmp = module_func(ctx, m)))) {
    if(m->init_func)
      JS_DefinePropertyValueStr(ctx, obj, "initFunc", tmp, 0);
    else
      JS_SetPropertyStr(ctx, obj, "func", tmp);
  }

  if(!js_is_null_or_undefined((tmp = JS_DupValue(ctx, m->meta_obj))))
    JS_SetPropertyStr(ctx, obj, "metaObj", tmp);

  if(!js_is_null_or_undefined((tmp = JS_DupValue(ctx, m->eval_exception))))
    JS_SetPropertyStr(ctx, obj, "evalException", tmp);

  {
    JSAtom atom = js_symbol_static_atom(ctx, "toStringTag");
    JS_DefinePropertyValue(ctx, obj, atom, JS_NewString(ctx, "Module"), 0);
    JS_FreeAtom(ctx, atom);
  }

  {

    buf[2 + fmt_xlonglong0(&buf[2], (long long)(uintptr_t)m, __SIZEOF_POINTER__ * 2)] = 0;

    JS_DefinePropertyValueStr(ctx, obj, "address", JS_NewString(ctx, buf), 0);
  }
}

JSValue
module_object(JSContext* ctx, JSModuleDef* m) {
  JSValue obj = JS_NewObject(ctx);
  module_make_object(ctx, m, obj);
  return obj;
}

int
module_exports_get(JSContext* ctx, JSModuleDef* m, BOOL rename_default, JSValueConst exports) {
  JSAtom def = JS_NewAtom(ctx, "default");
  int i;

  for(i = 0; i < m->export_entries_count; i++) {
    JSExportEntry* entry = &m->export_entries[i];
    JSVarRef* ref = entry->u.local.var_ref;
    JSValue val = JS_UNDEFINED;
    JSAtom name = entry->export_name;

    if(ref) {
      val = JS_DupValue(ctx, ref->pvalue ? *ref->pvalue : ref->value);

      if(rename_default && name == def)
        name = m->module_name;
    }

    JS_SetProperty(ctx, exports, name, val);
  }

  JS_FreeAtom(ctx, def);
  return i;
}

JSValue
module_imports(JSContext* ctx, JSModuleDef* m) {
  JSValue obj = m->import_entries_count > 0 ? JS_NewArray(ctx) : JS_UNDEFINED;
  int i;

  for(i = 0; i < m->import_entries_count; i++) {
    JSImportEntry* entry = &m->import_entries[i];
    JSAtom name = entry->import_name;
    /*JSReqModuleEntry* req_module = &m->req_module_entries[entry->req_module_idx];
    JSAtom module_name = req_module->module_name;*/

    JSValue import_value = JS_NewArray(ctx);

    JS_SetPropertyUint32(ctx, import_value, 0, JS_AtomToValue(ctx, name));
    JS_SetPropertyUint32(ctx, import_value, 1, JS_NewUint32(ctx, entry->req_module_idx));
    JS_SetPropertyUint32(ctx, obj, i, import_value);
  }

  return obj;
}

JSValue
module_reqmodules(JSContext* ctx, JSModuleDef* m) {
  JSValue obj = m->req_module_entries_count > 0 ? JS_NewArray(ctx) : JS_UNDEFINED;
  int i;

  for(i = 0; i < m->req_module_entries_count; i++) {
    JSReqModuleEntry* req_module = &m->req_module_entries[i];
    JSAtom module_name = req_module->module_name;
    JSModuleDef* module = req_module->module;

    JSValue req_module_value = JS_NewArray(ctx);

    JS_SetPropertyUint32(ctx, req_module_value, 0, JS_AtomToValue(ctx, module_name));
    JS_SetPropertyUint32(ctx, req_module_value, 1, JS_NewInt32(ctx, js_module_index(ctx, module)));
    JS_SetPropertyUint32(ctx, obj, i, req_module_value);
  }

  return obj;
}

JSValue
module_default_export(JSContext* ctx, JSModuleDef* m) {
  JSAtom def = JS_NewAtom(ctx, "default");
  JSValue ret = JS_UNDEFINED;
  int i;

  for(i = 0; i < m->export_entries_count; i++) {
    JSExportEntry* entry = &m->export_entries[i];
    JSVarRef* ref = entry->u.local.var_ref;
    JSAtom name = entry->export_name;

    if(ref) {

      if(name == def) {
        ret = JS_DupValue(ctx, ref->pvalue ? *ref->pvalue : ref->value);
        break;
      }
    }
  }

  JS_FreeAtom(ctx, def);
  return ret;
}

JSValue
module_ns(JSContext* ctx, JSModuleDef* m) {
  return JS_DupValue(ctx, m->module_ns);
}

JSValue
module_exception(JSContext* ctx, JSModuleDef* m) {
  return m->eval_has_exception ? JS_DupValue(ctx, m->eval_exception) : JS_NULL;
}

JSValue
module_meta_obj(JSContext* ctx, JSModuleDef* m) {
  return JS_DupValue(ctx, m->meta_obj);
}

static JSValue
call_module_func(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) {
  union {
    JSModuleInitFunc* init_func;
    int32_t i[2];
  } u;

  u.i[0] = JS_VALUE_GET_INT(data[0]);
  u.i[1] = JS_VALUE_GET_INT(data[1]);

  if(argc >= 1) {
    JSModuleDef* m;

    if((m = js_module_def(ctx, argv[0])))
      return JS_NewInt32(ctx, u.init_func(ctx, m));
  }

  return JS_ThrowTypeError(ctx, "argument 1 module expected");
}

JSValue
module_func(JSContext* ctx, JSModuleDef* m) {
  JSValue func = JS_UNDEFINED;

  if(JS_IsFunction(ctx, m->func_obj)) {
    func = JS_DupValue(ctx, m->func_obj);
  } else if(m->init_func) {
    union {
      JSModuleInitFunc* init_func;
      int32_t i[2];
    } u = {m->init_func};

    JSValueConst data[2] = {
        JS_MKVAL(JS_TAG_INT, u.i[0]),
        JS_MKVAL(JS_TAG_INT, u.i[1]),
    };

    func = JS_NewCFunctionData(ctx, call_module_func, 1, 0, 2, data);
  }

  return func;
}

JSValue
module_name(JSContext* ctx, JSModuleDef* m) {

  if(m->module_name < (size_t)JS_GetRuntime(ctx)->atom_count)
    return JS_AtomToValue(ctx, m->module_name);

  return JS_UNDEFINED;
}

const char*
module_namecstr(JSContext* ctx, JSModuleDef* m) {
  return JS_AtomToCString(ctx, m->module_name);
}

JSValue
module_exports_find(JSContext* ctx, JSModuleDef* m, JSAtom atom) {
  int i;

  for(i = 0; i < m->export_entries_count; i++) {
    JSExportEntry* entry = &m->export_entries[i];

    if(entry->export_name == atom) {
      JSVarRef* ref = entry->u.local.var_ref;
      JSValue export = ref ? JS_DupValue(ctx, ref->pvalue ? *ref->pvalue : ref->value) : JS_UNDEFINED;
      return export;
    }
  }

  return JS_UNDEFINED;
}

JSModuleDef*
module_next(JSContext* ctx, JSModuleDef* m) {
  return m->link.next != js_modules_list(ctx) ? list_entry(m->link.next, JSModuleDef, link) : 0;
}

JSModuleDef*
module_prev(JSContext* ctx, JSModuleDef* m) {
  return m->link.prev != js_modules_list(ctx) ? list_entry(m->link.prev, JSModuleDef, link) : 0;
}

JSModuleDef*
module_last(JSContext* ctx) {
  struct list_head* list = js_modules_list(ctx);

  return list_empty(list) ? 0 : list_entry(list->prev, JSModuleDef, link);
}

void
module_rename(JSContext* ctx, JSModuleDef* m, JSAtom name) {
  JS_FreeAtom(ctx, m->module_name);
  m->module_name = name;
}

static void
js_arraybuffer_freestring(JSRuntime* rt, void* opaque, void* ptr) {
  JSString* jstr = opaque;
  JS_FreeValueRT(rt, JS_MKPTR(JS_TAG_STRING, jstr));
}

JSValue
js_arraybuffer_fromstring(JSContext* ctx, JSValueConst str) {
  JSString* jstr;

  if(!JS_IsString(str))
    return JS_ThrowTypeError(ctx, "Not a string");

  JS_DupValue(ctx, str);
  jstr = JS_VALUE_GET_PTR(str);

  return JS_NewArrayBuffer(ctx, jstr->u.str8, jstr->len, js_arraybuffer_freestring, jstr, FALSE);
}

void*
js_sab_alloc(void* opaque, size_t size) {
  JSSABHeader* sab;

  if(!(sab = malloc(sizeof(JSSABHeader) + size)))
    return 0;

  sab->ref_count = 1;
  return sab->buf;
}

void
js_sab_free(void* opaque, void* ptr) {
  JSSABHeader* sab;
  int ref_count;
  sab = (JSSABHeader*)((uint8_t*)ptr - sizeof(JSSABHeader));
  ref_count = atomic_add_int(&sab->ref_count, -1);
  assert(ref_count >= 0);

  if(ref_count == 0)
    free(sab);
}

void
js_sab_dup(void* opaque, void* ptr) {
  JSSABHeader* sab;
  sab = (JSSABHeader*)((uint8_t*)ptr - sizeof(JSSABHeader));
  atomic_add_int(&sab->ref_count, 1);
}

JSValueConst
js_cstring_value(const char* ptr) {
  return JS_MKPTR(JS_TAG_STRING, (JSString*)(void*)(ptr - offsetof(JSString, u)));
}

char*
js_cstring_dup(JSContext* ctx, const char* str) {
  /* purposely removing constness */
  JSString* p = (JSString*)(void*)(str - offsetof(JSString, u));
  JS_DupValue(ctx, JS_MKPTR(JS_TAG_STRING, p));
  return (char*)str;
}

size_t
js_cstring_len(JSValueConst v) {
  JSString* p;

  if(JS_IsString(v)) {
    p = JS_VALUE_GET_PTR(v);
    return p->len;
  }

  return 0;
}

char*
js_cstring_ptr(JSValueConst v) {
  JSString* p;

  if(JS_IsString(v)) {
    p = JS_VALUE_GET_PTR(v);
    return (char*)p->u.str8;
  }

  return 0;
}

const char*
js_class_name(JSContext* ctx, JSClassID id) {
  JSAtom atom = JS_GetRuntime(ctx)->class_array[id].class_name;
  return JS_AtomToCString(ctx, atom);
}

JSAtom
js_class_atom(JSContext* ctx, JSClassID id) {
  JSAtom atom = 0;

  if(id > 0 && id < (JSClassID)JS_GetRuntime(ctx)->class_count)
    atom = JS_GetRuntime(ctx)->class_array[id].class_name;
  return atom;
}

JSClassID
js_class_find(JSContext* ctx, const char* name) {
  JSAtom atom = JS_NewAtom(ctx, name);
  JSRuntime* rt = JS_GetRuntime(ctx);
  int i, n = rt->class_count;

  for(i = 0; i < n; i++)

    if(rt->class_array[i].class_name == atom)
      return i;

  return -1;
}

JSClassID
js_class_id(JSContext* ctx, int id) {
  return JS_GetRuntime(ctx)->class_array[id].class_id;
}

JSValue
js_opcode_array(JSContext* ctx, const JSOpCode* opcode) {
  JSValue ret = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, ret, 0, JS_NewUint32(ctx, opcode->size));
  JS_SetPropertyUint32(ctx, ret, 1, JS_NewUint32(ctx, opcode->n_pop));
  JS_SetPropertyUint32(ctx, ret, 2, JS_NewUint32(ctx, opcode->n_push));
  JS_SetPropertyUint32(ctx, ret, 3, JS_NewUint32(ctx, opcode->fmt));
  JS_SetPropertyUint32(ctx, ret, 4, JS_NewString(ctx, opcode->name));
  return ret;
}

JSValue
js_opcode_object(JSContext* ctx, const struct JSOpCode* opcode) {
  JSValue ret = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, ret, "size", JS_NewUint32(ctx, opcode->size));
  JS_SetPropertyStr(ctx, ret, "n_pop", JS_NewUint32(ctx, opcode->n_pop));
  JS_SetPropertyStr(ctx, ret, "n_push", JS_NewUint32(ctx, opcode->n_push));
  JS_SetPropertyStr(ctx, ret, "fmt", JS_NewUint32(ctx, opcode->fmt));
  JS_SetPropertyStr(ctx, ret, "name", JS_NewString(ctx, opcode->name));
  return ret;
}

JSValue
js_get_bytecode(JSContext* ctx, JSValueConst value) {
  JSValue ret = JS_UNDEFINED;

  if(JS_IsFunction(ctx, value)) {
    JSObject* obj = JS_VALUE_GET_OBJ(value);
    JSFunctionBytecode* fnbc;

    if((fnbc = obj->u.func.function_bytecode)) {
      ret = JS_NewArrayBufferCopy(ctx, fnbc->byte_code_buf, fnbc->byte_code_len);
    }
  }

  return ret;
}

JSValue
js_opcode_list(JSContext* ctx, BOOL as_object) {
  JSValue ret = JS_NewArray(ctx);
  size_t i, j, len = countof(js_opcodes);

  for(i = 0, j = 0; i < len; i++) {

    if(i >= OP_TEMP_START && i < OP_TEMP_END)
      continue;

    JS_SetPropertyUint32(ctx, ret, j++, (as_object ? js_opcode_object : js_opcode_array)(ctx, &js_opcodes[i]));
  }

  return ret;
}

#ifdef HAVE_JS_DEBUGGER_BUILD_BACKTRACE
JSValue js_debugger_build_backtrace(JSContext* ctx, const uint8_t* cur_pc);

JSValue
js_stack_get(JSContext* ctx) {
  return js_debugger_build_backtrace(ctx, ctx->rt->current_stack_frame->cur_pc);
}
#endif

#define SHORT_OPCODES 1

const JSOpCode js_opcodes[OP_COUNT + (OP_TEMP_END - OP_TEMP_START)] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) {size, n_pop, n_push, OP_FMT_##f, #id},
#include <quickjs-opcode.h>
#undef DEF
#undef FMT
};
