#include "defines.h"
#include "quickjs-mysql.h"
#include "utils.h"
#include "buffer-utils.h"
#include "char-utils.h"
#include "js-utils.h"
#include "async-closure.h"

/**
 * \addtogroup quickjs-mysql
 * @{
 */

thread_local VISIBLE JSClassID js_connectparams_class_id = 0, js_mysqlerror_class_id = 0, js_mysql_class_id = 0, js_mysqlresult_class_id = 0;
thread_local JSValue mysqlerror_proto = {{0}, JS_TAG_UNDEFINED}, mysqlerror_ctor = {{0}, JS_TAG_UNDEFINED}, mysql_proto = {{0}, JS_TAG_UNDEFINED}, mysql_ctor = {{0}, JS_TAG_UNDEFINED},
                     mysqlresult_proto = {{0}, JS_TAG_UNDEFINED}, mysqlresult_ctor = {{0}, JS_TAG_UNDEFINED};

static JSValue js_mysqlresult_wrap(JSContext* ctx, MYSQL_RES* res);

static const int wait_read = MYSQL_WAIT_READ;
static const int wait_write = MYSQL_WAIT_WRITE;

typedef enum {
  RESULT_OBJECT = 1 << 0,
  RESULT_STRING = 1 << 1,
  RESULT_TBLNAM = 1 << 2,
  RESULT_ITERAT = 1 << 3,
} ResultFlags;

struct ConnectParameters {
  int ref_count;
  char *host, *user, *password, *db;
  uint32_t port;
  char* socket;
  int64_t flags;
};

typedef char* FieldNameFunc(JSContext*, MYSQL_FIELD const*);
typedef JSValue RowValueFunc(JSContext*, MYSQL_RES*, MYSQL_ROW, ResultFlags);
typedef struct ConnectParameters MYSQLConnectParameters;

static char* field_id(JSContext* ctx, MYSQL_FIELD const* field);
static char* field_name(JSContext* ctx, MYSQL_FIELD const* field);
static JSValue field_array(JSContext* ctx, MYSQL_FIELD* field);
static FieldNameFunc* field_namefunc(MYSQL_FIELD* fields, uint32_t num_fields);
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

static JSValue js_mysqlerror_new(JSContext* ctx, const char* msg);

static inline AsyncEvent
to_asyncevent(int my_wait) {
  return ((my_wait & MYSQL_WAIT_WRITE) ? WANT_WRITE : 0) | ((my_wait & MYSQL_WAIT_READ) ? WANT_READ : 0);
}

static inline int
to_mysql_wait(AsyncEvent e) {
  return ((e & WANT_WRITE) ? MYSQL_WAIT_WRITE : 0) | ((e & WANT_READ) ? MYSQL_WAIT_READ : 0);
}

static void
js_mysql_print_value(JSContext* ctx, DynBuf* out, JSValueConst value) {

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

  } else if(js_is_numeric(ctx, value)) {
    BOOL notanum = !JS_IsNumber(value);
    JSValue val = notanum ? js_value_coerce(ctx, "Number", value) : JS_DupValue(ctx, value);
    size_t len;
    const void* src = JS_ToCStringLen(ctx, &len, val);

    JS_FreeValue(ctx, val);
    dbuf_put(out, src, len);
    JS_FreeCString(ctx, src);

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

static void
js_mysql_print_fields(JSContext* ctx, DynBuf* out, JSPropertyEnum* tmp_tab, uint32_t tmp_len) {
  for(uint32_t i = 0; i < tmp_len; i++) {
    const char* str;
    if(i > 0)
      dbuf_putstr(out, ", ");
    str = JS_AtomToCString(ctx, tmp_tab[i].atom);
    dbuf_putc(out, '`');
    dbuf_putstr(out, str);
    dbuf_putc(out, '`');
    JS_FreeCString(ctx, str);
  }
}

static void
js_mysql_print_insert(JSContext* ctx, DynBuf* out) {
  dbuf_putstr(out, "INSERT INTO ");
}

static void
js_mysql_print_values(JSContext* ctx, DynBuf* out, JSValueConst values) {
  JSValue item, iter = js_iterator_new(ctx, values);

  if(!JS_IsUndefined(iter)) {
    BOOL done = FALSE;

    dbuf_putc(out, '(');
    for(int i = 0;; i++) {
      item = js_iterator_next(ctx, iter, &done);
      if(done)
        break;
      if(i > 0)
        dbuf_putstr(out, ", ");
      js_mysql_print_value(ctx, out, item);
      JS_FreeValue(ctx, item);
    }
    dbuf_putc(out, ')');

  } else {
    JSPropertyEnum* tmp_tab;
    uint32_t tmp_len;

    if(JS_GetOwnPropertyNames(ctx, &tmp_tab, &tmp_len, values, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
      JS_FreeValue(ctx, iter);
      JS_ThrowTypeError(ctx, "argument is must be an object");
      return;
    }

    dbuf_putc(out, '(');
    js_mysql_print_fields(ctx, out, tmp_tab, tmp_len);
    dbuf_putc(out, ')');

    dbuf_putstr(out, " VALUES (");
    for(uint32_t i = 0; i < tmp_len; i++) {
      if(i > 0)
        dbuf_putstr(out, ", ");
      item = JS_GetProperty(ctx, values, tmp_tab[i].atom);
      js_mysql_print_value(ctx, out, item);
      JS_FreeValue(ctx, item);
    }
    dbuf_putc(out, ')');
    js_propertyenums_free(ctx, tmp_tab, tmp_len);
  }
}

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

static inline MYSQLConnectParameters*
js_connectparams_data(JSValueConst value) {
  return JS_GetOpaque(value, js_connectparams_class_id);
}

static inline MYSQLConnectParameters*
js_connectparams_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_connectparams_class_id);
}

