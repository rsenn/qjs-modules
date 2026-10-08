
#undef _ISOC99_SOURCE
#define _ISOC99_SOURCE 1
#include "utils.h"
#include "defines.h"
#include "vector.h"
#include "buffer-utils.h"
#include "debug.h"
#include "path.h"
#include <list.h>
#include <cutils.h>
#include <libregexp.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifdef _WIN32
#include "compat/mmap-win32.h"
#else
#include <sys/mman.h>
#endif
#include <quickjs-libc.h>
#include <float.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

static JSValue generator_prototype, asyncgenerator_prototype, typedarray_prototype;
// static JSModuleDef* io_module;

/**
 * \addtogroup utils
 * @{
 */
void quicksort_r(void*, size_t, size_t, int (*)(const void*, const void*, void*), void*);
int strverscmp(const char*, const char*);

#ifndef INFINITY
#define INFINITY __builtin_inf()
#endif

#ifdef USE_WORKER
#include <pthread.h>
#endif

js_realloc_helper(utils_js_realloc);
js_realloc_rt_helper(utils_js_realloc_rt);

size_t
list_size(struct list_head* list) {
  struct list_head* el;
  size_t i = 0;

  list_for_each(el, list) {
    ++i;
  }

  return i;
}

struct list_head*
list_front(const struct list_head* list) {
  return list->next != list ? list->next : 0;
}

struct list_head*
list_back(const struct list_head* list) {
  return list->prev != list ? list->prev : 0;
}

struct list_head*
list_unlink_before(struct list_head* list) {
  struct list_head* prev = list->prev;

  prev->next = NULL;
  list->prev = NULL;

  return prev;
}

struct list_head
list_unlink(struct list_head* start, struct list_head* end) {
  struct list_head *prev, *last;

  prev = list_unlink_before(start);
  last = list_unlink_before(end);

  list_link_next(prev, end);
  list_link_prev(end, prev);

  return (struct list_head){last, start};
}

void
list_link_next(struct list_head* node, struct list_head* newn) {
  node->next = newn;
  newn->prev = node;
}

void
list_link_prev(struct list_head* node, struct list_head* newn) {
  node->prev = newn;
  newn->next = node;
}
void
__list_splice(struct list_head* list, struct list_head* head) {
  struct list_head* first = list->next;
  struct list_head* last = list->prev;
  struct list_head* at = head->next;

  first->prev = head;
  head->next = first;

  last->next = at;
  at->prev = last;
}

/**
 * @brief join two lists
 * @param list  the new list to add.
 * @param head  the place to add it in the first list.
 */
void
list_splice(struct list_head* list, struct list_head* head) {
  if(!list_empty(list))
    __list_splice(list, head);
}

void
__list_sort(struct list_head* head, int (*cmp)(struct list_head* a, struct list_head* b, void*), void* opaque) {
  struct list_head *p, *q, *e, *list, *tail, *oldhead;
  int insize, nmerges, psize, qsize, i;

  list = head->next;
  list_del(head);
  insize = 1;
  for(;;) {
    p = oldhead = list;
    list = tail = NULL;
    nmerges = 0;

    while(p) {
      nmerges++;
      q = p;
      psize = 0;
      for(i = 0; i < insize; i++) {
        psize++;
        q = q->next == oldhead ? NULL : q->next;
        if(!q)
          break;
      }

      qsize = insize;
      while(psize > 0 || (qsize > 0 && q)) {
        if(!psize) {
          e = q;
          q = q->next;
          qsize--;
          if(q == oldhead)
            q = NULL;
        } else if(!qsize || !q) {
          e = p;
          p = p->next;
          psize--;
          if(p == oldhead)
            p = NULL;
        } else if(cmp(p, q, opaque) <= 0) {
          e = p;
          p = p->next;
          psize--;
          if(p == oldhead)
            p = NULL;
        } else {
          e = q;
          q = q->next;
          qsize--;
          if(q == oldhead)
            q = NULL;
        }
        if(tail)
          tail->next = e;
        else
          list = e;
        e->prev = tail;
        tail = e;
      }
      p = q;
    }

    tail->next = list;
    list->prev = tail;

    if(nmerges <= 1)
      break;

    insize *= 2;
  }

  head->next = list;
  head->prev = list->prev;
  list->prev->next = head;
  list->prev = head;
}

void
__list_reverse(struct list_head* head) {
  if(!list_empty(head)) {
    struct list_head *p, *q, *list, *tail;

    list = head->next;
    tail = head->prev;
    list_del(head);
    init_list_head(head);

    for(p = list; (q = p->next); p = q) {
      list_add(p, head);

      if(p == tail)
        break;
    }
  }
}

/**
 * Delete a list entry by making the prev/next entries point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */

/**
 * @brief delete from one list and add as another's head
 * @param list the entry to move
 * @param head the head that will precede our entry
 */

/**
 * @brief delete from one list and add as another's tail
 * @param list the entry to move
 * @param head the head that will follow our entry
 */
/**
 * @brief replace old entry by new one
 * @param old the element to be replaced
 * @param new the new element to insert
 *
 * If @old was empty, it will be overwritten.
 */

/* merge result: dprev <-> (shead <-> ... <-> stail) <-> dnext */

int
regexp_flags_fromstring(const char* s) {
  int flags = 0;

  if(str_contains(s, 'g'))
    flags |= LRE_FLAG_GLOBAL;

  if(str_contains(s, 'i'))
    flags |= LRE_FLAG_IGNORECASE;

  if(str_contains(s, 'm'))
    flags |= LRE_FLAG_MULTILINE;

  if(str_contains(s, 's'))
    flags |= LRE_FLAG_DOTALL;

#ifdef LRE_FLAG_UTF16
  if(str_contains(s, 'u'))
    flags |= LRE_FLAG_UTF16;
#endif

  if(str_contains(s, 'y'))
    flags |= LRE_FLAG_STICKY;

  return flags;
}

int
regexp_flags_tostring(int flags, char* buf) {
  char* out = buf;

  if(flags & LRE_FLAG_GLOBAL)
    *out++ = 'g';

  if(flags & LRE_FLAG_IGNORECASE)
    *out++ = 'i';

  if(flags & LRE_FLAG_MULTILINE)
    *out++ = 'm';

  if(flags & LRE_FLAG_DOTALL)
    *out++ = 's';

#ifdef LRE_FLAG_UTF16
  if(flags & LRE_FLAG_UTF16)
    *out++ = 'u';
#endif

  if(flags & LRE_FLAG_STICKY)
    *out++ = 'y';

  *out = '\0';
  return out - buf;
}

int
regexp_from_argv(RegExp* re, int argc, JSValueConst argv[], JSContext* ctx) {
  const char* flagstr;
  int ret = 1;

  assert(argc > 0);

  if(js_is_regexp(ctx, argv[0])) {
    re->source = js_get_propertystr_stringlen(ctx, argv[0], "source", &re->len);
    re->flags = regexp_flags_fromstring((flagstr = js_get_propertystr_cstring(ctx, argv[0], "flags")));
    JS_FreeCString(ctx, flagstr);
  } else {
    re->source = js_tostringlen(ctx, &re->len, argv[0]);

    if(argc > 1 && JS_IsString(argv[1])) {
      re->flags = regexp_flags_fromstring((flagstr = JS_ToCString(ctx, argv[1])));
      JS_FreeCString(ctx, flagstr);
      ret++;
    }
  }

  return ret;
}

RegExp
regexp_from_dbuf(DynBuf* dbuf, int flags) {
  RegExp re = {(char*)dbuf->buf, dbuf->size, flags};

  dbuf->buf = 0;
  dbuf->allocated_size = 0;
  dbuf->size = 0;

  return re;
}

uint8_t*
regexp_compile(RegExp re, JSContext* ctx) {
  char error_msg[64];
  int len = 0;
  uint8_t* bytecode;

  if(!(bytecode = lre_compile(&len, error_msg, sizeof(error_msg), re.source, re.len, re.flags, ctx)))
    JS_ThrowInternalError(ctx, "Error compiling regex /%.*s/: %s", (int)re.len, re.source, error_msg);

  return bytecode;
}

JSValue
regexp_to_value(RegExp re, JSContext* ctx) {
  char flagstr[32] = {0};
  size_t flaglen = regexp_flags_tostring(re.flags, flagstr);
  JSValueConst args[2] = {JS_NewStringLen(ctx, re.source, re.len), JS_NewStringLen(ctx, flagstr, flaglen)};
  JSValue regex, ctor = js_global_get_str(ctx, "RegExp");

  regex = JS_CallConstructor(ctx, ctor, 2, args);

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  return regex;
}

int64_t
js_array_length(JSContext* ctx, JSValueConst array) {
  int64_t len = -1;
  JSValue length = JS_GetPropertyStr(ctx, array, "length");

  if(JS_IsNumber(length))
    JS_ToInt64(ctx, &len, length);

  JS_FreeValue(ctx, length);
  return len;
}

int64_t
js_array_clear(JSContext* ctx, JSValueConst array) {
  int64_t len = js_array_length(ctx, array);
  JSAtom splice = JS_NewAtom(ctx, "splice");
  JSValueConst args[] = {
      JS_NewInt64(ctx, 0),
      JS_NewInt64(ctx, len),
  };

  JSValue ret = JS_Invoke(ctx, array, splice, countof(args), args);
  JS_FreeAtom(ctx, splice);

  if(JS_IsException(ret))
    return -1;

  len = js_array_length(ctx, ret);
  JS_FreeValue(ctx, ret);

  assert(js_array_length(ctx, array) == 0);
  return len;
}

JSValue
js_intv_to_array(JSContext* ctx, int const* intv, size_t len) {
  JSValue ret = JS_NewArray(ctx);

  if(intv)
    for(size_t i = 0; i < len; i++)
      JS_SetPropertyUint32(ctx, ret, i, JS_NewInt32(ctx, intv[i]));

  return ret;
}

char**
js_array_to_argv(JSContext* ctx, size_t* lenp, JSValueConst array) {
  size_t i, len = js_array_length(ctx, array);
  char** ret = js_mallocz(ctx, sizeof(char*) * (len + 1));

  for(i = 0; i < len; i++) {
    JSValue item = JS_GetPropertyUint32(ctx, array, i);

    ret[i] = js_tostring(ctx, item);
    JS_FreeValue(ctx, item);
  }

  if(lenp)
    *lenp = len;

  return ret;
}

int32_t*
js_array_to_int32v(JSContext* ctx, size_t* lenp, JSValueConst array) {
  size_t i, len = js_array_length(ctx, array);
  int32_t* ret = js_mallocz(ctx, sizeof(int32_t) * (len + 1));

  for(i = 0; i < len; i++) {
    JSValue item = JS_GetPropertyUint32(ctx, array, i);

    JS_ToInt32(ctx, &ret[i], item);
    JS_FreeValue(ctx, item);
  }

  if(lenp)
    *lenp = len;

  return ret;
}

int64_t*
js_array_to_int64v(JSContext* ctx, size_t* lenp, JSValueConst array) {
  size_t i, len = js_array_length(ctx, array);
  int64_t* ret = js_mallocz(ctx, sizeof(int64_t) * (len + 1));

  for(i = 0; i < len; i++) {
    JSValue item = JS_GetPropertyUint32(ctx, array, i);

    JS_ToInt64Ext(ctx, &ret[i], item);
    JS_FreeValue(ctx, item);
  }

  if(lenp)
    *lenp = len;

  return ret;
}

int
js_array_copys(JSContext* ctx, JSValueConst array, int n, char** stra) {
  int i, len = MIN_NUM(n, js_array_length(ctx, array));

  for(i = 0; i < len; i++) {
    JSValue item = JS_GetPropertyUint32(ctx, array, i);

    if(stra[i])
      js_free(ctx, stra[i]);

    stra[i] = js_tostring(ctx, item);
    JS_FreeValue(ctx, item);
  }

  return i;
}

