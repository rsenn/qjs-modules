#include "defines.h"
#include "quickjs-mysql.h"
#include "utils.h"
#include "buffer-utils.h"
#include "char-utils.h"

/**
 * \addtogroup quickjs-mysql
 * @{
 */

thread_local VISIBLE JSClassID js_mysql_class_id = 0, js_mysqlresult_class_id = 0;
thread_local JSValue mysql_proto = {{JS_TAG_UNDEFINED}}, mysql_ctor = {{JS_TAG_UNDEFINED}}, mysqlresult_proto = {{JS_TAG_UNDEFINED}},
                     mysqlresult_ctor = {{JS_TAG_UNDEFINED}};

static JSValue js_mysqlresult_wrap(JSContext* ctx, MYSQL_RES* res);

enum MySQLResultType {
  RESULT_OBJECT = 1,
  RESULT_STRING = 2,
  RESULT_TABLENAME = 4,
};

struct ConnectParameters {
  const char *host, *user, *password, *db;
  uint32_t port;
  const char* socket;
  int64_t flags;
};

typedef char* MySQLNameFunc(JSContext*, MYSQL_FIELD const*);
typedef JSValue MySQLRowFunc(JSContext*, MYSQL_RES*, MYSQL_ROW, int);

static char* field_id(JSContext* ctx, MYSQL_FIELD const* field);
static char* field_name(JSContext* ctx, MYSQL_FIELD const* field);
static JSValue field_array(JSContext* ctx, MYSQL_FIELD* field);
static MySQLNameFunc* field_namefunc(MYSQL_FIELD* fields, uint32_t num_fields);
static BOOL field_is_integer(MYSQL_FIELD const* field);
static BOOL field_is_float(MYSQL_FIELD const* field);
static BOOL field_is_decimal(MYSQL_FIELD const* field);
static BOOL field_is_number(MYSQL_FIELD const* field);
static BOOL field_is_boolean(MYSQL_FIELD const* field);
static BOOL field_is_null(MYSQL_FIELD const* field);
static BOOL field_is_date(MYSQL_FIELD const* field);
static BOOL field_is_string(MYSQL_FIELD const* field);
static BOOL field_is_blob(MYSQL_FIELD const* field);
static JSValue string_to_value(JSContext* ctx, const char* func_name, const char* s);
static JSValue string_to_object(JSContext* ctx, const char* ctor_name, const char* s);

#define string_to_number(ctx, s) string_to_value(ctx, "Number", s);
#define string_to_bigdecimal(ctx, s) string_to_value(ctx, "BigDecimal", s);
#define string_to_bigint(ctx, s) string_to_value(ctx, "BigInt", s);
#define string_to_bigfloat(ctx, s) string_to_value(ctx, "BigFloat", s);
#define string_to_date(ctx, s) string_to_object(ctx, "Date", s);

static void value_to_mysql(JSContext* ctx, DynBuf* out, JSValueConst value);
static JSValue value_to_string(JSContext* ctx, JSValueConst value);

static void
value_yield(JSContext* ctx, JSValueConst resolve, JSValueConst value) {
  JSValue ret = JS_Call(ctx, resolve, JS_UNDEFINED, 1, &value);
  JS_FreeValue(ctx, ret);
}

static void
value_yield_free(JSContext* ctx, JSValueConst resolve, JSValueConst value) {
  value_yield(ctx, resolve, value);
  JS_FreeValue(ctx, value);
}

static void
connectparams_init(JSContext* ctx, struct ConnectParameters* c, int argc, JSValueConst argv[]) {
  c->host = argc > 0 ? JS_ToCString(ctx, argv[0]) : 0;
  c->user = argc > 1 ? JS_ToCString(ctx, argv[1]) : 0;
  c->password = argc > 2 ? JS_ToCString(ctx, argv[2]) : 0;
  c->db = argc > 3 ? JS_ToCString(ctx, argv[3]) : 0;

  if(argc > 4 && JS_IsNumber(argv[4]))
    JS_ToUint32(ctx, &c->port, argv[4]);
  else
    c->port = 3306;

  c->socket = argc > 5 ? JS_ToCString(ctx, argv[5]) : 0;

  if(argc > 6)
    JS_ToInt64(ctx, &c->flags, argv[6]);
  else
    c->flags = 0;
}

static void
connectparams_free(JSContext* ctx, struct ConnectParameters* c) {
  js_cstring_destroy(ctx, c->host);
  js_cstring_destroy(ctx, c->user);
  js_cstring_destroy(ctx, c->password);
  js_cstring_destroy(ctx, c->db);
  js_cstring_destroy(ctx, c->socket);
}

static BOOL
mysql_nonblock(MYSQL* my) {
  my_bool val;
  mysql_get_option(my, MYSQL_OPT_NONBLOCK, &val);
  return val;
}

MYSQL*
js_mysql_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_mysql_class_id);
}

static JSValue
js_mysql_new(JSContext* ctx, JSValueConst proto, MYSQL* my) {
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
  return js_mysql_new(ctx, mysql_proto, my);
}

static inline int
js_mysql_rtype(JSContext* ctx, JSValueConst value) {
  return js_get_propertystr_int32(ctx, value, "resultType");
}

enum {
  METHOD_ESCAPE_STRING,
  METHOD_GET_OPTION,
  METHOD_SET_OPTION,
};

