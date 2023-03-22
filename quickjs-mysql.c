#include "defines.h"
#include "quickjs-mysql.h"
#include "utils.h"

/**
 * \addtogroup quickjs-mysql
 * @{
 */

#define max(a, b) ((a) > (b) ? (a) : (b))

thread_local VISIBLE JSClassID js_mysql_class_id = 0;
thread_local JSValue mysql_proto = {{JS_TAG_UNDEFINED}}, mysql_ctor = {{JS_TAG_UNDEFINED}};

thread_local VISIBLE JSClassID js_mysqlresult_class_id = 0;
thread_local JSValue mysqlresult_proto = {{JS_TAG_UNDEFINED}}, mysqlresult_ctor = {{JS_TAG_UNDEFINED}};

static JSValue js_mysqlresult_wrap_proto(JSContext* ctx, JSValueConst proto, MYSQL_RES* res);
static JSValue js_mysqlresult_wrap(JSContext* ctx, MYSQL_RES* res);

struct MySQLOperation {
  int status;
  JSValue resolve, reject;
};

MYSQL*
js_mysql_data(JSContext* ctx, JSValueConst value) {
  MYSQL* my;
  my = JS_GetOpaque2(ctx, value, js_mysql_class_id);
  return my;
}

static JSValue
js_mysql_wrap_proto(JSContext* ctx, JSValueConst proto, MYSQL* my) {
  JSValue obj;

  if(js_mysql_class_id == 0)
    js_mysql_init(ctx, 0);
  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = JS_DupValue(ctx, mysql_proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_mysql_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, my);

  return obj;
fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_mysql_wrap(JSContext* ctx, MYSQL* my) {
  return js_mysql_wrap_proto(ctx, mysql_proto, my);
}

static JSValue
js_mysql_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  MYSQL* my = 0;
  JSValue proto = JS_GetPropertyStr(ctx, this_val, "prototype");
  JSValue ret = JS_UNDEFINED;

  switch(magic) {}

  return ret;
}

enum {
  PROP_MORE_RESULTS,
  PROP_AFFECTED_ROWS,
  PROP_SOCKET,
  PROP_INFO,
  PROP_ERRNO,
  PROP_ERROR,
  PROP_INSERT_ID,
  PROP_CHARACTER_SET,
  PROP_TIMEOUT_VALUE,
  PROP_TIMEOUT_VALUE_MS,
  PROP_SERVER_NAME,
};

static JSValue
js_mysql_getter(JSContext* ctx, JSValueConst this_val, int magic) {
  MYSQL* my;
  JSValue ret = JS_UNDEFINED;

  if(!(my = js_mysql_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case PROP_MORE_RESULTS: {
      ret = JS_NewBool(ctx, mysql_more_results(my));
      break;
    }
    case PROP_AFFECTED_ROWS: {
      ret = JS_NewInt64(ctx, mysql_affected_rows(my));
      break;
    }
    case PROP_SOCKET: {
      ret = JS_NewInt32(ctx, mysql_get_socket(my));
      break;
    }
    case PROP_ERRNO: {
      ret = JS_NewInt32(ctx, mysql_errno(my));
      break;
    }
    case PROP_ERROR: {
      const char* error = mysql_info(my);
      ret = error && *error ? JS_NewString(ctx, error) : JS_NULL;
      break;
    }
    case PROP_INFO: {
      const char* info = mysql_info(my);
      ret = info && *info ? JS_NewString(ctx, info) : JS_NULL;
      break;
    }
    case PROP_INSERT_ID: {
      ret = JS_NewInt64(ctx, mysql_insert_id(my));
      break;
    }
    case PROP_CHARACTER_SET: {
      const char* charset = mysql_character_set_name(my);
      ret = charset && *charset ? JS_NewString(ctx, charset) : JS_NULL;
      break;
    }
    case PROP_TIMEOUT_VALUE: {
      ret = JS_NewUint32(ctx, mysql_get_timeout_value(my));
      break;
    }
    case PROP_TIMEOUT_VALUE_MS: {
      ret = JS_NewUint32(ctx, mysql_get_timeout_value_ms(my));
      break;
    }
    case PROP_SERVER_NAME: {
      const char* name = mysql_get_server_name(my);
      ret = name && *name ? JS_NewString(ctx, name) : JS_NULL;
      break;
    }
  }

  return ret;
}

static JSValue
js_mysql_setter(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MYSQL* my;
  JSValue ret = JS_UNDEFINED;

  if(!(my = js_mysql_data(ctx, this_val)))
    return ret;

  switch(magic) {}
  return ret;
}

static JSValue
js_mysql_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  MYSQL* my;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if((my = mysql_init(NULL)))
    mysql_options(my, MYSQL_OPT_NONBLOCK, 0);

  obj = js_mysql_wrap_proto(ctx, proto, my);
  JS_FreeValue(ctx, proto);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_mysql_connect_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue func_data[]) {
  printf("%s\n", __func__);

  return JS_UNDEFINED;
}