JSAtom
js_atom_from(JSContext* ctx, const char* str) {
  if(str[0] == '[') {
    size_t objlen = str_chr(&str[1], '.');
    JSValue obj = js_global_get_str_n(ctx, &str[1], objlen);
    size_t proplen = str_chr(&str[1 + objlen + 1], ']');
    JSAtom ret, prop = JS_NewAtomLen(ctx, &str[1 + objlen + 1], proplen);
    JSValue val = JS_GetProperty(ctx, obj, prop);

    JS_FreeAtom(ctx, prop);
    ret = JS_ValueToAtom(ctx, val);
    JS_FreeValue(ctx, val);
    return ret;
  }

  return JS_NewAtom(ctx, str);
}

const char*
js_atom_to_cstringlen(JSContext* ctx, size_t* len, JSAtom atom) {
  JSValue v = JS_AtomToValue(ctx, atom);
  const char* s = JS_ToCStringLen(ctx, len, v);

  JS_FreeValue(ctx, v);
  return s;
}

BOOL
js_atom_is_index(JSContext* ctx, int64_t* pval, JSAtom atom) {
  JSValue value;
  BOOL ret = FALSE;
  int64_t index;

  if(JS_ATOM_ISINT(atom)) {
    if(pval)
      *pval = JS_ATOM_TOINT(atom);

    return TRUE;
  }

  value = JS_AtomToValue(ctx, atom);

  if(JS_IsSymbol(value)) {
  } else if(JS_IsString(value)) {
    const char* s = JS_ToCString(ctx, value);

    if(is_digit_char(s[s[0] == '-'])) {
      index = atoll(s);
      ret = TRUE;
    }

    JS_FreeCString(ctx, s);
  } else if(!JS_ToInt64Ext(ctx, &index, value)) {
    ret = TRUE;
  }

  if(ret == TRUE)
    if(pval)
      *pval = index;

  return ret;
}

BOOL
js_atom_is_symbol(JSContext* ctx, JSAtom atom) {
  JSValue value = JS_AtomToValue(ctx, atom);
  BOOL ret = JS_IsSymbol(value);
  JS_FreeValue(ctx, value);
  return ret;
}

const char*
js_function_name(JSContext* ctx, JSValueConst value) {
  JSValue name;
  const char* s = 0;
  int32_t i = -1;
  JSValue str = js_value_tostring(ctx, "Function", value);
  JSAtom atom = JS_NewAtom(ctx, "indexOf");
  JSValue args[2] = {
      JS_NewString(ctx, "function "),
  };
  JSValue idx = JS_Invoke(ctx, str, atom, 1, args);
  JS_FreeValue(ctx, args[0]);
  JS_ToInt32(ctx, &i, idx);

  if(i != 0) {
    JS_FreeAtom(ctx, atom);
    JS_FreeValue(ctx, str);
    return 0;
  }

  args[0] = JS_NewString(ctx, "(");
  idx = JS_Invoke(ctx, str, atom, 1, args);
  JS_FreeValue(ctx, args[0]);
  JS_FreeAtom(ctx, atom);
  atom = JS_NewAtom(ctx, "substring");
  args[0] = JS_NewUint32(ctx, 9);
  args[1] = idx;
  name = JS_Invoke(ctx, str, atom, 2, args);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, str);
  JS_FreeAtom(ctx, atom);
  s = JS_ToCString(ctx, name);
  JS_FreeValue(ctx, name);
  return s;
}

int
js_function_argc(JSContext* ctx, JSValueConst value) {
  return js_get_propertystr_int32(ctx, value, "length");
}

static JSValue
js_function_bound_this(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue func_data[]) {
  int bound_args = magic;
  JSValue args[argc + bound_args];

  for(int i = 0; i < bound_args; i++)
    args[i] = func_data[i + 2];

  for(int j = 0; j < argc; j++)
    args[bound_args + j] = argv[j];

  return JS_Call(ctx, func_data[0], func_data[1], bound_args + argc, args);
}

JSValue
js_function_bind_this(JSContext* ctx, JSValueConst func, JSValueConst this_val) {
  JSValue data[] = {
      JS_DupValue(ctx, func),
      JS_DupValue(ctx, this_val),
  };

  return JS_NewCFunctionData(ctx, js_function_bound_this, js_function_argc(ctx, func), 0, countof(data), data);
}

static JSValue
js_function_throw_fn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValueConst data[]) {

  if(!JS_IsUndefined(data[0]))
    return JS_Throw(ctx, data[0]);

  return JS_DupValue(ctx, argc >= 1 ? argv[0] : JS_UNDEFINED);
}

JSValue
js_function_throw(JSContext* ctx, JSValueConst err) {
  JSValueConst data[1];
  data[0] = JS_DupValue(ctx, err);
  return JS_NewCFunctionData(ctx, js_function_throw_fn, 0, 0, 1, data);
}

static JSValue
js_function_return_value_fn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValueConst data[]) {
  return data[0];
}

JSValue
js_function_return_value(JSContext* ctx, JSValueConst value) {
  JSValue data[] = {
      JS_DupValue(ctx, value),
  };

  return JS_NewCFunctionData(ctx, js_function_return_value_fn, 0, 0, countof(data), data);
}

JSValue
js_function_return_undefined(JSContext* ctx) {
  return js_function_return_value(ctx, JS_UNDEFINED);
}

JSValue
js_global_get_str(JSContext* ctx, const char* prop) {
  JSValue global_obj = JS_GetGlobalObject(ctx);
  JSValue ret = JS_GetPropertyStr(ctx, global_obj, prop);
  JS_FreeValue(ctx, global_obj);
  return ret;
}

JSValue
js_global_get_str_n(JSContext* ctx, const char* prop, size_t len) {
  JSAtom atom = JS_NewAtomLen(ctx, prop, len);
  JSValue ret = js_global_get_atom(ctx, atom);
  JS_FreeAtom(ctx, atom);
  return ret;
}

JSValue
js_global_get_atom(JSContext* ctx, JSAtom prop) {
  JSValue global_obj = JS_GetGlobalObject(ctx);
  JSValue ret = JS_GetProperty(ctx, global_obj, prop);
  JS_FreeValue(ctx, global_obj);
  return ret;
}

JSValue
js_global_prototype(JSContext* ctx, const char* class_name) {
  return js_global_static_func(ctx, class_name, "prototype");
}

JSValue
js_global_static_func(JSContext* ctx, const char* class_name, const char* func_name) {
  JSValue ctor = js_global_get_str(ctx, class_name);
  JSValue func = JS_GetPropertyStr(ctx, ctor, func_name);
  JS_FreeValue(ctx, ctor);
  return func;
}

JSValue
js_global_prototype_func(JSContext* ctx, const char* class_name, const char* func_name) {
  JSValue proto = js_global_prototype(ctx, class_name);
  JSValue func = JS_GetPropertyStr(ctx, proto, func_name);
  JS_FreeValue(ctx, proto);
  return func;
}

BOOL
js_global_instanceof(JSContext* ctx, JSValueConst obj, const char* prop) {
  JSValue ctor = js_global_get_str(ctx, prop);
  BOOL ret = JS_IsInstanceOf(ctx, obj, ctor);

  if(!ret) {
    JSValue proto = JS_GetPropertyStr(ctx, ctor, "prototype");
    ret = js_has_prototype(ctx, obj, proto);
    JS_FreeValue(ctx, proto);
  }

  JS_FreeValue(ctx, ctor);
  return ret;
}

JSValue
js_iterator_method(JSContext* ctx, JSValueConst obj) {
  JSValue ret = JS_UNDEFINED;
  JSAtom atom = js_symbol_static_atom(ctx, "asyncIterator");

  if(JS_HasProperty(ctx, obj, atom))
    ret = JS_GetProperty(ctx, obj, atom);

  JS_FreeAtom(ctx, atom);

  if(!JS_IsFunction(ctx, ret)) {
    atom = js_symbol_static_atom(ctx, "iterator");

    if(JS_HasProperty(ctx, obj, atom))
      ret = JS_GetProperty(ctx, obj, atom);

    JS_FreeAtom(ctx, atom);
  }

  return ret;
}

JSValue
js_iterator_new(JSContext* ctx, JSValueConst obj) {
  JSValue ret = JS_UNDEFINED, fn = js_iterator_method(ctx, obj);

  if(JS_IsFunction(ctx, fn))
    ret = JS_Call(ctx, fn, obj, 0, 0);

  JS_FreeValue(ctx, fn);
  return ret;
}

JSValue
js_iterator_next(JSContext* ctx, JSValueConst obj, BOOL* done_p) {
  JSValue fn = JS_GetPropertyStr(ctx, obj, "next");
  JSValue result = JS_Call(ctx, fn, obj, 0, 0);
  JS_FreeValue(ctx, fn);
  JSValue done = JS_GetPropertyStr(ctx, result, "done");
  JSValue value = JS_GetPropertyStr(ctx, result, "value");
  JS_FreeValue(ctx, result);
  *done_p = JS_ToBool(ctx, done);
  JS_FreeValue(ctx, done);
  return value;
}

JSValue
js_iterator_result(JSContext* ctx, JSValueConst value, BOOL done) {
  JSValue ret = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, ret, "value", JS_DupValue(ctx, value));
  JS_SetPropertyStr(ctx, ret, "done", JS_NewBool(ctx, done));

  return ret;
}

static JSValue
js_iterator_then_fn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValueConst data[]) {
  JSValue ret = JS_NewObject(ctx);

  if(argc >= 1)
    JS_SetPropertyStr(ctx, ret, "value", JS_DupValue(ctx, argv[0]));
  JS_SetPropertyStr(ctx, ret, "done", JS_DupValue(ctx, data[0]));

  return ret;
}

JSValue
js_iterator_then(JSContext* ctx, BOOL done) {
  JSValueConst data[] = {
      JS_NewBool(ctx, done),
  };

  return JS_NewCFunctionData(ctx, js_iterator_then_fn, 1, 0, countof(data), data);
}

JSClassID
js_object_classid(JSValueConst v) {
  void* p;

  if((p = js_value_obj(v)))
    return ((uint16_t*)p)[3];

  return 0;
}

void*
js_object_opaque(JSValueConst v) {
  void* p;

  if((p = js_value_obj(v)))
    return ((void**)p)[DEF6432(6, 7)];

  return 0;
}

int
js_object_refcount(JSValueConst v) {
  void* p;

  if((p = js_value_obj(v)))
    return ((int*)p)[0];

  return -1;
}

JSValue
js_object_constructor(JSContext* ctx, JSValueConst value) {
  JSValue ctor = JS_UNDEFINED;

  if(JS_IsObject(value)) {
    if(!js_has_propertystr(ctx, value, "constructor")) {
      JSValue proto = JS_GetPrototype(ctx, value);

      if(JS_IsObject(proto) && !js_object_same(value, proto)) {
        ctor = js_object_constructor(ctx, proto);
        JS_FreeValue(ctx, proto);
      }
    } else {
      ctor = JS_GetPropertyStr(ctx, value, "constructor");
    }
  }

  return ctor;
}

JSValue
js_object_species(JSContext* ctx, JSValueConst value) {
  JSValue ctor = js_object_constructor(ctx, value);
  JSAtom symbol_species = js_symbol_static_atom(ctx, "species");
  JSValue species = JS_GetProperty(ctx, ctor, symbol_species);
  JS_FreeAtom(ctx, symbol_species);
  JS_FreeValue(ctx, ctor);
  return species;
}

