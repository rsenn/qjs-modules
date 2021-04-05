#include "utils.h"

InputBuffer
js_input_buffer(JSContext* ctx, JSValueConst value) {
  InputBuffer ret = {0, 0, 0, &input_buffer_free_default};

  if(JS_IsString(value)) {
    ret.x = (const uint8_t*)JS_ToCStringLen(ctx, &ret.n, value);
    ret.free = JS_FreeCString;
  } else if(js_is_arraybuffer(ctx, value)) {
    ret.x = JS_GetArrayBuffer(ctx, &ret.n, value);
  } else {
    JS_ThrowTypeError(ctx, "Invalid type for input buffer");
  }
  return ret;
}

BOOL
js_value_equals(JSContext* ctx, JSValueConst a, JSValueConst b) {
  int32_t ta, tb;
  BOOL ret = FALSE;
  ta = js_value_type(a);
  tb = js_value_type(a);
  if(ta != tb)
    return FALSE;

  if(ta & TYPE_INT) {
    int32_t inta, intb;

    inta = JS_VALUE_GET_INT(a);
    intb = JS_VALUE_GET_INT(b);
    ret = inta == intb;
  } else if(ta & TYPE_BOOL) {
    BOOL boola, boolb;

    boola = !!JS_VALUE_GET_BOOL(a);
    boolb = !!JS_VALUE_GET_BOOL(b);
    ret = boola == boolb;

  } else if(ta & TYPE_FLOAT64) {
    double flta, fltb;

    flta = JS_VALUE_GET_FLOAT64(a);
    fltb = JS_VALUE_GET_FLOAT64(b);
    ret = flta == fltb;

  } else if(ta & TYPE_OBJECT) {
    void *obja, *objb;

    obja = JS_VALUE_GET_OBJ(a);
    objb = JS_VALUE_GET_OBJ(b);

    ret = obja == objb;
  } else if(ta & TYPE_STRING) {
    const char *stra, *strb;

    stra = JS_ToCString(ctx, a);
    strb = JS_ToCString(ctx, b);

    ret = !strcmp(stra, strb);

    JS_FreeCString(ctx, stra);
    JS_FreeCString(ctx, strb);
  }

  return ret;
}

JSValue
js_value_clone(JSContext* ctx, JSValueConst value) {
  int32_t type = js_value_type(value);
  JSValue ret = JS_UNDEFINED;
  switch(type) {

    /* case TYPE_NULL: {
     ret = JS_NULL;
     break;
     }
     case TYPE_UNDEFINED: {
     ret = JS_UNDEFINED;
     break;
     }
     case TYPE_STRING: {
     size_t len;
     const char* str;
     str = JS_ToCStringLen(ctx, &len, value);
     ret = JS_NewStringLen(ctx, str, len);
     JS_FreeCString(ctx, str);
     break;
     }*/
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
    case TYPE_OBJECT: {
      JSPropertyEnum* tab_atom;
      uint32_t tab_atom_len;
      ret = JS_IsArray(ctx, value) ? JS_NewArray(ctx) : JS_NewObject(ctx);
      if(!JS_GetOwnPropertyNames(
             ctx, &tab_atom, &tab_atom_len, value, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY)) {
        uint32_t i;
        for(i = 0; i < tab_atom_len; i++) {
          JSValue prop;
          prop = JS_GetProperty(ctx, value, tab_atom[i].atom);
          JS_SetProperty(ctx, ret, tab_atom[i].atom, js_value_clone(ctx, prop));
        }
      }
      break;
    }
    case TYPE_UNDEFINED:
    case TYPE_NULL:
    case TYPE_SYMBOL:
    case TYPE_STRING:
    case TYPE_BIG_DECIMAL:
    case TYPE_BIG_INT:
    case TYPE_BIG_FLOAT: {
      ret = JS_DupValue(ctx, value);
      break;
    }
    default: {
      ret = JS_ThrowTypeError(ctx, "No such type: %08x\n", type);
      break;
    }
  }
  return ret;
}

void
js_atom_dump(JSContext* ctx, JSAtom atom, DynBuf* db, BOOL color) {
  const char* str;
  BOOL is_int;
  str = JS_AtomToCString(ctx, atom);
  is_int = js_atom_isint(atom) || is_integer(str);
  if(color)
    dbuf_putstr(db, is_int ? "\x1b[33m" : "\x1b[1;30m");

  dbuf_putstr(db, str);
  if(color)
    dbuf_putstr(db, "\x1b[1;36m");

  if(!is_int)
    dbuf_printf(db, "(0x%x)", js_atom_tobinary(atom));

  if(color)
    dbuf_putstr(db, "\x1b[m");
}

void
js_value_dump(JSContext* ctx, JSValueConst value, DynBuf* db) {
  const char* str;
  size_t len;
  if(JS_IsArray(ctx, value)) {
    dbuf_putstr(db, "[object Array]");
  } else {
    str = JS_ToCStringLen(ctx, &len, value);
    dbuf_append(db, (const uint8_t*)str, len);
    JS_FreeCString(ctx, str);
  }
}

