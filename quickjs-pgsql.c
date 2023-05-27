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
thread_local JSValue pgsqlerror_proto = {{0},JS_TAG_UNDEFINED}, pgsqlerror_ctor = {{0},JS_TAG_UNDEFINED}, pgsql_proto = {{0},JS_TAG_UNDEFINED},
                     pgsql_ctor = {{0},JS_TAG_UNDEFINED}, pgresult_proto = {{0},JS_TAG_UNDEFINED}, pgresult_ctor = {{0},JS_TAG_UNDEFINED};

static JSValue js_pgresult_wrap(JSContext* ctx, PGresult* res);
static JSValue string_to_value(JSContext* ctx, const char* func_name, const char* s);
static JSValue string_to_object(JSContext* ctx, const char* ctor_name, const char* s);
static void result_free(JSRuntime* rt, void* opaque, void* ptr);

#define string_to_number(ctx, s) string_to_value(ctx, "Number", s);
#define string_to_date(ctx, s) string_to_object(ctx, "Date", s);

struct PGConnection;
struct PGResult;

struct PGResult {
  int ref_count;
  PGresult* result;
  struct PGConnection* conn;
  uint32_t row_index;
};

struct PGResultIterator {
  JSContext* ctx;
  struct PGResult* result;
  uint32_t row_index;
};

struct PGConnection {
  int ref_count;
  PGconn* conn;
  BOOL nonblocking;
  struct PGResult* result;
};

struct PGConnectParameters {
  const char **keywords, **values;
  size_t num_params;
};

typedef struct PGConnection PGSQLConnection;
typedef struct PGResult PGSQLResult;
typedef struct PGResultIterator PGSQLResultIterator;
typedef struct PGConnectParameters PGSQLConnectParameters;

typedef char* FieldNameFunc(JSContext*, PGSQLResult*, int field);
typedef JSValue RowValueFunc(JSContext*, PGSQLResult*, int, int);

static char* field_id(JSContext* ctx, PGSQLResult*, int field);
static char* field_name(JSContext* ctx, PGSQLResult*, int field);
static FieldNameFunc* field_namefunc(PGresult* res);
static JSValue field_array(PGSQLResult*, int, JSContext*);
static BOOL field_is_integer(PGresult* res, int field);
static BOOL field_is_json(PGresult* res, int field);
static BOOL field_is_float(PGresult* res, int field);
static BOOL field_is_number(PGresult* res, int field);
static BOOL field_is_binary(PGresult* res, int field);
static BOOL field_is_boolean(PGresult* res, int field);
static BOOL field_is_null(PGresult* res, int field);
static BOOL field_is_date(PGresult* res, int field);
static BOOL field_is_string(PGresult* res, int field);

enum ResultFlags {
  RESULT_OBJECT = 1,
  RESULT_STRING = 2,
  RESULT_TABLENAME = 4,
};

static PGSQLResult* pgresult_dup(PGSQLResult* ptr);
static void pgresult_free(JSRuntime* rt, void* ptr, void* mem);
static JSValue pgresult_row(PGSQLResult* opaque, uint32_t row, RowValueFunc*, JSContext* ctx);
static int64_t pgresult_cmdtuples(PGSQLResult* opaque);
static void pgresult_set_conn(PGSQLResult* opaque, PGSQLConnection* conn, JSContext* ctx);

static JSValue js_pgresult_new(JSContext* ctx, JSValueConst proto, PGresult* res);
static JSValue js_pgsqlerror_new(JSContext* ctx, const char* msg);

static void
connectparams_parse(JSContext* ctx, PGSQLConnectParameters* c, const char* params) {
  char* err;
  PQconninfoOption* options;
  size_t i, j = 0, len = 0;

  if((options = PQconninfoParse(params, &err))) {

    for(i = 0; options[i].keyword; i++) {
      if(options[i].val)
        ++len;
    }

    c->num_params = len;

    c->keywords = js_malloc(ctx, sizeof(char*) * (len + 1));
    c->values = js_malloc(ctx, sizeof(char*) * (len + 1));

    for(i = 0; options[i].keyword; i++) {
      if(options[i].val) {
        c->keywords[j] = js_strdup(ctx, options[i].keyword);
        c->values[j] = options[i].val ? js_strdup(ctx, options[i].val) : 0;
        ++j;
      }
    }

    c->keywords[len] = c->values[len] = 0;

    PQconninfoFree(options);
  }
}