char*
js_object_classname(JSContext* ctx, JSValueConst value) {
  const char* str;
  char* name = 0;
  int namelen;
  JSValue proto, ctor = js_object_constructor(ctx, value);

  if(!JS_IsFunction(ctx, ctor)) {
    proto = JS_GetPrototype(ctx, value);
    ctor = js_object_constructor(ctx, proto);
  }

  if((str = JS_ToCString(ctx, ctor))) {
    if(!strncmp(str, "function ", 9)) {
      namelen = byte_chr(str + 9, strlen(str) - 9, '(');

      if(namelen)
        name = js_strndup(ctx, str + 9, namelen);
    }
  }

  if(!name) {
    if(str)
      JS_FreeCString(ctx, str);

    if((str = JS_ToCString(ctx, JS_GetPropertyStr(ctx, ctor, "name"))) && *str)
      name = js_strdup(ctx, str);
  }

  if(str)
    JS_FreeCString(ctx, str);

  return name;
}

int
js_object_equals(JSContext* ctx, JSValueConst a, JSValueConst b, BOOL deep) {
  JSPropertyEnum *atoms_a = 0, *atoms_b = 0;
  uint32_t natoms_a, natoms_b;
  // int32_t ta = js_value_type(ctx, a), tb = js_value_type(ctx, b);
  int ret = -1;

  // assert(ta == TYPE_OBJECT);
  // assert(tb == TYPE_OBJECT);

  if(JS_GetOwnPropertyNames(ctx, &atoms_a, &natoms_a, a, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY))
    goto end;

  if(JS_GetOwnPropertyNames(ctx, &atoms_b, &natoms_b, b, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY))
    goto end;

  ret = 0;

  if(natoms_a != natoms_b)
    goto end;

  quicksort_r(&atoms_a, natoms_a, sizeof(JSPropertyEnum), &js_propenum_cmp, ctx);
  quicksort_r(&atoms_b, natoms_b, sizeof(JSPropertyEnum), &js_propenum_cmp, ctx);

  for(uint32_t i = 0; i < natoms_a; i++) {
    if(atoms_a[i].atom != atoms_b[i].atom)
      goto end;

    JSValue prop_a = JS_GetProperty(ctx, a, atoms_a[i].atom);
    JSValue prop_b = JS_GetProperty(ctx, b, atoms_b[i].atom);

    BOOL ret = js_value_equals(ctx, prop_a, prop_b, deep);

    JS_FreeValue(ctx, prop_a);
    JS_FreeValue(ctx, prop_b);

    if(!ret)
      goto end;
  }

  ret = 1;

end:
  if(atoms_a)
    js_propertyenums_free(ctx, atoms_a, natoms_a);
  if(atoms_b)
    js_propertyenums_free(ctx, atoms_b, natoms_b);
  return ret;
}

BOOL
js_object_same2(JSContext* ctx, JSValueConst a, JSValueConst b, BOOL deep) {
  return js_object_same(a, b);
}

int
js_object_is(JSContext* ctx, JSValueConst value, const char* cmp) {
  BOOL ret = FALSE;
  const char* str;

  if((str = js_object_tostring(ctx, value))) {
    ret = strcmp(str, cmp) == 0;
    JS_FreeCString(ctx, str);
  }

  return ret;
}

JSValue
js_object_new(JSContext* ctx, const char* class_name, int argc, JSValueConst argv[]) {
  JSValue ctor = js_global_get_str(ctx, class_name);
  JSValue obj = JS_CallConstructor(ctx, ctor, argc, argv);
  JS_FreeValue(ctx, ctor);
  return obj;
}

JSAtom*
js_object_properties(JSContext* ctx, uint32_t* lenptr, JSValueConst obj, int flags) {
  JSAtom* atoms = NULL;
  uint32_t num_atoms = 0;
  JSValue proto = JS_DupValue(ctx, obj);

  do {
    JSPropertyEnum* tmp_tab;
    uint32_t tmp_len, pos = 0, i, j;

    if(JS_GetOwnPropertyNames(ctx, &tmp_tab, &tmp_len, proto, flags & ~JS_GPN_RECURSIVE))
      break;

    atoms = js_realloc(ctx, atoms, sizeof(JSAtom) * (num_atoms + tmp_len));

    for(i = 0; i < tmp_len; i++) {

      for(j = 0; j < num_atoms; j++)

        if(atoms[j] == tmp_tab[i].atom)
          break;

      if(j < num_atoms)
        continue;

      atoms[num_atoms + pos] = tmp_tab[i].atom;
      pos++;
    }

    num_atoms += pos;

    js_free(ctx, tmp_tab);
    tmp_tab = NULL;

    if(!(flags & JS_GPN_RECURSIVE))
      break;

    JSValue tmp = JS_GetPrototype(ctx, proto);
    JS_FreeValue(ctx, proto);
    proto = tmp;
  } while(JS_IsObject(proto));

  JS_FreeValue(ctx, proto);

  if(lenptr)
    *lenptr = num_atoms;

  return atoms;
}

int
js_object_copy(JSContext* ctx, JSValueConst dst, JSValueConst src) {
  JSPropertyEnum* tmp_tab;
  uint32_t tmp_len;

  if(JS_GetOwnPropertyNames(ctx, &tmp_tab, &tmp_len, src, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_PRIVATE_MASK))
    return -1;

  for(uint32_t i = 0; i < tmp_len; i++) {
    JSValue prop = JS_GetProperty(ctx, src, tmp_tab[i].atom);
    JS_SetProperty(ctx, dst, tmp_tab[i].atom, prop);
  }

  js_propertyenums_free(ctx, tmp_tab, tmp_len);

  return tmp_len;
}

int64_t
js_object_keyof(JSContext* ctx, JSValueConst obj, JSValueConst value) {
  JSPropertyEnum* tmp_tab;
  uint32_t tmp_len;
  int64_t ret = -1;

  if(JS_GetOwnPropertyNames(ctx, &tmp_tab, &tmp_len, obj, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK /*| JS_GPN_ENUM_ONLY*/))
    return -1;

  for(uint32_t i = 0; i < tmp_len; i++) {
    JSValue prop = JS_GetProperty(ctx, obj, tmp_tab[i].atom);
    int r = js_value_equals(ctx, value, prop, FALSE);
    JS_FreeValue(ctx, prop);

    if(r > 0) {
      ret = (int64_t)(uint32_t)tmp_tab[i].atom;
      break;
    }
  }

  js_propertyenums_free(ctx, tmp_tab, tmp_len);

  return ret;
}

BOOL
js_has_propertystr(JSContext* ctx, JSValueConst obj, const char* str) {
  JSAtom atom = JS_NewAtom(ctx, str);
  BOOL ret = JS_HasProperty(ctx, obj, atom);
  JS_FreeAtom(ctx, atom);
  return ret;
}

BOOL
js_get_propertystr_bool(JSContext* ctx, JSValueConst obj, const char* str) {
  BOOL ret = FALSE;
  JSValue value = JS_GetPropertyStr(ctx, obj, str);

  if(!JS_IsException(value))
    ret = JS_ToBool(ctx, value);

  JS_FreeValue(ctx, value);
  return ret;
}

const char*
js_get_propertystr_cstring(JSContext* ctx, JSValueConst obj, const char* prop) {
  JSAtom atom = JS_NewAtom(ctx, prop);
  const char* ret = 0;

  if(JS_HasProperty(ctx, obj, atom)) {
    JSValue value = JS_GetProperty(ctx, obj, atom);
    ret = JS_ToCString(ctx, value);
    JS_FreeValue(ctx, value);
  }

  return ret;
}

const char*
js_get_propertystr_cstringlen(JSContext* ctx, JSValueConst obj, const char* prop, size_t* lenp) {
  const char* ret;
  JSValue value = JS_GetPropertyStr(ctx, obj, prop);

  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;

  ret = JS_ToCStringLen(ctx, lenp, value);
  JS_FreeValue(ctx, value);
  return ret;
}

int32_t
js_get_propertyint_int32(JSContext* ctx, JSValueConst obj, uint32_t prop) {
  JSValue value = JS_GetPropertyUint32(ctx, obj, prop);
  int32_t ret;
  JS_ToInt32(ctx, &ret, value);
  JS_FreeValue(ctx, value);
  return ret;
}

int64_t
js_get_propertyint_int64(JSContext* ctx, JSValueConst obj, uint32_t prop) {
  JSValue value = JS_GetPropertyUint32(ctx, obj, prop);
  int64_t ret;
  JS_ToInt64Ext(ctx, &ret, value);
  JS_FreeValue(ctx, value);
  return ret;
}

double
js_get_propertyint_float64(JSContext* ctx, JSValueConst obj, uint32_t prop) {
  double ret;
  JSValue value = JS_GetPropertyUint32(ctx, obj, prop);
  JS_ToFloat64(ctx, &ret, value);
  JS_FreeValue(ctx, value);
  return ret;
}

char*
js_get_property_string(JSContext* ctx, JSValueConst obj, JSAtom prop) {
  char* ret;
  JSValue value = JS_GetProperty(ctx, obj, prop);

  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;

  ret = js_tostring(ctx, value);
  JS_FreeValue(ctx, value);
  return ret;
}

const char*
js_get_property_cstring(JSContext* ctx, JSValueConst obj, JSAtom prop) {
  const char* ret;
  JSValue value = JS_GetProperty(ctx, obj, prop);

  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;

  ret = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);
  return ret;
}

char*
js_get_propertystr_string(JSContext* ctx, JSValueConst obj, const char* prop) {
  char* ret;
  JSValue value = JS_GetPropertyStr(ctx, obj, prop);

  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;

  ret = js_tostring(ctx, value);
  JS_FreeValue(ctx, value);
  return ret;
}

char*
js_get_propertystr_stringlen(JSContext* ctx, JSValueConst obj, const char* prop, size_t* lenp) {
  char* ret;
  JSValue value = JS_GetPropertyStr(ctx, obj, prop);

  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;

  ret = js_tostringlen(ctx, lenp, value);
  JS_FreeValue(ctx, value);
  return ret;
}

int32_t
js_get_propertystr_int32(JSContext* ctx, JSValueConst obj, const char* prop) {
  int32_t ret;
  JSValue value = JS_GetPropertyStr(ctx, obj, prop);

  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;

  JS_ToInt32(ctx, &ret, value);
  JS_FreeValue(ctx, value);
  return ret;
}

int64_t
js_get_propertystr_int64(JSContext* ctx, JSValueConst obj, const char* prop) {
  int64_t ret;
  JSValue value = JS_GetPropertyStr(ctx, obj, prop);

  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;

  JS_ToInt64(ctx, &ret, value);
  JS_FreeValue(ctx, value);
  return ret;
}

uint64_t
js_get_propertystr_uint64(JSContext* ctx, JSValueConst obj, const char* prop) {
  uint64_t ret;
  JSValue value = JS_GetPropertyStr(ctx, obj, prop);

  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;

  JS_ToIndex(ctx, &ret, value);
  JS_FreeValue(ctx, value);
  return ret;
}

JSAtom
js_get_propertystr_atom(JSContext* ctx, JSValueConst obj, const char* prop) {
  JSAtom ret;
  JSValue value = JS_GetPropertyStr(ctx, obj, prop);

  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;

  ret = JS_ValueToAtom(ctx, value);
  JS_FreeValue(ctx, value);
  return ret;
}

void
js_set_propertyint_string(JSContext* ctx, JSValueConst obj, uint32_t i, const char* str) {
  JSValue value = JS_NewString(ctx, str);
  JS_SetPropertyUint32(ctx, obj, i, value);
}

void
js_set_propertystr_int(JSContext* ctx, JSValueConst obj, const char* prop, int32_t value) {
  JS_SetPropertyStr(ctx, obj, prop, JS_NewInt32(ctx, value));
}