const char*
js_function_name(JSContext* ctx, JSValueConst value) {
  JSAtom atom;
  JSValue str, name, args[2], idx;
  const char* s = 0;
  int32_t i = -1;
  str = js_value_tostring(ctx, "Function", value);
  atom = JS_NewAtom(ctx, "indexOf");
  args[0] = JS_NewString(ctx, "function ");
  idx = JS_Invoke(ctx, str, atom, 1, args);
  JS_FreeValue(ctx, args[0]);
  JS_ToInt32(ctx, &i, idx);
  if(i != 0) {
    JS_FreeAtom(ctx, atom);
    JS_FreeValue(ctx, str);
    JS_FreeValue(ctx, args[0]);
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

char*
js_object_classname(JSContext* ctx, JSValueConst value) {
  JSValue proto, ctor;
  const char* str;
  char* name = 0;
  int namelen;
  proto = JS_GetPrototype(ctx, value);
  ctor = JS_GetPropertyStr(ctx, proto, "constructor");
  if((str = JS_ToCString(ctx, ctor))) {
    if(!strncmp(str, "function ", 9)) {
      namelen = byte_chr(str + 9, strlen(str) - 9, '(');
      name = js_strndup(ctx, str + 9, namelen);
    }
  }
  if(!name) {
    if(str)
      JS_FreeCString(ctx, str);

    if((str = JS_ToCString(ctx, JS_GetPropertyStr(ctx, ctor, "name"))))
      name = js_strdup(ctx, str);
  }
  if(str)
    JS_FreeCString(ctx, str);

  return name;
}

int
js_is_arraybuffer(JSContext* ctx, JSValueConst value) {
  int ret = 0;
  int n, m;
  void* obj = JS_VALUE_GET_OBJ(value);
  char* name = 0;
  if((name = js_object_classname(ctx, value))) {
    n = strlen(name);
    m = n >= 11 ? n - 11 : 0;
    if(!strcmp(name + m, "ArrayBuffer"))
      ret = 1;
  }
  if(!ret) {
    const char* str;
    JSValue ctor = js_global_get(ctx, "ArrayBuffer");
    if(JS_IsInstanceOf(ctx, value, ctor))
      ret = 1;
    else if(!JS_IsArray(ctx, value) && (str = js_object_tostring(ctx, value))) {
      ret = strstr(str, "ArrayBuffer]") != 0;
      JS_FreeCString(ctx, str);
    }

    JS_FreeValue(ctx, ctor);
  }
  if(name)
    js_free(ctx, (void*)name);

  return ret;
}

BOOL
js_object_equals(JSContext* ctx, JSValueConst a, JSValueConst b) {
  BOOL ret = FALSE;
  JSPropertyEnum *atoms_a, *atoms_b;
  uint32_t i, natoms_a, natoms_b;
  int32_t ta, tb;
  ta = js_value_type(a);
  tb = js_value_type(b);
  assert(ta == TYPE_OBJECT);
  assert(tb == TYPE_OBJECT);
  if(JS_GetOwnPropertyNames(ctx, &atoms_a, &natoms_a, a, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY))
    return FALSE;

  if(JS_GetOwnPropertyNames(ctx, &atoms_b, &natoms_b, b, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY))
    return FALSE;

  if(natoms_a != natoms_b)
    return FALSE;

  qsort_r(&atoms_a, natoms_a, sizeof(JSPropertyEnum), &js_propenum_cmp, ctx);
  qsort_r(&atoms_b, natoms_b, sizeof(JSPropertyEnum), &js_propenum_cmp, ctx);
  for(i = 0; i < natoms_a; i++)
    if(atoms_a[i].atom != atoms_b[i].atom)
      return FALSE;
  return TRUE;
}

JSValue
js_value_tostring(JSContext* ctx, const char* class_name, JSValueConst value) {
  JSAtom atom;
  JSValue proto, tostring, str;
  proto = js_global_prototype(ctx, class_name);
  atom = JS_NewAtom(ctx, "toString");
  tostring = JS_GetProperty(ctx, proto, atom);
  JS_FreeValue(ctx, proto);
  JS_FreeAtom(ctx, atom);
  str = JS_Call(ctx, tostring, value, 0, 0);
  JS_FreeValue(ctx, tostring);
  return str;
}

void
dbuf_put_escaped_pred(DynBuf* db, const char* str, size_t len, int (*pred)(int)) {
  size_t i = 0, j;
  while(i < len) {
    if((j = predicate_find(&str[i], len - i, pred))) {
      dbuf_append(db, (const uint8_t*)&str[i], j);
      i += j;
    }
    if(i == len)
      break;
    dbuf_putc(db, '\\');

    if(str[i] == 0x1b)
      dbuf_append(db, (const uint8_t*)"x1b", 3);
    else
      dbuf_putc(db, escape_char_letter(str[i]));
    i++;
  }
}