static JSValue
js_mysql_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  MYSQL* my = 0;
  JSValue ret = JS_UNDEFINED;

  if(!(my = js_mysql_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case METHOD_ESCAPE_STRING: {
      char* dst;
      const char* src;
      size_t len;

      if(!(src = JS_ToCStringLen(ctx, &len, argv[0]))) {
        ret = JS_ThrowTypeError(ctx, "argument 1 must be string");
        break;
      }

      if((!(dst = js_malloc(ctx, 2 * len + 1)))) {
        ret = JS_ThrowOutOfMemory(ctx);
        break;
      }

      len = mysql_real_escape_string(my, dst, src, len);
      ret = JS_NewStringLen(ctx, dst, len);
      js_free(ctx, dst);
      break;
    }

    case METHOD_GET_OPTION: {
      int32_t opt = -1;

      JS_ToInt32(ctx, &opt, argv[0]);

      switch(opt) {
        /* unsigned int */
        case MYSQL_OPT_CONNECT_TIMEOUT:
        case MYSQL_OPT_PROTOCOL:
        case MYSQL_OPT_READ_TIMEOUT:
        case MYSQL_OPT_WRITE_TIMEOUT: {
          unsigned int val = 0;
          mysql_get_option(my, opt, &val);
          ret = JS_NewUint32(ctx, val);
          break;
        }
        /* unsigned long */
        case MYSQL_OPT_MAX_ALLOWED_PACKET:
        case MYSQL_OPT_NET_BUFFER_LENGTH: {
          unsigned long val = 0;
          mysql_get_option(my, opt, &val);
          ret = JS_NewInt64(ctx, val);
          break;
        }

        /* my_bool */
        case MYSQL_ENABLE_CLEARTEXT_PLUGIN:
        case MYSQL_OPT_CAN_HANDLE_EXPIRED_PASSWORDS:
        case MYSQL_OPT_GUESS_CONNECTION:
        case MYSQL_OPT_LOCAL_INFILE:
        case MYSQL_OPT_RECONNECT:
        case MYSQL_OPT_USE_RESULT:
        case MYSQL_OPT_SSL_ENFORCE:
        case MYSQL_OPT_SSL_VERIFY_SERVER_CERT:
        case MYSQL_OPT_USE_EMBEDDED_CONNECTION:
        case MYSQL_OPT_USE_REMOTE_CONNECTION:
        case MYSQL_REPORT_DATA_TRUNCATION:
        case MYSQL_SECURE_AUTH:
        case MYSQL_OPT_NONBLOCK: {
          my_bool val;
          mysql_get_option(my, opt, &val);
          ret = JS_NewBool(ctx, val);
          break;
        }

        /* const char*/
        case MYSQL_DEFAULT_AUTH:
        case MYSQL_OPT_BIND:
        case MYSQL_OPT_SSL_CA:
        case MYSQL_OPT_SSL_CAPATH:
        case MYSQL_OPT_SSL_CERT:
        case MYSQL_OPT_SSL_CIPHER:
        case MYSQL_OPT_SSL_CRL:
        case MYSQL_OPT_SSL_CRLPATH:
        case MYSQL_OPT_SSL_KEY:
        case MYSQL_OPT_TLS_VERSION:
        case MYSQL_PLUGIN_DIR:
        case MYSQL_READ_DEFAULT_FILE:
        case MYSQL_READ_DEFAULT_GROUP:
        case MYSQL_SERVER_PUBLIC_KEY:
        case MYSQL_SET_CHARSET_DIR:
        case MYSQL_SET_CHARSET_NAME:
        case MYSQL_SET_CLIENT_IP:
        case MYSQL_SHARED_MEMORY_BASE_NAME: {
          const char* val;
          mysql_get_option(my, opt, &val);
          ret = val ? JS_NewString(ctx, val) : JS_NULL;
          break;
        }

        default: {
          ret = JS_ThrowInternalError(ctx, "no such option %d", opt);
          break;
        }
      }

      break;
    }

    case METHOD_SET_OPTION: {
      int32_t opt = -1;

      JS_ToInt32(ctx, &opt, argv[0]);

      switch(opt) {
        /* unsigned int */
        case MYSQL_OPT_CONNECT_TIMEOUT:
        case MYSQL_OPT_PROTOCOL:
        case MYSQL_OPT_READ_TIMEOUT:
        case MYSQL_OPT_WRITE_TIMEOUT: {
          uint32_t val;

          JS_ToUint32(ctx, &val, argv[1]);

          if(mysql_options(my, opt, (unsigned int*)&val))
            ret = JS_ThrowInternalError(ctx, "mysql error: %s", mysql_error(my));

          break;
        }
        /* unsigned long */
        case MYSQL_OPT_MAX_ALLOWED_PACKET:
        case MYSQL_OPT_NET_BUFFER_LENGTH: {
          int64_t val;

          JS_ToInt64(ctx, &val, argv[1]);

          if(mysql_options(my, opt, (unsigned long*)&val))
            ret = JS_ThrowInternalError(ctx, "mysql error: %s", mysql_error(my));

          break;
        }

        case MYSQL_OPT_NONBLOCK: {
          mysql_options(my, opt, 0);
          break;
        }

        /* my_bool */
        case MYSQL_ENABLE_CLEARTEXT_PLUGIN:
        case MYSQL_OPT_CAN_HANDLE_EXPIRED_PASSWORDS:
        case MYSQL_OPT_GUESS_CONNECTION:
        case MYSQL_OPT_LOCAL_INFILE:
        case MYSQL_OPT_RECONNECT:
        case MYSQL_OPT_USE_RESULT:
        case MYSQL_OPT_SSL_ENFORCE:
        case MYSQL_OPT_SSL_VERIFY_SERVER_CERT:
        case MYSQL_OPT_USE_EMBEDDED_CONNECTION:
        case MYSQL_OPT_USE_REMOTE_CONNECTION:
        case MYSQL_REPORT_DATA_TRUNCATION:
        case MYSQL_SECURE_AUTH: {
          my_bool val = JS_ToBool(ctx, argv[1]);

          mysql_options(my, opt, &val);
          break;
        }

        /* const char*/
        case MYSQL_DEFAULT_AUTH:
        case MYSQL_OPT_BIND:
        case MYSQL_OPT_SSL_CA:
        case MYSQL_OPT_SSL_CAPATH:
        case MYSQL_OPT_SSL_CERT:
        case MYSQL_OPT_SSL_CIPHER:
        case MYSQL_OPT_SSL_CRL:
        case MYSQL_OPT_SSL_CRLPATH:
        case MYSQL_OPT_SSL_KEY:
        case MYSQL_OPT_TLS_VERSION:
        case MYSQL_PLUGIN_DIR:
        case MYSQL_READ_DEFAULT_FILE:
        case MYSQL_READ_DEFAULT_GROUP:
        case MYSQL_SERVER_PUBLIC_KEY:
        case MYSQL_SET_CHARSET_DIR:
        case MYSQL_SET_CHARSET_NAME:
        case MYSQL_SET_CLIENT_IP:
        case MYSQL_SHARED_MEMORY_BASE_NAME: {
          const char* val;

          val = JS_ToCString(ctx, argv[1]);

          if(mysql_options(my, opt, val))
            ret = JS_ThrowInternalError(ctx, "mysql error: %s", mysql_error(my));

          break;
        }

        default: {
          ret = JS_ThrowInternalError(ctx, "no such option %d", opt);
          break;
        }
      }

      break;
    }
  }

  return ret;
}

enum {
  PROP_MORE_RESULTS,
  PROP_AFFECTED_ROWS,
  PROP_WARNING_COUNT,
  PROP_FD,
  PROP_INFO,
  PROP_ERRNO,
  PROP_ERROR,
  PROP_INSERT_ID,
  PROP_CHARSET,
  PROP_TIMEOUT,
  PROP_TIMEOUT_MS,
  PROP_SERVER_NAME,
  PROP_SERVER_INFO,
  PROP_SERVER_VERSION,
  PROP_USER,
  PROP_PASSWORD,
  PROP_HOST,
  PROP_DB,
  PROP_PORT,
  PROP_UNIX_SOCKET,

};

static JSValue
js_mysql_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MYSQL* my = JS_GetOpaque(this_val, js_mysql_class_id);
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case PROP_MORE_RESULTS: {
      ret = JS_NewBool(ctx, mysql_more_results(my));
      break;
    }
    case PROP_AFFECTED_ROWS: {
      my_ulonglong affected = mysql_affected_rows(my);

      if((signed)affected != -1ll)
        ret = JS_NewInt64(ctx, affected);

      break;
    }
    case PROP_WARNING_COUNT: {
      ret = JS_NewUint32(ctx, mysql_warning_count(my));
      break;
    }
    case PROP_FD: {
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
    case PROP_CHARSET: {
      const char* charset = mysql_character_set_name(my);
      ret = charset && *charset ? JS_NewString(ctx, charset) : JS_NULL;
      break;
    }
    case PROP_TIMEOUT: {
      ret = JS_NewUint32(ctx, mysql_get_timeout_value(my));
      break;
    }
    case PROP_TIMEOUT_MS: {
      ret = JS_NewUint32(ctx, mysql_get_timeout_value_ms(my));
      break;
    }
    case PROP_SERVER_NAME: {
      const char* name = mysql_get_server_name(my);
      ret = name && *name ? JS_NewString(ctx, name) : JS_NULL;
      break;
    }
    case PROP_SERVER_INFO: {
      const char* info = mysql_get_server_info(my);
      ret = info && *info ? JS_NewString(ctx, info) : JS_NULL;
      break;
    }
    case PROP_SERVER_VERSION: {
      ret = JS_NewUint32(ctx, mysql_get_server_version(my));
      break;
    }
    case PROP_USER: {
      ret = my->user ? JS_NewString(ctx, my->user) : JS_NULL;
      break;
    }
    case PROP_PASSWORD: {
      ret = my->passwd ? JS_NewString(ctx, my->passwd) : JS_NULL;
      break;
    }
    case PROP_HOST: {
      ret = my->host ? JS_NewString(ctx, my->host) : JS_NULL;
      break;
    }
    case PROP_PORT: {
      if(my->port > 0)
        ret = JS_NewUint32(ctx, my->port);
      break;
    }
    case PROP_DB: {
      ret = my->db ? JS_NewString(ctx, my->db) : JS_NULL;

      break;
    }
    case PROP_UNIX_SOCKET: {
      ret = my->unix_socket ? JS_NewString(ctx, my->unix_socket) : JS_NULL;
      break;
    }
  }

  return ret;
}