static JSValue
js_mysql_connect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue promise = JS_UNDEFINED, data[5], handler;
  const char *host = 0, *user = 0, *password = 0, *db = 0, *unix_socket = 0;
  uint32_t port = 0;
  int64_t client_flags = 0;
  MYSQL *my, *ret = 0;
  int result;

  if(!(my = js_mysql_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(argc >= 1 && JS_IsString(argv[0]))
    host = JS_ToCString(ctx, argv[0]);
  if(argc >= 2 && JS_IsString(argv[2]))
    user = JS_ToCString(ctx, argv[2]);
  if(argc >= 3 && JS_IsString(argv[3]))
    password = JS_ToCString(ctx, argv[3]);
  if(argc >= 4 && JS_IsString(argv[4]))
    db = JS_ToCString(ctx, argv[4]);
  if(argc >= 5 && JS_IsNumber(argv[5]))
    JS_ToUint32(ctx, &port, argv[5]);
  if(argc >= 6 && JS_IsString(argv[6]))
    unix_socket = JS_ToCString(ctx, argv[6]);
  if(argc >= 7 && JS_IsNumber(argv[7]))
    JS_ToInt64(ctx, &client_flags, argv[7]);

  result = mysql_real_connect_start(&ret, my, host, user, password, db, port, unix_socket, client_flags);
  printf("%s result=%d\n", __func__, result);

  BOOL wr = !!(result & MYSQL_WAIT_WRITE);

  promise = JS_NewPromiseCapability(ctx, &data[3]);

  data[0] = JS_NewInt32(ctx, wr);
  data[1] = JS_DupValue(ctx, this_val);
  data[2] = js_iohandler_fn(ctx, wr);

  handler = JS_NewCFunctionData(ctx, js_mysql_connect_handler, 0, 0, countof(data), data);

  if(!js_iohandler_set(ctx, data[2], mysql_get_socket(my), handler)) {
    JS_FreeValue(ctx, JS_Call(ctx, data[4], JS_UNDEFINED, 0, 0));
    // return JS_ThrowInternalError(ctx, "failed setting %s handler", wr ? "write" : "read");
  }

  return promise;
}

static JSValue
js_mysql_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  MYSQL* my;

  if(!(my = js_mysql_data(ctx, this_val)))
    return JS_EXCEPTION;

  // ret = JS_NewInt32(ctx, mysql_read_close(my));

  return ret;
}

static JSValue
js_mysql_version(JSContext* ctx, JSValueConst this_val) {
  return JS_NewUint32(ctx, mysql_get_client_version());
}

static JSValue
js_mysql_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