static void
connectparams_init(JSContext* ctx, MYSQLConnectParameters* cp, int argc, JSValueConst argv[]) {
  cp->ref_count = 1;

  if(argc == 1 && JS_IsObject(argv[0])) {
    JSValueConst args[] = {
        JS_GetPropertyStr(ctx, argv[0], "host"),
        JS_GetPropertyStr(ctx, argv[0], "user"),
        JS_GetPropertyStr(ctx, argv[0], "password"),
        JS_GetPropertyStr(ctx, argv[0], "db"),
        JS_GetPropertyStr(ctx, argv[0], "port"),
        JS_GetPropertyStr(ctx, argv[0], "socket"),
        JS_GetPropertyStr(ctx, argv[0], "flags"),
    };

    connectparams_init(ctx, cp, countof(args), args);

    for(size_t i = 0; i < countof(args); i++)
      JS_FreeValue(ctx, args[i]);
  } else {
    cp->host = argc > 0 ? js_tostring(ctx, argv[0]) : 0;
    cp->user = argc > 1 ? js_tostring(ctx, argv[1]) : 0;
    cp->password = argc > 2 ? js_tostring(ctx, argv[2]) : 0;
    cp->db = argc > 3 ? js_tostring(ctx, argv[3]) : 0;

    if(argc > 4 && JS_IsNumber(argv[4]))
      JS_ToUint32(ctx, &cp->port, argv[4]);
    else
      cp->port = 3306;

    cp->socket = argc > 5 ? js_tostring(ctx, argv[5]) : 0;

    if(argc > 6)
      JS_ToInt64(ctx, &cp->flags, argv[6]);
    else
      cp->flags = 0;
  }
}

static inline MYSQLConnectParameters*
connectparams_dup(MYSQLConnectParameters* cp) {
  ++cp->ref_count;
  return cp;
}

static MYSQLConnectParameters*
connectparams_new(JSContext* ctx, int argc, JSValueConst argv[]) {
  MYSQLConnectParameters* cp;

  if(!(cp = js_mallocz(ctx, sizeof(MYSQLConnectParameters))))
    return 0;

  connectparams_init(ctx, cp, argc, argv);

  return cp;
}

static void
connectparams_release(JSRuntime* rt, MYSQLConnectParameters* cp) {
  js_free_rt(rt, cp->host);
  js_free_rt(rt, cp->user);
  js_free_rt(rt, cp->password);
  js_free_rt(rt, cp->db);
  js_free_rt(rt, cp->socket);
}

static void
connectparams_free(JSRuntime* rt, void* ptr) {
  MYSQLConnectParameters* cp = ptr;

  if(--cp->ref_count == 0) {
    connectparams_release(rt, cp);
    js_free_rt(rt, cp);
  }
}

static void
js_connectparams_finalizer(JSRuntime* rt, JSValue val) {
  MYSQLConnectParameters* cp;

  if((cp = js_connectparams_data(val)))
    connectparams_free(rt, cp);
}

