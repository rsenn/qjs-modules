#include "defines.h"
#include "quickjs-pgsql.h"
#include "utils.h"
#include "buffer-utils.h"
#include "char-utils.h"
#include "js-utils.h"

/**
 * \addtogroup quickjs-pgsql
 * @{
 */

thread_local VISIBLE JSClassID js_pgsqlerror_class_id = 0, js_pgconn_class_id = 0, js_pgresult_class_id = 0;
thread_local JSValue pgsqlerror_proto = {{JS_TAG_UNDEFINED}}, pgsqlerror_ctor = {{JS_TAG_UNDEFINED}}, pgsql_proto = {{JS_TAG_UNDEFINED}},
                     pgsql_ctor = {{JS_TAG_UNDEFINED}}, pgresult_proto = {{JS_TAG_UNDEFINED}}, pgresult_ctor = {{JS_TAG_UNDEFINED}};

static JSValue js_pgresult_wrap(JSContext* ctx, PGresult* res);

enum ResultFlags {
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

struct PGsqlConn {
  PGconn* conn;
  PGresult* result;
};

static JSValue js_pgsqlerror_new(JSContext* ctx, const char* msg);

static void
js_pgconn_print_value(JSContext* ctx, struct PGsqlConn* pq, DynBuf* out, JSValueConst value) {

  if(JS_IsNull(value) || JS_IsUndefined(value) || js_is_nan(value)) {
    dbuf_putstr(out, "NULL");

  } else if(JS_IsBool(value)) {
    dbuf_putstr(out, JS_ToBool(ctx, value) ? "TRUE" : "FALSE");

  } else if(JS_IsString(value)) {
    size_t len;
    const char* src = JS_ToCStringLen(ctx, &len, value);
    char* dst;
    int err = 0;

    dbuf_putc(out, '\'');
    dst = (char*)dbuf_reserve(out, len * 2 + 1);
    len = PQescapeStringConn(pq->conn, dst, src, len, &err);
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

    js_pgconn_print_value(ctx, pq, out, val);
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

static void
js_pgconn_print_fields(JSContext* ctx, DynBuf* out, JSPropertyEnum* tmp_tab, uint32_t tmp_len) {
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
js_pgconn_print_insert(JSContext* ctx, DynBuf* out) {
  dbuf_putstr(out, "INSERT INTO ");
}

static void
js_pgconn_print_values(JSContext* ctx, struct PGsqlConn* pq, DynBuf* out, JSValueConst values) {
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
      js_pgconn_print_value(ctx, pq, out, item);
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
    js_pgconn_print_fields(ctx, out, tmp_tab, tmp_len);
    dbuf_putc(out, ')');

    dbuf_putstr(out, " VALUES (");
    for(uint32_t i = 0; i < tmp_len; i++) {
      if(i > 0)
        dbuf_putstr(out, ", ");
      item = JS_GetProperty(ctx, values, tmp_tab[i].atom);
      js_pgconn_print_value(ctx, pq, out, item);
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
pgsql_nonblock(struct PGsqlConn* pq) {
  BOOL val;
  val = PQisnonblocking(pq);
  return val;
}

struct PGsqlConn*
js_pgconn_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_pgconn_class_id);
}

static JSValue
js_pgconn_new(JSContext* ctx, JSValueConst proto, struct PGsqlConn* pq) {
  JSValue obj;

  if(js_pgconn_class_id == 0)
    js_pgconn_init(ctx, 0);

  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = JS_DupValue(ctx, pgsql_proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_pgconn_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, pq);

  return obj;
fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_pgconn_wrap(JSContext* ctx, struct PGsqlConn* pq) {
  return js_pgconn_new(ctx, pgsql_proto, pq);
}

static inline int
js_pgconn_rtype(JSContext* ctx, JSValueConst value) {
  return js_get_propertystr_int32(ctx, value, "resultType");
}

enum {
  METHOD_ESCAPE_STRING,
  METHOD_GET_OPTION,
  METHOD_SET_OPTION,
};

static JSValue
js_pgconn_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  struct PGsqlConn* pq = 0;
  JSValue ret = JS_UNDEFINED;

  if(!(pq = js_pgconn_data(ctx, this_val)))
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

      len = pgsql_real_escape_string(pq, dst, src, len);
      ret = JS_NewStringLen(ctx, dst, len);
      js_free(ctx, dst);
      break;
    }

    case METHOD_GET_OPTION: {

      break;
    }

    case METHOD_SET_OPTION: {

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

  PROP_CLIENT_INFO,
  PROP_CLIENT_VERSION,
  PROP_THREAD_SAFE,
  PROP_EOF,
  PROP_NUM_ROWS,
  PROP_NUM_FIELDS,
  PROP_CURRENT_FIELD,
};

static JSValue
js_pgconn_get(JSContext* ctx, JSValueConst this_val, int magic) {
  struct PGsqlConn* pq;
  JSValue ret = JS_UNDEFINED;

  if(!(pq = JS_GetOpaque(this_val, js_pgconn_class_id)))
    return JS_UNDEFINED;

  switch(magic) {
    case PROP_MORE_RESULTS: {
      ret = JS_NewBool(ctx, pgsql_more_results(pq));
      break;
    }
    case PROP_AFFECTED_ROWS: {
      /*   my_ulonglong affected;

         if((signed)(affected = pgsql_affected_rows(pq)) != -1ll)
           ret = JS_NewInt64(ctx, affected);*/

      break;
    }
    case PROP_WARNING_COUNT: {
      ret = JS_NewUint32(ctx, pgsql_warning_count(pq));
      break;
    }
    case PROP_FIELD_COUNT: {
      ret = JS_NewUint32(ctx, pgsql_field_count(pq));
      break;
    }
    case PROP_FD: {
      ret = JS_NewInt32(ctx, PQsocket(pq));
      break;
    }
    case PROP_ERRNO: {
      ret = JS_NewInt32(ctx, pgsql_errno(pq));
      break;
    }
    case PROP_ERROR: {
      const char* error = pgsql_info(pq);
      ret = error && *error ? JS_NewString(ctx, error) : JS_NULL;
      break;
    }
    case PROP_INFO: {
      const char* info = pgsql_info(pq);
      ret = info && *info ? JS_NewString(ctx, info) : JS_NULL;
      break;
    }
    case PROP_INSERT_ID: {
      ret = JS_NewInt64(ctx, pgsql_insert_id(pq));
      break;
    }
    case PROP_CHARSET: {
      const char* charset = pgsql_character_set_name(pq);
      ret = charset && *charset ? JS_NewString(ctx, charset) : JS_NULL;
      break;
    }
    case PROP_TIMEOUT: {
      ret = JS_NewUint32(ctx, pgsql_get_timeout_value(pq));
      break;
    }
    case PROP_TIMEOUT_MS: {
      ret = JS_NewUint32(ctx, pgsql_get_timeout_value_ms(pq));
      break;
    }
    case PROP_SERVER_NAME: {
      const char* name = pgsql_get_server_name(pq);
      ret = name && *name ? JS_NewString(ctx, name) : JS_NULL;
      break;
    }
    case PROP_SERVER_INFO: {
      const char* info = pgsql_get_server_info(pq);
      ret = info && *info ? JS_NewString(ctx, info) : JS_NULL;
      break;
    }
    case PROP_SERVER_VERSION: {
      ret = JS_NewUint32(ctx, pgsql_get_server_version(pq));
      break;
    }
    case PROP_USER: {
      char* user = PQuser(pq);
      ret = user ? JS_NewString(ctx, user) : JS_NULL;
      break;
    }
    case PROP_PASSWORD: {
      char* pass = PQpass(pq);
      ret = pass ? JS_NewString(ctx, pass) : JS_NULL;
      break;
    }
    case PROP_HOST: {
      char* host = PQhost(pq);
      ret = host ? JS_NewString(ctx, host) : JS_NULL;

      break;
    }
    case PROP_PORT: {
      char* port = PQport(pq);
      ret = port ? JS_NewString(ctx, port) : JS_NULL;

      break;
    }
    case PROP_DB: {
      char* db = PQdb(pq);
      ret = db ? JS_NewString(ctx, db) : JS_NULL;
      break;
    }
  }

  return ret;
}

static JSValue
js_pgconn_getstatic(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case PROP_CLIENT_INFO: {
      const char* info = pgsql_get_client_info();
      ret = info && *info ? JS_NewString(ctx, info) : JS_NULL;
      break;
    }
    case PROP_CLIENT_VERSION: {
      ret = JS_NewUint32(ctx, pgsql_get_client_version());
      break;
    }
    case PROP_THREAD_SAFE: {
      ret = JS_NewBool(ctx, pgsql_thread_safe());
      break;
    }
  }

  return ret;
}

static JSValue
js_pgconn_value_string(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  struct PGsqlConn* pq;
  DynBuf buf;

  if(!(pq = js_pgconn_data(ctx, this_val)))
    return JS_EXCEPTION;

  dbuf_init2(&buf, 0, 0);

  for(int i = 0; i < argc; i++) {
    if(i > 0)
      dbuf_putstr(&buf, ", ");
    js_pgconn_print_value(ctx, pq, &buf, argv[i]);
  }

  ret = JS_NewStringLen(ctx, (const char*)buf.buf, buf.size);
  dbuf_free(&buf);
  return ret;
}

static JSValue
js_pgconn_values_string(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  struct PGsqlConn* pq;
  DynBuf buf;

  if(!(pq = js_pgconn_data(ctx, this_val)))
    return JS_EXCEPTION;

  dbuf_init2(&buf, 0, 0);

  for(int i = 0; i < argc; i++) {
    if(i > 0)
      dbuf_putstr(&buf, ", ");

    js_pgconn_print_values(ctx, pq, &buf, argv[i]);
  }

  ret = JS_NewStringLen(ctx, (const char*)buf.buf, buf.size);
  dbuf_free(&buf);
  return ret;
}

static JSValue
js_pgconn_insert_query(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  struct PGsqlConn* pq;
  DynBuf buf;
  const char* tbl;
  size_t tbl_len;

  if(!(pq = js_pgconn_data(ctx, this_val)))
    return JS_EXCEPTION;

  tbl = JS_ToCStringLen(ctx, &tbl_len, argv[0]);

  dbuf_init2(&buf, 0, 0);
  js_pgconn_print_insert(ctx, &buf);
  dbuf_put(&buf, (const uint8_t*)tbl, tbl_len);
  dbuf_putstr(&buf, " ");

  for(int i = 1; i < argc; i++) {
    if(i > 1)
      dbuf_putstr(&buf, ", ");

    js_pgconn_print_values(ctx, pq, &buf, argv[i]);
  }

  dbuf_putstr(&buf, ";");

  ret = JS_NewStringLen(ctx, (const char*)buf.buf, buf.size);
  dbuf_free(&buf);
  return ret;
}

static JSValue
js_pgconn_escape_string(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
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

  len = pgsql_escape_string(dst, src, len);
  ret = JS_NewStringLen(ctx, dst, len);
  js_free(ctx, dst);

  return ret;
}

static JSValue
js_pgconn_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  struct PGsqlConn* pq;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if((pq = pgsql_init(NULL))) {
    // pgsql_options(pq, PGSQL_OPT_NONBLOCK, 0);
  }

  obj = js_pgconn_new(ctx, proto, pq);
  JS_FreeValue(ctx, proto);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_pgconn_connect_cont(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue data[]) {
  int32_t wantwrite, oldstate, newstate, fd;
  struct PGsqlConn *pq, *ret = 0;

  if(!(pq = js_pgconn_data(ctx, data[1])))
    return JS_EXCEPTION;

  JS_ToInt32(ctx, &wantwrite, data[0]);

  oldstate = wantwrite ? PGRES_POLLING_WRITING : PGRES_POLLING_READING;

  newstate = PQconnectPoll(pq);
  fd = PQsocket(pq);

  if(newstate == 0) {
    js_iohandler_set(ctx, data[2], fd, JS_NULL);
    int ret;

    if((ret = pgsql_set_character_set(pq, "utf8"))) {
      printf("failed setting PgSQL character set to UTF8: %s (%ii)", pgsql_error(pq), pgsql_errno(pq));
    }

    JS_Call(ctx, data[3], JS_UNDEFINED, 1, &data[1]);
  } else if(newstate != oldstate) {
    JSValue handler, hdata[5] = {
                         JS_NewInt32(ctx, wantwrite),
                         JS_DupValue(ctx, data[1]),
                         js_iohandler_fn(ctx, !!(newstate & PGRES_POLLING_WRITING)),
                         JS_DupValue(ctx, data[3]),
                         JS_DupValue(ctx, data[4]),
                     };
    handler = JS_NewCFunctionData(ctx, js_pgconn_connect_cont, 0, 0, countof(hdata), hdata);

    js_iohandler_set(ctx, data[2], fd, JS_NULL);
    js_iohandler_set(ctx, hdata[2], fd, handler);
    JS_FreeValue(ctx, handler);
  }

#ifdef DEBUG_OUTPUT
  printf("%s wantwrite=%i fd=%i newstate=%i pq=%p ret=%p error='%s'\n", __func__, wantwrite, fd, newstate, pq, ret, pgsql_error(pq));
#endif

  return JS_UNDEFINED;
}

static JSValue
js_pgconn_connect_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue promise = JS_UNDEFINED, data[5], handler;
  const char* conninfo;
  struct PGsqlConn *pq, *ret = 0;
  int32_t wantwrite, state, fd;

  if(!(pq = js_pgconn_data(ctx, this_val)))
    return JS_EXCEPTION;

  conninfo = JS_ToCString(ctx, argv[0]);

  pq->conn = PQconnectStart(conninfo);
  fd = PQsocket(pq->conn);

#ifdef DEBUG_OUTPUT
  printf("%s state=%d fd=%" PRId32 "\n", __func__, state, fd);
#endif

  wantwrite = !!(state & PGRES_POLLING_WRITING);

  promise = JS_NewPromiseCapability(ctx, &data[3]);

  data[0] = JS_NewInt32(ctx, wantwrite);
  data[1] = JS_DupValue(ctx, this_val);
  data[2] = js_iohandler_fn(ctx, wantwrite);

  handler = JS_NewCFunctionData(ctx, js_pgconn_connect_cont, 0, 0, countof(data), data);

  if(!js_iohandler_set(ctx, data[2], fd, handler))
    JS_Call(ctx, data[4], JS_UNDEFINED, 0, 0);

  JS_FreeCString(ctx, conninfo);

  return promise;
}

static JSValue
js_pgconn_connect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct PGsqlConn* pq;

  if(!(pq = js_pgconn_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(!pgsql_nonblock(pq)) {
    struct PGsqlConn* ret;
    struct ConnectParameters c = {0, 0, 0, 0, 0};

    connectparams_init(ctx, &c, argc, argv);

    ret = pgsql_real_connect(pq, c.host, c.user, c.password, c.db, c.port, c.socket, c.flags);

    connectparams_free(ctx, &c);

    return ret ? JS_DupValue(ctx, this_val) : JS_NULL;
  }

  return js_pgconn_connect_start(ctx, this_val, argc, argv);
}

static JSValue
js_pgconn_query_cont(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue data[]) {
  int32_t wantwrite, oldstate, newstate, fd;
  int ret = 0;
  struct PGsqlConn* pq = 0;

  if(!(pq = js_pgconn_data(ctx, data[1])))
    return JS_EXCEPTION;

  fd = PQsocket(pq);
  JS_ToInt32(ctx, &wantwrite, data[0]);

  oldstate = wantwrite ? PGRES_POLLING_WRITING : PGRES_POLLING_READING;

  newstate = pgsql_real_query_cont(&ret, pq, oldstate);

  if(newstate == 0) {
    PGresult* res = pgsql_use_result(pq);
    JSValue res_val = res ? js_pgresult_wrap(ctx, res) : JS_NULL;

    js_iohandler_set(ctx, data[2], fd, JS_NULL);

    if(pgsql_errno(pq)) {
      JSValue err = js_pgsqlerror_new(ctx, pgsql_error(pq));
      JS_Call(ctx, data[4], JS_UNDEFINED, 1, &err);
      JS_FreeValue(ctx, err);
    } else /*if(res) */ {
      if(res)
        JS_DefinePropertyValueStr(ctx, res_val, "handle", JS_DupValue(ctx, data[1]), JS_PROP_CONFIGURABLE);

      /*JSValue res_type = JS_GetPropertyStr(ctx, data[1], "resultType");

         if(!JS_IsUndefined(res_type) && !JS_IsException(res_type))
           JS_SetPropertyStr(ctx, res_val, "resultType", res_type);*/
      JS_Call(ctx, data[3], JS_UNDEFINED, 1, &res_val);
      JS_FreeValue(ctx, res_val);
    }

  } else if(newstate != oldstate) {
    JSValue handler, hdata[5] = {
                         JS_NewInt32(ctx, wantwrite),
                         JS_DupValue(ctx, data[1]),
                         js_iohandler_fn(ctx, !!(newstate & PGRES_POLLING_WRITING)),
                         JS_DupValue(ctx, data[3]),
                         JS_DupValue(ctx, data[4]),
                     };
    handler = JS_NewCFunctionData(ctx, js_pgconn_query_cont, 0, 0, countof(hdata), hdata);

    js_iohandler_set(ctx, data[2], fd, JS_NULL);
    js_iohandler_set(ctx, hdata[2], fd, handler);
    JS_FreeValue(ctx, handler);
  }

#ifdef DEBUG_OUTPUT
  printf("%s wantwrite=%i fd=%i newstate=%i pq=%p ret=%d error='%s'\n", __func__, wantwrite, fd, newstate, pq, ret, pgsql_error(pq));
#endif

  return JS_UNDEFINED;
}

static JSValue
js_pgconn_query_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue promise = JS_UNDEFINED, data[5], handler;
  const char* query = 0;
  size_t query_len;
  struct PGsqlConn* pq;
  int wantwrite, state, ret = 0, fd;

  if(!(pq = js_pgconn_data(ctx, this_val)))
    return JS_EXCEPTION;

  query = JS_ToCStringLen(ctx, &query_len, argv[0]);
  state = pgsql_real_query_start(&ret, pq, query, query_len);
  fd = PQsocket(pq);

#ifdef DEBUG_OUTPUT
  printf("%s state=%d ret=%d query='%.*s'\n", __func__, state, ret, (int)query_len, query);
#endif

  wantwrite = !!(state & PGRES_POLLING_WRITING);

  promise = JS_NewPromiseCapability(ctx, &data[3]);

  /* if(state == 0) {
     PGresult* res = pgsql_use_result(pq);
     JSValue res_val = res ? js_pgresult_wrap(ctx, res) : JS_NULL;

     js_iohandler_set(ctx, data[2], fd, JS_NULL);

     if(pgsql_errno(pq)) {
       JSValue err = js_pgsqlerror_new(ctx, pgsql_error(pq));
       JS_Call(ctx, data[4], JS_UNDEFINED, 1, &err);
       JS_FreeValue(ctx, err);
     } else {
       if(res)
         JS_DefinePropertyValueStr(ctx, res_val, "handle", JS_DupValue(ctx, data[1]), JS_PROP_CONFIGURABLE);

       JS_Call(ctx, data[3], JS_UNDEFINED, 1, &res_val);
       JS_FreeValue(ctx, res_val);
     }

   } else*/
  {

    data[0] = JS_NewInt32(ctx, wantwrite);
    data[1] = JS_DupValue(ctx, this_val);
    data[2] = js_iohandler_fn(ctx, wantwrite);

    handler = JS_NewCFunctionData(ctx, js_pgconn_query_cont, 0, 0, countof(data), data);

    if(!js_iohandler_set(ctx, data[2], fd, handler))
      JS_Call(ctx, data[4], JS_UNDEFINED, 0, 0);
  }

  return promise;
}

static JSValue
js_pgconn_query(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct PGsqlConn* pq;

  if(!(pq = js_pgconn_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(!pgsql_nonblock(pq)) {
    PGresult* res = 0;
    const char* query = 0;
    size_t query_len;
    int state;
    JSValue ret = JS_UNDEFINED;

    query = JS_ToCStringLen(ctx, &query_len, argv[0]);
    state = pgsql_real_query(pq, query, query_len);

    if(state == 0) {
      res = pgsql_store_result(pq);
      ret = res ? js_pgresult_wrap(ctx, res) : JS_NULL;
    }

    if(res)
      JS_DefinePropertyValueStr(ctx, ret, "handle", JS_DupValue(ctx, this_val), JS_PROP_CONFIGURABLE);

    return ret;
  }

  return js_pgconn_query_start(ctx, this_val, argc, argv);
}

static JSValue
js_pgconn_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  struct PGsqlConn* pq;

  if(!(pq = js_pgconn_data(ctx, this_val)))
    return JS_EXCEPTION;

  pgsql_close(pq);

  JS_SetOpaque(this_val, 0);

  return ret;
}

static void
js_pgconn_finalizer(JSRuntime* rt, JSValue val) {
  struct PGsqlConn* pq;

  if((pq = JS_GetOpaque(val, js_pgconn_class_id))) {
    pgsql_close(pq);
  }
}

static JSClassDef js_pgconn_class = {
    .class_name = "PgSQL",
    .finalizer = js_pgconn_finalizer,
};

static const JSCFunctionListEntry js_pgconn_funcs[] = {
    JS_CGETSET_MAGIC_DEF("moreResults", js_pgconn_get, 0, PROP_MORE_RESULTS),
    JS_CGETSET_MAGIC_DEF("affectedRows", js_pgconn_get, 0, PROP_AFFECTED_ROWS),
    JS_CGETSET_MAGIC_DEF("warningCount", js_pgconn_get, 0, PROP_WARNING_COUNT),
    JS_CGETSET_MAGIC_DEF("fieldCount", js_pgconn_get, 0, PROP_FIELD_COUNT),
    JS_CGETSET_MAGIC_DEF("fd", js_pgconn_get, 0, PROP_FD),
    JS_CGETSET_MAGIC_DEF("errno", js_pgconn_get, 0, PROP_ERRNO),
    JS_CGETSET_MAGIC_DEF("error", js_pgconn_get, 0, PROP_ERROR),
    JS_CGETSET_MAGIC_DEF("info", js_pgconn_get, 0, PROP_INFO),
    JS_CGETSET_MAGIC_DEF("insertId", js_pgconn_get, 0, PROP_INSERT_ID),
    JS_CGETSET_MAGIC_DEF("charset", js_pgconn_get, 0, PROP_CHARSET),
    JS_CGETSET_MAGIC_DEF("timeout", js_pgconn_get, 0, PROP_TIMEOUT),
    JS_CGETSET_MAGIC_DEF("timeoutMs", js_pgconn_get, 0, PROP_TIMEOUT_MS),
    JS_CGETSET_MAGIC_DEF("serverName", js_pgconn_get, 0, PROP_SERVER_NAME),
    JS_CGETSET_MAGIC_DEF("serverInfo", js_pgconn_get, 0, PROP_SERVER_INFO),
    JS_CGETSET_MAGIC_DEF("user", js_pgconn_get, 0, PROP_USER),
    JS_CGETSET_MAGIC_DEF("password", js_pgconn_get, 0, PROP_PASSWORD),
    JS_CGETSET_MAGIC_DEF("host", js_pgconn_get, 0, PROP_HOST),
    JS_CGETSET_MAGIC_DEF("port", js_pgconn_get, 0, PROP_PORT),
    JS_CGETSET_MAGIC_DEF("db", js_pgconn_get, 0, PROP_DB),
    JS_CFUNC_DEF("connect", 1, js_pgconn_connect),
    JS_CFUNC_DEF("query", 1, js_pgconn_query),
    JS_CFUNC_DEF("close", 0, js_pgconn_close),
    JS_ALIAS_DEF("execute", "query"),
    JS_CFUNC_MAGIC_DEF("escapeString", 1, js_pgconn_methods, METHOD_ESCAPE_STRING),
    JS_CFUNC_MAGIC_DEF("getOption", 1, js_pgconn_methods, METHOD_GET_OPTION),
    JS_CFUNC_MAGIC_DEF("setOption", 2, js_pgconn_methods, METHOD_SET_OPTION),
    JS_PROP_INT32_DEF("resultType", 0, JS_PROP_C_W_E),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PgSQL", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_pgconn_static[] = {
    JS_CGETSET_MAGIC_DEF("clientInfo", js_pgconn_getstatic, 0, PROP_CLIENT_INFO),
    JS_CGETSET_MAGIC_DEF("clientVersion", js_pgconn_getstatic, 0, PROP_CLIENT_VERSION),
    JS_CGETSET_MAGIC_DEF("threadSafe", js_pgconn_getstatic, 0, PROP_THREAD_SAFE),
    JS_CFUNC_DEF("escapeString", 1, js_pgconn_escape_string),
    JS_CFUNC_DEF("valueString", 0, js_pgconn_value_string),
    JS_CFUNC_DEF("valuesString", 1, js_pgconn_values_string),
    JS_CFUNC_DEF("insertQuery", 2, js_pgconn_insert_query),
};

static const JSCFunctionListEntry js_pgconn_defines[] = {
    JS_PROP_INT32_DEF("RESULT_OBJECT", RESULT_OBJECT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("RESULT_STRING", RESULT_STRING, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("RESULT_TABLENAME", RESULT_TABLENAME, JS_PROP_CONFIGURABLE),
    /* JS_PROP_INT32_DEF("WAIT_READ", PGRES_POLLING_READING, JS_PROP_CONFIGURABLE),
     JS_PROP_INT32_DEF("WAIT_WRITE", PGRES_POLLING_WRITING, JS_PROP_CONFIGURABLE),
     JS_PROP_INT32_DEF("WAIT_EXCEPT", PGSQL_WAIT_EXCEPT, JS_PROP_CONFIGURABLE),
     JS_PROP_INT32_DEF("WAIT_TIMEOUT", PGSQL_WAIT_TIMEOUT, JS_PROP_CONFIGURABLE),*/

};

static JSValue
js_pgsqlerror_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  JSAtom prop;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, pgsqlerror_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_pgsqlerror_class_id);
  JS_FreeValue(ctx, proto);

  if(argc > 0) {
    prop = JS_NewAtom(ctx, "message");
    JS_DefinePropertyValue(ctx, obj, prop, JS_DupValue(ctx, argv[0]), JS_PROP_C_W_E);
    JS_FreeAtom(ctx, prop);
  }

  if(argc > 1) {
    prop = JS_NewAtom(ctx, "type");
    JS_DefinePropertyValue(ctx, obj, prop, JS_DupValue(ctx, argv[1]), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, prop);
  }

  JSValue stack = js_error_stack(ctx);
  prop = JS_NewAtom(ctx, "stack");
  JS_DefinePropertyValue(ctx, obj, prop, stack, JS_PROP_CONFIGURABLE);
  JS_FreeAtom(ctx, prop);

  return obj;
}

static JSValue
js_pgsqlerror_new(JSContext* ctx, const char* msg) {
  JSValue ret, obj;
  JSValue argv[2];

  obj = JS_NewObjectProtoClass(ctx, pgsqlerror_proto, js_pgsqlerror_class_id);
  if(JS_IsException(obj))
    return JS_EXCEPTION;

  argv[0] = JS_NewString(ctx, msg);
  obj = js_pgsqlerror_constructor(ctx, pgsqlerror_ctor, 1, argv);

  JS_FreeValue(ctx, argv[0]);
  JS_FreeValue(ctx, argv[1]);

  return obj;
}

static JSClassDef js_pgsqlerror_class = {
    .class_name = "PgSQLError",
};

static const JSCFunctionListEntry js_pgsqlerror_funcs[] = {
    JS_PROP_STRING_DEF("name", "PgSQLError", JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("type", 0, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PgSQLError", JS_PROP_CONFIGURABLE),
};

/*static JSValue
result_value(JSContext* ctx, PGSQL_FIELD const* field, char* buf, size_t len, int rtype) {
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
result_array(JSContext* ctx, PGresult* res, PGSQL_ROW row, int rtype) {
  JSValue ret = JS_NewArray(ctx);
  uint32_t i, num_fields = pgsql_num_fields(res);
  PGSQL_FIELD* fields = pgsql_fetch_fields(res);
  unsigned long* field_lengths = pgsql_fetch_lengths(res);

  for(i = 0; i < num_fields; i++) {
#ifdef DEBUG_OUTPUT_
    printf("%s num_fields=%" PRIu32 " row[%" PRIu32 "] = '%.*s'\n", __func__, num_fields, i, (int)(field_lengths[i] > 32 ? 32 : field_lengths[i]), row[i]);
#endif
    JS_SetPropertyUint32(ctx, ret, i, result_value(ctx, &fields[i], row[i], field_lengths[i], rtype));
  }

  return ret;
}

static JSValue
result_object(JSContext* ctx, PGresult* res, PGSQL_ROW row, int rtype) {
  JSValue ret = JS_NewObject(ctx);
  uint32_t i, num_fields = pgsql_num_fields(res);
  PGSQL_FIELD* fields = pgsql_fetch_fields(res);
  FieldNameFunc* fn = (rtype & RESULT_TABLENAME) ? field_id : field_namefunc(fields, num_fields);
  unsigned long* field_lengths = pgsql_fetch_lengths(res);

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
result_row(JSContext* ctx, PGresult* res, PGSQL_ROW row, int rtype) {
  JSValue val;
  RowValueFunc* row_func = (rtype & RESULT_OBJECT) ? result_object : result_array;

  val = row ? row_func(ctx, res, row, rtype) : JS_NULL;

  return val;
}

static JSValue
result_iterate(JSContext* ctx, PGresult* res, PGSQL_ROW row, int rtype) {
  JSValue ret, val = result_row(ctx, res, row, rtype);
  ret = js_iterator_result(ctx, val, row ? FALSE : TRUE);
  JS_FreeValue(ctx, val);
  return ret;
}

static void
result_yield(JSContext* ctx, JSValueConst func, PGresult* res, PGSQL_ROW row, int rtype) {
  JSValue val = result_row(ctx, res, row, rtype);

  JSValue item = js_iterator_result(ctx, val, row ? FALSE : TRUE);
  JS_FreeValue(ctx, val);

  value_yield_free(ctx, func, item);
}

static void
result_resolve(JSContext* ctx, JSValueConst func, PGresult* res, PGSQL_ROW row, int rtype) {
  JSValue value = row ? result_row(ctx, res, row, rtype) : JS_NULL;

  value_yield_free(ctx, func, value);
}*/

PGresult*
js_pgresult_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_pgresult_class_id);
}

struct PGsqlConn*
js_pgresult_handle(JSContext* ctx, JSValueConst value) {
  PGresult* res;
  struct PGsqlConn* pq = 0;
  JSValue handle = JS_GetPropertyStr(ctx, value, "handle");

  if(JS_IsObject(handle))
    pq = JS_GetOpaque(handle, js_pgconn_class_id);

  JS_FreeValue(ctx, handle);

  /*  if(!pq)
      if((res = JS_GetOpaque(value, js_pgresult_class_id)))
        pq = res->handle;
  */
  return pq;
}

BOOL
js_pgresult_nonblock(JSContext* ctx, JSValueConst value) {
  struct PGsqlConn* pq;

  if((pq = js_pgresult_handle(ctx, value)))
    return pgsql_nonblock(pq);

  return FALSE;
}

static inline int
js_pgresult_rtype(JSContext* ctx, JSValueConst value) {
  JSValue handle = JS_GetPropertyStr(ctx, value, "handle");
  int rtype = js_pgconn_rtype(ctx, handle);
  JS_FreeValue(ctx, handle);
  return rtype;
}

static JSValue
js_pgresult_next_cont(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue data[]) {
  /* PGSQL_ROW row;
   JSValue my_val;
   PGresult* res;
   struct PGsqlConn* pq = 0;
   int wantwrite, oldstate, newstate, fd;
   uint32_t num_fields, field_count;

   if(!(res = js_pgresult_data(ctx, data[1])))
     return JS_EXCEPTION;

   if(!(pq = js_pgresult_handle(ctx, data[1])))
     pq = res->handle;

   JS_ToInt32(ctx, &wantwrite, data[0]);

   oldstate = wantwrite ? PGRES_POLLING_WRITING : PGRES_POLLING_READING;
   field_count = res->field_count;

   newstate = pgsql_fetch_row_cont(&row, res, oldstate);

   num_fields = newstate ? 0 : pgsql_num_fields(res);
   fd = PQsocket(pq);

 #ifdef DEBUG_OUTPUT
   printf("%s field_count=%" PRIu32 " num_fields=%" PRIu32 " newstate=%d\n", __func__, field_count, num_fields, newstate);
 #endif

   if(newstate == 0 && num_fields == field_count) {
     js_iohandler_set(ctx, data[2], fd, JS_NULL);

     (magic ? result_resolve : result_yield)(ctx, data[3], res, row, js_pgresult_rtype(ctx, data[1]));

   } else if(newstate != oldstate) {
     JSValue handler, hdata[5] = {
                          JS_NewInt32(ctx, wantwrite),
                          JS_DupValue(ctx, data[1]),
                          js_iohandler_fn(ctx, !!(newstate & PGRES_POLLING_WRITING)),
                          JS_DupValue(ctx, data[3]),
                          JS_DupValue(ctx, data[4]),
                      };
     handler = JS_NewCFunctionData(ctx, js_pgresult_next_cont, 0, magic, countof(hdata), hdata);

     js_iohandler_set(ctx, data[2], fd, JS_NULL);
     js_iohandler_set(ctx, hdata[2], fd, handler);
     JS_FreeValue(ctx, handler);
   }

 #ifdef DEBUG_OUTPUT
   printf("%s wantwrite=%i fd=%i pq=%p error='%s'\n", __func__, wantwrite, fd, pq, pgsql_error(pq));
 #endif*/

  return JS_UNDEFINED;
}

static JSValue
js_pgresult_next_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  /* JSValue my_val, promise = JS_UNDEFINED, data[5], handler;
   int64_t client_flags = 0;
   struct PGsqlConn* pq;
   PGresult* res;
   PGSQL_ROW row;
   int wantwrite, state;
   uint32_t num_fields, field_count;

   if(!(res = js_pgresult_data(ctx, this_val)))
     return JS_EXCEPTION;

   pq = js_pgresult_handle(ctx, this_val);
   assert(pq);

   field_count = res->field_count; // pgsql_field_count(pq);
   state = pgsql_fetch_row_start(&row, res);

   num_fields = state ? 0 : pgsql_num_fields(res);

 #ifdef DEBUG_OUTPUT
   printf("%s field_count=%" PRIu32 " num_fields=%" PRIu32 " state=%d\n", __func__, field_count, num_fields, state);
 #endif

   wantwrite = !!(state & PGRES_POLLING_WRITING);

   promise = JS_NewPromiseCapability(ctx, &data[3]);

   if(state == 0 && num_fields == field_count) {
     int rtype = js_pgresult_rtype(ctx, this_val);
     (magic ? result_resolve : result_yield)(ctx, data[3], res, row, rtype);

   } else {
     data[0] = JS_NewInt32(ctx, wantwrite);
     data[1] = JS_DupValue(ctx, this_val);
     data[2] = js_iohandler_fn(ctx, wantwrite);

     handler = JS_NewCFunctionData(ctx, js_pgresult_next_cont, 0, magic, countof(data), data);

     if(!js_iohandler_set(ctx, data[2], PQsocket(pq), handler))
       JS_Call(ctx, data[4], JS_UNDEFINED, 0, 0);
   }

   return promise;*/
  return JS_UNDEFINED;
}

static JSValue
js_pgresult_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  /* PGresult* res;

   if(!(res = js_pgresult_data(ctx, this_val)))
     return JS_EXCEPTION;

   if(!js_pgresult_nonblock(ctx, this_val)) {
     JSValue tmp, ret;
     PGSQL_ROW row;

     row = pgsql_fetch_row(res);

     ret = (magic ? result_row : result_iterate)(ctx, res, row, js_pgresult_rtype(ctx, this_val));

     // ret = magic ? JS_DupValue(ctx, tmp) : js_iterator_result(ctx, tmp, row ? FALSE : TRUE);
     // JS_FreeValue(ctx, tmp);
     return ret;
   }*/

  return js_pgresult_next_start(ctx, this_val, argc, argv, magic);
}

enum {
  METHOD_FETCH_FIELD,
  METHOD_FETCH_FIELDS,
};

static JSValue
js_pgresult_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  PGresult* res;

