#include "defines.h"
#include "quickjs-mysql.h"

/**
 * \addtogroup quickjs-MYSQL
 * @{
 */

#define max(a, b) ((a) > (b) ? (a) : (b))

thread_local VISIBLE JSClassID js_mysql_class_id = 0;
thread_local JSValue mysql_proto = {{JS_TAG_UNDEFINED}}, mysql_ctor = {{JS_TAG_UNDEFINED}};

thread_local VISIBLE JSClassID js_mysqlresult_class_id = 0;
thread_local JSValue mysqlresult_proto = {{JS_TAG_UNDEFINED}}, mysqlresult_ctor = {{JS_TAG_UNDEFINED}};

enum {
  MYSQL_METHOD_READ,
  MYSQL_METHOD_WRITE,
  MYSQL_METHOD_READFILE,
  MYSQL_METHOD_WRITEFILE,
};
enum {
  MYSQL_PROP_FORMAT,
  MYSQL_PROP_COMPRESSION,
  MYSQL_PROP_FILTERS,
  MYSQL_PROP_FILECOUNT,
};

enum {
  MYSQLRESULT_METHOD_READ,
  MYSQLRESULT_METHOD_WRITE,
  MYSQLRESULT_METHOD_READFILE,
  MYSQLRESULT_METHOD_WRITEFILE,
};
enum {
  ENTRY_ATIME,
  ENTRY_BIRTHTIME,
  ENTRY_CTIME,
  ENTRY_DEV,
  ENTRY_DEVMAJOR,
  ENTRY_DEVMINOR,
  ENTRY_FILETYPE,
  ENTRY_FFLAGS,
  ENTRY_GID,
  ENTRY_GNAME,
  ENTRY_HARDLINK,
  ENTRY_INO,
  ENTRY_INO64,
  ENTRY_LINK,
  ENTRY_MODE,
  ENTRY_MTIME,
  ENTRY_NLINK,
  ENTRY_PATHNAME,
  ENTRY_PERM,
  ENTRY_RDEV,
  ENTRY_RDEVMAJOR,
  ENTRY_RDEVMINOR,
  ENTRY_SIZE,
  ENTRY_SYMLINK,
  ENTRY_UID,
  ENTRY_UNAME
};

static JSValue js_mysqlresult_wrap_proto(JSContext* ctx, JSValueConst proto, struct MYSQL_RES* ent);
static JSValue js_mysqlresult_wrap(JSContext* ctx, struct MYSQL_RES* ent);

struct MySQLInstance {
  JSValue MYSQL;
};
struct MySQLResultRef {
  JSContext* ctx;
  JSValueConst callback, args[2];
};

static void
js_mysql_free_buffer(JSRuntime* rt, void* opaque, void* ptr) {
  struct MySQLInstance* ainst = opaque;
  JS_FreeValueRT(rt, ainst->MYSQL);
  js_free_rt(rt, ainst);
}

static void
js_mysql_progress_callback(void* opaque) {
  struct MySQLResultRef* aeref = opaque;

  JSValue ret = JS_Call(aeref->ctx, aeref->callback, JS_UNDEFINED, 2, aeref->args);
  JS_FreeValue(aeref->ctx, ret);
}

struct MYSQL*
js_mysql_data(JSContext* ctx, JSValueConst value) {
  struct MYSQL* ar;
  ar = JS_GetOpaque2(ctx, value, js_mysql_class_id);
  return ar;
}

static JSValue
js_mysql_wrap_proto(JSContext* ctx, JSValueConst proto, struct MYSQL* ar) {
  JSValue obj;

  if(js_mysql_class_id == 0)
    js_mysql_init(ctx, 0);
  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = JS_DupValue(ctx, mysql_proto);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_mysql_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ar);

  return obj;
fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_mysql_wrap(JSContext* ctx, struct MYSQL* ar) {
  return js_mysql_wrap_proto(ctx, mysql_proto, ar);
}

static JSValue
js_mysql_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  struct MYSQL* ar = 0;
  JSValue proto = JS_GetPropertyStr(ctx, this_val, "prototype");
  JSValue ret = JS_UNDEFINED;

  switch(magic) {}

  return ret;
}