static JSClassDef js_connectparams_class = {
    .class_name = "JSConnectParams",
    .finalizer = js_connectparams_finalizer,
};

static JSValue
js_connectparams_from(JSContext* ctx, int argc, JSValueConst argv[]) {
  JSValue obj;
  MYSQLConnectParameters* cp;

  if(js_connectparams_class_id == 0) {
    JS_NewClassID(&js_connectparams_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_connectparams_class_id, &js_connectparams_class);
  }

  obj = JS_NewObjectClass(ctx, js_connectparams_class_id);

  if(JS_IsException(obj))
    return JS_EXCEPTION;

  if((cp = connectparams_new(ctx, argc, argv)))
    JS_SetOpaque(obj, cp);

  return obj;
}

static BOOL
mysql_nonblock(MYSQL* my) {
  my_bool val;
  mysql_get_option(my, MYSQL_OPT_NONBLOCK, &val);
  return val;
}

MYSQL*
js_mysql_data(JSValueConst value) {
  return JS_GetOpaque(value, js_mysql_class_id);
}

MYSQL*
js_mysql_data2(JSContext* ctx, JSValueConst value) {
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

enum {
  METHOD_ESCAPE_STRING,
  METHOD_GET_OPTION,
  METHOD_SET_OPTION,
};

static JSValue
js_mysql_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  MYSQL* my = 0;
  JSValue ret = JS_UNDEFINED;

  if(!(my = js_mysql_data2(ctx, this_val)))
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
  PROP_FIELD_COUNT,
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
  PROP_CLIENT_INFO,
  PROP_CLIENT_VERSION,
  PROP_THREAD_SAFE,
  PROP_EOF,
  PROP_NUM_ROWS,
  PROP_NUM_FIELDS,
  PROP_CURRENT_FIELD,
  PROP_STATUS,
  PROP_PENDING,
  PROP_SOCKET,
};

static JSValue
js_mysql_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MYSQL* my;
  JSValue ret = JS_UNDEFINED;

  if(!(my = JS_GetOpaque(this_val, js_mysql_class_id)))
    return JS_UNDEFINED;

  switch(magic) {
    case PROP_MORE_RESULTS: {
      ret = JS_NewBool(ctx, mysql_more_results(my));
      break;
    }
    case PROP_AFFECTED_ROWS: {
      my_ulonglong affected;

      if((signed)(affected = mysql_affected_rows(my)) != -1ll)
        ret = JS_NewInt64(ctx, affected);

      break;
    }
    case PROP_WARNING_COUNT: {
      ret = JS_NewUint32(ctx, mysql_warning_count(my));
      break;
    }
    case PROP_FIELD_COUNT: {
      ret = JS_NewUint32(ctx, mysql_field_count(my));
      break;
    }
    case PROP_SOCKET: {
      ret = JS_NewInt64(ctx, mysql_get_socket(my));
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
    case PROP_STATUS: {
      ret = JS_NewInt32(ctx, my->status);
      break;
    }
    case PROP_PENDING: {
      AsyncClosure* ac;
      int32_t pending = 0;

      if((ac = asyncclosure_lookup(mysql_get_socket(my))))
        pending = to_mysql_wait(ac->state);

      ret = JS_NewInt32(ctx, pending);
      break;
    }
  }

  return ret;
}

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
  DynBuf buf;
  dbuf_init2(&buf, 0, 0);

  for(int i = 0; i < argc; i++) {
    if(i > 0)
      dbuf_putstr(&buf, ", ");
    js_mysql_print_value(ctx, &buf, argv[i]);
  }

  ret = JS_NewStringLen(ctx, (const char*)buf.buf, buf.size);
  dbuf_free(&buf);
  return ret;
}

static JSValue
js_mysql_values_string(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  DynBuf buf;

  dbuf_init2(&buf, 0, 0);

  for(int i = 0; i < argc; i++) {
    if(i > 0)
      dbuf_putstr(&buf, ", ");

    js_mysql_print_values(ctx, &buf, argv[i]);
  }

  ret = JS_NewStringLen(ctx, (const char*)buf.buf, buf.size);
  dbuf_free(&buf);
  return ret;
}