void
js_set_propertystr_string(JSContext* ctx, JSValueConst obj, const char* prop, const char* str) {
  JSValue value = JS_NewString(ctx, str);
  JS_SetPropertyStr(ctx, obj, prop, value);
}

void
js_set_propertystr_stringlen(JSContext* ctx, JSValueConst obj, const char* prop, const char* str, size_t len) {
  JSValue value = JS_NewStringLen(ctx, str, len);
  JS_SetPropertyStr(ctx, obj, prop, value);
}

#define JS_CLASS_PROBE_MAX 65536

typedef struct {
  uint32_t class_id; /* 0 means free entry */
  JSAtom class_name;
} JSClassHead;

#define JS_CLASS_ENTRY_SIZE (sizeof(JSClassHead) + 4 * sizeof(void*))

/* JSRuntime is opaque and its layout differs between quickjs builds, so class_count is derived
 * from the public JS_IsRegisteredClass() instead of read from a hardcoded offset; JS_NewClass()
 * only ever grows class_count to the highest registered id + 1, which makes the two equal. */
uint32_t
js_class_count(JSRuntime* rt) {
  uint32_t count = 0;

  for(uint32_t i = 1; i < JS_CLASS_PROBE_MAX; ++i)
    if(JS_IsRegisteredClass(rt, i))
      count = i + 1;

  return count;
}

/* class_array has no public accessor, so its offset inside JSRuntime is found by scanning for
 * the `int class_count; JSClass* class_array;` pair and cross-checking the built-in classes
 * (ids 1-5 are always registered at their own index). Returns 0 if not found, never faults on
 * the way: candidate pointers must lie within 4GB of the runtime itself. */
static const char*
js_class_array(JSRuntime* rt) {
  static ptrdiff_t offset = -1;

  if(offset < 0) {
    uint32_t count = js_class_count(rt);

    offset = 0;

    for(size_t o = 0; count > 5 && o < 4096; o += sizeof(uint32_t)) {
      size_t ao = (o + sizeof(uint32_t) + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
      const char* arr;
      uintptr_t dist;
      uint32_t i;

      if(*(uint32_t*)((char*)rt + o) != count)
        continue;

      arr = *(const char**)((char*)rt + ao);

      if((uintptr_t)arr < 4096 || (uintptr_t)arr % sizeof(void*))
        continue;

      dist = arr > (const char*)rt ? arr - (const char*)rt : (const char*)rt - arr;

      if((uint64_t)dist >> 32)
        continue;

      for(i = 1; i <= 5; ++i)
        if(((const JSClassHead*)(arr + i * JS_CLASS_ENTRY_SIZE))->class_id != i)
          break;

      if(i > 5) {
        offset = ao;
        break;
      }
    }
  }

  return offset ? *(const char**)((const char*)rt + offset) : 0;
}

static const JSClassHead*
js_class_entry(JSRuntime* rt, JSClassID id) {
  const char* arr;

  if(id < 1 || id >= js_class_count(rt) || !(arr = js_class_array(rt)))
    return 0;

  return (const JSClassHead*)(arr + (size_t)id * JS_CLASS_ENTRY_SIZE);
}

JSAtom
js_class_atom(JSContext* ctx, JSClassID id) {
  const JSClassHead* entry = js_class_entry(JS_GetRuntime(ctx), id);

  return entry && entry->class_id ? JS_DupAtom(ctx, entry->class_name) : 0;
}

JSClassID
js_class_id(JSContext* ctx, JSClassID id) {
  const JSClassHead* entry = js_class_entry(JS_GetRuntime(ctx), id);

  return entry ? entry->class_id : 0;
}

JSValue
js_class_value(JSContext* ctx, JSClassID id) {
  uint32_t class_count = js_class_count(JS_GetRuntime(ctx));

  if(id < 1 || id >= class_count)
    return JS_ThrowRangeError(ctx, "id %d out of range (max: %u)", (int)id, (unsigned)class_count);

  if(js_class_id(ctx, id)) {
    JSAtom atom = js_class_atom(ctx, id);
    JSValue ret = JS_AtomToValue(ctx, atom);
    JS_FreeAtom(ctx, atom);
    return ret;
  }

  return JS_UNDEFINED;
}

JSClassID
js_class_find(JSContext* ctx, JSAtom name) {
  JSRuntime* rt = JS_GetRuntime(ctx);
  uint32_t class_count = js_class_count(rt);

  for(uint32_t i = 1; i < class_count; ++i) {
    const JSClassHead* entry = js_class_entry(rt, i);

    if(entry && entry->class_id && entry->class_name == name)
      return i;
  }

  return -1;
}

const char*
js_object_tostring(JSContext* ctx, JSValueConst value) {
  static JSValue method;

  if(JS_VALUE_GET_TAG(method) != JS_TAG_OBJECT)
    method = js_global_prototype_func(ctx, "Object", "toString");

  return js_object_tostring2(ctx, method, value);
}

const char*
js_object_tostring2(JSContext* ctx, JSValueConst method, JSValueConst value) {
  JSValue str = JS_Call(ctx, method, value, 0, 0);
  const char* s = JS_ToCString(ctx, str);
  JS_FreeValue(ctx, str);
  return s;
}

const char*
js_function_tostring(JSContext* ctx, JSValueConst value) {
  JSValue str = js_value_tostring(ctx, "Function", value);
  const char* s = JS_ToCString(ctx, str);
  JS_FreeValue(ctx, str);
  return s;
}

JSValue
js_function_prototype(JSContext* ctx) {
  return js_global_prototype(ctx, "Function");
}

int
js_propenum_cmp(const void* a, const void* b, void* ptr) {
  JSContext* ctx = ptr;
  const char* stra = JS_AtomToCString(ctx, ((const JSPropertyEnum*)a)->atom);
  const char* strb = JS_AtomToCString(ctx, ((const JSPropertyEnum*)b)->atom);
  int ret = strverscmp(stra, strb);
  JS_FreeCString(ctx, stra);
  JS_FreeCString(ctx, strb);
  return ret;
}

void
js_propertyenums_clear(JSContext* ctx, JSPropertyEnum* props, size_t len) {
  for(uint32_t i = 0; i < len; i++)
    JS_FreeAtom(ctx, props[i].atom);
  // js_free(ctx, props);
}

void
js_strv_free(JSContext* ctx, char** strv) {
  if(strv == 0)
    return;

  for(size_t i = 0; strv[i]; i++)
    js_free(ctx, strv[i]);

  js_free(ctx, strv);
}

void
js_strv_free_rt(JSRuntime* rt, char** strv) {
  if(strv == 0)
    return;

  for(size_t i = 0; strv[i]; i++)
    js_free_rt(rt, strv[i]);

  js_free_rt(rt, strv);
}

JSValue
js_strv_to_array(JSContext* ctx, char** strv) {
  JSValue ret = JS_NewArray(ctx);

  if(strv)
    for(size_t i = 0; strv[i]; i++)
      JS_SetPropertyUint32(ctx, ret, i, JS_NewString(ctx, strv[i]));

  return ret;
}

size_t
js_strv_length(char** strv) {
  size_t i = 0;

  while(strv[i])
    ++i;

  return i;
}

char**
js_strv_dup(JSContext* ctx, char** strv) {
  size_t i, len = js_strv_length(strv);
  char** ret = js_malloc(ctx, (len + 1) * sizeof(char*));

  for(i = 0; i < len; i++)
    ret[i] = js_strdup(ctx, strv[i]);

  ret[i] = 0;
  return ret;
}

int32_t*
js_argv_to_int32v(JSContext* ctx, int argc, JSValueConst argv[]) {
  int32_t* ret;

  if((ret = js_malloc(ctx, sizeof(int32_t) * argc)))
    for(int i = 0; i < argc; i++)
      if(JS_ToInt32(ctx, &ret[i], argv[i]))
        ret[i] = 0;

  return ret;
}

JSAtom*
js_argv_to_atoms(JSContext* ctx, int argc, JSValueConst argv[]) {
  JSAtom* ret;

  if((ret = js_malloc(ctx, sizeof(JSAtom) * argc)))
    for(int i = 0; i < argc; i++)
      ret[i] = JS_ValueToAtom(ctx, argv[i]);

  return ret;
}

JSAtom
js_symbol_static_atom(JSContext* ctx, const char* name) {
  JSValue sym = js_symbol_static_value(ctx, name);
  JSAtom ret = JS_ValueToAtom(ctx, sym);
  JS_FreeValue(ctx, sym);
  return ret;
}

JSValue
js_symbol_static_value(JSContext* ctx, const char* name) {
  JSValue ctor = js_symbol_ctor(ctx);
  JSValue ret = JS_GetPropertyStr(ctx, ctor, name);
  JS_FreeValue(ctx, ctor);
  return ret;
}

JSValue
js_symbol_ctor(JSContext* ctx) {
  return js_global_get_str(ctx, "Symbol");
}

JSValue
js_symbol_invoke_static(JSContext* ctx, const char* name, JSValueConst arg) {
  JSAtom method_name = JS_NewAtom(ctx, name);
  JSValue ret = JS_Invoke(ctx, js_symbol_ctor(ctx), method_name, 1, &arg);
  JS_FreeAtom(ctx, method_name);
  return ret;
}

JSValue
js_symbol_for(JSContext* ctx, const char* sym_for) {
  JSValue key = JS_NewString(ctx, sym_for);
  JSValue sym = js_symbol_invoke_static(ctx, "for", key);
  JS_FreeValue(ctx, key);
  return sym;
}

JSValue
js_symbol_keyfor(JSContext* ctx, JSValueConst sym) {
  return js_symbol_invoke_static(ctx, "keyFor", sym);
}

JSAtom
js_symbol_for_atom(JSContext* ctx, const char* sym_for) {
  JSValue sym = js_symbol_for(ctx, sym_for);
  JSAtom atom = JS_ValueToAtom(ctx, sym);
  JS_FreeValue(ctx, sym);
  return atom;
}

JSValue
js_symbol_to_string(JSContext* ctx, JSValueConst sym) {
  JSValue value = js_symbol_keyfor(ctx, sym);

  if(JS_IsUndefined(value)) {
    JSAtom atom = JS_ValueToAtom(ctx, sym);
    JSValue str = JS_AtomToString(ctx, atom);
    JS_FreeAtom(ctx, atom);
    return str;
  }

  return value;
}

const char*
js_symbol_to_cstring(JSContext* ctx, JSValueConst sym) {
  JSValue value = js_symbol_to_string(ctx, sym);
  const char* str = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);
  return str;
}

JSValue*
js_values_dup(JSContext* ctx, int nvalues, JSValueConst* values) {
  JSValue* ret = js_mallocz_rt(JS_GetRuntime(ctx), sizeof(JSValue) * nvalues);

  for(int i = 0; i < nvalues; i++)
    ret[i] = JS_DupValueRT(JS_GetRuntime(ctx), values[i]);

  return ret;
}

/*void
js_values_free(JSContext* ctx, int nvalues, JSValueConst* values) {
  int i;

  for(i = 0; i < nvalues; i++) JS_FreeValue(ctx, values[i]);
  js_free(ctx, values);
}*/

void
js_values_free(JSRuntime* rt, int nvalues, JSValueConst* values) {
  for(int i = 0; i < nvalues; i++)
    JS_FreeValueRT(rt, values[i]);

  js_free_rt(rt, values);
}

JSValue
js_values_toarray(JSContext* ctx, int nvalues, JSValueConst* values) {
  JSValue ret = JS_NewArray(ctx);

  for(int i = 0; i < nvalues; i++)
    JS_SetPropertyUint32(ctx, ret, i, JS_DupValue(ctx, values[i]));

  return ret;
}