static void
connectparams_fromobj(JSContext* ctx, PGSQLConnectParameters* c, JSValueConst obj) {
  JSPropertyEnum* tmp_tab;
  uint32_t tmp_len, i;

  if(JS_GetOwnPropertyNames(ctx, &tmp_tab, &tmp_len, obj, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
    JS_ThrowTypeError(ctx, "argument is must be an object");
    return;
  }

  c->num_params = tmp_len;
  c->keywords = js_malloc(ctx, sizeof(char*) * (tmp_len + 1));
  c->values = js_malloc(ctx, sizeof(char*) * (tmp_len + 1));

  for(i = 0; i < tmp_len; i++) {
    c->keywords[i] = js_atom_tostring(ctx, tmp_tab[i].atom);
    c->values[i] = js_get_property_string(ctx, obj, tmp_tab[i].atom);
  }

  js_propertyenums_free(ctx, tmp_tab, tmp_len);
}

static void
connectparams_init(JSContext* ctx, PGSQLConnectParameters* c, int argc, JSValueConst argv[]) {
  if(argc == 1 && JS_IsString(argv[0])) {
    const char* str = JS_ToCString(ctx, argv[0]);
    connectparams_parse(ctx, c, str);
    JS_FreeCString(ctx, str);
  } else if(argc > 0 && JS_IsObject(argv[0])) {
    connectparams_fromobj(ctx, c, argv[0]);
  } else {
    static const char* param_names[] = {
        "host",
        "user",
        "password",
        "dbname",
        "port",
        "connect_timeout",
    };
    int i;
    if(argc > countof(param_names))
      argc = countof(param_names);

    c->num_params = argc;
    c->keywords = js_malloc(ctx, sizeof(char*) * (argc + 1));
    c->values = js_malloc(ctx, sizeof(char*) * (argc + 1));

    for(i = 0; i < argc; i++) {
      c->keywords[i] = js_strdup(ctx, param_names[i]);
      c->values[i] = js_tostring(ctx, argv[i]);
    }
    c->keywords[argc] = c->values[argc] = 0;
  }
}

static void
connectparams_free(JSContext* ctx, PGSQLConnectParameters* c) {
  size_t i;

  for(i = 0; i < c->num_params; i++) {
    js_free(ctx, (void*)c->keywords[i]);
    js_free(ctx, (void*)c->values[i]);
  }
  js_free(ctx, c->keywords);
  js_free(ctx, c->values);
}

static void
js_pgconn_print_value(JSContext* ctx, PGSQLConnection* pq, DynBuf* out, JSValueConst value) {

  if(JS_IsNull(value) || JS_IsUndefined(value) || js_is_nan(value)) {
    dbuf_putstr(out, "NULL");

  } else if(JS_IsBool(value)) {
    dbuf_putstr(out, JS_ToBool(ctx, value) ? "TRUE" : "FALSE");

  } else if(JS_IsString(value)) {
    size_t len;
    const char* src = JS_ToCStringLen(ctx, &len, value);
    char* dst;

    if(pq && pq->conn) {
      dst = PQescapeLiteral(pq->conn, src, len);
      dbuf_putstr(out, dst);
      PQfreemem(dst);
    } else {
      dbuf_putc(out, '\'');
      dst = (char*)dbuf_reserve(out, len * 2 + 1);
      len = PQescapeString(dst, src, len);
      out->size += len;
      dbuf_putc(out, '\'');
    }

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
    JSValue val = JS_IsNumber(value) ? JS_DupValue(ctx, value) : js_value_coerce(ctx, "Number", value);
    size_t len;
    const char* src = JS_ToCStringLen(ctx, &len, val);
    dbuf_put(out, (const uint8_t*)src, len);
    // js_pgconn_print_value(ctx, pq, out, val);
    JS_FreeValue(ctx, val);

  } else if(js_is_arraybuffer(ctx, value)) {

    InputBuffer input = js_input_buffer(ctx, value);
    char buf[FMT_XLONG] = {'\\'};

    static const uint8_t hexdigits[] = "0123456789ABCDEF";
    dbuf_putstr(out, "'\\x");
    for(size_t i = 0; i < input.size; i++)
      dbuf_put(out, (const uint8_t*)buf, fmt_xlong0(buf, input.data[i], 2));

    dbuf_putstr(out, "'::bytea");

    input_buffer_free(&input, ctx);
  } else {
    JSValue str = JS_JSONStringify(ctx, value, JS_NULL, JS_NULL);

    js_pgconn_print_value(ctx, pq, out, str);
    JS_FreeValue(ctx, str);
  }
}

static void
js_pgconn_print_field(JSContext* ctx, PGSQLConnection* pq, DynBuf* out, JSValueConst value) {
  const char* name;
  size_t namelen;

  if((name = JS_ToCStringLen(ctx, &namelen, value))) {
    char* escaped;

    escaped = PQescapeIdentifier(pq->conn, name, namelen);
    JS_FreeCString(ctx, name);

    if(escaped) {
      dbuf_putstr(out, escaped);
      PQfreemem(escaped);
    }
  }
}

/*
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
*/
static void
js_pgconn_print_insert(JSContext* ctx, DynBuf* out) {
  dbuf_putstr(out, "INSERT INTO ");
}

static void
js_pgconn_print_fields(JSContext* ctx, PGSQLConnection* pq, DynBuf* out, JSValueConst fields) {
  JSValue item, iter = js_iterator_new(ctx, fields);

  if(!JS_IsUndefined(iter)) {
    BOOL done = FALSE;

    dbuf_putc(out, '(');
    for(int i = 0;; i++) {
      item = js_iterator_next(ctx, iter, &done);
      if(done)
        break;
      if(i > 0)
        dbuf_putstr(out, ", ");

      js_pgconn_print_field(ctx, pq, out, item);
      JS_FreeValue(ctx, item);
    }
    dbuf_putc(out, ')');
  }
}

static void
js_pgconn_print_values(JSContext* ctx, PGSQLConnection* pq, DynBuf* out, JSValueConst values) {
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

  } /*else {
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
      js_pgconn_print_value(ctx, out, item);
      JS_FreeValue(ctx, item);
    }
    dbuf_putc(out, ')');
    js_propertyenums_free(ctx, tmp_tab, tmp_len);
  }*/
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

static PGSQLConnection*
pgconn_new(JSContext* ctx) {
  PGSQLConnection* pq;

  if(!(pq = js_malloc(ctx, sizeof(PGSQLConnection))))
    return 0;

  *pq = (PGSQLConnection){1, NULL, FALSE, NULL};

  return pq;
}

static PGSQLConnection*
pgconn_dup(PGSQLConnection* pq) {
  ++pq->ref_count;
  return pq;
}

static void
pgconn_free(PGSQLConnection* pq, JSRuntime* rt) {
  if(--pq->ref_count == 0) {
    if(pq->result) {
      pgresult_free(rt, pq->result, 0);
      pq->result = 0;
    }
    if(pq->conn) {
      PQfinish(pq->conn);
      pq->conn = 0;
    }
    js_free_rt(rt, pq);
  }
}

static BOOL
pgconn_nonblock(PGSQLConnection* pq) {
  return pq->conn ? PQisnonblocking(pq->conn) : pq->nonblocking;
}

static const char*
pgconn_error(PGSQLConnection* pq) {
  return PQerrorMessage(pq->conn);
}

static void
pgconn_set_result(PGSQLConnection* pq, PGSQLResult* opaque, JSContext* ctx) {
  if(pq->result) {
    pgresult_free(JS_GetRuntime(ctx), pq->result, 0);
    pq->result = 0;
  }
  pq->result = opaque ? pgresult_dup(opaque) : 0;
}

static JSValue
pgconn_result(PGSQLConnection* pq, PGresult* res, JSContext* ctx) {
  JSValue value = js_pgresult_new(ctx, pgresult_proto, res);
  PGSQLResult* opaque = JS_GetOpaque(value, js_pgresult_class_id);
  pgconn_set_result(pq, opaque, ctx);
  pgresult_set_conn(opaque, pq, ctx);
  return value;
}

static JSValue
pgconn_get_result(PGSQLConnection* pq, JSContext* ctx) {
  PGresult* res;

  if((res = PQgetResult(pq->conn)))
    return pgconn_result(pq, res, ctx);

  return JS_NULL;
}

static char*
pgconn_lookup_oid(PGSQLConnection* pq, Oid oid, JSContext* ctx) {
  PGresult* res;
  DynBuf buf;
  char* ret = 0;
  js_dbuf_init(ctx, &buf);
  dbuf_putstr(&buf, "SELECT relname FROM pg_class WHERE oid=");
  dbuf_put_uint32(&buf, oid);
  dbuf_putc(&buf, ';');
  dbuf_0(&buf);

  if((res = PQexec(pq->conn, (const char*)buf.buf))) {
    char* s;

    if((s = PQgetvalue(res, 0, 0))) {
      ret = js_strdup(ctx, s);
    }
  }
  dbuf_free(&buf);
  return ret;
}

PGSQLConnection*
js_pgconn_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_pgconn_class_id);
}

static JSValue
js_pgconn_new(JSContext* ctx, JSValueConst proto) {
  JSValue obj;

  if(js_pgconn_class_id == 0)
    js_pgsql_init(ctx, 0);

  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = JS_DupValue(ctx, pgsql_proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_pgconn_class_id);
  if(JS_IsException(obj))
    goto fail;

  return obj;
fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_pgconn_wrap(JSContext* ctx, JSValueConst proto, PGconn* conn) {
  PGSQLConnection* pq;
  JSValue ret = JS_UNDEFINED;

  if(!(pq = pgconn_new(ctx)))
    return JS_EXCEPTION;

  pq->conn = conn;

  if(!JS_IsException((ret = js_pgconn_new(ctx, proto))))
    JS_SetOpaque(ret, pq);

  return ret;
}

static inline int
js_pgconn_rtype(JSContext* ctx, JSValueConst value) {
  return js_get_propertystr_int32(ctx, value, "resultType");
}

enum {
  METHOD_ESCAPE_STRING,
};

static JSValue
js_pgconn_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  PGSQLConnection* pq = 0;
  JSValue ret = JS_UNDEFINED;

  if(!(pq = js_pgconn_data2(ctx, this_val)))
    return JS_EXCEPTION;

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

      len = pq && pq->conn ? PQescapeStringConn(pq->conn, dst, src, len, 0) : PQescapeString(dst, src, len);
      ret = JS_NewStringLen(ctx, dst, len);
      js_free(ctx, dst);
      break;
    }
  }

  return ret;
}

enum {
  PROP_CMD_TUPLES,
  PROP_NONBLOCKING,
  PROP_FD,
  PROP_OPTIONS,
  PROP_ERRNO,
  PROP_ERROR_MESSAGE,
  PROP_INSERT_ID,
  PROP_CLIENT_ENCODING,
  PROP_PROTOCOL_VERSION,
  PROP_SERVER_VERSION,
  PROP_USER,
  PROP_PASSWORD,
  PROP_HOST,
  PROP_DB,
  PROP_PORT,
  PROP_CONNINFO,

  PROP_CLIENT_INFO,
  PROP_CLIENT_VERSION,
  PROP_THREAD_SAFE,
  PROP_EOF,
  PROP_NUM_ROWS,
  PROP_NUM_FIELDS,
};