static JSValue
js_mysql_insert_query(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  DynBuf buf;
  const char* tbl;
  size_t tbl_len;

  tbl = JS_ToCStringLen(ctx, &tbl_len, argv[0]);

  dbuf_init2(&buf, 0, 0);
  js_mysql_print_insert(ctx, &buf);
  dbuf_put(&buf, (const uint8_t*)tbl, tbl_len);
  dbuf_putstr(&buf, " ");

  for(int i = 1; i < argc; i++) {
    if(i > 1)
      dbuf_putstr(&buf, ", ");

    js_mysql_print_values(ctx, &buf, argv[i]);
  }

  dbuf_putstr(&buf, ";");

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

  if((my = mysql_init(NULL)))
    mysql_options(my, MYSQL_OPT_NONBLOCK, 0);

  obj = js_mysql_new(ctx, proto, my);

  if(argc > 0) {
    JSValue params = js_connectparams_from(ctx, argc, argv);
    JSAtom prop = js_symbol_for_atom(ctx, "MYSQLConnectParameters");
    JS_DefinePropertyValue(ctx, obj, prop, params, JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, prop);
  }

  JS_FreeValue(ctx, proto);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static int
js_mysql_fd(JSContext* ctx, JSValueConst this_val) {
  MYSQL* my = js_mysql_data(this_val);

#ifdef _WIN32
  int fd;
  SOCKET s, sock = mysql_get_socket(my);
  BOOL has_fd = js_has_propertystr(ctx, this_val, fd);

  for(;;) {
    if(has_fd) {
      fd = js_get_propertystr_int32(ctx, this_val, "fd");
      s = _get_osfhandle(fd);

      if(s != sock) {
        printf("WARNING: filedescriptor %d is socket handle %p, but the MySQL socket is %p\n", fd, s, sock);
        js_delete_propertystr(ctx, this_val, "fd");
        has_fd = FALSE;
        continue
      }
    }
  }
  else {
    sock = mysql_get_socket(my);
    fd = sock != INVALID_HANDLE_VALUE ? _open_osfhandle(sock, _O_BINARY | _O_RDWR) : -1;
#ifdef DEBUG_OUTPUT
    printf("filedescriptor %d created from socket handle %p\n", fd, sock);
#endif
    js_set_propertystr_int(ctx, this_val, "fd", fd);
  }
  return fd;
}
#else
  return mysql_get_socket(my);
#endif
}

static JSValue
js_mysql_connect_continue(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  AsyncClosure* ac = ptr;
  MYSQL *my = js_mysql_data(ac->result), *ret = 0;
  int state = mysql_real_connect_cont(&ret, my, to_mysql_wait(ac->state));

  asyncclosure_change_event(ac, to_asyncevent(state));

  if(state == 0)
    asyncclosure_resolve(ac);

  return JS_UNDEFINED;
}

static JSValue
js_mysql_connect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  AsyncClosure* ac;
  MYSQL *my, *ret = 0;
  MYSQLConnectParameters* c;
  int state;
  JSAtom prop;

  if(!(my = js_mysql_data2(ctx, this_val)))
    return JS_EXCEPTION;

  prop = js_symbol_for_atom(ctx, "MYSQLConnectParameters");

  /* when obj[Symbol.for('MYSQLConnectParameters')] is present connect parameters were supplied to the constructor */
  if(JS_HasProperty(ctx, this_val, prop)) {
    JSValue obj = JS_GetProperty(ctx, this_val, prop);
    c = connectparams_dup(JS_GetOpaque(obj, js_connectparams_class_id));
    JS_FreeValue(ctx, obj);
  } else {
    c = connectparams_new(ctx, argc, argv);
  }

  state = mysql_real_connect_start(&ret, my, c->host, c->user, c->password, c->db, c->port, c->socket, c->flags);
  ac = asyncclosure_new(ctx, js_mysql_fd(ctx, this_val), to_asyncevent(state), this_val, &js_mysql_connect_continue);

  asyncclosure_opaque(ac, c, connectparams_free);

  return asyncclosure_promise(ac);
}

static JSValue
js_mysql_query_continue(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  AsyncClosure* ac = ptr;
  int err = 0, state;

  state = mysql_real_query_cont(&err, ac->opaque, to_mysql_wait(ac->state));

  asyncclosure_change_event(ac, to_asyncevent(state));

  if(state == 0) {
    MYSQL_RES* res;

    if(err) {
      JSValue error = js_mysqlerror_new(ctx, mysql_error(ac->opaque));
      asyncclosure_error(ac, error);
      JS_FreeValue(ctx, error);
    } else if((res = mysql_use_result(ac->opaque))) {
      JS_SetOpaque(ac->result, res);
      asyncclosure_resolve(ac);
    }
  }

  return JS_UNDEFINED;
}