static const char* const js_value_typenames[] = {
    "undefined",     "null",         "bool",      "int",   "object", "string",
    "symbol",
#ifdef QJS_BIGNUM_EXT
    "big_float",
#endif
    "big_int",
#ifdef QJS_BIGNUM_EXT
    "big_decimal",
#endif
    "float64",       "nan",          "function",  "array", "module", "function_bytecode",
    "uninitialized", "catch_offset", "exception", 0,
};

const char* const*
js_value_types() {
  return js_value_typenames;
}

const int
js_value_types_length() {
  for(int i = 0;; i++)
    if(js_value_typenames[i] == 0)
      return i;

  return -1;
}

const char*
js_value_typeof(JSValueConst value) {
  ValueTypeFlag flag = js_value_type_flag(value);

  if(flag == FLAG_INVALID)
    return NULL;

  return js_value_typenames[flag];
}

const char*
js_value_type_name(ValueType type) {
  ValueTypeFlag flag = js_value_type2flag(type);

  if(flag >= 0 && (unsigned)flag < countof(js_value_typenames))
    return js_value_typenames[flag];

  return 0;
}

const char*
js_value_typestr(JSContext* ctx, JSValueConst value) {
  ValueType type = js_value_type(ctx, value);

  return js_value_type_name(type);
}

ValueType
js_value_type(JSContext* ctx, JSValueConst value) {
  ValueTypeFlag flag;
  ValueType type = 0;

  if((flag = js_value_type_get(ctx, value)) == FLAG_INVALID)
    return 0;

  if(flag == FLAG_ARRAY /*|| flag == FLAG_FUNCTION*/)
    type |= TYPE_OBJECT;

  type |= 1 << flag;

  return type;
}

JSValue
js_value_clone(JSContext* ctx, JSValueConst value) {
  ValueType type = 1 << js_value_type_get(ctx, value);
  JSValue ret = JS_UNDEFINED;

  switch(type) {
    case TYPE_STRING: {
      size_t len;
      const char* str = JS_ToCStringLen(ctx, &len, value);
      ret = JS_NewStringLen(ctx, str, len);
      JS_FreeCString(ctx, str);
      break;
    }

    case TYPE_INT: {
      ret = JS_NewInt32(ctx, JS_VALUE_GET_INT(value));
      break;
    }

    case TYPE_FLOAT64: {
      ret = JS_NewFloat64(ctx, JS_VALUE_GET_FLOAT64(value));
      break;
    }

    case TYPE_BOOL: {
      ret = JS_NewBool(ctx, JS_VALUE_GET_BOOL(value));
      break;
    }

    case TYPE_FUNCTION:
    case TYPE_ARRAY:
    case TYPE_OBJECT: {
      JSPropertyEnum* tab_atom;
      uint32_t tab_atom_len;

      ret = JS_IsArray(ctx, value) ? JS_NewArray(ctx) : JS_NewObject(ctx);

      if(!JS_GetOwnPropertyNames(ctx, &tab_atom, &tab_atom_len, value, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY)) {
        for(uint32_t i = 0; i < tab_atom_len; i++) {
          JSValue prop = JS_GetProperty(ctx, value, tab_atom[i].atom);

          JS_SetProperty(ctx, ret, tab_atom[i].atom, js_value_clone(ctx, prop));
        }
      }

      break;
    }

    case TYPE_UNDEFINED:
    case TYPE_NULL:
    case TYPE_SYMBOL:
#ifdef QJS_BIGNUM_EXT
    case TYPE_BIG_DECIMAL:
    case TYPE_BIG_FLOAT:
#endif
    case TYPE_BIG_INT: {
      ret = JS_DupValue(ctx, value);
      break;
    }

    default: {
      ret = JS_ThrowTypeError(ctx, "No such type: %s (0x%08x)\n", js_value_type_name(type), type);
      break;
    }
  }

  return ret;
}

void
js_value_dump(JSContext* ctx, JSValueConst value, DynBuf* db) {
  /*const char* str;*/
  size_t len;

  dbuf_putstr(db, js_value_typestr(ctx, value));
  dbuf_putstr(db, " ");

  switch(js_value_type_get(ctx, value)) {
    case FLAG_EXCEPTION: {
      dbuf_putstr(db, "[exception]");
      break;
    }

    case FLAG_MODULE: {
      dbuf_putstr(db, "[module]");
      break;
    }

    case FLAG_FUNCTION: {
      JSValue src = js_invoke(ctx, value, "toSource", 0, 0);

      js_value_dump(ctx, src, db);
      JS_FreeValue(ctx, src);
      break;
    }

    case FLAG_OBJECT: {
      const char* str = js_object_tostring(ctx, value);

      dbuf_putstr(db, str);
      JS_FreeCString(ctx, str);

      if(db->size && db->buf[db->size - 1] == '\n')
        db->size--;

      break;
    }

    default: {
      int is_string = JS_IsString(value);

      if(is_string)
        dbuf_putc(db, '"');

      const char* str = JS_ToCStringLen(ctx, &len, value);
      dbuf_append(db, (const uint8_t*)str, len);

      JS_FreeCString(ctx, str);

      if(is_string)
        dbuf_putc(db, '"');
#ifdef QJS_BIGNUM_EXT
      else if(JS_IsBigFloat(value))
        dbuf_putc(db, 'l');
      else if(JS_IsBigDecimal(value))
        dbuf_putc(db, 'm');
#endif
      else if(JS_IsBigInt(ctx, value))
        dbuf_putc(db, 'n');

      break;
    }
  }
}

int
js_value_equals(JSContext* ctx, JSValueConst a, JSValueConst b, BOOL deep) {
  int32_t ta = js_value_type(ctx, a), tb = js_value_type(ctx, b);
  BOOL ret = FALSE;

  if(ta != tb) {
    ret = FALSE;
  } else if(ta & tb & (TYPE_NULL | TYPE_UNDEFINED | TYPE_NAN)) {
    ret = TRUE;
  } else if(ta & tb & (TYPE_BIGNUM)) {
    const char *astr, *bstr = 0;

    if((astr = JS_ToCString(ctx, a)) && (bstr = JS_ToCString(ctx, b)))
      ret = !strcmp(astr, bstr);

    if(astr)
      JS_FreeCString(ctx, astr);
    if(bstr)
      JS_FreeCString(ctx, bstr);

  } else if(ta & TYPE_INT) {
    int32_t inta = JS_VALUE_GET_INT(a), intb = JS_VALUE_GET_INT(b);

    ret = inta == intb;
  } else if(ta & TYPE_BOOL) {
    BOOL boola = !!JS_VALUE_GET_BOOL(a), boolb = !!JS_VALUE_GET_BOOL(b);

    ret = boola == boolb;

  } else if(ta & TYPE_FLOAT64) {
    double flta = JS_VALUE_GET_FLOAT64(a), fltb = JS_VALUE_GET_FLOAT64(b);

    ret = flta == fltb;

  } else if(ta & TYPE_OBJECT) {
    ret = js_object_same(a, b);

    if(deep && !ret)
      ret = js_object_equals(ctx, a, b, TRUE);

  } else if(ta & TYPE_STRING) {
    const char *stra = JS_ToCString(ctx, a), *strb = JS_ToCString(ctx, b);

    if(stra && strb)
      ret = !strcmp(stra, strb);

    if(stra)
      JS_FreeCString(ctx, stra);
    if(strb)
      JS_FreeCString(ctx, strb);
  } else if(ta & TYPE_SYMBOL) {
    JSAtom aa = JS_ValueToAtom(ctx, a), ab = JS_ValueToAtom(ctx, b);

    ret = aa == ab;

    JS_FreeAtom(ctx, aa);
    JS_FreeAtom(ctx, ab);
  }

  return ret;
}

int
js_value_tosize(JSContext* ctx, size_t* sz, JSValueConst value) {
  uint64_t u64 = *sz;
  int r = JS_ToIndex(ctx, &u64, value);

  *sz = u64;
  return r;
}

JSValue
js_value_coerce(JSContext* ctx, const char* func_name, JSValueConst arg) {
  return js_global_call(ctx, func_name, 1, &arg);
}

void*
js_value_ptr(JSValueConst v) {
  return JS_VALUE_GET_PTR(v);
}

JSValueConst
js_value_mkptr(int tag, void* ptr) {
  return JS_MKPTR(tag, ptr);
}

JSValueConst
js_value_mkobj(void* obj) {
  return JS_MKPTR(JS_TAG_OBJECT, obj);
}

void*
js_value_obj(JSValueConst v) {
  return (JS_IsObject(v) && !JS_IsNull(v)) ? JS_VALUE_GET_PTR(v) : 0;
}

void
js_cstring_dump(JSContext* ctx, JSValueConst value, DynBuf* db) {
  size_t len;
  const char* str = JS_ToCStringLen(ctx, &len, value);

  dbuf_append(db, (const uint8_t*)str, len);
  JS_FreeCString(ctx, str);
}

JSValue
module_value(JSContext* ctx, JSModuleDef* m) {
  return m != NULL ? JS_DupValue(ctx, JS_MKPTR(JS_TAG_MODULE, m)) : JS_NULL;
}

const char*
module_namecstr(JSContext* ctx, JSModuleDef* m) {
  JSAtom atom = JS_GetModuleName(ctx, m);
  const char* ret = JS_AtomToCString(ctx, atom);

  JS_FreeAtom(ctx, atom);
  return ret;
}

JSModuleDef*
js_module_def(JSContext* ctx, JSValueConst value) {
  if(JS_VALUE_GET_TAG(value) == JS_TAG_MODULE)
    return JS_VALUE_GET_PTR(value);

  if(JS_IsObject(value)) {
    JSAtom atom = js_symbol_static_atom(ctx, "toStringTag");
    uint64_t addrval = 0;
    const char *tag, *addr;

    if(JS_HasProperty(ctx, value, atom) && js_has_propertystr(ctx, value, "address") && (tag = js_get_property_cstring(ctx, value, atom)) &&
       !strcmp(tag, "Module")) {
      if((addr = js_get_propertystr_cstring(ctx, value, "address"))) {
        if(addr[0] == '0' && addr[1] == 'x')
          if(scan_xlonglong(addr + 2, &addrval) == 0)
            addrval = 0;

        JS_FreeCString(ctx, addr);
      }

      JS_FreeCString(ctx, tag);
    }

    JS_FreeAtom(ctx, atom);

    if(addrval)
      return (JSModuleDef*)(void*)(uintptr_t)addrval;
  }

  return 0;
}

BOOL
js_is_primitive(JSValueConst obj) {
  switch(JS_VALUE_GET_TAG(obj)) {
#ifdef QJS_BIGNUM_EXT
    case JS_TAG_BIG_DECIMAL:
    case JS_TAG_BIG_FLOAT:
#endif
    case JS_TAG_BIG_INT:
    case JS_TAG_SYMBOL:
    case JS_TAG_STRING:
    case JS_TAG_INT:
    case JS_TAG_BOOL:
    case JS_TAG_NULL:
    case JS_TAG_UNDEFINED:
    case JS_TAG_FLOAT64: return TRUE;
  }

  return FALSE;
}

#if HAVE_JS_GETCLASSID
/* Constructs a throwaway instance of the named global constructor to learn
 * its JS_GetClassID() tag, since built-in class IDs aren't exposed as
 * constants in quickjs.h (they vary by quickjs build/version) but ARE fixed
 * for the lifetime of the process once assigned - one probe per type, cached
 * by the caller, replaces an instanceof-plus-prototype-walk on every call. */