static JSValue
js_mysql_getter(JSContext* ctx, JSValueConst this_val, int magic) {
  struct MYSQL* ar;
  JSValue ret = JS_UNDEFINED;

  if(!(ar = js_mysql_data(ctx, this_val)))
    return ret;

  switch(magic) {}
  return ret;
}

static JSValue
js_mysql_setter(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  struct MYSQL* ar;
  JSValue ret = JS_UNDEFINED;

  if(!(ar = js_mysql_data(ctx, this_val)))
    return ret;

  switch(magic) {}
  return ret;
}

static JSValue
js_mysql_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = js_mysql_wrap_proto(ctx, proto, 0);
  JS_FreeValue(ctx, proto);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_mysql_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  retuzrn JS_UNDEFINED;
}

static JSValue
js_mysql_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  struct MYSQL* ar;

  if(!(ar = js_mysql_data(ctx, this_val)))
    return JS_EXCEPTION;

  ret = JS_NewInt32(ctx, mysql_read_close(ar));

  return ret;
}

static JSValue
js_mysql_version(JSContext* ctx, JSValueConst this_val) {
  return JS_NewString(ctx, mysql_version_details());
}

static JSValue
js_mysql_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

static void
js_mysql_finalizer(JSRuntime* rt, JSValue val) {
  struct MYSQL* ar = JS_GetOpaque(val, js_mysql_class_id);
  if(ar) {
    mysql_free(ar);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_mysql_class = {
    .class_name = "MySQL",
    .finalizer = js_mysql_finalizer,
};

static const JSCFunctionListEntry js_mysql_funcs[] = {
    JS_CGETSET_MAGIC_DEF("format", js_mysql_getter, 0, MYSQL_PROP_FORMAT),
    JS_CGETSET_MAGIC_DEF("compression", js_mysql_getter, 0, MYSQL_PROP_COMPRESSION),
    JS_CGETSET_MAGIC_DEF("filters", js_mysql_getter, 0, MYSQL_PROP_FILTERS),
    JS_CGETSET_MAGIC_DEF("fileCount", js_mysql_getter, 0, MYSQL_PROP_FILECOUNT),
    JS_CFUNC_DEF("read", 1, js_mysql_read),
    JS_CFUNC_DEF("close", 0, js_mysql_close),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_mysql_iterator),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MySQL", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_mysql_static_funcs[] = {
    JS_PROP_INT32_DEF("SEEK_SET", SEEK_SET, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SEEK_CUR", SEEK_CUR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SEEK_END", SEEK_END, JS_PROP_ENUMERABLE),
    JS_CONSTANT(MYSQL_COUNT_ERROR),
    JS_CONSTANT(MYSQL_WAIT_READ),
    JS_CONSTANT(MYSQL_WAIT_WRITE),
    JS_CONSTANT(MYSQL_WAIT_EXCEPT),
    JS_CONSTANT(MYSQL_WAIT_TIMEOUT),
};

struct MYSQL_RES*
js_mysqlresult_data(JSContext* ctx, JSValueConst value) {
  struct MYSQL_RES* ent;
  ent = JS_GetOpaque2(ctx, value, js_mysqlresult_class_id);
  return ent;
}

static JSValue
js_mysqlresult_wrap_proto(JSContext* ctx, JSValueConst proto, struct MYSQL_RES* ent) {
  JSValue obj;

  if(js_mysql_class_id == 0)
    js_mysql_init(ctx, 0);

  if(js_is_nullish(ctx, proto))
    proto = mysqlresult_proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_mysqlresult_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ent);

  return obj;
fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_mysqlresult_wrap(JSContext* ctx, struct MYSQL_RES* ent) {
  return js_mysqlresult_wrap_proto(ctx, mysqlresult_proto, ent);
}

static JSValue
js_mysqlresult_getter(JSContext* ctx, JSValueConst this_val, int magic) {
  struct MYSQL_RES* ent;
  JSValue ret = JS_UNDEFINED;

  if(!(ent = js_mysqlresult_data(ctx, this_val)))
    return ret;

  switch(magic) {}
  return ret;
}

static JSValue
js_mysqlresult_setter(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  struct MYSQL_RES* ent;
  JSValue ret = JS_UNDEFINED;

  if(!(ent = js_mysqlresult_data(ctx, this_val)))
    return ret;

  switch(magic) {}
  return ret;
}

static JSValue
js_mysqlresult_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  return js_mysqlresult_wrap_proto(ctx, proto, 0);

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_mysqlresult_finalizer(JSRuntime* rt, JSValue val) {
  struct MYSQL_RES* ent = JS_GetOpaque(val, js_mysqlresult_class_id);
  if(ent) {
    mysql_entry_free(ent);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_mysqlresult_class = {
    .class_name = "MySQLResult",
    .finalizer = js_mysqlresult_finalizer,
};

static const JSCFunctionListEntry js_mysqlresult_funcs[] = {
    JS_CGETSET_MAGIC_DEF("atime", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_ATIME),
    JS_CGETSET_MAGIC_DEF("ctime", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_CTIME),
    JS_CGETSET_MAGIC_DEF("mtime", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_MTIME),
    JS_CGETSET_MAGIC_DEF("birthtime", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_BIRTHTIME),
    JS_CGETSET_MAGIC_DEF("dev", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_DEV),
    JS_CGETSET_MAGIC_DEF("devmajor", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_DEVMAJOR),
    JS_CGETSET_MAGIC_DEF("devminor", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_DEVMINOR),
    JS_CGETSET_MAGIC_DEF("rdev", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_RDEV),
    JS_CGETSET_MAGIC_DEF("rdevmajor", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_RDEVMAJOR),
    JS_CGETSET_MAGIC_DEF("rdevminor", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_RDEVMINOR),
    JS_CGETSET_MAGIC_DEF("filetype", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_FILETYPE),
    JS_CGETSET_MAGIC_DEF("fflags", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_FFLAGS),
    JS_CGETSET_MAGIC_DEF("uid", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_UID),
    JS_CGETSET_MAGIC_DEF("gid", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_GID),
    JS_CGETSET_MAGIC_DEF("ino", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_INO),
    // JS_ALIAS_DEF("ino64", "ino"),
    JS_CGETSET_MAGIC_DEF("nlink", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_NLINK),
    JS_CGETSET_ENUMERABLE_DEF("pathname", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_PATHNAME),
    JS_CGETSET_MAGIC_DEF("uname", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_UNAME),
    JS_CGETSET_MAGIC_DEF("gname", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_GNAME),
    JS_CGETSET_MAGIC_DEF("mode", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_MODE),
    JS_CGETSET_MAGIC_DEF("perm", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_PERM),
    JS_CGETSET_ENUMERABLE_DEF("size", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_SIZE),
    JS_CGETSET_MAGIC_DEF("symlink", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_SYMLINK),
    JS_CGETSET_MAGIC_DEF("hardlink", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_HARDLINK),
    JS_CGETSET_MAGIC_DEF("link", js_mysqlresult_getter, js_mysqlresult_setter, ENTRY_LINK),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MySQLResult", JS_PROP_CONFIGURABLE),
};

int
js_mysql_init(JSContext* ctx, JSModuleDef* m) {

  if(js_mysql_class_id == 0) {
    JS_NewClassID(&js_mysql_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_mysql_class_id, &js_mysql_class);

    mysql_ctor = JS_NewCFunction2(ctx, js_mysql_constructor, "MySQL", 1, JS_CFUNC_constructor, 0);
    mysql_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, mysql_proto, js_mysql_funcs, countof(js_mysql_funcs));
    JS_SetPropertyFunctionList(ctx, mysql_ctor, js_mysql_static_funcs, countof(js_mysql_static_funcs));
    JS_SetClassProto(ctx, js_mysql_class_id, mysql_proto);

    JS_NewClassID(&js_mysqlresult_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_mysqlresult_class_id, &js_mysqlresult_class);

    mysqlresult_ctor = JS_NewCFunction2(ctx, js_mysqlresult_constructor, "MySQLResult", 1, JS_CFUNC_constructor, 0);
    mysqlresult_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, mysqlresult_proto, js_mysqlresult_funcs, countof(js_mysqlresult_funcs));
    JS_SetClassProto(ctx, js_mysqlresult_class_id, mysqlresult_proto);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "MySQL", mysql_ctor);
    JS_SetModuleExport(ctx, m, "MySQLResult", mysqlresult_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_mysql
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  if(!(m = JS_NewCModule(ctx, module_name, &js_mysql_init)))
    return m;
  JS_AddModuleExport(ctx, m, "MySQL");
  return m;
}

/**
 * @}
 */