static JSValue
js_mysql_query(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  AsyncClosure* ac;
  const char* query = 0;
  size_t i;
  MYSQL* my;
  int wantwrite, state, err = 0, fd;

  if(!(my = js_mysql_data2(ctx, this_val)))
    return JS_EXCEPTION;

  query = JS_ToCStringLen(ctx, &i, argv[0]);
  state = mysql_real_query_start(&err, my, query, i);
  ac = asyncclosure_new(ctx, js_mysql_fd(ctx, this_val), to_asyncevent(state), JS_NewObjectProtoClass(ctx, mysqlresult_proto, js_mysqlresult_class_id), &js_mysql_query_continue);

#ifdef DEBUG_OUTPUT
  printf("%s state=%d err=%d query='%.*s'\n", __func__, state, err, (int)i, query);
#endif

  asyncclosure_opaque(ac, my, NULL);

  return asyncclosure_promise(ac);
}

static JSValue
js_mysql_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  MYSQL* my;
  AsyncClosure* ac;
  int fd = -1;

  if(!(my = js_mysql_data2(ctx, this_val)))
    return JS_EXCEPTION;

#ifdef _WIN32
  if(js_has_propertystr(ctx, this_val, "fd")) {
    fd = js_get_propertystr_int32(ctx, this_val, "fd");
    _close(fd);
  }
#else
  fd = mysql_get_socket(my);
#endif

  if(fd > -1)
    if((ac = asyncclosure_lookup(fd)))
      asyncclosure_done(ac);

  mysql_close(my);

  JS_SetOpaque(this_val, 0);

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
    JS_CGETSET_MAGIC_DEF("fieldCount", js_mysql_get, 0, PROP_FIELD_COUNT),
#ifndef _WIN32
    JS_CGETSET_MAGIC_DEF("fd", js_mysql_get, 0, PROP_SOCKET),
#endif
    JS_CGETSET_MAGIC_DEF("socket", js_mysql_get, 0, PROP_SOCKET),
    JS_CGETSET_MAGIC_DEF("errno", js_mysql_get, 0, PROP_ERRNO),
    JS_CGETSET_MAGIC_DEF("error", js_mysql_get, 0, PROP_ERROR),
    JS_CGETSET_MAGIC_DEF("info", js_mysql_get, 0, PROP_INFO),
    JS_CGETSET_MAGIC_DEF("insertId", js_mysql_get, 0, PROP_INSERT_ID),
    JS_CGETSET_MAGIC_DEF("charset", js_mysql_get, 0, PROP_CHARSET),
    JS_CGETSET_MAGIC_DEF("timeout", js_mysql_get, 0, PROP_TIMEOUT),
    JS_CGETSET_MAGIC_DEF("timeoutMs", js_mysql_get, 0, PROP_TIMEOUT_MS),
    JS_CGETSET_MAGIC_DEF("serverName", js_mysql_get, 0, PROP_SERVER_NAME),
    JS_CGETSET_MAGIC_DEF("serverInfo", js_mysql_get, 0, PROP_SERVER_INFO),
    JS_CGETSET_MAGIC_DEF("serverVersion", js_mysql_get, 0, PROP_SERVER_VERSION),
    JS_CGETSET_MAGIC_DEF("user", js_mysql_get, 0, PROP_USER),
    JS_CGETSET_MAGIC_DEF("password", js_mysql_get, 0, PROP_PASSWORD),
    JS_CGETSET_MAGIC_DEF("host", js_mysql_get, 0, PROP_HOST),
    JS_CGETSET_MAGIC_DEF("port", js_mysql_get, 0, PROP_PORT),
    JS_CGETSET_MAGIC_DEF("socket", js_mysql_get, 0, PROP_UNIX_SOCKET),
    JS_CGETSET_MAGIC_DEF("db", js_mysql_get, 0, PROP_DB),
    JS_CGETSET_MAGIC_DEF("status", js_mysql_get, 0, PROP_STATUS),
    JS_CGETSET_MAGIC_DEF("pending", js_mysql_get, 0, PROP_PENDING),
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
    JS_CFUNC_DEF("valueString", 0, js_mysql_value_string),
    JS_CFUNC_DEF("valuesString", 1, js_mysql_values_string),
    JS_CFUNC_DEF("insertQuery", 2, js_mysql_insert_query),
};