static JSClassID
js_probe_class_id(JSContext* ctx, const char* class_name, int argc, JSValueConst* argv) {
  JSValue ctor = js_global_get_str(ctx, class_name);
  JSClassID id = JS_INVALID_CLASS_ID;

  if(JS_IsFunction(ctx, ctor)) {
    JSValue inst = JS_CallConstructor(ctx, ctor, argc, argv);

    if(JS_IsException(inst))
      JS_FreeValue(ctx, JS_GetException(ctx));
    else
      id = JS_GetClassID(inst);

    JS_FreeValue(ctx, inst);
  }

  JS_FreeValue(ctx, ctor);
  return id;
}
#endif

BOOL
js_is_arraybuffer(JSContext* ctx, JSValueConst value) {
#if HAVE_JS_GETCLASSID
  static JSClassID id;
  static BOOL probed;

  if(!probed) {
    JSValueConst argv[] = {JS_NewInt32(ctx, 0)};
    id = js_probe_class_id(ctx, "ArrayBuffer", 1, argv);
    probed = TRUE;
  }

  if(id != JS_INVALID_CLASS_ID)
    return JS_GetClassID(value) == id;
#endif

  return JS_IsObject(value) && (js_global_instanceof(ctx, value, "ArrayBuffer") || js_object_is(ctx, value, "[object ArrayBuffer]"));
}

BOOL
js_is_sharedarraybuffer(JSContext* ctx, JSValueConst value) {
#if HAVE_JS_GETCLASSID
  static JSClassID id;
  static BOOL probed;

  if(!probed) {
    JSValueConst argv[] = {JS_NewInt32(ctx, 0)};
    id = js_probe_class_id(ctx, "SharedArrayBuffer", 1, argv);
    probed = TRUE;
  }

  if(id != JS_INVALID_CLASS_ID)
    return JS_GetClassID(value) == id;
#endif

  return JS_IsObject(value) && (js_global_instanceof(ctx, value, "SharedArrayBuffer") || js_object_is(ctx, value, "[object SharedArrayBuffer]"));
}

BOOL
js_is_date(JSContext* ctx, JSValueConst value) {
#if HAVE_JS_GETCLASSID
  static JSClassID id;
  static BOOL probed;

  if(!probed) {
    id = js_probe_class_id(ctx, "Date", 0, 0);
    probed = TRUE;
  }

  if(id != JS_INVALID_CLASS_ID)
    return JS_GetClassID(value) == id;
#endif

  return JS_IsObject(value) && (js_global_instanceof(ctx, value, "Date") || js_object_is(ctx, value, "[object Date]"));
}

BOOL
js_is_map(JSContext* ctx, JSValueConst value) {
#if HAVE_JS_GETCLASSID
  static JSClassID id;
  static BOOL probed;

  if(!probed) {
    id = js_probe_class_id(ctx, "Map", 0, 0);
    probed = TRUE;
  }

  if(id != JS_INVALID_CLASS_ID)
    return JS_GetClassID(value) == id;
#endif

  return JS_IsObject(value) && (js_global_instanceof(ctx, value, "Map") || js_object_is(ctx, value, "[object Map]"));
}

BOOL
js_is_weakmap(JSContext* ctx, JSValueConst value) {
#if HAVE_JS_GETCLASSID
  static JSClassID id;
  static BOOL probed;

  if(!probed) {
    id = js_probe_class_id(ctx, "WeakMap", 0, 0);
    probed = TRUE;
  }

  if(id != JS_INVALID_CLASS_ID)
    return JS_GetClassID(value) == id;
#endif

  return JS_IsObject(value) && (js_global_instanceof(ctx, value, "WeakMap") || js_object_is(ctx, value, "[object WeakMap]"));
}

BOOL
js_is_set(JSContext* ctx, JSValueConst value) {
#if HAVE_JS_GETCLASSID
  static JSClassID id;
  static BOOL probed;

  if(!probed) {
    id = js_probe_class_id(ctx, "Set", 0, 0);
    probed = TRUE;
  }

  if(id != JS_INVALID_CLASS_ID)
    return JS_GetClassID(value) == id;
#endif

  return JS_IsObject(value) && (js_global_instanceof(ctx, value, "Set") || js_object_is(ctx, value, "[object Set]"));
}

BOOL
js_is_generator(JSContext* ctx, JSValueConst value) {
  JSValue ctor = js_generator_constructor(ctx);
  BOOL ret = JS_IsInstanceOf(ctx, value, ctor);
  JS_FreeValue(ctx, ctor);

  if(!ret) {
    JSValue proto = js_generator_prototype(ctx);
    ret = js_has_prototype(ctx, value, proto);
    JS_FreeValue(ctx, proto);
  }

  return ret;
}

BOOL
js_is_regexp(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) && js_global_instanceof(ctx, value, "RegExp");
}

BOOL
js_is_promise(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) && js_global_instanceof(ctx, value, "Promise");
}

BOOL
js_is_dataview(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) && js_global_instanceof(ctx, value, "DataView");
}

BOOL
js_is_error(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) && js_global_instanceof(ctx, value, "Error");
}

BOOL
js_is_iterable(JSContext* ctx, JSValueConst obj) {
  BOOL ret = FALSE;
  JSAtom atom = js_symbol_static_atom(ctx, "iterator");

  if(JS_HasProperty(ctx, obj, atom))
    ret = TRUE;

  JS_FreeAtom(ctx, atom);
  return ret;
}

BOOL
js_is_nan(JSValueConst obj) {
  return JS_VALUE_IS_NAN(obj);
}

JSValue
js_typedarray_prototype(JSContext* ctx) {
  if(JS_VALUE_GET_TAG(typedarray_prototype) != JS_TAG_OBJECT) {
    JSValue u8_proto = js_global_prototype(ctx, "Uint8Array");
    JSValue ta_proto = JS_GetPrototype(ctx, u8_proto);

    JS_FreeValue(ctx, u8_proto);
    typedarray_prototype = JS_DupValue(ctx, ta_proto);
    return ta_proto;
  }

  return JS_DupValue(ctx, typedarray_prototype);
}

JSValue
js_typedarray_newv(JSContext* ctx, int bits, BOOL floating, BOOL sign, int argc, JSValueConst argv[]) {
  char class_name[64] = {0};

  snprintf(class_name, sizeof(class_name), "%s%s%dArray", (!floating && bits >= 64) ? "Big" : "", floating ? "Float" : sign ? "Int" : "Uint", bits);
  {
    JSValue ctor = js_global_get_str(ctx, class_name);
    JSValue ret = JS_CallConstructor(ctx, ctor, argc, argv);
    JS_FreeValue(ctx, ctor);
    return ret;
  }
}

JSValue
js_typedarray_new(JSContext* ctx, int bits, BOOL floating, BOOL sign, JSValueConst buffer) {
  return js_typedarray_newv(ctx, bits, floating, sign, 1, &buffer);
}

JSValue
js_invoke(JSContext* ctx, JSValueConst this_obj, const char* method, int argc, JSValueConst argv[]) {
  JSAtom atom = JS_NewAtom(ctx, method);
  JSValue ret = JS_Invoke(ctx, this_obj, atom, argc, argv);

  JS_FreeAtom(ctx, atom);
  return ret;
}

JSValue
js_symbol_operatorset_value(JSContext* ctx) {
  return js_symbol_static_value(ctx, "operatorSet");
}

JSAtom
js_symbol_operatorset_atom(JSContext* ctx) {
  JSValue operator_set = js_symbol_operatorset_value(ctx);
  JSAtom atom = JS_ValueToAtom(ctx, operator_set);

  JS_FreeValue(ctx, operator_set);
  return atom;
}

JSValue
js_operators_create(JSContext* ctx, JSValue* this_obj) {
  JSValue operators = js_global_get_str(ctx, "Operators");
  JSValue create_fun = JS_UNDEFINED;

  /* Engines built without operator overloading have no Operators global; reading a property off undefined would leave a pending exception. */
  if(JS_IsObject(operators))
    create_fun = JS_GetPropertyStr(ctx, operators, "create");

  if(this_obj)
    *this_obj = operators;
  else
    JS_FreeValue(ctx, operators);

  return create_fun;
}

JSValue
js_number_new(JSContext* ctx, int32_t n) {
  if(n == INT32_MAX)
    return JS_NewFloat64(ctx, INFINITY);

  return JS_NewInt32(ctx, n);
}

BOOL
js_number_integral(JSValueConst value) {
  int tag = JS_VALUE_GET_TAG(value);

  if(tag == JS_TAG_INT)
    return TRUE;

  if(tag == JS_TAG_FLOAT64) {
    double num = JS_VALUE_GET_FLOAT64(value);
    int64_t i = num;

    if((num - i) < DBL_EPSILON)
      return TRUE;

    // return fmod(num, 1.0l) == 0.0l;
  }

  return FALSE;
}

JSValue
js_date_new(JSContext* ctx, JSValueConst arg) {
  JSValue ctor = js_global_get_str(ctx, "Date");
  JSValue ret = JS_CallConstructor(ctx, ctor, 1, &arg);

  JS_FreeValue(ctx, ctor);
  return ret;
}

JSValue
js_date_from_ms(JSContext* ctx, int64_t ms) {
  JSValue arg = JS_NewInt64(ctx, ms);
  JSValue ret = js_date_new(ctx, arg);

  JS_FreeValue(ctx, arg);
  return ret;
}

JSValue
js_date_from_time_ns(JSContext* ctx, time_t t, long ns) {
  return js_date_from_ms(ctx, t * 1000ull + ns / 1000000ull);
}

int64_t
js_date_gettime(JSContext* ctx, JSValueConst arg) {
  int64_t r = -1;
  JSAtom method = JS_NewAtom(ctx, "getTime");
  JSValue value = JS_Invoke(ctx, arg, method, 0, 0);

  JS_FreeAtom(ctx, method);

  if(JS_IsNumber(value))
    JS_ToInt64(ctx, &r, value);

  JS_FreeValue(ctx, value);
  return r;
}

int64_t
js_date_time(JSContext* ctx, JSValue arg) {
  int64_t r = -1;

  if(JS_IsObject(arg))
    r = js_date_gettime(ctx, arg);
  else if(!js_is_nullish(ctx, arg))
    JS_ToInt64(ctx, &r, arg);

  return r;
}

struct timespec
js_date_timespec(JSContext* ctx, JSValue arg) {
  int64_t r = js_date_time(ctx, arg);
  struct timespec ts;

  ts.tv_sec = r / 1000ull;
  ts.tv_nsec = (r % 1000ull) * 1000000ull;

  return ts;
}

void
js_arraybuffer_freevalue(JSRuntime* rt, void* opaque, void* ptr) {
  JSValue* valptr = opaque;

  JS_FreeValueRT(rt, *valptr);
  js_free_rt(rt, opaque);
}

JSValue
js_arraybuffer_fromvalue(JSContext* ctx, void* x, size_t n, JSValueConst val) {
  JSValue* valptr;

  if(!(valptr = js_malloc(ctx, sizeof(JSValue))))
    return JS_EXCEPTION;

  *valptr = JS_DupValue(ctx, val);

  return JS_NewArrayBuffer(ctx, x, n, js_arraybuffer_freevalue, valptr, FALSE);
}

void
js_arraybuffer_freeptr(JSRuntime* rt, void* opaque, void* ptr) {
  js_free_rt(rt, ptr);
}

int64_t
js_arraybuffer_bytelength(JSContext* ctx, JSValueConst value) {
  int64_t len = -1;

  if(js_is_arraybuffer(ctx, value)) {
    JSValue length = JS_GetPropertyStr(ctx, value, "byteLength");

    JS_ToInt64(ctx, &len, length);
    JS_FreeValue(ctx, length);
  }

  return len;
}

/*void
js_arraybuffer_freestring(JSRuntime* rt, void* opaque, void* ptr) {
  JSString* jstr = opaque;

  JS_FreeValueRT(rt, JS_MKPTR(JS_TAG_STRING, jstr));
}*/