static JSValue
js_pgconn_get(JSContext* ctx, JSValueConst this_val, int magic) {
  PGSQLConnection* pq;
  JSValue ret = JS_UNDEFINED;

  if(!(pq = js_pgconn_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_CMD_TUPLES: {
      if(pq->result)
        ret = JS_NewInt64(ctx, pgresult_cmdtuples(pq->result));
      break;
    }
    case PROP_NONBLOCKING: {
      ret = JS_NewBool(ctx, pq->conn ? PQisnonblocking(pq->conn) : pq->nonblocking);
      break;
    }
    case PROP_FD: {
      ret = JS_NewInt32(ctx, PQsocket(pq->conn));
      break;
    }
    case PROP_ERROR_MESSAGE: {
      const char* error = PQerrorMessage(pq->conn);
      ret = error && *error ? JS_NewString(ctx, error) : JS_NULL;
      break;
    }
    case PROP_OPTIONS: {
      const char* options = PQoptions(pq->conn);
      ret = options && *options ? JS_NewString(ctx, options) : JS_NULL;
      break;
    }
    case PROP_INSERT_ID: {
      PGresult* res;
      int64_t id = -1;
      if((res = PQexec(pq->conn, "SELECT lastval();"))) {
        char* val;

        if((val = PQgetvalue(res, 0, 0))) {
          if(scan_longlong(val, &id) > 0)
            ret = JS_NewInt64(ctx, id);
        }
        PQclear(res);
      }
      break;
    }
    case PROP_CLIENT_ENCODING: {
      int encoding = PQclientEncoding(pq->conn);
      const char* charset = pg_encoding_to_char(encoding);
      ret = charset && *charset ? JS_NewString(ctx, charset) : JS_NULL;
      break;
    }
    case PROP_PROTOCOL_VERSION: {
      ret = JS_NewUint32(ctx, PQprotocolVersion(pq->conn));
      break;
    }
    case PROP_SERVER_VERSION: {
      ret = JS_NewUint32(ctx, PQserverVersion(pq->conn));
      break;
    }
    case PROP_USER: {
      char* user = PQuser(pq->conn);
      ret = user ? JS_NewString(ctx, user) : JS_NULL;
      break;
    }
    case PROP_PASSWORD: {
      char* pass = PQpass(pq->conn);
      ret = pass ? JS_NewString(ctx, pass) : JS_NULL;
      break;
    }
    case PROP_HOST: {
      char* host = PQhost(pq->conn);
      ret = host ? JS_NewString(ctx, host) : JS_NULL;

      break;
    }
    case PROP_PORT: {
      char* port = PQport(pq->conn);
      ret = port ? JS_NewString(ctx, port) : JS_NULL;

      break;
    }
    case PROP_DB: {
      char* db = PQdb(pq->conn);
      ret = db ? JS_NewString(ctx, db) : JS_NULL;
      break;
    }
    case PROP_CONNINFO: {
      PQconninfoOption* info;

      if((info = PQconninfo(pq->conn))) {
        int i;
        ret = JS_NewObject(ctx);
        for(i = 0; info[i].keyword; i++)
          if(info[i].val)
            JS_SetPropertyStr(ctx, ret, info[i].keyword, info[i].val ? JS_NewString(ctx, info[i].val) : JS_NULL);
      }

      break;
    }
  }

  return ret;
}

static JSValue
js_pgconn_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  PGSQLConnection* pq;

  if(!(pq = js_pgconn_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_NONBLOCKING: {
      pq->nonblocking = JS_ToBool(ctx, value);

      if(pq->conn)
        if(PQsetnonblocking(pq->conn, pq->nonblocking))
          return JS_Throw(ctx, js_pgsqlerror_new(ctx, PQerrorMessage(pq->conn)));

      break;
    }
    case PROP_CLIENT_ENCODING: {
      const char* charset;

      if((charset = JS_ToCString(ctx, value))) {
        int ret;
        ret = PQsetClientEncoding(pq->conn, charset);
        JS_FreeCString(ctx, charset);
        if(ret)
          return JS_Throw(ctx, js_pgsqlerror_new(ctx, PQerrorMessage(pq->conn)));
      }
      break;
    }
  }

  return JS_UNDEFINED;
}

static JSValue
js_pgconn_value_string(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  DynBuf buf;
  PGSQLConnection* pq;

  if(!(pq = js_pgconn_data2(ctx, this_val)))
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
  DynBuf buf;
  PGSQLConnection* pq;

  if(!(pq = js_pgconn_data2(ctx, this_val)))
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
  DynBuf buf;
  const char* tbl;
  size_t tbl_len;
  PGSQLConnection* pq;
  int i = 1, j;

  if(!(pq = js_pgconn_data2(ctx, this_val)))
    return JS_EXCEPTION;

  tbl = JS_ToCStringLen(ctx, &tbl_len, argv[0]);

  dbuf_init2(&buf, 0, 0);
  js_pgconn_print_insert(ctx, &buf);
  dbuf_put(&buf, (const uint8_t*)tbl, tbl_len);
  dbuf_putstr(&buf, " ");

  if(i + 1 < argc) {
    js_pgconn_print_fields(ctx, pq, &buf, argv[i++]);
  }

  dbuf_putstr(&buf, " VALUES ");

  for(j = i; j < argc; j++) {
    if(JS_IsString(argv[j]))
      break;

    if(i > j)
      dbuf_putstr(&buf, ", ");

    js_pgconn_print_values(ctx, pq, &buf, argv[j]);
  }

  if(j < argc) {
    dbuf_putstr(&buf, " RETURNING ");
    js_pgconn_print_field(ctx, pq, &buf, argv[j++]);
  }

  dbuf_putstr(&buf, ";");

  ret = JS_NewStringLen(ctx, (const char*)buf.buf, buf.size);
  dbuf_free(&buf);
  return ret;
}

static JSValue
js_pgconn_escape_string(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  PGSQLConnection* pq;
  char* dst;
  const char* src;
  size_t len;

  pq = JS_GetOpaque(this_val, js_pgconn_class_id);

  if(!(src = JS_ToCStringLen(ctx, &len, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be string");

  if((!(dst = js_malloc(ctx, 2 * len + 1)))) {
    JS_FreeCString(ctx, src);
    return JS_ThrowOutOfMemory(ctx);
  }

  len = pq && pq->conn ? PQescapeStringConn(pq->conn, dst, src, len, NULL) : PQescapeString(dst, src, len);
  ret = JS_NewStringLen(ctx, dst, len);
  js_free(ctx, dst);

  return ret;
}

static JSValue
js_pgconn_escape_alloc(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  PGSQLConnection* pq;
  char* dst;
  const char* src;
  size_t len;

  if(!(pq = js_pgconn_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(src = JS_ToCStringLen(ctx, &len, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be string");

  dst = magic ? PQescapeIdentifier(pq->conn, src, len) : PQescapeLiteral(pq->conn, src, len);
  ret = JS_NewString(ctx, dst);
  PQfreemem(dst);

  return ret;
}

static JSValue
js_pgconn_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = js_pgconn_wrap(ctx, proto, 0);
  JS_FreeValue(ctx, proto);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_pgconn_escape_bytea(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  PGSQLConnection* pq;
  char* dst;
  size_t len, dlen = 0;

  pq = JS_GetOpaque(this_val, js_pgconn_class_id);

  InputBuffer src = js_input_chars(ctx, argv[0]);

  if(!src.size)
    return JS_ThrowTypeError(ctx, "argument 1 must be string or ArrayBuffer");

  dst = (char*)(pq && pq->conn ? PQescapeByteaConn(pq->conn, (unsigned char*)src.data, src.size, &dlen)
                               : PQescapeBytea((unsigned char*)src.data, src.size, &dlen));

  if(dlen > 0 && dst[dlen - 1] == '\0')
    dlen--;

  ret = JS_NewStringLen(ctx, dst, dlen);
  PQfreemem(dst);

  input_buffer_free(&src, ctx);

  return ret;
}

static JSValue
js_pgconn_unescape_bytea(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  char* dst;
  size_t len, dlen = 0;
  const char* src;

  if(!(src = JS_ToCString(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be string");

  dst = (char*)PQunescapeBytea((unsigned char*)src, &dlen);
  JS_FreeCString(ctx, src);

  return JS_NewArrayBuffer(ctx, (uint8_t*)dst, dlen, &result_free, 0, FALSE);
}

static JSValue
js_pgconn_connect_cont(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue data[]) {
  int32_t fd;
  PGSQLConnection* pq;
  PostgresPollingStatusType newstate, oldstate;

  if(!(pq = js_pgconn_data2(ctx, data[0])))
    return JS_EXCEPTION;

  oldstate = magic;
  newstate = PQconnectPoll(pq->conn);
  fd = PQsocket(pq->conn);

  if(newstate == PGRES_POLLING_OK) {
    js_iohandler_set(ctx, data[1], fd, JS_NULL);
    int ret;

    if((ret = PQsetClientEncoding(pq->conn, "utf8"))) {
      printf("failed setting PGSQL character set to utf8: %s", PQerrorMessage(pq->conn));
    }

    JS_Call(ctx, data[2], JS_UNDEFINED, 1, &data[0]);
  } else if(newstate != oldstate) {
    JSValue handler, hdata[4] = {
                         JS_DupValue(ctx, data[0]),
                         js_iohandler_fn(ctx, newstate == PGRES_POLLING_WRITING),
                         JS_DupValue(ctx, data[2]),
                         JS_DupValue(ctx, data[3]),
                     };
    handler = JS_NewCFunctionData(ctx, js_pgconn_connect_cont, 0, newstate, countof(hdata), hdata);

    js_iohandler_set(ctx, data[1], fd, JS_NULL);
    js_iohandler_set(ctx, hdata[1], fd, handler);
    JS_FreeValue(ctx, handler);

    JS_FreeValue(ctx, hdata[0]);
    JS_FreeValue(ctx, hdata[1]);
    JS_FreeValue(ctx, hdata[2]);
    JS_FreeValue(ctx, hdata[3]);
  }

#ifdef DEBUG_OUTPUT
  printf("%s fd=%i newstate=%i pq=%p ret=%p error='%s'\n", __func__, fd, newstate, pq, pgconn_error(pq));
#endif

  return JS_UNDEFINED;
}

static JSValue
js_pgconn_connect_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue promise = JS_UNDEFINED, data[4], handler;
  PGSQLConnection* pq;
  int32_t fd;
  PostgresPollingStatusType ret;
  PGSQLConnectParameters params;

  if(!(pq = js_pgconn_data2(ctx, this_val)))
    return JS_EXCEPTION;

  connectparams_init(ctx, &params, argc, argv);

  pq->conn = PQconnectStartParams(params.keywords, params.values, 1);

  connectparams_free(ctx, &params);

  if(PQsetnonblocking(pq->conn, 1))
    return JS_Throw(ctx, js_pgsqlerror_new(ctx, PQerrorMessage(pq->conn)));

  fd = PQsocket(pq->conn);
  ret = PQconnectPoll(pq->conn);

#ifdef DEBUG_OUTPUT
  printf("%s ret=%d fd=%d" PRId32 "\n", __func__, ret, fd);
#endif

  promise = JS_NewPromiseCapability(ctx, &data[2]);

  data[0] = JS_DupValue(ctx, this_val);
  data[1] = js_iohandler_fn(ctx, ret == PGRES_POLLING_WRITING);

  if(ret == PGRES_POLLING_READING || ret == PGRES_POLLING_WRITING) {
    handler = JS_NewCFunctionData(ctx, js_pgconn_connect_cont, 0, ret, countof(data), data);

    if(!js_iohandler_set(ctx, data[1], fd, handler))
      JS_Call(ctx, data[3], JS_UNDEFINED, 0, 0);

    JS_FreeValue(ctx, handler);
  } else {
    js_pgconn_connect_cont(ctx, this_val, argc, argv, ret, data);
  }

  JS_FreeValue(ctx, data[0]);
  JS_FreeValue(ctx, data[1]);
  JS_FreeValue(ctx, data[2]);
  JS_FreeValue(ctx, data[3]);

  return promise;
}

static JSValue
js_pgconn_connect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  PGSQLConnection* pq;
  JSValue ret = JS_UNDEFINED;

  if(!(pq = js_pgconn_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!pgconn_nonblock(pq)) {

    PGSQLConnectParameters params;
    connectparams_init(ctx, &params, argc, argv);

    pq->conn = PQconnectdbParams(params.keywords, params.values, 1);

    connectparams_free(ctx, &params);

    ret = JS_NewBool(ctx, pq->conn != NULL);
  } else {
    ret = js_pgconn_connect_start(ctx, this_val, argc, argv);
  }

  return ret;
}

static JSValue
js_pgconn_query_cont(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue data[]) {
  int32_t fd;
  int ret = 0;
  PGSQLConnection* pq = 0;

  if(!(pq = js_pgconn_data2(ctx, data[0])))
    return JS_EXCEPTION;

  fd = PQsocket(pq->conn);
  ret = PQconsumeInput(pq->conn);

  if(ret == 0) {
    JSValue err = js_pgsqlerror_new(ctx, PQerrorMessage(pq->conn));
    JS_Call(ctx, data[3], JS_UNDEFINED, 1, &err);
    JS_FreeValue(ctx, err);
  } else {

    if(!PQisBusy(pq->conn)) {
      PGresult* res = PQgetResult(pq->conn);
      JSValue res_val = pgconn_result(pq, res, ctx);

      js_iohandler_set(ctx, data[1], fd, JS_NULL);

      JS_Call(ctx, data[2], JS_UNDEFINED, 1, &res_val);
      JS_FreeValue(ctx, res_val);
    }
  }

#ifdef DEBUG_OUTPUT
  printf("%s wantwrite=%i fd=%i newstate=%i pq=%p ret=%d error='%s'\n", __func__, wantwrite, fd, newstate, pq, ret, pgconn_error(pq));
#endif

  return JS_UNDEFINED;
}

static JSValue
js_pgconn_query_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue promise = JS_UNDEFINED, data[4], handler;
  const char* query = 0;
  PGSQLConnection* pq;
  int ret = 0, fd;

  if(!(pq = js_pgconn_data2(ctx, this_val)))
    return JS_EXCEPTION;

  query = JS_ToCString(ctx, argv[0]);
  ret = PQsendQuery(pq->conn, query);
  fd = PQsocket(pq->conn);

#ifdef DEBUG_OUTPUT
  printf("%s ret=%d query='%.*s'\n", __func__, ret, (int)query_len, query);
#endif

  promise = JS_NewPromiseCapability(ctx, &data[2]);

  if(ret == 0) {
    JSValue err = js_pgsqlerror_new(ctx, PQerrorMessage(pq->conn));
    JS_Call(ctx, data[3], JS_UNDEFINED, 1, &err);
    JS_FreeValue(ctx, err);
  } else {

    data[0] = JS_DupValue(ctx, this_val);
    data[1] = js_iohandler_fn(ctx, 0);

    if(PQisBusy(pq->conn)) {
      handler = JS_NewCFunctionData(ctx, js_pgconn_query_cont, 0, 0, countof(data), data);

      if(!js_iohandler_set(ctx, data[1], fd, handler))
        JS_Call(ctx, data[3], JS_UNDEFINED, 0, 0);

      JS_FreeValue(ctx, handler);
    } else {
      js_pgconn_query_cont(ctx, this_val, argc, argv, 0, data);
    }

    JS_FreeValue(ctx, data[0]);
    JS_FreeValue(ctx, data[1]);
  }

  JS_FreeValue(ctx, data[2]);
  JS_FreeValue(ctx, data[3]);

  return promise;
}

static JSValue
js_pgconn_query(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  PGSQLConnection* pq;

  if(!(pq = js_pgconn_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!pgconn_nonblock(pq)) {
    PGresult* res = 0;
    const char* query = 0;
    JSValue ret = JS_UNDEFINED;

    query = JS_ToCString(ctx, argv[0]);
    res = PQexec(pq->conn, query);

    ret = res ? pgconn_result(pq, res, ctx) : JS_NULL;

    if(res)
      JS_DefinePropertyValueStr(ctx, ret, "handle", JS_DupValue(ctx, this_val), JS_PROP_CONFIGURABLE);

    return ret;
  }

  return js_pgconn_query_start(ctx, this_val, argc, argv);
}

static JSValue
js_pgconn_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  PGSQLConnection* pq;

  if(!(pq = js_pgconn_data2(ctx, this_val)))
    return JS_EXCEPTION;

  PQfinish(pq->conn);
  pq->conn = 0;

  return ret;
}

static void
js_pgconn_finalizer(JSRuntime* rt, JSValue val) {
  PGSQLConnection* pq;

  if((pq = JS_GetOpaque(val, js_pgconn_class_id)))
    pgconn_free(pq, rt);
}

static JSClassDef js_pgconn_class = {
    .class_name = "PGconn",
    .finalizer = js_pgconn_finalizer,
};

static const JSCFunctionListEntry js_pgconn_funcs[] = {
    JS_CGETSET_MAGIC_DEF("cmdTuples", js_pgconn_get, 0, PROP_CMD_TUPLES),
    JS_CGETSET_MAGIC_DEF("affectedRows", js_pgconn_get, 0, PROP_CMD_TUPLES),

    JS_CGETSET_MAGIC_DEF("nonblocking", js_pgconn_get, js_pgconn_set, PROP_NONBLOCKING),
    JS_CGETSET_MAGIC_DEF("fd", js_pgconn_get, 0, PROP_FD),
    JS_CGETSET_MAGIC_DEF("errorMessage", js_pgconn_get, 0, PROP_ERROR_MESSAGE),
    JS_CGETSET_MAGIC_DEF("options", js_pgconn_get, 0, PROP_OPTIONS),
    JS_CGETSET_MAGIC_DEF("insertId", js_pgconn_get, 0, PROP_INSERT_ID),
    JS_CGETSET_MAGIC_DEF("charset", js_pgconn_get, js_pgconn_set, PROP_CLIENT_ENCODING),
    JS_CGETSET_MAGIC_DEF("protocolVersion", js_pgconn_get, 0, PROP_PROTOCOL_VERSION),
    JS_CGETSET_MAGIC_DEF("serverVersion", js_pgconn_get, 0, PROP_SERVER_VERSION),
    JS_CGETSET_MAGIC_DEF("user", js_pgconn_get, 0, PROP_USER),
    JS_CGETSET_MAGIC_DEF("password", js_pgconn_get, 0, PROP_PASSWORD),
    JS_CGETSET_MAGIC_DEF("host", js_pgconn_get, 0, PROP_HOST),
    JS_CGETSET_MAGIC_DEF("port", js_pgconn_get, 0, PROP_PORT),
    JS_CGETSET_MAGIC_DEF("db", js_pgconn_get, 0, PROP_DB),
    JS_CGETSET_MAGIC_DEF("conninfo", js_pgconn_get, 0, PROP_CONNINFO),
    JS_CFUNC_DEF("connect", 1, js_pgconn_connect),
    JS_CFUNC_DEF("query", 1, js_pgconn_query),
    JS_CFUNC_DEF("close", 0, js_pgconn_close),
    JS_ALIAS_DEF("execute", "query"),
    JS_CFUNC_DEF("escapeString", 1, js_pgconn_escape_string),
    JS_CFUNC_MAGIC_DEF("escapeLiteral", 1, js_pgconn_escape_alloc, 0),
    JS_CFUNC_MAGIC_DEF("escapeIdentifier", 1, js_pgconn_escape_alloc, 1),
    JS_CFUNC_DEF("escapeBytea", 1, js_pgconn_escape_bytea),
    JS_CFUNC_DEF("unescapeBytea", 1, js_pgconn_unescape_bytea),
    JS_CFUNC_DEF("valueString", 0, js_pgconn_value_string),
    JS_CFUNC_DEF("valuesString", 1, js_pgconn_values_string),
    JS_CFUNC_DEF("insertQuery", 2, js_pgconn_insert_query),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PGconn", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_pgconn_static[] = {
    JS_CFUNC_DEF("escapeString", 1, js_pgconn_escape_string),
    JS_CFUNC_DEF("escapeBytea", 1, js_pgconn_escape_bytea),
    JS_CFUNC_DEF("unescapeBytea", 1, js_pgconn_unescape_bytea),
};

static const JSCFunctionListEntry js_pgconn_defines[] = {
    JS_PROP_INT32_DEF("RESULT_OBJECT", RESULT_OBJECT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("RESULT_STRING", RESULT_STRING, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("RESULT_TABLENAME", RESULT_TABLENAME, JS_PROP_CONFIGURABLE),
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
    .class_name = "PGerror",
};

static const JSCFunctionListEntry js_pgsqlerror_funcs[] = {
    JS_PROP_STRING_DEF("name", "PGerror", JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("type", 0, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PGerror", JS_PROP_CONFIGURABLE),
};

static void
result_free(JSRuntime* rt, void* opaque, void* ptr) {
  PQfreemem(ptr);
}

static JSValue
result_value(JSContext* ctx, PGSQLResult* opaque, int field, char* buf, size_t len, int rtype) {
  PGresult* res = opaque->result;
  JSValue ret = JS_UNDEFINED;

  if(buf == 0)
    return (rtype & RESULT_STRING) ? JS_NewString(ctx, "NULL") : JS_NULL;

  if(field_is_boolean(res, field)) {
    BOOL value = *(BOOL*)buf;

    if((rtype & RESULT_STRING))
      return JS_NewString(ctx, value ? "1" : "0");

    return JS_NewBool(ctx, value);
  }

  if(!(rtype & RESULT_STRING)) {
    if(field_is_number(res, field))
      return string_to_number(ctx, buf);
    if(field_is_date(res, field)) {
      DynBuf tmp;
      dbuf_init2(&tmp, 0, 0);
      dbuf_putstr(&tmp, buf);

      if(tmp.size > 10 && tmp.buf[10] == ' ')
        tmp.buf[10] = 'T';

      if(tmp.size >= 3 && tmp.buf[tmp.size - 3] == '+')
        dbuf_putstr(&tmp, ":00");

      dbuf_0(&tmp);

      ret = string_to_date(ctx, (const char*)tmp.buf);
      dbuf_free(&tmp);

      return ret;
    }
  }

  if(!(rtype & RESULT_STRING)) {
    if(field_is_json(res, field))
      return JS_ParseJSON(ctx, buf, strlen(buf), 0);
  }

  if(field_is_binary(res, field)) {
    if((rtype & RESULT_STRING))
      return JS_NewStringLen(ctx, buf, len);

    unsigned char* dst;
    size_t dlen;

    if((dst = PQunescapeBytea((const unsigned char*)buf, &dlen)))
      return JS_NewArrayBuffer(ctx, (uint8_t*)dst, dlen, &result_free, 0, FALSE);
  }

  ret = JS_NewString(ctx, buf);

  return ret;
}

static JSValue
result_array(JSContext* ctx, PGSQLResult* opaque, int row, int rtype) {
  PGresult* res = opaque->result;
  JSValue ret = JS_NewArray(ctx);
  uint32_t i, num_fields = PQnfields(res);

  for(i = 0; i < num_fields; i++) {
    int len = PQgetlength(res, row, i);
    char* col = PQgetisnull(res, row, i) ? NULL : PQgetvalue(res, row, i);
#ifdef DEBUG_OUTPUT_
    printf("%s num_fields=%" PRIu32 " row[%" PRIu32 "] = '%.*s'\n", __func__, num_fields, i, (int)(len > 32 ? 32 : len), col);
#endif
    JS_SetPropertyUint32(ctx, ret, i, result_value(ctx, opaque, i, col, len, rtype));
  }

  return ret;
}

static JSValue
result_object(JSContext* ctx, PGSQLResult* opaque, int row, int rtype) {
  PGresult* res = opaque->result;
  JSValue ret = JS_NewObject(ctx);
  uint32_t i, num_fields = PQnfields(res);
  FieldNameFunc* fn = (rtype & RESULT_TABLENAME) ? field_id : field_namefunc(res);

  for(i = 0; i < num_fields; i++) {
    char* id;

    if((id = fn(ctx, opaque, i))) {
      int len = PQgetlength(res, row, i);
      char* col = PQgetisnull(res, row, i) ? NULL : PQgetvalue(res, row, i);

      JS_SetPropertyStr(ctx, ret, id, result_value(ctx, opaque, i, col, len, rtype));
      js_free(ctx, id);
    }
  }

  return ret;
}

static JSValue
result_row(JSContext* ctx, PGSQLResult* opaque, int row, int rtype) {
  PGresult* res = opaque->result;
  int rows = PQntuples(res);
  RowValueFunc* row_func = (rtype & RESULT_OBJECT) ? result_object : result_array;
  JSValue val = row >= 0 && row < rows ? row_func(ctx, opaque, row, rtype) : JS_NULL;
  return val;
}

static JSValue
result_iterate(JSContext* ctx, PGSQLResult* opaque, int row, int rtype) {
  PGresult* res = opaque->result;
  int rows = PQntuples(res);
  JSValue ret, val = result_row(ctx, opaque, row, rtype);
  ret = js_iterator_result(ctx, val, row >= 0 && row < rows ? FALSE : TRUE);
  JS_FreeValue(ctx, val);
  return ret;
}

/*
static void
result_yield(JSContext* ctx, JSValueConst func, PGSQLResult* opaque, int row, int rtype) {
  int rows = PQntuples(opaque->result);
  JSValue val = result_row(ctx, opaque, row, rtype);

  JSValue item = js_iterator_result(ctx, val, row >= 0 && row < rows ? FALSE : TRUE);
  JS_FreeValue(ctx, val);

  value_yield_free(ctx, func, item);
}

static void
result_resolve(JSContext* ctx, JSValueConst func, PGSQLResult* opaque, int row, int rtype) {
  JSValue value = result_row(ctx, opaque, row, rtype);

  value_yield_free(ctx, func, value);
}*/

static PGSQLResult*
pgresult_new(JSContext* ctx) {

  PGSQLResult* opaque;

  if(!(opaque = js_malloc(ctx, sizeof(PGSQLResult))))
    return 0;

  *opaque = (PGSQLResult){1, NULL, NULL, 0};

  return opaque;
}

static PGSQLResult*
pgresult_dup(PGSQLResult* ptr) {
  ++ptr->ref_count;
  return ptr;
}

static void
pgresult_free(JSRuntime* rt, void* ptr, void* mem) {
  PGSQLResult* opaque = ptr;
  if(--opaque->ref_count == 0) {
    if(opaque->result) {
      PQclear(opaque->result);
      opaque->result = 0;
    }
    if(opaque->conn) {
      pgconn_free(opaque->conn, rt);
      opaque->conn = 0;
    }
    js_free_rt(rt, opaque);
  }
}

static JSValue
pgresult_row(PGSQLResult* opaque, uint32_t row, RowValueFunc* fn, JSContext* ctx) {
  PGresult* res = opaque->result;
  uint32_t i, cols = PQnfields(res);
  JSValue ret /* = JS_NewArray(ctx)*/;

  ret = fn(ctx, opaque, row, 0);

  return ret;
}

static int64_t
pgresult_cmdtuples(PGSQLResult* opaque) {
  int64_t ret = -1;
  if(opaque->result) {
    char* cmdtuples = PQcmdTuples(opaque->result);
    scan_longlong(cmdtuples, &ret);
  }
  return ret;
}

static void
pgresult_set_conn(PGSQLResult* opaque, PGSQLConnection* conn, JSContext* ctx) {
  if(opaque->conn) {
    pgconn_free(opaque->conn, JS_GetRuntime(ctx));
    opaque->conn = 0;
  }
  opaque->conn = conn ? pgconn_dup(conn) : 0;
}

PGSQLResult*
js_pgresult_opaque2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_pgresult_class_id);
}

PGresult*
js_pgresult_data2(JSContext* ctx, JSValueConst value) {
  PGSQLResult* opaque;

  if((opaque = JS_GetOpaque2(ctx, value, js_pgresult_class_id)))
    return opaque->result;

  return 0;
}

enum {
  METHOD_NEXT,
  METHOD_FETCH_FIELD,
  METHOD_FETCH_FIELDS,
  METHOD_FETCH_ROW,
  METHOD_FETCH_ASSOC,
};

static JSValue
js_pgresult_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, BOOL* pdone, int magic) {
  PGSQLResult* opaque;
  uint32_t ntuples;
  JSValue ret = JS_NULL;
  int rtype = 0;

  if(!(opaque = js_pgresult_opaque2(ctx, this_val)))
    return JS_EXCEPTION;

  ntuples = PQntuples(opaque->result);

  switch(magic) {

    case METHOD_FETCH_ASSOC: rtype |= RESULT_OBJECT; break;
    case METHOD_FETCH_ROW:
    default: rtype |= 0; break;
  }

  if(opaque->row_index < ntuples) {
    ret = result_row(ctx, opaque, opaque->row_index++, rtype);
    *pdone = FALSE;
  } else {
    *pdone = TRUE;
  }

  return ret;
}

static JSValue
js_pgresult_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  PGresult* res;

  if(!(res = js_pgresult_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_FETCH_FIELD: {
      uint32_t index;

      if(JS_ToUint32(ctx, &index, argv[0]))
        return JS_ThrowTypeError(ctx, "argument 1 must be a positive index");

      if(index >= PQnfields(res))
        return JS_ThrowRangeError(ctx, "argument 1 must be smaller than total fields (%" PRIu32 ")", PQnfields(res));

      ret = field_array(JS_GetOpaque(this_val, js_pgresult_class_id), index, ctx);

      break;
    }
    case METHOD_FETCH_FIELDS: {
      uint32_t i, num_fields = PQnfields(res);

      ret = JS_NewArray(ctx);

      for(i = 0; i < num_fields; i++)
        JS_SetPropertyUint32(ctx, ret, i, field_array(JS_GetOpaque(this_val, js_pgresult_class_id), i, ctx));
      break;
    }
    case METHOD_FETCH_ROW:
    case METHOD_FETCH_ASSOC: {
      BOOL done = FALSE;

      ret = js_pgresult_next(ctx, this_val, argc, argv, &done, magic);
      break;
    }
  }

  return ret;
}

static JSValue
js_pgresult_new(JSContext* ctx, JSValueConst proto, PGresult* res) {
  JSValue obj;
  PGSQLResult* opaque;

  if(!(opaque = pgresult_new(ctx)))
    return JS_EXCEPTION;

  if(js_pgresult_class_id == 0)
    js_pgsql_init(ctx, 0);

  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = pgresult_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_pgresult_class_id);
  if(JS_IsException(obj))
    goto fail;

  opaque->result = res;

  JS_SetOpaque(obj, opaque);

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

  if(!(res = js_pgresult_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_EOF: {
      PGSQLResult* opaque;

      if((opaque = JS_GetOpaque(this_val, js_pgresult_class_id)))
        ret = JS_NewBool(ctx, opaque->row_index == PQntuples(res));
      break;
    }
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

static JSValue
js_pgresult_iterator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  PGSQLResultIterator* closure = ptr;
  PGSQLResult* opaque = closure->result;
  int rows = PQntuples(opaque->result);
  JSValue ret;

  ret = result_iterate(ctx, opaque, closure->row_index, 0);

  if(closure->row_index < rows)
    ++closure->row_index;

  return ret;
}

static void
js_pgresult_iterator_free(void* ptr) {
  PGSQLResultIterator* closure = ptr;

  pgresult_free(JS_GetRuntime(closure->ctx), closure->result, 0);
}

static JSValue
js_pgresult_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_NewObject(ctx);
  PGSQLResultIterator* closure;
  PGSQLResult* opaque;

  if(!(opaque = js_pgresult_opaque2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(closure = js_malloc(ctx, sizeof(PGSQLResultIterator))))
    return JS_EXCEPTION;

  *closure = (PGSQLResultIterator){ctx, pgresult_dup(opaque), 0};

  JS_SetPropertyStr(ctx, ret, "next", js_function_cclosure(ctx, js_pgresult_iterator_next, 0, 0, closure, js_pgresult_iterator_free));

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

static int
js_pgresult_get_own_property(JSContext* ctx, JSPropertyDescriptor* pdesc, JSValueConst obj, JSAtom prop) {
  PGresult* res = js_pgresult_data2(ctx, obj);
  JSValue value = JS_UNDEFINED;
  int64_t index;
  size_t num = PQntuples(res);

  if(js_atom_is_index(ctx, &index, prop)) {
    if(index < 0)
      index = ((index % num) + num) % num;

    if(index < (int64_t)num) {
      value = pgresult_row(JS_GetOpaque(obj, js_pgresult_class_id), index, &result_array, ctx);

      if(pdesc) {
        pdesc->flags = 0;
        pdesc->value = value;
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
      }
      return TRUE;
    }
  }
  return FALSE;
}

static void
js_pgresult_finalizer(JSRuntime* rt, JSValue val) {
  PGSQLResult* opaque;

  if((opaque = JS_GetOpaque(val, js_pgresult_class_id)))
    pgresult_free(rt, opaque, 0);
}

static JSClassExoticMethods js_pgresult_exotic_methods = {
    .get_own_property = js_pgresult_get_own_property,
};

static JSClassDef js_pgresult_class = {
    .class_name = "PGresult",
    .finalizer = js_pgresult_finalizer,
    .exotic = &js_pgresult_exotic_methods,
};

static const JSCFunctionListEntry js_pgresult_funcs[] = {
    JS_CGETSET_MAGIC_DEF("eof", js_pgresult_get, 0, PROP_EOF),
    // JS_ITERATOR_NEXT_DEF("next", 0, js_pgresult_next, METHOD_NEXT),
    JS_CGETSET_MAGIC_FLAGS_DEF("numRows", js_pgresult_get, 0, PROP_NUM_ROWS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("numFields", js_pgresult_get, 0, PROP_NUM_FIELDS, JS_PROP_ENUMERABLE),
    JS_CFUNC_MAGIC_DEF("fetchField", 1, js_pgresult_functions, METHOD_FETCH_FIELD),
    JS_CFUNC_MAGIC_DEF("fetchFields", 0, js_pgresult_functions, METHOD_FETCH_FIELDS),
    JS_CFUNC_MAGIC_DEF("fetchRow", 0, js_pgresult_functions, METHOD_FETCH_ROW),
    JS_CFUNC_MAGIC_DEF("fetchAssoc", 0, js_pgresult_functions, METHOD_FETCH_ASSOC),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_pgresult_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PGresult", JS_PROP_CONFIGURABLE),
};

int
js_pgsql_init(JSContext* ctx, JSModuleDef* m) {
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

    pgsqlerror_ctor = JS_NewCFunction2(ctx, js_pgsqlerror_constructor, "PGerror", 1, JS_CFUNC_constructor, 0);
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
    JS_SetModuleExport(ctx, m, "PGerror", pgsqlerror_ctor);
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
  if(!(m = JS_NewCModule(ctx, module_name, &js_pgsql_init)))
    return m;
  JS_AddModuleExport(ctx, m, "PGconn");
  JS_AddModuleExport(ctx, m, "PGerror");
  JS_AddModuleExport(ctx, m, "PGresult");
  return m;
}

static char*
field_id(JSContext* ctx, PGSQLResult* opaque, int field) {
  PGresult* res = opaque->result;
  DynBuf buf;

  Oid table = PQftable(res, field);
  char* table_name;
  dbuf_init2(&buf, 0, 0);

  if((table_name = pgconn_lookup_oid(opaque->conn, table, ctx))) {
    dbuf_putstr(&buf, table_name);
    dbuf_putc(&buf, '.');
  }
  dbuf_putstr(&buf, PQfname(res, field));
  dbuf_0(&buf);

  return (char*)buf.buf;
}

static char*
field_name(JSContext* ctx, PGSQLResult* opaque, int field) {
  return js_strdup(ctx, PQfname(opaque->result, field));
}

static FieldNameFunc*
field_namefunc(PGresult* res) {
  uint32_t i, j, num_fields = PQnfields(res);
  BOOL eq = FALSE;

  for(i = 0; !eq && i < num_fields; i++)
    for(j = 0; !eq && j < num_fields; j++)
      if(i != j && !strcmp(PQfname(res, i), PQfname(res, j)))
        return field_id;

  return field_name;
}

static const char*
field_type(PGresult* res, int field) {
  const char* type = 0;
  switch(PQftype(res, field)) {
    case 16: type = "bool"; break;
    case 17: type = "bytea"; break;
    case 18: type = "char"; break;
    case 19: type = "name"; break;
    case 20: type = "int8"; break;
    case 21: type = "int2"; break;
    case 22: type = "int2vector"; break;
    case 23: type = "int4"; break;
    case 24: type = "regproc"; break;
    case 25: type = "text"; break;
    case 26: type = "oid"; break;
    case 27: type = "tid"; break;
    case 28: type = "xid"; break;
    case 29: type = "cid"; break;
    case 30: type = "oidvector"; break;
    case 114: type = "json"; break;
    case 142: type = "xml"; break;
    case 194: type = "pgnodetree"; break;
    case 3361: type = "pgndistinct"; break;
    case 3402: type = "pgdependencies"; break;
    case 5017: type = "pgmcvlist"; break;
    case 32: type = "pgddlcommand"; break;
    case 600: type = "point"; break;
    case 601: type = "lseg"; break;
    case 602: type = "path"; break;
    case 603: type = "box"; break;
    case 604: type = "polygon"; break;
    case 628: type = "line"; break;
    case 700: type = "float4"; break;
    case 701: type = "float8"; break;
    case 705: type = "unknown"; break;
    case 718: type = "circle"; break;
    case 790: type = "cash"; break;
    case 829: type = "macaddr"; break;
    case 869: type = "inet"; break;
    case 650: type = "cidr"; break;
    case 774: type = "macaddr8"; break;
    case 1033: type = "aclitem"; break;
    case 1042: type = "bpchar"; break;
    case 1043: type = "varchar"; break;
    case 1082: type = "date"; break;
    case 1083: type = "time"; break;
    case 1114: type = "timestamp"; break;
    case 1184: type = "timestamptz"; break;
    case 1186: type = "interval"; break;
    case 1266: type = "timetz"; break;
    case 1560: type = "bit"; break;
    case 1562: type = "varbit"; break;
    case 1700: type = "numeric"; break;
    case 1790: type = "refcursor"; break;
    case 2202: type = "regprocedure"; break;
    case 2203: type = "regoper"; break;
    case 2204: type = "regoperator"; break;
    case 2205: type = "regclass"; break;
    case 2206: type = "regtype"; break;
    case 4096: type = "regrole"; break;
    case 4089: type = "regnamespace"; break;
    case 2950: type = "uuid"; break;
    case 3220: type = "lsn"; break;
    case 3614: type = "tsvector"; break;
    case 3642: type = "gtsvector"; break;
    case 3615: type = "tsquery"; break;
    case 3734: type = "regconfig"; break;
    case 3769: type = "regdictionary"; break;
    case 3802: type = "jsonb"; break;
    case 4072: type = "jsonpath"; break;
    case 2970: type = "txid_snapshot"; break;
    case 3904: type = "int4range"; break;
    case 3906: type = "numrange"; break;
    case 3908: type = "tsrange"; break;
    case 3910: type = "tstzrange"; break;
    case 3912: type = "daterange"; break;
    case 3926: type = "int8range"; break;
    case 2249: type = "record"; break;
    case 2287: type = "recordarray"; break;
    case 2275: type = "cstring"; break;
    case 2276: type = "any"; break;
    case 2277: type = "anyarray"; break;
    case 2278: type = "void"; break;
    case 2279: type = "trigger"; break;
    case 3838: type = "evttrigger"; break;
    case 2280: type = "language_handler"; break;
    case 2281: type = "internal"; break;
    case 2282: type = "opaque"; break;
    case 2283: type = "anyelement"; break;
    case 2776: type = "anynonarray"; break;
    case 3500: type = "anyenum"; break;
    case 3115: type = "fdw_handler"; break;
    case 325: type = "index_am_handler"; break;
    case 3310: type = "tsm_handler"; break;
    case 269: type = "table_am_handler"; break;
    case 3831: type = "anyrange"; break;
    case 1000: type = "boolarray"; break;
    case 1001: type = "byteaarray"; break;
    case 1002: type = "chararray"; break;
    case 1003: type = "namearray"; break;
    case 1016: type = "int8array"; break;
    case 1005: type = "int2array"; break;
    case 1006: type = "int2vectorarray"; break;
    case 1007: type = "int4array"; break;
    case 1008: type = "regprocarray"; break;
    case 1009: type = "textarray"; break;
    case 1028: type = "oidarray"; break;
    case 1010: type = "tidarray"; break;
    case 1011: type = "xidarray"; break;
    case 1012: type = "cidarray"; break;
    case 1013: type = "oidvectorarray"; break;
    case 199: type = "jsonarray"; break;
    case 143: type = "xmlarray"; break;
    case 1017: type = "pointarray"; break;
    case 1018: type = "lsegarray"; break;
    case 1019: type = "patharray"; break;
    case 1020: type = "boxarray"; break;
    case 1027: type = "polygonarray"; break;
    case 629: type = "linearray"; break;
    case 1021: type = "float4array"; break;
    case 1022: type = "float8array"; break;
    case 719: type = "circlearray"; break;
    case 791: type = "moneyarray"; break;
    case 1040: type = "macaddrarray"; break;
    case 1041: type = "inetarray"; break;
    case 651: type = "cidrarray"; break;
    case 775: type = "macaddr8array"; break;
    case 1034: type = "aclitemarray"; break;
    case 1014: type = "bpchararray"; break;
    case 1015: type = "varchararray"; break;
    case 1182: type = "datearray"; break;
    case 1183: type = "timearray"; break;
    case 1115: type = "timestamparray"; break;
    case 1185: type = "timestamptzarray"; break;
    case 1187: type = "intervalarray"; break;
    case 1270: type = "timetzarray"; break;
    case 1561: type = "bitarray"; break;
    case 1563: type = "varbitarray"; break;
    case 1231: type = "numericarray"; break;
    case 2201: type = "refcursorarray"; break;
    case 2207: type = "regprocedurearray"; break;
    case 2208: type = "regoperarray"; break;
    case 2209: type = "regoperatorarray"; break;
    case 2210: type = "regclassarray"; break;
    case 2211: type = "regtypearray"; break;
    case 4097: type = "regrolearray"; break;
    case 4090: type = "regnamespacearray"; break;
    case 2951: type = "uuidarray"; break;
    case 3221: type = "pg_lsnarray"; break;
    case 3643: type = "tsvectorarray"; break;
    case 3644: type = "gtsvectorarray"; break;
    case 3645: type = "tsqueryarray"; break;
    case 3735: type = "regconfigarray"; break;
    case 3770: type = "regdictionaryarray"; break;
    case 3807: type = "jsonbarray"; break;
    case 4073: type = "jsonpatharray"; break;
    case 2949: type = "txid_snapshotarray"; break;
    case 3905: type = "int4rangearray"; break;
    case 3907: type = "numrangearray"; break;
    case 3909: type = "tsrangearray"; break;
    case 3911: type = "tstzrangearray"; break;
    case 3913: type = "daterangearray"; break;
    case 3927: type = "int8rangearray"; break;
    default: break;
  }
  return type;
}

static JSValue
field_array(PGSQLResult* opaque, int field, JSContext* ctx) {
  PGresult* res = opaque->result;
  JSValue ret = JS_NewArray(ctx);
  const char* type = 0;
  DynBuf buf;
  FieldNameFunc* fn = field_namefunc(res);
  char* name;

  dbuf_init2(&buf, 0, 0);

  name = fn(ctx, opaque, field);
  JS_SetPropertyUint32(ctx, ret, 0, name ? JS_NewString(ctx, name) : JS_NULL);
  js_free(ctx, name);

  type = field_type(res, field);

  dbuf_putstr(&buf, type);

  JS_SetPropertyUint32(ctx, ret, 1, JS_NewStringLen(ctx, (const char*)buf.buf, buf.size));

  dbuf_free(&buf);

  int size = PQfsize(res, field);
  JS_SetPropertyUint32(ctx, ret, 2, size >= 0 ? JS_NewInt32(ctx, size) : JS_NULL);
  int modifier = PQfmod(res, field);
  JS_SetPropertyUint32(ctx, ret, 3, modifier >= 0 ? JS_NewInt32(ctx, modifier) : JS_NULL);
  JS_SetPropertyUint32(ctx, ret, 4, JS_NewString(ctx, field_is_binary(res, field) ? "binary" : "text"));
  Oid table = PQftable(res, field);

  char* table_name = pgconn_lookup_oid(opaque->conn, table, ctx);

  JS_SetPropertyUint32(ctx, ret, 5, table_name ? JS_NewString(ctx, table_name) : JS_NULL);

  if(table_name)
    js_free(ctx, table_name);

  return ret;
}

static BOOL
field_is_integer(PGresult* res, int field) {
  const char* type = field_type(res, field);
  if(str_start(type, "int")) {
    unsigned short n;
    size_t i = 3, j;
    j = scan_ushort(&type[i], &n);

    if(j > 0) {
      i += j;
      return type[i] == '\0';
    }
  }
  return FALSE;
}

static BOOL
field_is_json(PGresult* res, int field) {
  return !strcmp("json", field_type(res, field));
}

static BOOL
field_is_float(PGresult* res, int field) {
  const char* type = field_type(res, field);
  if(str_start(type, "float")) {
    unsigned short n;
    size_t i = 5, j;
    j = scan_ushort(&type[i], &n);
    if(j > 0) {
      i += j;
      return type[i] == '\0';
    }
  }
  return FALSE;
}

static BOOL
field_is_number(PGresult* res, int field) {
  return field_is_integer(res, field) || field_is_float(res, field);
}

static BOOL
field_is_binary(PGresult* res, int field) {
  return PQfformat(res, field) || !strcmp("bytea", field_type(res, field));
}

static BOOL
field_is_boolean(PGresult* res, int field) {
  return !strcmp(field_type(res, field), "bool");
}

static BOOL
field_is_null(PGresult* res, int field) {
  return !strcmp(field_type(res, field), "void");
}

static BOOL
field_is_date(PGresult* res, int field) {
  const char* type = field_type(res, field);

  if(!strcmp(type, "date"))
    return TRUE;
  if(!strcmp(type, "time"))
    return TRUE;
  if(!strcmp(type, "timestamp"))
    return TRUE;
  if(!strcmp(type, "timestamptz"))
    return TRUE;
  if(!strcmp(type, "interval"))
    return TRUE;
  if(!strcmp(type, "timetz"))
    return TRUE;
  return FALSE;
}

static BOOL
field_is_string(PGresult* res, int field) {
  const char* type = field_type(res, field);

  if(!strcmp(type, "varchar"))
    return TRUE;
  if(!strcmp(type, "bytea"))
    return TRUE;

  return FALSE;
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

/**
 * @}
 */