static const JSCFunctionListEntry js_mysql_defines[] = {
    JS_PROP_INT32_DEF("RESULT_OBJECT", RESULT_OBJECT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("RESULT_STRING", RESULT_STRING, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("RESULT_TBLNAM", RESULT_TBLNAM, JS_PROP_CONFIGURABLE),
    JS_PROP_INT64_DEF("COUNT_ERROR", MYSQL_COUNT_ERROR, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DATABASE_DRIVER", MYSQL_DATABASE_DRIVER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DEFAULT_AUTH", MYSQL_DEFAULT_AUTH, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ENABLE_CLEARTEXT_PLUGIN", MYSQL_ENABLE_CLEARTEXT_PLUGIN, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("INIT_COMMAND", MYSQL_INIT_COMMAND, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATUS_READY", MYSQL_STATUS_READY, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATUS_GET_RESULT", MYSQL_STATUS_GET_RESULT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATUS_USE_RESULT", MYSQL_STATUS_USE_RESULT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATUS_QUERY_SENT", MYSQL_STATUS_QUERY_SENT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATUS_SENDING_LOAD_DATA", MYSQL_STATUS_SENDING_LOAD_DATA, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATUS_FETCHING_DATA", MYSQL_STATUS_FETCHING_DATA, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATUS_NEXT_RESULT_PENDING", MYSQL_STATUS_NEXT_RESULT_PENDING, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATUS_QUIT_SENT", MYSQL_STATUS_QUIT_SENT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATUS_STMT_RESULT", MYSQL_STATUS_STMT_RESULT, JS_PROP_CONFIGURABLE),
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
js_mysqlerror_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  JSAtom prop;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, mysqlerror_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_mysqlerror_class_id);
  JS_FreeValue(ctx, proto);

  if(argc > 0) {
    prop = JS_NewAtom(ctx, "message");
    JS_DeleteProperty(ctx, obj, prop, 0);
    JS_DefinePropertyValue(ctx, obj, prop, JS_DupValue(ctx, argv[0]), JS_PROP_C_W_E);
    JS_FreeAtom(ctx, prop);
  }

  if(argc > 1) {
    prop = JS_NewAtom(ctx, "type");
    JS_DeleteProperty(ctx, obj, prop, 0);
    JS_DefinePropertyValue(ctx, obj, prop, JS_DupValue(ctx, argv[1]), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, prop);
  }

  JSValue stack = js_error_stack(ctx);
  prop = JS_NewAtom(ctx, "stack");
  JS_DeleteProperty(ctx, obj, prop, 0);
  JS_DefinePropertyValue(ctx, obj, prop, stack, JS_PROP_CONFIGURABLE);
  JS_FreeAtom(ctx, prop);

  return obj;
}

static JSValue
js_mysqlerror_new(JSContext* ctx, const char* msg) {
  JSValue ret, obj;
  JSValue argv[1];

  obj = JS_NewObjectProtoClass(ctx, mysqlerror_proto, js_mysqlerror_class_id);
  if(JS_IsException(obj))
    return JS_EXCEPTION;

  argv[0] = JS_NewString(ctx, msg);
  obj = js_mysqlerror_constructor(ctx, mysqlerror_ctor, countof(argv), argv);

  for(size_t i = 0; i < countof(argv); i++)
    JS_FreeValue(ctx, argv[i]);

  return obj;
}

static JSClassDef js_mysqlerror_class = {
    .class_name = "MySQLError",
};

static const JSCFunctionListEntry js_mysqlerror_funcs[] = {
    JS_PROP_STRING_DEF("name", "MySQLError", JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("type", 0, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MySQLError", JS_PROP_CONFIGURABLE),
};

static JSValue
result_value(JSContext* ctx, MYSQL_FIELD const* field, char* buf, size_t len, ResultFlags rtype) {
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
result_array(JSContext* ctx, MYSQL_RES* res, MYSQL_ROW row, ResultFlags rtype) {
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
result_object(JSContext* ctx, MYSQL_RES* res, MYSQL_ROW row, ResultFlags rtype) {
  JSValue ret = JS_NewObject(ctx);
  uint32_t i, num_fields = mysql_num_fields(res);
  MYSQL_FIELD* fields = mysql_fetch_fields(res);
  FieldNameFunc* fn = (rtype & RESULT_TBLNAM) ? field_id : field_namefunc(fields, num_fields);
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
result_row(JSContext* ctx, MYSQL_RES* res, MYSQL_ROW row, ResultFlags rtype) {
  JSValue val;
  RowValueFunc* row_func = (rtype & RESULT_OBJECT) ? result_object : result_array;

  val = row ? row_func(ctx, res, row, rtype) : JS_NULL;

  return val;
}

static JSValue
result_iterate(JSContext* ctx, MYSQL_RES* res, MYSQL_ROW row, ResultFlags rtype) {
  JSValue ret, val = result_row(ctx, res, row, rtype);
  ret = js_iterator_result(ctx, val, row ? FALSE : TRUE);
  JS_FreeValue(ctx, val);
  return ret;
}

static void
result_yield(JSContext* ctx, JSValueConst func, MYSQL_RES* res, MYSQL_ROW row, ResultFlags rtype) {
  JSValue val = result_row(ctx, res, row, rtype);

  JSValue item = js_iterator_result(ctx, val, row ? FALSE : TRUE);
  JS_FreeValue(ctx, val);

  value_yield_free(ctx, func, item);
}

static void
result_resolve(JSContext* ctx, JSValueConst func, MYSQL_RES* res, MYSQL_ROW row, ResultFlags rtype) {
  JSValue value = row ? result_row(ctx, res, row, rtype) : JS_NULL;

  value_yield_free(ctx, func, value);
}

typedef struct PACK {
  ResultFlags flags;
  MYSQL* conn;
  MYSQL_RES* res;
  uint32_t field_count;
  uint64_t num_rows;
} ResultIterator;
ENDPACK

static ResultIterator*
result_iterator_new(JSContext* ctx, MYSQL* my, MYSQL_RES* res, ResultFlags flags) {
  ResultIterator* ri;

  if(!(ri = js_mallocz(ctx, sizeof(ResultIterator))))
    return 0;

  ri->flags = flags;
  ri->conn = my;
  ri->res = res;
  ri->field_count = mysql_field_count(my);
  ri->num_rows = mysql_num_rows(res);

  return ri;
}

static void
result_iterator_value(ResultIterator* ri, MYSQL_ROW row, AsyncClosure* ac) {
  JSValue result = JS_UNDEFINED;
  JSContext* ctx = ac->ctx;

  if(row)
    if(mysql_num_fields(ri->res) == ri->field_count)
      result = result_row(ctx, ri->res, row, ri->flags);

  if(ri->flags & RESULT_ITERAT) {
    JSValue tmp = js_iterator_result(ctx, result, JS_IsUndefined(result));
    JS_FreeValue(ctx, result);
    result = tmp;
  }

  asyncclosure_yield(ac, result);
  JS_FreeValue(ctx, result);
}

MYSQL_RES*
js_mysqlresult_data2(JSContext* ctx, JSValueConst value) {
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

static JSValue
js_mysqlresult_next_continue(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  AsyncClosure* ac = ptr;
  ResultIterator* ri = ac->opaque;
  MYSQL_RES* res = ri->res;
  MYSQL_ROW row;
  int state;

  state = mysql_fetch_row_cont(&row, res, to_mysql_wait(ac->state));

  asyncclosure_change_event(ac, to_asyncevent(state));

  if(state == 0) {

    if(row) {
      if(mysql_num_fields(res) == ri->field_count) {
        ac->result = result_row(ctx, res, row, ri->flags);

        asyncclosure_resolve(ac);
      }

    } else {
      JSValue error = js_mysqlerror_new(ctx, mysql_error(ri->conn));
      asyncclosure_error(ac, error);
      JS_FreeValue(ctx, error);
    }
  }

  return JS_UNDEFINED;
}

static JSValue
js_mysqlresult_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  MYSQL_RES* res;

  if(!(res = js_mysqlresult_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!mysql_eof(res)) {
    MYSQL_ROW row;
    MYSQL* my = js_mysqlresult_handle(ctx, this_val);
    int state = mysql_fetch_row_start(&row, res);
    AsyncClosure* ac = asyncclosure_new(ctx, js_mysql_fd(ctx, this_val), to_asyncevent(state), JS_NULL, &js_mysqlresult_next_continue);

#ifdef DEBUG_OUTPUT
    printf("%s state=%d\n", __func__, state);
#endif

    asyncclosure_opaque(ac, result_iterator_new(ctx, my, res, magic), &js_free_rt);

    if(state == 0)
      result_iterator_value(ac->opaque, row, ac);

    return asyncclosure_promise(ac);
  }

  return JS_ThrowRangeError(ctx, "MySQLResult is EOF");
}

enum {
  METHOD_FETCH_FIELD,
  METHOD_FETCH_FIELDS,
};

static JSValue
js_mysqlresult_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  MYSQL_RES* res;

  if(!(res = js_mysqlresult_data2(ctx, this_val)))
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

        for(i = 0; i < num_fields; i++)
          JS_SetPropertyUint32(ctx, ret, i, field_array(ctx, &fields[i]));
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
js_mysqlresult_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MYSQL_RES* res;
  JSValue ret = JS_UNDEFINED;

  if(!(res = js_mysqlresult_data2(ctx, this_val)))
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
    JS_CFUNC_MAGIC_DEF("next", 0, js_mysqlresult_next, RESULT_ITERAT),
    JS_CGETSET_MAGIC_DEF("eof", js_mysqlresult_get, 0, PROP_EOF),
    JS_CGETSET_MAGIC_FLAGS_DEF("numRows", js_mysqlresult_get, 0, PROP_NUM_ROWS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("numFields", js_mysqlresult_get, 0, PROP_NUM_FIELDS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("fieldCount", js_mysqlresult_get, 0, PROP_FIELD_COUNT),
    JS_CGETSET_MAGIC_DEF("currentField", js_mysqlresult_get, 0, PROP_CURRENT_FIELD),
    JS_CFUNC_MAGIC_DEF("fetchField", 1, js_mysqlresult_functions, METHOD_FETCH_FIELD),
    JS_CFUNC_MAGIC_DEF("fetchFields", 0, js_mysqlresult_functions, METHOD_FETCH_FIELDS),
    JS_CFUNC_MAGIC_DEF("fetchRow", 0, js_mysqlresult_next, 0),
    JS_CFUNC_MAGIC_DEF("[Symbol.iterator]", 0, js_mysqlresult_iterator, METHOD_ITERATOR),
    JS_CFUNC_MAGIC_DEF("[Symbol.asyncIterator]", 0, js_mysqlresult_iterator, METHOD_ASYNC_ITERATOR),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MySQLResult", JS_PROP_CONFIGURABLE),
};

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

static FieldNameFunc*
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

    JSValue error = JS_NewError(ctx);
    JSValue error_proto = JS_GetPrototype(ctx, error);
    JS_FreeValue(ctx, error);

    JS_NewClassID(&js_mysqlerror_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_mysqlerror_class_id, &js_mysqlerror_class);

    mysqlerror_ctor = JS_NewCFunction2(ctx, js_mysqlerror_constructor, "MySQLError", 1, JS_CFUNC_constructor, 0);
    mysqlerror_proto = JS_NewObjectProto(ctx, error_proto);
    JS_FreeValue(ctx, error_proto);

    JS_SetPropertyFunctionList(ctx, mysqlerror_proto, js_mysqlerror_funcs, countof(js_mysqlerror_funcs));

    JS_SetClassProto(ctx, js_mysqlerror_class_id, mysqlerror_proto);

    JS_NewClassID(&js_mysqlresult_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_mysqlresult_class_id, &js_mysqlresult_class);

    mysqlresult_ctor = JS_NewCFunction2(ctx, js_mysqlresult_constructor, "MySQLResult", 1, JS_CFUNC_constructor, 0);
    mysqlresult_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, mysqlresult_proto, js_mysqlresult_funcs, countof(js_mysqlresult_funcs));
    JS_SetClassProto(ctx, js_mysqlresult_class_id, mysqlresult_proto);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "MySQL", mysql_ctor);
    JS_SetModuleExport(ctx, m, "MySQLError", mysqlerror_ctor);
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
  JS_AddModuleExport(ctx, m, "MySQLError");
  JS_AddModuleExport(ctx, m, "MySQLResult");
  return m;
}

/**
 * @}
 */