static void
js_arraybuffer_mmap_free(JSRuntime* rt, void* opaque, void* ptr) {
  munmap(ptr, (size_t)opaque);
}

JSValue
js_arraybuffer_mmap(JSContext* ctx, const char* filename, BOOL shared) {
  MemoryBlock mb = block_mmap(filename, shared);

  if(mb.base == 0)
    return JS_ThrowInternalError(ctx, "Failed mmap() of '%s'", filename);

  return JS_NewArrayBuffer(ctx, mb.base, mb.size, &js_arraybuffer_mmap_free, (void*)mb.size, shared);
}

JSValue
js_eval_module(JSContext* ctx, JSValueConst obj, BOOL load_only) {
  int tag = JS_VALUE_GET_TAG(obj);

  if(tag == JS_TAG_MODULE) {
    if(!load_only && JS_ResolveModule(ctx, obj) < 0) {
      JS_FreeValue(ctx, obj);
      return JS_ThrowInternalError(ctx, "Failed resolving module");
    }

    js_module_set_import_meta(ctx, obj, FALSE, !load_only);

    return load_only ? JS_DupValue(ctx, obj) : JS_EvalFunction(ctx, obj);
  }

  return JS_ThrowInternalError(ctx, "invalid tag %i", tag);
}

JSValue
js_eval_binary(JSContext* ctx, const uint8_t* buf, size_t buf_len, BOOL load_only) {
  JSValue obj = JS_ReadObject(ctx, buf, buf_len, JS_READ_OBJ_BYTECODE);

  if(JS_IsException(obj))
    return obj;

  if(!load_only) {
    JSValue tmp = js_eval_module(ctx, obj, load_only);
    int tag = JS_VALUE_GET_TAG(tmp);

    if(!JS_IsException(tmp) && !JS_IsUndefined(tmp))
      if(tag >= JS_TAG_FIRST && tag <= JS_TAG_FLOAT64)
        return tmp;
  }

  return obj;
}

JSValue
js_eval_buf(JSContext* ctx, const void* buf, size_t buf_len, const char* filename, int eval_flags) {
  JSValue ret;
  BOOL as_module = (eval_flags & JS_EVAL_TYPE_MASK) == JS_EVAL_TYPE_MODULE;
  int flags = (eval_flags & (JS_EVAL_TYPE_MASK | JS_EVAL_FLAG_MASK)) | (as_module ? JS_EVAL_FLAG_COMPILE_ONLY : 0);

  if(!filename)
    filename = "<input>";

  ret = JS_Eval(ctx, buf, buf_len, filename, flags);

  if(as_module) {
    /* for the modules, we compile then run to be able to set import.meta */
    if(!JS_IsException(ret)) {
      js_module_set_import_meta(ctx, ret, filename[0] != '<', !!(eval_flags & JS_EVAL_IS_MAIN));

      ret = JS_EvalFunction(ctx, ret);
    }
  }

  return ret;
}

JSValue
js_eval_this_buf(JSContext* ctx, JSValueConst this_obj, const void* buf, size_t buf_len, const char* filename, int eval_flags) {
  JSValue ret;
  BOOL as_module = (eval_flags & JS_EVAL_TYPE_MASK) == JS_EVAL_TYPE_MODULE;
  int flags = (eval_flags & (JS_EVAL_TYPE_MASK | JS_EVAL_FLAG_MASK)) | (as_module ? JS_EVAL_FLAG_COMPILE_ONLY : 0);

  if(!filename)
    filename = "<input>";

  ret = JS_EvalThis(ctx, this_obj, buf, buf_len, filename, flags);

  if(as_module) {
    /* for the modules, we compile then run to be able to set import.meta */
    if(!JS_IsException(ret)) {
      js_module_set_import_meta(ctx, ret, filename[0] != '<', !!(eval_flags & JS_EVAL_IS_MAIN));

      JSValue tmp = JS_EvalFunction(ctx, ret);
      JS_FreeValue(ctx, ret);
      ret = tmp;
    }

    if(JS_VALUE_GET_TAG(ret) == JS_TAG_MODULE) {
      JSModuleDef* m;

      if((m = js_value_ptr(ret))) {
      }
    }
  }

  return ret;
}

JSValue
js_eval_file(JSContext* ctx, const char* filename, int eval_flags) {
  uint8_t* buf;
  size_t buf_len;

  if(!(buf = js_load_file(ctx, &buf_len, filename)))
    return JS_ThrowInternalError(ctx, "Error loading '%s': %s", filename, strerror(errno));

  return js_eval_buf(ctx, buf, buf_len, filename, eval_flags);
}

JSValue
js_eval_this_file(JSContext* ctx, JSValueConst this_obj, const char* filename, int eval_flags) {
  uint8_t* buf;
  size_t buf_len;

  if(!(buf = js_load_file(ctx, &buf_len, filename)))
    return JS_ThrowInternalError(ctx, "Error loading '%s': %s", filename, strerror(errno));

  return js_eval_this_buf(ctx, this_obj, buf, buf_len, filename, eval_flags);
}

int
js_eval_str(JSContext* ctx, const char* str, const char* file, int flags) {
  int32_t ret = 0;
  JSValue val = js_eval_buf(ctx, str, strlen(str), file, flags);

  if(JS_IsException(val))
    ret = -1;
  else if(JS_IsNumber(val))
    JS_ToInt32(ctx, &ret, val);

  return ret;
}

FORMAT_STRING(3, 4) JSValue js_eval_fmt(JSContext* ctx, int flags, const char* fmt, ...) {
  JSValue ret;
  va_list ap;
  DynBuf buf;
  size_t len;

  dbuf_init2(&buf, ctx, (realloc_func*)&utils_js_realloc);

  va_start(ap, fmt);
  len = vsnprintf((char*)buf.buf, 0, fmt, ap);
  va_end(ap);

  dbuf_claim(&buf, len + 1);

  va_start(ap, fmt);
  dbuf_vprintf(&buf, fmt, ap);
  va_end(ap);

  dbuf_0(&buf);
  ret = js_eval_buf(ctx, (const char*)buf.buf, buf.size, NULL, flags);
  dbuf_free(&buf);

  return ret;
}

thread_local uint64_t js_pending_signals = 0;

void
js_error_dump(JSContext* ctx, JSValueConst error, DynBuf* db) {
  const char *str, *stack = 0;

  if(JS_IsObject(error)) {
    JSValue st = JS_GetPropertyStr(ctx, error, "stack");

    if(!JS_IsUndefined(st))
      stack = JS_ToCString(ctx, st);

    JS_FreeValue(ctx, st);
  }

  if((str = JS_ToCString(ctx, error))) {
    const char* type = JS_IsObject(error) ? js_object_classname(ctx, error) : js_value_typestr(ctx, error);

    if(!str_start(str, type)) {
      dbuf_putstr(db, type);
      dbuf_putstr(db, ": ");
    }

    dbuf_putstr(db, str);
    dbuf_putc(db, '\n');

    if(stack) {
      dbuf_putstr(db, "STACK\n");
      dbuf_putstr(db, stack);
      dbuf_putc(db, '\n');
    }

    dbuf_0(db);
  }

  if(stack)
    JS_FreeCString(ctx, stack);

  JS_FreeCString(ctx, str);
}

char*
js_error_tostring(JSContext* ctx, JSValueConst error) {
  DynBuf db;

  dbuf_init2(&db, ctx, (realloc_func*)&utils_js_realloc);
  js_error_dump(ctx, error, &db);

  return (char*)db.buf;
}

void
js_error_print(JSContext* ctx, JSValueConst error) {
  const char *str = 0, *stack = 0;

  if(JS_IsObject(error)) {
    JSValue st = JS_GetPropertyStr(ctx, error, "stack");

    if(!JS_IsUndefined(st))
      stack = JS_ToCString(ctx, st);

    JS_FreeValue(ctx, st);
  }

  if(!JS_IsNull(error) && (str = JS_ToCString(ctx, error))) {
    const char* type = JS_IsObject(error) ? js_object_classname(ctx, error) : js_value_typestr(ctx, error);
    const char* exception = str;
    size_t typelen = strlen(type);

    if(!strncmp(exception, type, typelen) && exception[typelen] == ':')
      exception += typelen + 2;

    fprintf(stderr, "%s: %s\n", type, exception);
  }

  if(stack && *stack)
    fprintf(stderr, "Stack:\n%s\n", stack);

  fflush(stderr);

  if(stack)
    JS_FreeCString(ctx, stack);

  if(str)
    JS_FreeCString(ctx, str);
}

JSValue
js_error_stack(JSContext* ctx) {
  JSValue error = JS_NewError(ctx); // js_object_error(ctx, "");
  JSValue stack = JS_GetPropertyStr(ctx, error, "stack");

  JS_FreeValue(ctx, error);

  return stack;
}

/* Namespace of an already-loadable module, for hosts (qjsm) that don't expose os/io as globals.
   JS_LoadModule() settles through the job queue even for a loaded module, so the jobs are run here. */
static JSValue
js_module_namespace_sync(JSContext* ctx, const char* module_name) {
  JSRuntime* rt = JS_GetRuntime(ctx);
  JSContext* job_ctx;
  JSValue ns = JS_UNDEFINED, promise = JS_LoadModule(ctx, ".", module_name);

  if(JS_IsException(promise))
    return promise;

  while(JS_PromiseState(ctx, promise) == JS_PROMISE_PENDING && JS_ExecutePendingJob(rt, &job_ctx) > 0) {}

  if(JS_PromiseState(ctx, promise) == JS_PROMISE_FULFILLED)
    ns = JS_PromiseResult(ctx, promise);

  JS_FreeValue(ctx, promise);
  return ns;
}

JSValue
js_iohandler_fn(JSContext* ctx, BOOL write, const char* global_obj) {
  const char* handlers[2] = {"setReadHandler", "setWriteHandler"};
  JSValue set_handler = JS_UNDEFINED, ns = js_global_get_str(ctx, global_obj ? global_obj : "os");
  const char* module_name = global_obj ? global_obj : "os";

  if(js_is_null_or_undefined(ns)) {
    JS_FreeValue(ctx, ns);
    ns = js_module_namespace_sync(ctx, module_name);

    if(JS_IsException(ns))
      return JS_ThrowReferenceError(ctx, "'%s' module required", module_name);
  }

  if(!js_is_null_or_undefined(ns))
    set_handler = JS_GetPropertyStr(ctx, ns, handlers[!!write]);

  JS_FreeValue(ctx, ns);

  if(js_is_null_or_undefined(set_handler))
    return JS_ThrowReferenceError(ctx, "no %s.%s function", module_name, handlers[!!write]);

  return set_handler;
}

BOOL
js_iohandler_set(JSContext* ctx, JSValueConst set_handler, int fd, JSValue handler) {

  if(JS_IsException(set_handler))
    return FALSE;

  JSValue args[2] = {
      JS_NewInt32(ctx, fd),
      handler,
  };
  JSValue ret = JS_Call(ctx, set_handler, JS_UNDEFINED, countof(args), args);

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);

  if(JS_IsException(ret))
    return FALSE;

  JS_FreeValue(ctx, ret);

  return TRUE;
}

JSValue
js_promise_then(JSContext* ctx, JSValueConst promise, JSValueConst func) {
  return js_invoke(ctx, promise, "then", 1, &func);
}

JSValue
js_promise_immediate(JSContext* ctx, BOOL reject, JSValueConst value) {
  JSValue ret, promise, resolving_funcs[2];
  promise = JS_NewPromiseCapability(ctx, resolving_funcs);
  ret = JS_Call(ctx, resolving_funcs[!!reject], JS_UNDEFINED, 1, &value);
  JS_FreeValue(ctx, ret);

  JS_FreeValue(ctx, resolving_funcs[0]);
  JS_FreeValue(ctx, resolving_funcs[1]);
  return promise;
}