  if(!(res = js_pgresult_data(ctx, this_val)))
    return JS_EXCEPTION;
  /*
    switch(magic) {
      case METHOD_FETCH_FIELD: {
        uint32_t index;
        PGSQL_FIELD* field;

        if(JS_ToUint32(ctx, &index, argv[0]))
          return JS_ThrowTypeError(ctx, "argument 1 must be a positive index");

        if(index >= pgsql_num_fields(res))
          return JS_ThrowRangeError(ctx, "argument 1 must be smaller than total fields (%" PRIu32 ")", pgsql_num_fields(res));

        if((field = pgsql_fetch_field_direct(res, index)))
          ret = field_array(ctx, field);

        break;
      }
      case METHOD_FETCH_FIELDS: {
        PGSQL_FIELD* fields;

        if((fields = pgsql_fetch_fields(res))) {
          uint32_t i, num_fields = pgsql_num_fields(res);

          ret = JS_NewArray(ctx);

          for(i = 0; i < num_fields; i++) JS_SetPropertyUint32(ctx, ret, i, field_array(ctx, &fields[i]));
        }
        break;
      }
    }
  */
  return ret;
}

static JSValue
js_pgresult_new(JSContext* ctx, JSValueConst proto, PGresult* res) {
  JSValue obj;

  if(js_pgresult_class_id == 0)
    js_pgconn_init(ctx, 0);

  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = pgresult_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_pgresult_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, res);

  return obj;
fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_pgresult_wrap(JSContext* ctx, PGresult* res) {
  JSValue obj = JS_NULL;

  if(res)
    obj = js_pgresult_new(ctx, pgresult_proto, res);

  return obj;
}

static JSValue
js_pgresult_get(JSContext* ctx, JSValueConst this_val, int magic) {
  PGresult* res;
  JSValue ret = JS_UNDEFINED;

  if(!(res = js_pgresult_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_NUM_ROWS: {
      ret = JS_NewInt64(ctx, PQntuples(res));
      break;
    }
    case PROP_NUM_FIELDS: {
      ret = JS_NewInt64(ctx, PQnfields(res));
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
js_pgresult_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  struct PGsqlConn* pq = js_pgresult_handle(ctx, this_val);

  assert(pq);

  BOOL block = !pgsql_nonblock(pq);

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
js_pgresult_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  return js_pgresult_new(ctx, proto, 0);

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_pgresult_finalizer(JSRuntime* rt, JSValue val) {
  PGresult* res;

  if((res = JS_GetOpaque(val, js_pgresult_class_id))) {
    pgsql_free_result(res);
  }
}

static JSClassDef js_pgresult_class = {
    .class_name = "PGresult",
    .finalizer = js_pgresult_finalizer,
};

static const JSCFunctionListEntry js_pgresult_funcs[] = {
    JS_CFUNC_MAGIC_DEF("next", 0, js_pgresult_next, 0),
    JS_CGETSET_MAGIC_DEF("eof", js_pgresult_get, 0, PROP_EOF),
    JS_CGETSET_MAGIC_FLAGS_DEF("numRows", js_pgresult_get, 0, PROP_NUM_ROWS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("numFields", js_pgresult_get, 0, PROP_NUM_FIELDS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("fieldCount", js_pgresult_get, 0, PROP_FIELD_COUNT),
    JS_CGETSET_MAGIC_DEF("currentField", js_pgresult_get, 0, PROP_CURRENT_FIELD),
    JS_CFUNC_MAGIC_DEF("fetchField", 1, js_pgresult_functions, METHOD_FETCH_FIELD),
    JS_CFUNC_MAGIC_DEF("fetchFields", 0, js_pgresult_functions, METHOD_FETCH_FIELDS),
    JS_CFUNC_MAGIC_DEF("fetchRow", 0, js_pgresult_next, 1),
    JS_CFUNC_MAGIC_DEF("[Symbol.iterator]", 0, js_pgresult_iterator, METHOD_ITERATOR),
    JS_CFUNC_MAGIC_DEF("[Symbol.asyncIterator]", 0, js_pgresult_iterator, METHOD_ASYNC_ITERATOR),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PGresult", JS_PROP_CONFIGURABLE),
};

int
js_pgconn_init(JSContext* ctx, JSModuleDef* m) {
  if(js_pgconn_class_id == 0) {

    JS_NewClassID(&js_pgconn_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_pgconn_class_id, &js_pgconn_class);

    pgsql_ctor = JS_NewCFunction2(ctx, js_pgconn_constructor, "PGconn", 1, JS_CFUNC_constructor, 0);
    pgsql_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, pgsql_proto, js_pgconn_funcs, countof(js_pgconn_funcs));
    JS_SetPropertyFunctionList(ctx, pgsql_proto, js_pgconn_defines, countof(js_pgconn_defines));
    JS_SetPropertyFunctionList(ctx, pgsql_ctor, js_pgconn_static, countof(js_pgconn_static));
    JS_SetPropertyFunctionList(ctx, pgsql_ctor, js_pgconn_defines, countof(js_pgconn_defines));
    JS_SetClassProto(ctx, js_pgconn_class_id, pgsql_proto);

    JSValue error = JS_NewError(ctx);
    JSValue error_proto = JS_GetPrototype(ctx, error);
    JS_FreeValue(ctx, error);

    JS_NewClassID(&js_pgsqlerror_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_pgsqlerror_class_id, &js_pgsqlerror_class);

    pgsqlerror_ctor = JS_NewCFunction2(ctx, js_pgsqlerror_constructor, "PgSQLError", 1, JS_CFUNC_constructor, 0);
    pgsqlerror_proto = JS_NewObjectProto(ctx, error_proto);
    JS_FreeValue(ctx, error_proto);

    JS_SetPropertyFunctionList(ctx, pgsqlerror_proto, js_pgsqlerror_funcs, countof(js_pgsqlerror_funcs));

    JS_SetClassProto(ctx, js_pgsqlerror_class_id, pgsqlerror_proto);

    JS_NewClassID(&js_pgresult_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_pgresult_class_id, &js_pgresult_class);

    pgresult_ctor = JS_NewCFunction2(ctx, js_pgresult_constructor, "PGresult", 1, JS_CFUNC_constructor, 0);
    pgresult_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, pgresult_proto, js_pgresult_funcs, countof(js_pgresult_funcs));
    JS_SetClassProto(ctx, js_pgresult_class_id, pgresult_proto);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "PGconn", pgsql_ctor);
    JS_SetModuleExport(ctx, m, "PgSQLError", pgsqlerror_ctor);
    JS_SetModuleExport(ctx, m, "PGresult", pgresult_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_pgsql
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  if(!(m = JS_NewCModule(ctx, module_name, &js_pgconn_init)))
    return m;
  JS_AddModuleExport(ctx, m, "PGconn");
  JS_AddModuleExport(ctx, m, "PgSQLError");
  JS_AddModuleExport(ctx, m, "PGresult");
  return m;
}

/**
 * @}
 */