enum {
  PROP_CLIENT_INFO,
  PROP_CLIENT_VERSION,
  PROP_THREAD_SAFE,
};

static JSValue
js_mysql_getstatic(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case PROP_CLIENT_INFO: {
      const char* info = mysql_get_client_info();
      ret = info && *info ? JS_NewString(ctx, info) : JS_NULL;
      break;
    }
    case PROP_CLIENT_VERSION: {
      ret = JS_NewUint32(ctx, mysql_get_client_version());
      break;
    }
    case PROP_THREAD_SAFE: {
      ret = JS_NewBool(ctx, mysql_thread_safe());
      break;
    }
  }

  return ret;
}

static JSValue
js_mysql_value_string(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  int i;
  DynBuf buf;
  dbuf_init2(&buf, 0, 0);

  for(i = 0; i < argc; i++) {
    if(i > 0)
      dbuf_putc(&buf, ',');
    value_to_mysql(ctx, &buf, argv[i]);
  }

  ret = JS_NewStringLen(ctx, (const char*)buf.buf, buf.size);
  dbuf_free(&buf);
  return ret;
}

static JSValue
js_mysql_escape_string(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  char* dst;
  const char* src;
  size_t len;

  if(!(src = JS_ToCStringLen(ctx, &len, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be string");

  if((!(dst = js_malloc(ctx, 2 * len + 1)))) {
    JS_FreeCString(ctx, src);
    return JS_ThrowOutOfMemory(ctx);
  }

  len = mysql_escape_string(dst, src, len);
  ret = JS_NewStringLen(ctx, dst, len);
  js_free(ctx, dst);

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

  if((my = mysql_init(NULL))) {
    // mysql_options(my, MYSQL_OPT_NONBLOCK, 0);
  }

  obj = js_mysql_new(ctx, proto, my);
  JS_FreeValue(ctx, proto);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_mysql_connect_cont(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue data[]) {
  int32_t wantwrite, oldstate, newstate, fd;
  MYSQL *my, *ret = 0;

  if(!(my = js_mysql_data(ctx, data[1])))
    return JS_EXCEPTION;

  JS_ToInt32(ctx, &wantwrite, data[0]);

  oldstate = wantwrite ? MYSQL_WAIT_WRITE : MYSQL_WAIT_READ;

  newstate = mysql_real_connect_cont(&ret, my, oldstate);
  fd = mysql_get_socket(my);

  if(newstate == 0) {
    js_iohandler_set(ctx, data[2], fd, JS_NULL);
    int ret;

    if((ret = mysql_set_character_set(my, "utf8"))) {
      printf("failed setting MySQL character set to UTF8: %s (%ii)", mysql_error(my), mysql_errno(my));
    }

    JS_Call(ctx, data[3], JS_UNDEFINED, 1, &data[1]);
  } else if(newstate != oldstate) {
    JSValue handler, hdata[5] = {
                         JS_NewInt32(ctx, wantwrite),
                         JS_DupValue(ctx, data[1]),
                         js_iohandler_fn(ctx, !!(newstate & MYSQL_WAIT_WRITE)),
                         JS_DupValue(ctx, data[3]),
                         JS_DupValue(ctx, data[4]),
                     };
    handler = JS_NewCFunctionData(ctx, js_mysql_connect_cont, 0, 0, countof(hdata), hdata);

    js_iohandler_set(ctx, data[2], fd, JS_NULL);
    js_iohandler_set(ctx, hdata[2], fd, handler);
    JS_FreeValue(ctx, handler);
  }

#ifdef DEBUG_OUTPUT
  printf("%s wantwrite=%i fd=%i newstate=%i my=%p ret=%p error='%s'\n", __func__, wantwrite, fd, newstate, my, ret, mysql_error(my));
#endif

  return JS_UNDEFINED;
}

static JSValue
js_mysql_connect_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue promise = JS_UNDEFINED, data[5], handler;
  struct ConnectParameters c = {0, 0, 0, 0, 0};
  MYSQL *my, *ret = 0;
  int32_t wantwrite, state, fd;

  if(!(my = js_mysql_data(ctx, this_val)))
    return JS_EXCEPTION;

  connectparams_init(ctx, &c, argc, argv);

  state = mysql_real_connect_start(&ret, my, c.host, c.user, c.password, c.db, c.port, c.socket, c.flags);
  fd = mysql_get_socket(my);

#ifdef DEBUG_OUTPUT
  printf("%s state=%d fd=%" PRId32 " flags=%" PRId64 "\n", __func__, state, fd, flags);
#endif

  wantwrite = !!(state & MYSQL_WAIT_WRITE);

  promise = JS_NewPromiseCapability(ctx, &data[3]);

  data[0] = JS_NewInt32(ctx, wantwrite);
  data[1] = JS_DupValue(ctx, this_val);
  data[2] = js_iohandler_fn(ctx, wantwrite);

  handler = JS_NewCFunctionData(ctx, js_mysql_connect_cont, 0, 0, countof(data), data);

  if(!js_iohandler_set(ctx, data[2], fd, handler))
    JS_Call(ctx, data[4], JS_UNDEFINED, 0, 0);

  connectparams_free(ctx, &c);

  return promise;
}

static JSValue
js_mysql_connect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MYSQL *my, *ret = 0;
  struct ConnectParameters c = {0, 0, 0, 0, 0};

  if(!(my = js_mysql_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(mysql_nonblock(my))
    return js_mysql_connect_start(ctx, this_val, argc, argv);

  connectparams_init(ctx, &c, argc, argv);

  ret = mysql_real_connect(my, c.host, c.user, c.password, c.db, c.port, c.socket, c.flags);

  connectparams_free(ctx, &c);

  return ret ? JS_DupValue(ctx, this_val) : JS_NULL;
}

static JSValue
js_mysql_query_cont(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue data[]) {
  int32_t wantwrite, oldstate, newstate, fd;
  int ret = 0;
  MYSQL* my = 0;

  if(!(my = js_mysql_data(ctx, data[1])))
    return JS_EXCEPTION;

  fd = mysql_get_socket(my);
  JS_ToInt32(ctx, &wantwrite, data[0]);

  oldstate = wantwrite ? MYSQL_WAIT_WRITE : MYSQL_WAIT_READ;

  newstate = mysql_real_query_cont(&ret, my, oldstate);

  if(newstate == 0) {
    js_iohandler_set(ctx, data[2], fd, JS_NULL);

    MYSQL_RES* res = mysql_use_result(my);
    JSValue res_val = js_mysqlresult_wrap(ctx, res);

    if(res) {
      JS_DefinePropertyValueStr(ctx, res_val, "handle", JS_DupValue(ctx, data[1]), JS_PROP_CONFIGURABLE);

      /*JSValue res_type = JS_GetPropertyStr(ctx, data[1], "resultType");

         if(!JS_IsUndefined(res_type) && !JS_IsException(res_type))
           JS_SetPropertyStr(ctx, res_val, "resultType", res_type);*/
    }

    JS_Call(ctx, data[3], JS_UNDEFINED, 1, &res_val);
    JS_FreeValue(ctx, res_val);

  } else if(newstate != oldstate) {
    JSValue handler, hdata[5] = {
                         JS_NewInt32(ctx, wantwrite),
                         JS_DupValue(ctx, data[1]),
                         js_iohandler_fn(ctx, !!(newstate & MYSQL_WAIT_WRITE)),
                         JS_DupValue(ctx, data[3]),
                         JS_DupValue(ctx, data[4]),
                     };
    handler = JS_NewCFunctionData(ctx, js_mysql_query_cont, 0, 0, countof(hdata), hdata);

    js_iohandler_set(ctx, data[2], fd, JS_NULL);
    js_iohandler_set(ctx, hdata[2], fd, handler);
    JS_FreeValue(ctx, handler);
  }

#ifdef DEBUG_OUTPUT
  printf("%s wantwrite=%i fd=%i newstate=%i my=%p ret=%d error='%s'\n", __func__, wantwrite, fd, newstate, my, ret, mysql_error(my));
#endif

  return JS_UNDEFINED;
}

static JSValue
js_mysql_query_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue promise = JS_UNDEFINED, data[5], handler;
  const char* query = 0;
  size_t query_len;
  MYSQL* my;
  int32_t wantwrite, state, ret = 0;

  if(!(my = js_mysql_data(ctx, this_val)))
    return JS_EXCEPTION;

  query = JS_ToCStringLen(ctx, &query_len, argv[0]);
  state = mysql_real_query_start(&ret, my, query, query_len);

#ifdef DEBUG_OUTPUT
  printf("%s state=%d ret=%d\n", __func__, state, ret);
#endif

  wantwrite = !!(state & MYSQL_WAIT_WRITE);

  promise = JS_NewPromiseCapability(ctx, &data[3]);

  data[0] = JS_NewInt32(ctx, wantwrite);
  data[1] = JS_DupValue(ctx, this_val);
  data[2] = js_iohandler_fn(ctx, wantwrite);

  handler = JS_NewCFunctionData(ctx, js_mysql_query_cont, 0, 0, countof(data), data);

  if(!js_iohandler_set(ctx, data[2], mysql_get_socket(my), handler))
    JS_Call(ctx, data[4], JS_UNDEFINED, 0, 0);

  return promise;
}

static JSValue
js_mysql_query(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MYSQL* my;
  MYSQL_RES* res = 0;
  const char* query = 0;
  size_t query_len;
  int state;
  JSValue ret = JS_UNDEFINED;

  if(!(my = js_mysql_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(mysql_nonblock(my))
    return js_mysql_query_start(ctx, this_val, argc, argv);

  query = JS_ToCStringLen(ctx, &query_len, argv[0]);
  state = mysql_real_query(my, query, query_len);

  if(state == 0) {
    res = mysql_store_result(my);
    ret = res ? js_mysqlresult_wrap(ctx, res) : JS_NULL;
  }

  if(res)
    JS_DefinePropertyValueStr(ctx, ret, "handle", JS_DupValue(ctx, this_val), JS_PROP_CONFIGURABLE);

  return ret;
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

static void
js_mysql_finalizer(JSRuntime* rt, JSValue val) {
  MYSQL* my;

  if((my = JS_GetOpaque(val, js_mysql_class_id))) {
    mysql_close(my);
  }
}

static JSClassDef js_mysql_class = {
    .class_name = "MySQL",
    .finalizer = js_mysql_finalizer,
};

static const JSCFunctionListEntry js_mysql_funcs[] = {
    JS_CGETSET_MAGIC_DEF("moreResults", js_mysql_get, 0, PROP_MORE_RESULTS),
    JS_CGETSET_MAGIC_DEF("affectedRows", js_mysql_get, 0, PROP_AFFECTED_ROWS),
    JS_CGETSET_MAGIC_DEF("warningCount", js_mysql_get, 0, PROP_WARNING_COUNT),
    JS_CGETSET_MAGIC_DEF("fd", js_mysql_get, 0, PROP_FD),
    JS_CGETSET_MAGIC_DEF("errno", js_mysql_get, 0, PROP_ERRNO),
    JS_CGETSET_MAGIC_DEF("error", js_mysql_get, 0, PROP_ERROR),
    JS_CGETSET_MAGIC_DEF("info", js_mysql_get, 0, PROP_INFO),
    JS_CGETSET_MAGIC_DEF("insertId", js_mysql_get, 0, PROP_INSERT_ID),
    JS_CGETSET_MAGIC_DEF("charset", js_mysql_get, 0, PROP_CHARSET),
    JS_CGETSET_MAGIC_DEF("timeout", js_mysql_get, 0, PROP_TIMEOUT),
    JS_CGETSET_MAGIC_DEF("timeoutMs", js_mysql_get, 0, PROP_TIMEOUT_MS),
    JS_CGETSET_MAGIC_DEF("serverName", js_mysql_get, 0, PROP_SERVER_NAME),
    JS_CGETSET_MAGIC_DEF("serverInfo", js_mysql_get, 0, PROP_SERVER_INFO),
    JS_CGETSET_MAGIC_DEF("user", js_mysql_get, 0, PROP_USER),
    JS_CGETSET_MAGIC_DEF("password", js_mysql_get, 0, PROP_PASSWORD),
    JS_CGETSET_MAGIC_DEF("host", js_mysql_get, 0, PROP_HOST),
    JS_CGETSET_MAGIC_DEF("port", js_mysql_get, 0, PROP_PORT),
    JS_CGETSET_MAGIC_DEF("socket", js_mysql_get, 0, PROP_UNIX_SOCKET),
    JS_CGETSET_MAGIC_DEF("db", js_mysql_get, 0, PROP_DB),
    JS_CFUNC_DEF("connect", 1, js_mysql_connect),
    JS_CFUNC_DEF("query", 1, js_mysql_query),
    JS_CFUNC_DEF("close", 0, js_mysql_close),
    JS_ALIAS_DEF("execute", "query"),
    JS_CFUNC_MAGIC_DEF("escapeString", 1, js_mysql_methods, METHOD_ESCAPE_STRING),
    JS_CFUNC_MAGIC_DEF("getOption", 1, js_mysql_methods, METHOD_GET_OPTION),
    JS_CFUNC_MAGIC_DEF("setOption", 2, js_mysql_methods, METHOD_SET_OPTION),
    JS_PROP_INT32_DEF("resultType", 0, JS_PROP_C_W_E),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MySQL", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_mysql_static[] = {
    JS_CGETSET_MAGIC_DEF("clientInfo", js_mysql_getstatic, 0, PROP_CLIENT_INFO),
    JS_CGETSET_MAGIC_DEF("clientVersion", js_mysql_getstatic, 0, PROP_CLIENT_VERSION),
    JS_CGETSET_MAGIC_DEF("threadSafe", js_mysql_getstatic, 0, PROP_THREAD_SAFE),
    JS_CFUNC_DEF("escapeString", 1, js_mysql_escape_string),
    JS_CFUNC_DEF("valueString", 1, js_mysql_value_string),
};

static const JSCFunctionListEntry js_mysql_defines[] = {
    JS_PROP_INT32_DEF("RESULT_OBJECT", RESULT_OBJECT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("RESULT_STRING", RESULT_STRING, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("RESULT_TABLENAME", RESULT_TABLENAME, JS_PROP_CONFIGURABLE),
    JS_PROP_INT64_DEF("COUNT_ERROR", MYSQL_COUNT_ERROR, JS_PROP_CONFIGURABLE),
    /* JS_PROP_INT32_DEF("WAIT_READ", MYSQL_WAIT_READ, JS_PROP_CONFIGURABLE),
     JS_PROP_INT32_DEF("WAIT_WRITE", MYSQL_WAIT_WRITE, JS_PROP_CONFIGURABLE),
     JS_PROP_INT32_DEF("WAIT_EXCEPT", MYSQL_WAIT_EXCEPT, JS_PROP_CONFIGURABLE),
     JS_PROP_INT32_DEF("WAIT_TIMEOUT", MYSQL_WAIT_TIMEOUT, JS_PROP_CONFIGURABLE),*/
    JS_PROP_INT32_DEF("DATABASE_DRIVER", MYSQL_DATABASE_DRIVER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DEFAULT_AUTH", MYSQL_DEFAULT_AUTH, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ENABLE_CLEARTEXT_PLUGIN", MYSQL_ENABLE_CLEARTEXT_PLUGIN, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("INIT_COMMAND", MYSQL_INIT_COMMAND, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_BIND", MYSQL_OPT_BIND, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_CAN_HANDLE_EXPIRED_PASSWORDS", MYSQL_OPT_CAN_HANDLE_EXPIRED_PASSWORDS, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_COMPRESS", MYSQL_OPT_COMPRESS, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_CONNECT_ATTR_ADD", MYSQL_OPT_CONNECT_ATTR_ADD, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_CONNECT_ATTR_DELETE", MYSQL_OPT_CONNECT_ATTR_DELETE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_CONNECT_ATTR_RESET", MYSQL_OPT_CONNECT_ATTR_RESET, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_CONNECT_ATTRS", MYSQL_OPT_CONNECT_ATTRS, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_CONNECT_TIMEOUT", MYSQL_OPT_CONNECT_TIMEOUT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_GUESS_CONNECTION", MYSQL_OPT_GUESS_CONNECTION, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_LOCAL_INFILE", MYSQL_OPT_LOCAL_INFILE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_MAX_ALLOWED_PACKET", MYSQL_OPT_MAX_ALLOWED_PACKET, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_NAMED_PIPE", MYSQL_OPT_NAMED_PIPE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_NET_BUFFER_LENGTH", MYSQL_OPT_NET_BUFFER_LENGTH, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_NONBLOCK", MYSQL_OPT_NONBLOCK, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_PROTOCOL", MYSQL_OPT_PROTOCOL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_READ_TIMEOUT", MYSQL_OPT_READ_TIMEOUT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_RECONNECT", MYSQL_OPT_RECONNECT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_SSL_CA", MYSQL_OPT_SSL_CA, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_SSL_CAPATH", MYSQL_OPT_SSL_CAPATH, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_SSL_CERT", MYSQL_OPT_SSL_CERT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_SSL_CIPHER", MYSQL_OPT_SSL_CIPHER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_SSL_CRL", MYSQL_OPT_SSL_CRL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_SSL_CRLPATH", MYSQL_OPT_SSL_CRLPATH, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_SSL_ENFORCE", MYSQL_OPT_SSL_ENFORCE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_SSL_KEY", MYSQL_OPT_SSL_KEY, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_SSL_VERIFY_SERVER_CERT", MYSQL_OPT_SSL_VERIFY_SERVER_CERT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_TLS_VERSION", MYSQL_OPT_TLS_VERSION, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_USE_EMBEDDED_CONNECTION", MYSQL_OPT_USE_EMBEDDED_CONNECTION, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_USE_REMOTE_CONNECTION", MYSQL_OPT_USE_REMOTE_CONNECTION, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_USE_RESULT", MYSQL_OPT_USE_RESULT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPT_WRITE_TIMEOUT", MYSQL_OPT_WRITE_TIMEOUT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PLUGIN_DIR", MYSQL_PLUGIN_DIR, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PROGRESS_CALLBACK", MYSQL_PROGRESS_CALLBACK, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("READ_DEFAULT_FILE", MYSQL_READ_DEFAULT_FILE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("READ_DEFAULT_GROUP", MYSQL_READ_DEFAULT_GROUP, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("REPORT_DATA_TRUNCATION", MYSQL_REPORT_DATA_TRUNCATION, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SECURE_AUTH", MYSQL_SECURE_AUTH, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SERVER_PUBLIC_KEY", MYSQL_SERVER_PUBLIC_KEY, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SET_CHARSET_DIR", MYSQL_SET_CHARSET_DIR, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SET_CHARSET_NAME", MYSQL_SET_CHARSET_NAME, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SET_CLIENT_IP", MYSQL_SET_CLIENT_IP, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SHARED_MEMORY_BASE_NAME", MYSQL_SHARED_MEMORY_BASE_NAME, JS_PROP_CONFIGURABLE),
};

static JSValue
result_value(JSContext* ctx, MYSQL_FIELD const* field, char* buf, size_t len, int rtype) {
  JSValue ret = JS_UNDEFINED;

  if(buf == 0)
    return (rtype & RESULT_STRING) ? JS_NewString(ctx, "NULL") : JS_NULL;

  if(field_is_boolean(field)) {
    BOOL value = *(my_bool*)buf;
    if((rtype & RESULT_STRING))
      return JS_NewString(ctx, value ? "1" : "0");

    return JS_NewBool(ctx, value);
  }

  if(!(rtype & RESULT_STRING)) {
    if(field_is_number(field))
      return string_to_number(ctx, buf);
    if(field_is_decimal(field))
      return string_to_bigdecimal(ctx, buf);
    if(field_is_date(field)) {
      if(field->length == 19 && buf[10] == ' ')
        buf[10] = 'T';
      return string_to_date(ctx, buf);
    }
  }

  if(field_is_blob(field)) {
    if((rtype & RESULT_STRING))
      return JS_NewStringLen(ctx, buf, len);

    return JS_NewArrayBufferCopy(ctx, (uint8_t const*)buf, len);
  }

  ret = JS_NewString(ctx, buf);

  return ret;
}

static JSValue
result_array(JSContext* ctx, MYSQL_RES* res, MYSQL_ROW row, int rtype) {
  JSValue ret = JS_NewArray(ctx);
  uint32_t i, num_fields = mysql_num_fields(res);
  MYSQL_FIELD* fields = mysql_fetch_fields(res);
  unsigned long* field_lengths = mysql_fetch_lengths(res);

  for(i = 0; i < num_fields; i++) {
#ifdef DEBUG_OUTPUT_
    printf("%s num_fields=%" PRIu32 " row[%" PRIu32 "] = '%.*s'\n", __func__, num_fields, i, (int)(field_lengths[i] > 32 ? 32 : field_lengths[i]), row[i]);
#endif
    JS_SetPropertyUint32(ctx, ret, i, result_value(ctx, &fields[i], row[i], field_lengths[i], rtype));
  }

  return ret;
}

static JSValue
result_object(JSContext* ctx, MYSQL_RES* res, MYSQL_ROW row, int rtype) {
  JSValue ret = JS_NewObject(ctx);
  uint32_t i, num_fields = mysql_num_fields(res);
  MYSQL_FIELD* fields = mysql_fetch_fields(res);
  MySQLNameFunc* fn = (rtype & RESULT_TABLENAME) ? field_id : field_namefunc(fields, num_fields);
  unsigned long* field_lengths = mysql_fetch_lengths(res);

  for(i = 0; i < num_fields; i++) {
    char* id;

    if((id = fn(ctx, &fields[i]))) {
      JS_SetPropertyStr(ctx, ret, id, result_value(ctx, &fields[i], row[i], field_lengths[i], rtype));
      js_free(ctx, id);
    }
  }

  return ret;
}

static JSValue
result_row(JSContext* ctx, MYSQL_RES* res, MYSQL_ROW row, int rtype) {
  JSValue ret, val;
  MySQLRowFunc* row_func = (rtype & RESULT_OBJECT) ? result_object : result_array;

  val = row ? row_func(ctx, res, row, rtype) : JS_NULL;

  ret = js_iterator_result(ctx, val, row ? FALSE : TRUE);

  JS_FreeValue(ctx, val);

  return ret;
}

static void
result_yield(JSContext* ctx, JSValueConst func, MYSQL_RES* res, MYSQL_ROW row, int rtype) {
  JSValue ret = result_row(ctx, res, row, rtype);

  value_yield_free(ctx, func, ret);
}

MYSQL_RES*
js_mysqlresult_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_mysqlresult_class_id);
}

MYSQL*
js_mysqlresult_handle(JSContext* ctx, JSValueConst value) {
  MYSQL_RES* res;
  MYSQL* ret = 0;
  JSValue handle = JS_GetPropertyStr(ctx, value, "handle");

  if(JS_IsObject(handle))
    ret = JS_GetOpaque(handle, js_mysql_class_id);

  JS_FreeValue(ctx, handle);

  if(!ret)
    if((res = JS_GetOpaque(value, js_mysqlresult_class_id)))
      ret = res->handle;

  return ret;
}

BOOL
js_mysqlresult_nonblock(JSContext* ctx, JSValueConst value) {
  MYSQL* my;

  if((my = js_mysqlresult_handle(ctx, value)))
    return mysql_nonblock(my);

  return FALSE;
}

static inline int
js_mysqlresult_rtype(JSContext* ctx, JSValueConst value) {
  JSValue handle = JS_GetPropertyStr(ctx, value, "handle");
  int rtype = js_mysql_rtype(ctx, handle);
  JS_FreeValue(ctx, handle);
  return rtype;
}

static JSValue
js_mysqlresult_next_cont(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue data[]) {
  MYSQL_ROW row;
  JSValue my_val;
  MYSQL_RES* res;
  MYSQL* my = 0;
  int wantwrite, oldstate, newstate, fd;
  uint32_t num_fields, field_count;

  if(!(res = js_mysqlresult_data(ctx, data[1])))
    return JS_EXCEPTION;

  if(!(my = js_mysqlresult_handle(ctx, data[1])))
    my = res->handle;

  JS_ToInt32(ctx, &wantwrite, data[0]);

  oldstate = wantwrite ? MYSQL_WAIT_WRITE : MYSQL_WAIT_READ;

  newstate = mysql_fetch_row_cont(&row, res, oldstate);

  field_count = newstate ? 0 : mysql_field_count(my);
  num_fields = newstate ? 0 : mysql_num_fields(res);
  fd = mysql_get_socket(my);

#ifdef DEBUG_OUTPUT
  printf("%s field_count=%" PRIu32 " num_fields=%" PRIu32 " newstate=%d\n", __func__, field_count, num_fields, newstate);
#endif

  if(newstate == 0 && num_fields == field_count) {
    js_iohandler_set(ctx, data[2], fd, JS_NULL);

    value_yield_free(ctx, data[3], result_row(ctx, res, row, js_mysqlresult_rtype(ctx, data[1])));

  } else if(newstate != oldstate) {
    JSValue handler, hdata[5] = {
                         JS_NewInt32(ctx, wantwrite),
                         JS_DupValue(ctx, data[1]),
                         js_iohandler_fn(ctx, !!(newstate & MYSQL_WAIT_WRITE)),
                         JS_DupValue(ctx, data[3]),
                         JS_DupValue(ctx, data[4]),
                     };
    handler = JS_NewCFunctionData(ctx, js_mysqlresult_next_cont, 0, 0, countof(hdata), hdata);

    js_iohandler_set(ctx, data[2], fd, JS_NULL);
    js_iohandler_set(ctx, hdata[2], fd, handler);
    JS_FreeValue(ctx, handler);
  }

#ifdef DEBUG_OUTPUT
  printf("%s wantwrite=%i fd=%i my=%p error='%s'\n", __func__, wantwrite, fd, my, mysql_error(my));
#endif

  return JS_UNDEFINED;
}

static JSValue
js_mysqlresult_next_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue my_val, promise = JS_UNDEFINED, data[5], handler;
  int64_t client_flags = 0;
  MYSQL* my;
  MYSQL_RES* res;
  MYSQL_ROW row;
  int wantwrite, state;
  uint32_t num_fields, field_count;

  if(!(res = js_mysqlresult_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(my = js_mysqlresult_handle(ctx, this_val)))
    my = res->handle;

  field_count = mysql_field_count(my);

  state = mysql_fetch_row_start(&row, res);

  num_fields = state ? 0 : mysql_num_fields(res);

#ifdef DEBUG_OUTPUT
  printf("%s field_count=%" PRIu32 " num_fields=%" PRIu32 " state=%d\n", __func__, field_count, num_fields, state);
#endif

  wantwrite = !!(state & MYSQL_WAIT_WRITE);

  promise = JS_NewPromiseCapability(ctx, &data[3]);

  if(state == 0 && num_fields == field_count) {
    value_yield_free(ctx, data[3], result_row(ctx, res, row, js_mysqlresult_rtype(ctx, this_val)));

  } else {
    data[0] = JS_NewInt32(ctx, wantwrite);
    data[1] = JS_DupValue(ctx, this_val);
    data[2] = js_iohandler_fn(ctx, wantwrite);

    handler = JS_NewCFunctionData(ctx, js_mysqlresult_next_cont, 0, 0, countof(data), data);

    if(!js_iohandler_set(ctx, data[2], mysql_get_socket(my), handler))
      JS_Call(ctx, data[4], JS_UNDEFINED, 0, 0);
  }

  return promise;
}

static JSValue
js_mysqlresult_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue my_val, ret;
  MYSQL_ROW row;
  MYSQL_RES* res;

  if(!(res = js_mysqlresult_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(js_mysqlresult_nonblock(ctx, this_val))
    return js_mysqlresult_next_start(ctx, this_val, argc, argv);

  row = mysql_fetch_row(res);

  ret = result_row(ctx, res, row, js_mysqlresult_rtype(ctx, this_val));
  return ret;
}

enum {
  METHOD_FETCH_FIELD,
  METHOD_FETCH_FIELDS,
};

static JSValue
js_mysqlresult_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  MYSQL_RES* res;

  if(!(res = js_mysqlresult_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_FETCH_FIELD: {
      uint32_t index;
      MYSQL_FIELD* field;

      if(JS_ToUint32(ctx, &index, argv[0]))
        return JS_ThrowTypeError(ctx, "argument 1 must be a positive index");

      if(index >= mysql_num_fields(res))
        return JS_ThrowRangeError(ctx, "argument 1 must be smaller than total fields (%" PRIu32 ")", mysql_num_fields(res));

      if((field = mysql_fetch_field_direct(res, index)))
        ret = field_array(ctx, field);

      break;
    }
    case METHOD_FETCH_FIELDS: {
      MYSQL_FIELD* fields;

      if((fields = mysql_fetch_fields(res))) {
        uint32_t i, num_fields = mysql_num_fields(res);

        ret = JS_NewArray(ctx);

        for(i = 0; i < num_fields; i++) JS_SetPropertyUint32(ctx, ret, i, field_array(ctx, &fields[i]));
      }
      break;
    }
  }

  return ret;
}

static JSValue
js_mysqlresult_new(JSContext* ctx, JSValueConst proto, MYSQL_RES* res) {
  JSValue obj;

  if(js_mysqlresult_class_id == 0)
    js_mysql_init(ctx, 0);

  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = mysqlresult_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
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
  JSValue obj = JS_NULL;

  if(res)
    obj = js_mysqlresult_new(ctx, mysqlresult_proto, res);

  return obj;
}

enum {
  PROP_EOF,
  PROP_NUM_ROWS,
  PROP_NUM_FIELDS,
  PROP_FIELD_COUNT,
  PROP_CURRENT_FIELD,
};

static JSValue
js_mysqlresult_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MYSQL_RES* res;
  JSValue ret = JS_UNDEFINED;

  if(!(res = js_mysqlresult_data(ctx, this_val)))
    return JS_EXCEPTION;

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
    case PROP_FIELD_COUNT: {
      ret = JS_NewUint32(ctx, res->field_count);
      break;
    }
    case PROP_CURRENT_FIELD: {
      ret = JS_NewUint32(ctx, res->current_field);
      break;
    }
  }
  return ret;
}

enum {
  METHOD_ITERATOR,
  METHOD_ASYNC_ITERATOR,
};

static JSValue
js_mysqlresult_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  MYSQL* my = js_mysqlresult_handle(ctx, this_val);

  assert(my);

  BOOL block = !mysql_nonblock(my);

  switch(magic) {
    case METHOD_ASYNC_ITERATOR: {
      if(!block)
        ret = JS_DupValue(ctx, this_val);
      break;
    }
    case METHOD_ITERATOR: {
      if(block)
        ret = JS_DupValue(ctx, this_val);
      break;
    }
  }

  return ret;
}

static JSValue
js_mysqlresult_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  return js_mysqlresult_new(ctx, proto, 0);

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
}

static JSClassDef js_mysqlresult_class = {
    .class_name = "MySQLResult",
    .finalizer = js_mysqlresult_finalizer,
};

static const JSCFunctionListEntry js_mysqlresult_funcs[] = {
    JS_CFUNC_DEF("next", 0, js_mysqlresult_next),
    JS_CGETSET_MAGIC_DEF("eof", js_mysqlresult_get, 0, PROP_EOF),
    JS_CGETSET_MAGIC_FLAGS_DEF("numRows", js_mysqlresult_get, 0, PROP_NUM_ROWS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("numFields", js_mysqlresult_get, 0, PROP_NUM_FIELDS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("fieldCount", js_mysqlresult_get, 0, PROP_FIELD_COUNT),
    JS_CGETSET_MAGIC_DEF("currentField", js_mysqlresult_get, 0, PROP_CURRENT_FIELD),
    JS_CFUNC_MAGIC_DEF("fetchField", 1, js_mysqlresult_functions, METHOD_FETCH_FIELD),
    JS_CFUNC_MAGIC_DEF("fetchFields", 0, js_mysqlresult_functions, METHOD_FETCH_FIELDS),
    JS_CFUNC_MAGIC_DEF("[Symbol.iterator]", 0, js_mysqlresult_iterator, METHOD_ITERATOR),
    JS_CFUNC_MAGIC_DEF("[Symbol.asyncIterator]", 0, js_mysqlresult_iterator, METHOD_ASYNC_ITERATOR),
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
    JS_SetPropertyFunctionList(ctx, mysql_proto, js_mysql_defines, countof(js_mysql_defines));
    JS_SetPropertyFunctionList(ctx, mysql_ctor, js_mysql_static, countof(js_mysql_static));
    JS_SetPropertyFunctionList(ctx, mysql_ctor, js_mysql_defines, countof(js_mysql_defines));
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

static char*
field_id(JSContext* ctx, MYSQL_FIELD const* field) {
  DynBuf buf;

  dbuf_init2(&buf, 0, 0);
  dbuf_put(&buf, (const uint8_t*)field->table, field->table_length);
  dbuf_putstr(&buf, ".");
  dbuf_put(&buf, (const uint8_t*)field->name, field->name_length);
  dbuf_0(&buf);

  return (char*)buf.buf;
}

static char*
field_name(JSContext* ctx, MYSQL_FIELD const* field) {
  return js_strndup(ctx, field->name, field->name_length);
}

static MySQLNameFunc*
field_namefunc(MYSQL_FIELD* fields, uint32_t num_fields) {
  uint32_t i, j;
  BOOL eq = FALSE;

  for(i = 0; !eq && i < num_fields; i++)
    for(j = 0; !eq && j < num_fields; j++)
      if(i != j && fields[i].name_length == fields[j].name_length && byte_equal(fields[i].name, fields[i].name_length, fields[j].name))
        return field_id;

  return field_name;
}

static JSValue
field_array(JSContext* ctx, MYSQL_FIELD* field) {
  JSValue ret = JS_NewArray(ctx);
  const char* type = 0;
  DynBuf buf;
  dbuf_init2(&buf, 0, 0);

  JS_SetPropertyUint32(ctx, ret, 0, JS_NewStringLen(ctx, field->name, field->name_length));

  switch(field->type) {
    case MYSQL_TYPE_DECIMAL: type = "decimal"; break;
    case MYSQL_TYPE_TINY: type = "tiny"; break;
    case MYSQL_TYPE_SHORT: type = "short"; break;
    case MYSQL_TYPE_LONG: type = "long"; break;
    case MYSQL_TYPE_FLOAT: type = "float"; break;
    case MYSQL_TYPE_DOUBLE: type = "double"; break;
    case MYSQL_TYPE_NULL: type = "null"; break;
    case MYSQL_TYPE_TIMESTAMP: type = "timestamp"; break;
    case MYSQL_TYPE_LONGLONG: type = "longlong"; break;
    case MYSQL_TYPE_INT24: type = "int24"; break;
    case MYSQL_TYPE_DATE: type = "date"; break;
    case MYSQL_TYPE_TIME: type = "time"; break;
    case MYSQL_TYPE_DATETIME: type = "datetime"; break;
    case MYSQL_TYPE_YEAR: type = "year"; break;
    case MYSQL_TYPE_NEWDATE: type = "newdate"; break;
    case MYSQL_TYPE_VARCHAR: type = "varchar"; break;
    case MYSQL_TYPE_BIT: type = "bit"; break;
    case MYSQL_TYPE_TIMESTAMP2: type = "timestamp2"; break;
    case MYSQL_TYPE_DATETIME2: type = "datetime2"; break;
    case MYSQL_TYPE_TIME2: type = "time2"; break;
    case MYSQL_TYPE_NEWDECIMAL: type = "newdecimal"; break;
    case MYSQL_TYPE_ENUM: type = "enum"; break;
    case MYSQL_TYPE_SET: type = "set"; break;
    case MYSQL_TYPE_TINY_BLOB: type = "tiny_blob"; break;
    case MYSQL_TYPE_MEDIUM_BLOB: type = "medium_blob"; break;
    case MYSQL_TYPE_LONG_BLOB: type = "long_blob"; break;
    case MYSQL_TYPE_BLOB: type = "blob"; break;
    case MYSQL_TYPE_VAR_STRING: type = "var_string"; break;
    case MYSQL_TYPE_STRING: type = "string"; break;
    case MYSQL_TYPE_GEOMETRY: type = "geometry"; break;
    default: break;
  }

  dbuf_putstr(&buf, type);

  if(field->flags & UNSIGNED_FLAG)
    dbuf_putstr(&buf, " unsigned");

  if(field->flags & BINARY_FLAG)
    dbuf_putstr(&buf, " binary");

  if(field->flags & AUTO_INCREMENT_FLAG)
    dbuf_putstr(&buf, " auto_increment");

  JS_SetPropertyUint32(ctx, ret, 1, JS_NewStringLen(ctx, (const char*)buf.buf, buf.size));

  dbuf_free(&buf);

  JS_SetPropertyUint32(ctx, ret, 2, JS_NewUint32(ctx, field->length));
  JS_SetPropertyUint32(ctx, ret, 3, JS_NewUint32(ctx, field->max_length));
  JS_SetPropertyUint32(ctx, ret, 4, JS_NewUint32(ctx, field->decimals));
  JS_SetPropertyUint32(ctx, ret, 5, JS_NewString(ctx, (field->flags & NOT_NULL_FLAG) ? "NO" : "YES"));
  JS_SetPropertyUint32(ctx, ret, 6, JS_NewStringLen(ctx, field->def, field->def_length));

  return ret;
}

static BOOL
field_is_integer(MYSQL_FIELD const* field) {
  switch(field->type) {
    case MYSQL_TYPE_TINY:
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_INT24:
    case MYSQL_TYPE_BIT: {
      return (field->flags & NUM_FLAG);
    }
    default: {
      return FALSE;
    }
  }
}

static BOOL
field_is_float(MYSQL_FIELD const* field) {
  switch(field->type) {
    case MYSQL_TYPE_FLOAT:
    case MYSQL_TYPE_DOUBLE: {
      return (field->flags & NUM_FLAG);
    }
    default: {
      return FALSE;
    }
  }
}

static BOOL
field_is_decimal(MYSQL_FIELD const* field) {
  switch(field->type) {
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL:
    case MYSQL_TYPE_LONGLONG: {
      return (field->flags & NUM_FLAG);
    }
    default: {
      return FALSE;
    }
  }

  return FALSE;
}

static BOOL
field_is_number(MYSQL_FIELD const* field) {
  return field_is_integer(field) || field_is_float(field);
}

static BOOL
field_is_boolean(MYSQL_FIELD const* field) {
  switch(field->type) {
    case MYSQL_TYPE_TINY: {
      return field->length == 1;
    }
    case MYSQL_TYPE_BIT: {
      return field->length == 1 && (field->flags & UNSIGNED_FLAG);
    }
    default: {
      return FALSE;
    }
  }
}

static BOOL
field_is_null(MYSQL_FIELD const* field) {
  switch(field->type) {
    case MYSQL_TYPE_NULL: {
      return !(field->flags & NOT_NULL_FLAG);
    }
    default: {
      return FALSE;
    }
  }
}

static BOOL
field_is_date(MYSQL_FIELD const* field) {
  switch(field->type) {
    case MYSQL_TYPE_TIMESTAMP:
    case MYSQL_TYPE_DATE:
    case MYSQL_TYPE_TIME:
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_NEWDATE: {
      return !!(field->flags & TIMESTAMP_FLAG);
    }
    default: {
      return FALSE;
    }
  }
}

static BOOL
field_is_string(MYSQL_FIELD const* field) {
  switch(field->type) {
    case MYSQL_TYPE_BLOB:
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB: {
      if((field->flags & BLOB_FLAG))
        if(!(field->flags & BINARY_FLAG))
          return TRUE;
    }
    default: break;
  }

  switch(field->type) {
    case MYSQL_TYPE_VAR_STRING:
    case MYSQL_TYPE_STRING:
    case MYSQL_TYPE_ENUM:
    case MYSQL_TYPE_SET: {
      return TRUE;
    }
    default: {
      return FALSE;
    }
  }
}

static BOOL
field_is_blob(MYSQL_FIELD const* field) {

  if(!strcmp(field->org_table, "COLUMNS") && str_start(field->org_name, "COLUMN_"))
    return FALSE;

  if(field->type != MYSQL_TYPE_STRING)
    if(field->type != MYSQL_TYPE_VAR_STRING)
      if(!(field->flags & TIMESTAMP_FLAG))
        return (field->flags & BLOB_FLAG) && (field->flags & BINARY_FLAG);

  switch(field->type) {
    case MYSQL_TYPE_VAR_STRING:
    case MYSQL_TYPE_BLOB:
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB: {
      return !!(field->flags & BINARY_FLAG);
    }
    default: {
      return FALSE;
    }
  }
}

static JSValue
string_to_value(JSContext* ctx, const char* func_name, const char* s) {
  JSValue ret, arg = JS_NewString(ctx, s);
  ret = js_value_coerce(ctx, func_name, arg);
  JS_FreeValue(ctx, arg);
  return ret;
}

static JSValue
string_to_object(JSContext* ctx, const char* ctor_name, const char* s) {
  JSValue ret, arg = JS_NewString(ctx, s);
  ret = js_global_new(ctx, ctor_name, 1, &arg);
  JS_FreeValue(ctx, arg);
  return ret;
}

static void
value_to_mysql(JSContext* ctx, DynBuf* out, JSValueConst value) {

  if(JS_IsNull(value) || JS_IsUndefined(value) || js_is_nan(value)) {
    dbuf_putstr(out, "NULL");

  } else if(JS_IsBool(value)) {
    dbuf_putstr(out, JS_ToBool(ctx, value) ? "TRUE" : "FALSE");

  } else if(JS_IsString(value)) {
    size_t len;
    const char* src = JS_ToCStringLen(ctx, &len, value);
    char* dst;

    dbuf_putc(out, '\'');
    dst = (char*)dbuf_reserve(out, len * 2 + 1);
    len = mysql_escape_string(dst, src, len);
    out->size += len;
    dbuf_putc(out, '\'');

  } else if(js_is_date(ctx, value)) {
    size_t len;
    char* str;
    JSValue val;

    /*int64_t ut;
    val = js_invoke(ctx, value, "valueOf", 0, 0);
    JS_ToInt64(ctx, &ut, val);
    JS_FreeValue(ctx, val);

    dbuf_putstr(out, "FROM_UNIXTIME(");
    dbuf_printf(out, "%" PRId64, ut / 1000);
    if(ut % 1000) {
      dbuf_putc(out, '.');
      dbuf_printf(out, "%" PRId64, ut % 1000);
    }
    dbuf_putc(out, ')');*/

    val = js_invoke(ctx, value, "toISOString", 0, 0);
    str = js_tostringlen(ctx, &len, val);
    if(len >= 24) {
      if(str[23] == 'Z')
        len = 23;
    }
    if(len >= 19) {
      if(str[10] == 'T')
        str[10] = ' ';
    }
    dbuf_putc(out, '\'');
    dbuf_put(out, (const uint8_t*)str, len);
    dbuf_putc(out, '\'');

    js_free(ctx, str);
    JS_FreeValue(ctx, val);

  } else if(!JS_IsBigDecimal(value) && js_is_numeric(ctx, value)) {
    JSValue val = js_value_coerce(ctx, (JS_IsBigDecimal(value)) ? "Number" : "BigDecimal", value);

    value_to_mysql(ctx, out, val);
    JS_FreeValue(ctx, val);

  } else {

    InputBuffer input = js_input_buffer(ctx, value);

    if(input.size) {
      static const uint8_t hexdigits[] = "0123456789ABCDEF";
      dbuf_putstr(out, "0x");
      for(size_t i = 0; i < input.size; i++) {
        const uint8_t hex[2] = {
            hexdigits[(input.data[i] & 0xf0) >> 4],
            hexdigits[(input.data[i] & 0x0f)],
        };
        dbuf_put(out, hex, 2);
      }
      input_buffer_free(&input, ctx);

    } else {
      size_t len;
      const char* str = JS_ToCStringLen(ctx, &len, value);

      dbuf_put(out, (const uint8_t*)str, len);
      JS_FreeCString(ctx, str);
    }
  }
}

static JSValue
value_to_string(JSContext* ctx, JSValueConst value) {
  JSValue ret = JS_UNDEFINED;
  DynBuf buf;

  if(JS_IsNull(value) || JS_IsUndefined(value) || js_is_nan(value))
    return JS_NewString(ctx, "NULL");

  if(JS_IsBool(value))
    return JS_NewString(ctx, JS_ToBool(ctx, value) ? "TRUE" : "FALSE");

  dbuf_init2(&buf, 0, 0);
  value_to_mysql(ctx, &buf, value);
  ret = JS_NewStringLen(ctx, (const char*)buf.buf, buf.size);
  dbuf_free(&buf);
  return ret;
}

/**
 * @}
 */