JSValue
js_promise_resolve(JSContext* ctx, JSValueConst value) {
  return js_promise_immediate(ctx, FALSE, value);
}

JSValue
js_to_source(JSContext* ctx, JSValueConst this_obj) {
  JSValue ret = JS_UNDEFINED;
  JSAtom key = JS_NewAtom(ctx, "toSource");

  if(JS_HasProperty(ctx, this_obj, key))
    ret = JS_Invoke(ctx, this_obj, key, 0, 0);
  else
    ret = JS_ThrowTypeError(ctx, "value has no .toSource() method");

  JS_FreeAtom(ctx, key);
  return ret;
}

void
arguments_dump(Arguments const* args, /*JSContext* ctx,*/ DynBuf* dbuf) {
  int n = args->c, i;

  if(n > 1)
    dbuf_putstr(dbuf, "(");

  for(i = 0; i < n; i++) {
    const char* arg = args->v[i];

    if(i > 0)
      dbuf_putstr(dbuf, ", ");
    dbuf_putstr(dbuf, arg ? arg : "NULL");
  }

  if(n > 1)
    dbuf_putstr(dbuf, ")");
}

BOOL
arguments_alloc(Arguments* args, JSContext* ctx, int n) {
  int i, j, c;

  if(args->a) {

    if(!(args->v = js_realloc(ctx, args->v, sizeof(char*) * (n + 1))))
      return FALSE;

    for(i = args->c; i < args->a; i++)
      args->v[i] = 0;
    args->a = n;
  } else {
    char** v;

    if(!(v = js_mallocz(ctx, sizeof(char*) * (n + 1))))
      return FALSE;

    c = MIN_NUM(args->c, n);

    for(i = 0; i < c; i++)
      v[i] = js_strdup(ctx, args->v[i]);

    for(j = c; j < args->c; j++)
      js_free(ctx, (void*)args->v[j]);

    for(j = i; j <= n; j++)
      v[j] = 0;
    args->v = (const char**)v;
    args->c = c;
    args->a = n;
  }

  return TRUE;
}

const char*
arguments_push(Arguments* args, JSContext* ctx, const char* arg) {
  int r;

  if(args->c + 1 >= args->a)

    if(!arguments_alloc(args, ctx, args->a + 1))
      return 0;
  r = args->c;
  args->v[r] = js_strdup(ctx, arg);
  args->v[r + 1] = 0;
  args->c++;
  return args->v[r];
}

char*
js_tostringlen(JSContext* ctx, size_t* lenp, JSValueConst value) {
  size_t len;
  const char* cstr;
  char* ret = 0;

  if((cstr = JS_ToCStringLen(ctx, &len, value))) {
    ret = js_strndup(ctx, cstr, len);

    if(lenp)
      *lenp = len;

    JS_FreeCString(ctx, cstr);
  }

  return ret;
}

char*
js_atom_tostring(JSContext* ctx, JSAtom atom) {
  const char* cstr;
  char* ret = 0;

  if((cstr = JS_AtomToCString(ctx, atom))) {
    ret = js_strdup(ctx, cstr);
    JS_FreeCString(ctx, cstr);
  }

  return ret;
}

uint64_t
js_touint64(JSContext* ctx, JSValueConst value) {
  uint64_t ret = 0;
  if(JS_ToIndex(ctx, &ret, value)) {
    int64_t i64;
    JS_GetException(ctx);
    if(!JS_ToInt64Ext(ctx, &i64, value))
      ret = i64;
  }
  return ret;
}

int
js_toint64clamp(JSContext* ctx, int64_t* pres, JSValueConst val, int64_t min, int64_t max, int64_t neg_offset) {
  int res;

  if(!(res = JS_ToInt64Ext(ctx, pres, val))) {
    if(*pres < 0)
      *pres += neg_offset;
    if(*pres < min)
      *pres = min;
    else if(*pres > max)
      *pres = max;
  }

  return res;
}

void*
js_topointer(JSContext* ctx, JSValueConst value) {
  if(js_is_null_or_undefined(value))
    return 0;

  if(JS_IsObject(value)) {
    InputBuffer buf = js_input_buffer(ctx, value);
    void* ptr = (void*)inputbuffer_data(&buf);

    inputbuffer_free(&buf, ctx);

    if(ptr)
      return ptr;

    JS_FreeValue(ctx, JS_GetException(ctx));
  }

  return (void*)(uintptr_t)DEF6432(js_touint64, js_touint32)(ctx, value);
}

int JS_ToInt64Clamp(JSContext*, int64_t*, JSValueConst, int64_t, int64_t, int64_t);

char*
js_tostring(JSContext* ctx, JSValueConst value) {
  return js_tostringlen(ctx, 0, value);
}

char*
js_tosource(JSContext* ctx, JSValueConst value) {
  JSValue src = js_to_source(ctx, value);
  const char* str = JS_ToCString(ctx, src);
  JS_FreeValue(ctx, src);
  char* ret = js_strdup(ctx, str);
  JS_FreeCString(ctx, str);
  return ret;
}

wchar_t*
js_towstringlen(JSContext* ctx, size_t* lenp, JSValueConst value) {
  size_t i, len;
  const char* cstr;
  wchar_t* ret = 0;

  if((cstr = JS_ToCStringLen(ctx, &len, value))) {
    ret = js_mallocz(ctx, sizeof(wchar_t) * (len + 1));
    const uint8_t *ptr = (const uint8_t*)cstr, *end = (const uint8_t*)cstr + len;

    for(i = 0; ptr < end;)
      ret[i++] = unicode_from_utf8(ptr, end - ptr, &ptr);

    if(lenp)
      *lenp = i;
  }

  return ret;
}

JSValue
js_newpointer(JSContext* ctx, void* ptr) {
  if(ptr == 0)
    return JS_NULL;

  return DEF6432(JS_NewBigUint64, JS_NewUint32)(ctx, (uintptr_t)ptr);
}

JSValue
js_get_tostringtag_value(JSContext* ctx, JSValueConst obj) {
  JSAtom tostring_tag = js_symbol_static_atom(ctx, "toStringTag");
  JSValue ret = JS_GetProperty(ctx, obj, tostring_tag);
  JS_FreeAtom(ctx, tostring_tag);
  return ret;
}

void
js_set_tostringtag_value(JSContext* ctx, JSValueConst obj, JSValue value) {
  JSAtom tostring_tag = js_symbol_static_atom(ctx, "toStringTag");
  JS_DefinePropertyValue(ctx, obj, tostring_tag, value, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
  JS_FreeAtom(ctx, tostring_tag);
}

const char*
js_get_tostringtag_cstr(JSContext* ctx, JSValueConst obj) {
  JSValue tag = js_get_tostringtag_value(ctx, obj);
  const char* ret = 0;

  if(JS_IsString(tag))
    ret = JS_ToCString(ctx, tag);

  JS_FreeValue(ctx, tag);
  return ret;
}

typedef struct {
  CClosureFunc* func;
  uint16_t length;
  uint16_t magic;
  void* opaque;
  void (*opaque_finalize)(JSRuntime*, void*);
} CClosureRecord;

static thread_local JSClassID js_cclosure_class_id;

static inline CClosureRecord*
js_cclosure_data(JSValueConst value) {
  return JS_GetOpaque(value, js_cclosure_class_id);
}

static inline CClosureRecord*
js_cclosure_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_cclosure_class_id);
}

static JSValue
js_cclosure_call(JSContext* ctx, JSValueConst func_obj, JSValueConst this_val, int argc, JSValueConst argv[], int flags) {
  CClosureRecord* ccr;
  JSValueConst* arg_buf;

  if(!(ccr = js_cclosure_data2(ctx, func_obj)))
    return JS_EXCEPTION;

  /* XXX: could add the function on the stack for debug */

  if(unlikely(argc < ccr->length)) {
    int i;
    arg_buf = alloca(sizeof(arg_buf[0]) * ccr->length);

    for(i = 0; i < argc; i++)
      arg_buf[i] = argv[i];

    for(i = argc; i < ccr->length; i++)
      arg_buf[i] = JS_UNDEFINED;
  } else {
    arg_buf = argv;
  }

  return ccr->func(ctx, this_val, argc, arg_buf, ccr->magic, ccr->opaque);
}

static void
js_cclosure_finalizer(JSRuntime* rt, JSValue val) {
  CClosureRecord* ccr;

  if((ccr = js_cclosure_data(val))) {
    if(ccr->opaque_finalize)
      ccr->opaque_finalize(rt, ccr->opaque);

    js_free_rt(rt, ccr);
  }
}

static JSClassDef js_cclosure_class = {
    .class_name = "JSCClosure",
    .finalizer = js_cclosure_finalizer,
    .call = js_cclosure_call,
};

JSValue
js_function_cclosure(JSContext* ctx, CClosureFunc* func, int length, int magic, void* opaque, FinalizerFunc* opaque_finalize) {
  CClosureRecord* ccr;
  JSValue func_proto, func_obj;

  if(js_cclosure_class_id == 0) {
    JS_NewClassID(&js_cclosure_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_cclosure_class_id, &js_cclosure_class);
  }

  func_proto = js_function_prototype(ctx);
  func_obj = JS_NewObjectProtoClass(ctx, func_proto, js_cclosure_class_id);
  JS_FreeValue(ctx, func_proto);

  if(JS_IsException(func_obj))
    return func_obj;

  if(!(ccr = js_malloc(ctx, sizeof(CClosureRecord)))) {
    JS_FreeValue(ctx, func_obj);
    return JS_EXCEPTION;
  }

  ccr->func = func;
  ccr->length = length;
  ccr->magic = magic;
  ccr->opaque = opaque;
  ccr->opaque_finalize = opaque_finalize;

  JS_SetOpaque(func_obj, ccr);

  // JS_DefinePropertyValueStr(ctx, func_obj, "length", JS_NewUint32(ctx, length), JS_PROP_CONFIGURABLE);

  return func_obj;
}

JSValue
js_generator_prototype(JSContext* ctx) {
  JSValue ret;

  if(JS_VALUE_GET_TAG(generator_prototype) != JS_TAG_OBJECT) {
    const char* code = "(function *gen() {})()";
    JSValue gen = JS_Eval(ctx, code, strlen(code), "<internal>", 0);

    ret = JS_GetPrototype(ctx, gen);
    JS_FreeValue(ctx, gen);
    generator_prototype = JS_DupValue(ctx, ret);
  } else {
    ret = JS_DupValue(ctx, generator_prototype);
  }

  return ret;
}

int
js_offset_length(JSContext* ctx, int64_t size, int argc, JSValueConst argv[], int start_arg, OffsetLength* out) {
  int ret;

  if((ret = offsetlength_from_argv(out, size, argc - start_arg, argv + start_arg, ctx)) < 0) {
    ret = (-ret - 1) + start_arg;

    JS_ThrowTypeError(ctx, "argument %d is not %s (type: %s)", ret + 1, CONST_STRARRAY("an offset", "a length")[ret], js_value_typeof(argv[ret]));
    return -1;
  }

  return ret;
}

int
js_index_range(JSContext* ctx, int64_t size, int argc, JSValueConst argv[], int start_arg, IndexRange* ir) {
  IndexRange tmp = INDEX_RANGE_INIT();
  int ret;

  if((ret = indexrange_from_argv(ir ? ir : &tmp, size, argc, argv, ctx)) < 0) {
    JS_ThrowTypeError(ctx, "argument %d is not an index (type: %s)", -ret + start_arg, js_value_typeof(argv[-ret - 1 + start_arg]));
    return -1;
  }

  return ret;
}
/**
 * @}
 */