static void
js_mysql_finalizer(JSRuntime* rt, JSValue val) {
  MYSQL* my;
  if((my = JS_GetOpaque(val, js_mysql_class_id))) {
    mysql_close(my);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_mysql_class = {
    .class_name = "MySQL",
    .finalizer = js_mysql_finalizer,
};

static const JSCFunctionListEntry js_mysql_funcs[] = {
    JS_CGETSET_MAGIC_DEF("moreResults", js_mysql_getter, 0, PROP_MORE_RESULTS),
    JS_CGETSET_MAGIC_DEF("affectedRows", js_mysql_getter, 0, PROP_AFFECTED_ROWS),
    JS_CGETSET_MAGIC_DEF("socket", js_mysql_getter, 0, PROP_SOCKET),
    JS_CGETSET_MAGIC_DEF("errno", js_mysql_getter, 0, PROP_ERRNO),
    JS_CGETSET_MAGIC_DEF("error", js_mysql_getter, 0, PROP_ERROR),
    JS_CGETSET_MAGIC_DEF("info", js_mysql_getter, 0, PROP_INFO),
    JS_CGETSET_MAGIC_DEF("insertId", js_mysql_getter, 0, PROP_INSERT_ID),
    JS_CGETSET_MAGIC_DEF("characterSet", js_mysql_getter, 0, PROP_CHARACTER_SET),
    JS_CGETSET_MAGIC_DEF("timeoutValue", js_mysql_getter, 0, PROP_TIMEOUT_VALUE),
    JS_CGETSET_MAGIC_DEF("timeoutValueMs", js_mysql_getter, 0, PROP_TIMEOUT_VALUE_MS),
    JS_CGETSET_MAGIC_DEF("serverName", js_mysql_getter, 0, PROP_SERVER_NAME),
    JS_CFUNC_DEF("connect", 1, js_mysql_connect),
    JS_CFUNC_DEF("close", 0, js_mysql_close),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_mysql_iterator),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MySQL", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_mysql_static_funcs[] = {
    JS_CGETSET_DEF("clientVersion", js_mysql_version, 0),
    JS_PROP_INT64_DEF("MYSQL_COUNT_ERROR", MYSQL_COUNT_ERROR, JS_PROP_ENUMERABLE),
    JS_CONSTANT(MYSQL_WAIT_READ),
    JS_CONSTANT(MYSQL_WAIT_WRITE),
    JS_CONSTANT(MYSQL_WAIT_EXCEPT),
    JS_CONSTANT(MYSQL_WAIT_TIMEOUT),
    JS_CONSTANT(MYSQL_OPT_CONNECT_TIMEOUT),
    JS_CONSTANT(MYSQL_OPT_COMPRESS),
    JS_CONSTANT(MYSQL_OPT_NAMED_PIPE),
    JS_CONSTANT(MYSQL_INIT_COMMAND),
    JS_CONSTANT(MYSQL_READ_DEFAULT_FILE),
    JS_CONSTANT(MYSQL_READ_DEFAULT_GROUP),
    JS_CONSTANT(MYSQL_SET_CHARSET_DIR),
    JS_CONSTANT(MYSQL_SET_CHARSET_NAME),
    JS_CONSTANT(MYSQL_OPT_LOCAL_INFILE),
    JS_CONSTANT(MYSQL_OPT_PROTOCOL),
    JS_CONSTANT(MYSQL_SHARED_MEMORY_BASE_NAME),
    JS_CONSTANT(MYSQL_OPT_READ_TIMEOUT),
    JS_CONSTANT(MYSQL_OPT_WRITE_TIMEOUT),
    JS_CONSTANT(MYSQL_OPT_USE_RESULT),
    JS_CONSTANT(MYSQL_OPT_USE_REMOTE_CONNECTION),
    JS_CONSTANT(MYSQL_OPT_USE_EMBEDDED_CONNECTION),
    JS_CONSTANT(MYSQL_OPT_GUESS_CONNECTION),
    JS_CONSTANT(MYSQL_SET_CLIENT_IP),
    JS_CONSTANT(MYSQL_SECURE_AUTH),
    JS_CONSTANT(MYSQL_REPORT_DATA_TRUNCATION),
    JS_CONSTANT(MYSQL_OPT_RECONNECT),
    JS_CONSTANT(MYSQL_OPT_SSL_VERIFY_SERVER_CERT),
    JS_CONSTANT(MYSQL_PLUGIN_DIR),
    JS_CONSTANT(MYSQL_DEFAULT_AUTH),
    JS_CONSTANT(MYSQL_OPT_BIND),
    JS_CONSTANT(MYSQL_OPT_SSL_KEY),
    JS_CONSTANT(MYSQL_OPT_SSL_CERT),
    JS_CONSTANT(MYSQL_OPT_SSL_CA),
    JS_CONSTANT(MYSQL_OPT_SSL_CAPATH),
    JS_CONSTANT(MYSQL_OPT_SSL_CIPHER),
    JS_CONSTANT(MYSQL_OPT_SSL_CRL),
    JS_CONSTANT(MYSQL_OPT_SSL_CRLPATH),
    JS_CONSTANT(MYSQL_OPT_CONNECT_ATTR_RESET),
    JS_CONSTANT(MYSQL_OPT_CONNECT_ATTR_ADD),
    JS_CONSTANT(MYSQL_OPT_CONNECT_ATTR_DELETE),
    JS_CONSTANT(MYSQL_SERVER_PUBLIC_KEY),
    JS_CONSTANT(MYSQL_ENABLE_CLEARTEXT_PLUGIN),
    JS_CONSTANT(MYSQL_OPT_CAN_HANDLE_EXPIRED_PASSWORDS),
    JS_CONSTANT(MYSQL_OPT_SSL_ENFORCE),
    JS_CONSTANT(MYSQL_OPT_MAX_ALLOWED_PACKET),
    JS_CONSTANT(MYSQL_OPT_NET_BUFFER_LENGTH),
    JS_CONSTANT(MYSQL_OPT_TLS_VERSION),
    JS_CONSTANT(MYSQL_PROGRESS_CALLBACK),
    JS_CONSTANT(MYSQL_OPT_NONBLOCK),
    JS_CONSTANT(MYSQL_DATABASE_DRIVER),
    JS_CONSTANT(MYSQL_OPT_CONNECT_ATTRS),

};

MYSQL_RES*
js_mysqlresult_data(JSContext* ctx, JSValueConst value) {
  MYSQL_RES* res;
  res = JS_GetOpaque2(ctx, value, js_mysqlresult_class_id);
  return res;
}

static JSValue
js_mysqlresult_wrap_proto(JSContext* ctx, JSValueConst proto, MYSQL_RES* res) {
  JSValue obj;

  if(js_mysql_class_id == 0)
    js_mysql_init(ctx, 0);

  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = mysqlresult_proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_mysqlresult_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, res);

  return obj;
fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_mysqlresult_wrap(JSContext* ctx, MYSQL_RES* res) {
  return js_mysqlresult_wrap_proto(ctx, mysqlresult_proto, res);
}

enum {
  PROP_EOF,
  PROP_NUM_ROWS,
  PROP_NUM_FIELDS,
};

static JSValue
js_mysqlresult_getter(JSContext* ctx, JSValueConst this_val, int magic) {
  MYSQL_RES* res;
  JSValue ret = JS_UNDEFINED;

  if(!(res = js_mysqlresult_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case PROP_EOF: {
      ret = JS_NewBool(ctx, mysql_eof(res));
      break;
    }
    case PROP_NUM_ROWS: {
      ret = JS_NewInt64(ctx, mysql_num_rows(res));
      break;
    }
    case PROP_NUM_FIELDS: {
      ret = JS_NewInt64(ctx, mysql_num_fields(res));
      break;
    }
  }
  return ret;
}

static JSValue
js_mysqlresult_setter(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MYSQL_RES* res;
  JSValue ret = JS_UNDEFINED;

  if(!(res = js_mysqlresult_data(ctx, this_val)))
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
  MYSQL_RES* res;
  if((res = JS_GetOpaque(val, js_mysqlresult_class_id))) {
    mysql_free_result(res);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_mysqlresult_class = {
    .class_name = "MySQLResult",
    .finalizer = js_mysqlresult_finalizer,
};

static const JSCFunctionListEntry js_mysqlresult_funcs[] = {
    JS_CGETSET_MAGIC_DEF("eof", js_mysqlresult_getter, 0, PROP_EOF),
    JS_CGETSET_MAGIC_DEF("numRows", js_mysqlresult_getter, js_mysqlresult_setter, PROP_NUM_ROWS),
    JS_CGETSET_MAGIC_DEF("numFields", js_mysqlresult_getter, js_mysqlresult_setter, PROP_NUM_FIELDS),
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
  JS_AddModuleExport(ctx, m, "MySQLResult");
  return m;
}

/**
 * @}
 */
