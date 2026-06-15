#include "defines.h"
#include "quickjs-sqlite.h"
#include "utils.h"
#include "buffer-utils.h"
#include "char-utils.h"
#include "js-utils.h"
#include "iteration.h"
#include "property-enumeration.h"

/**
 * \addtogroup quickjs-sqlite
 * @{
 */

VISIBLE JSClassID js_sqliteerror_class_id = 0, js_sqlite_class_id = 0, js_sqliteresult_class_id = 0;
static JSValue sqliteerror_proto, sqliteerror_ctor, sqlite_proto, sqlite_ctor, sqliteresult_proto, sqliteresult_ctor;

static JSValue js_sqliteerror_new(JSContext* ctx, const char* msg);

struct SQLiteConnection;
struct SQLiteResult;

struct SQLiteResult {
  int ref_count;
  sqlite3_stmt* stmt;
  struct SQLiteConnection* conn;
  uint32_t row_index;
  BOOL done;
  BOOL has_columns;
};

struct SQLiteResultIterator {
  JSContext* ctx;
  struct SQLiteResult* result;
};

struct SQLiteConnection {
  int ref_count;
  sqlite3* db;
  struct SQLiteResult* result;
};

typedef struct SQLiteConnection SQLiteConnection;
typedef struct SQLiteResult SQLiteResult;
typedef struct SQLiteResultIterator SQLiteResultIterator;

typedef JSValue RowValueFunc(JSContext*, SQLiteResult*, int);

enum ResultFlags {
  RESULT_OBJECT = 1,
  RESULT_STRING = 2,
};

static SQLiteResult* sqliteresult_dup(SQLiteResult*);
static void sqliteresult_free(JSRuntime*, void*, void*);
static void sqliteresult_set_conn(SQLiteResult*, SQLiteConnection*, JSContext*);
static JSValue js_sqliteresult_new(JSContext*, JSValueConst, sqlite3_stmt*);

typedef void SQLitePrintFunction(JSContext*, SQLiteConnection*, DynBuf*, JSValueConst);

static void
js_sqlite_print_value(JSContext* ctx, SQLiteConnection* db, DynBuf* out, JSValueConst value) {

  if(JS_IsNull(value) || JS_IsUndefined(value) || js_is_nan(value)) {
    dbuf_putstr(out, "NULL");
  } else if(JS_IsBool(value)) {
    dbuf_putstr(out, JS_ToBool(ctx, value) ? "1" : "0");
  } else if(JS_IsString(value)) {
    size_t len;
    const char* src = JS_ToCStringLen(ctx, &len, value);
    char* dst = sqlite3_mprintf("%Q", src);

    if(dst) {
      dbuf_putstr(out, dst);
      sqlite3_free(dst);
    }

    JS_FreeCString(ctx, src);
  } else if(js_is_date(ctx, value)) {
    size_t len;
    char* str;
    JSValue val;

    val = js_invoke(ctx, value, "toISOString", 0, 0);
    str = js_tostringlen(ctx, &len, val);

    if(len >= 24)
      if(str[23] == 'Z')
        len = 23;

    if(len >= 19)
      if(str[10] == 'T')
        str[10] = ' ';

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
    JS_FreeCString(ctx, src);
    JS_FreeValue(ctx, val);
  } else if(js_is_arraybuffer(ctx, value)) {
    InputBuffer input = js_input_buffer(ctx, value);
    char buf[FMT_XLONG];

    dbuf_putstr(out, "X'");
    for(size_t i = 0; i < input.size; i++)
      dbuf_put(out, (const uint8_t*)buf, fmt_xlong0(buf, input.data[i], 2));

    dbuf_putc(out, '\'');

    inputbuffer_free(&input, ctx);
  } else if(JS_IsException(value)) {
    dbuf_putstr(out, "[exception]");
  } else {
    JSValue str = JS_JSONStringify(ctx, value, JS_NULL, JS_NULL);

    js_sqlite_print_value(ctx, db, out, str);
    JS_FreeValue(ctx, str);
  }
}

static void
js_sqlite_print_field(JSContext* ctx, SQLiteConnection* db, DynBuf* out, JSValueConst value) {
  const char* name;
  size_t namelen;

  if((name = JS_ToCStringLen(ctx, &namelen, value))) {
    char* escaped = sqlite3_mprintf("\"%w\"", name);

    if(escaped) {
      dbuf_putstr(out, escaped);
      sqlite3_free(escaped);
    }

    JS_FreeCString(ctx, name);
  }
}

static void
js_sqlite_print_iterable(JSContext* ctx, SQLiteConnection* db, DynBuf* out, JSValueConst iterable, SQLitePrintFunction* fn) {
  Iteration iter = ITERATION_INIT();
  int i = 0;

  if(!iteration_method_symbol(&iter, ctx, iterable, "iterator"))
    JS_ThrowTypeError(ctx, "value is not iterable");

  dbuf_putc(out, '(');

  while(!iteration_next(&iter, ctx)) {
    JSValue item = iteration_value(&iter, ctx);

    if(i++ > 0)
      dbuf_putstr(out, ", ");

    fn(ctx, db, out, item);
    JS_FreeValue(ctx, item);
  }

  dbuf_putc(out, ')');
}

static void
js_sqlite_print_values(JSContext* ctx, SQLiteConnection* db, DynBuf* out, JSValueConst values) {
  js_sqlite_print_iterable(ctx, db, out, values, js_sqlite_print_value);
}

static SQLiteConnection*
sqlite_new(JSContext* ctx) {
  SQLiteConnection* db;

  if(!(db = js_malloc(ctx, sizeof(SQLiteConnection))))
    return 0;

  *db = (SQLiteConnection){1, NULL, NULL};

  return db;
}

static SQLiteConnection*
sqlite_dup(SQLiteConnection* db) {
  ++db->ref_count;
  return db;
}

static void
sqlite_free(SQLiteConnection* db, JSRuntime* rt) {
  if(--db->ref_count == 0) {
    if(db->result) {
      sqliteresult_free(rt, db->result, 0);
      db->result = 0;
    }

    if(db->db) {
      sqlite3_close_v2(db->db);
      db->db = 0;
    }

    js_free_rt(rt, db);
  }
}

static const char*
sqlite_error(SQLiteConnection* db) {
  return db->db ? sqlite3_errmsg(db->db) : "no database";
}

static void
sqlite_set_result(SQLiteConnection* db, SQLiteResult* opaque, JSContext* ctx) {
  if(db->result) {
    sqliteresult_free(JS_GetRuntime(ctx), db->result, 0);
    db->result = 0;
  }

  db->result = opaque ? sqliteresult_dup(opaque) : 0;
}

static JSValue
sqlite_result(SQLiteConnection* db, sqlite3_stmt* stmt, JSContext* ctx) {
  JSValue value = js_sqliteresult_new(ctx, sqliteresult_proto, stmt);
  SQLiteResult* opaque = JS_GetOpaque(value, js_sqliteresult_class_id);

  sqlite_set_result(db, opaque, ctx);
  sqliteresult_set_conn(opaque, db, ctx);

  return value;
}

SQLiteConnection*
js_sqlite_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_sqlite_class_id);
}

sqlite3*
js_sqlite_data(JSContext* ctx, JSValueConst value) {
  SQLiteConnection* db;

  if((db = JS_GetOpaque2(ctx, value, js_sqlite_class_id)))
    return db->db;

  return 0;
}

static JSValue
js_sqlite_new(JSContext* ctx, JSValueConst proto) {
  JSValue obj;

  if(js_sqlite_class_id == 0)
    js_sqlite_init(ctx, 0);

  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = JS_DupValue(ctx, sqlite_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_sqlite_class_id);
  if(JS_IsException(obj))
    return JS_EXCEPTION;

  return obj;
}

static JSValue
js_sqlite_wrap(JSContext* ctx, JSValueConst proto, sqlite3* conn) {
  SQLiteConnection* db;
  JSValue ret = JS_UNDEFINED;

  if(!(db = sqlite_new(ctx)))
    return JS_EXCEPTION;

  db->db = conn;

  if(!JS_IsException((ret = js_sqlite_new(ctx, proto))))
    JS_SetOpaque(ret, db);

  return ret;
}

enum {
  PROP_AFFECTED_ROWS,
  PROP_INSERT_ID,
  PROP_TOTAL_CHANGES,
  PROP_ERROR_MESSAGE,
  PROP_ERROR_CODE,
  PROP_FILENAME,
  PROP_LIBVERSION,
  PROP_EOF,
  PROP_NUM_ROWS,
  PROP_NUM_FIELDS,
};

static JSValue
js_sqlite_get(JSContext* ctx, JSValueConst this_val, int magic) {
  SQLiteConnection* db;
  JSValue ret = JS_UNDEFINED;

  if(!(db = js_sqlite_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_AFFECTED_ROWS: {
      if(db->db)
        ret = JS_NewInt32(ctx, sqlite3_changes(db->db));
      break;
    }

    case PROP_INSERT_ID: {
      if(db->db)
        ret = JS_NewInt64(ctx, sqlite3_last_insert_rowid(db->db));
      break;
    }

    case PROP_TOTAL_CHANGES: {
      if(db->db)
        ret = JS_NewInt32(ctx, sqlite3_total_changes(db->db));
      break;
    }

    case PROP_ERROR_MESSAGE: {
      const char* error = db->db ? sqlite3_errmsg(db->db) : 0;
      ret = error && *error ? JS_NewString(ctx, error) : JS_NULL;
      break;
    }

    case PROP_ERROR_CODE: {
      if(db->db)
        ret = JS_NewInt32(ctx, sqlite3_errcode(db->db));
      break;
    }

    case PROP_FILENAME: {
      const char* file = db->db ? sqlite3_db_filename(db->db, "main") : 0;
      ret = file && *file ? JS_NewString(ctx, file) : JS_NULL;
      break;
    }
  }

  return ret;
}

static JSValue
js_sqlite_value_string(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  DynBuf buf;
  SQLiteConnection* db = JS_GetOpaque(this_val, js_sqlite_class_id);

  dbuf_init2(&buf, 0, 0);

  for(int i = 0; i < argc; i++) {
    if(i > 0)
      dbuf_putstr(&buf, ", ");

    js_sqlite_print_value(ctx, db, &buf, argv[i]);
  }

  ret = JS_NewStringLen(ctx, (const char*)buf.buf, buf.size);
  dbuf_free(&buf);
  return ret;
}

static JSValue
js_sqlite_values_string(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  DynBuf buf;
  SQLiteConnection* db = JS_GetOpaque(this_val, js_sqlite_class_id);

  dbuf_init2(&buf, 0, 0);

  for(int i = 0; i < argc; i++) {
    if(i > 0)
      dbuf_putstr(&buf, ", ");

    js_sqlite_print_values(ctx, db, &buf, argv[i]);
  }

  ret = JS_NewStringLen(ctx, (const char*)buf.buf, buf.size);
  dbuf_free(&buf);
  return ret;
}

static JSValue
js_sqlite_insert_query(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  DynBuf buf;
  const char* tbl;
  size_t tbl_len;
  SQLiteConnection* db;
  int i = 1, j;

  if(!(db = js_sqlite_data2(ctx, this_val)))
    return JS_EXCEPTION;

  tbl = JS_ToCStringLen(ctx, &tbl_len, argv[0]);

  dbuf_init2(&buf, 0, 0);
  dbuf_putstr(&buf, "INSERT INTO ");
  dbuf_put(&buf, (const uint8_t*)tbl, tbl_len);
  dbuf_putstr(&buf, " ");

  JS_FreeCString(ctx, tbl);

  if(js_is_iterable(ctx, argv[i])) {

    if(i + 1 < argc)
      js_sqlite_print_iterable(ctx, db, &buf, argv[i++], js_sqlite_print_field);

    dbuf_putstr(&buf, " VALUES ");

    for(j = i; j < argc; j++) {
      if(JS_IsString(argv[j]))
        break;

      if(j > i)
        dbuf_putstr(&buf, ", ");

      js_sqlite_print_values(ctx, db, &buf, argv[j]);
    }
    i = j;

  } else {
    PropertyEnumeration propenum = PROPENUM_INIT();

    if(!property_enumeration_init(&propenum, ctx, JS_DupValue(ctx, argv[i]), PROPENUM_DEFAULT_FLAGS)) {

      dbuf_putstr(&buf, "(");

      j = 0;
      do {
        JSValue field = property_enumeration_key(&propenum, ctx);

        if(j++ > 0)
          dbuf_putstr(&buf, ", ");

        js_sqlite_print_field(ctx, db, &buf, field);
        JS_FreeValue(ctx, field);

      } while(property_enumeration_next(&propenum));

      dbuf_putstr(&buf, ") VALUES (");

      property_enumeration_setpos(&propenum, 0);

      j = 0;
      do {
        JSValue value = property_enumeration_value(&propenum, ctx);

        if(j++ > 0)
          dbuf_putstr(&buf, ", ");

        js_sqlite_print_value(ctx, db, &buf, value);
        JS_FreeValue(ctx, value);

      } while(property_enumeration_next(&propenum));

      property_enumeration_reset(&propenum, JS_GetRuntime(ctx));

      dbuf_putstr(&buf, ")");

      i++;
    }
  }

  dbuf_putstr(&buf, ";");

  ret = JS_NewStringLen(ctx, (const char*)buf.buf, buf.size);
  dbuf_free(&buf);
  return ret;
}

static JSValue
js_sqlite_escape_string(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  const char* src;
  char* dst;
  size_t len;

  if(!(src = JS_ToCStringLen(ctx, &len, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be string");

  dst = sqlite3_mprintf("%q", src);
  JS_FreeCString(ctx, src);

  if(!dst)
    return JS_ThrowOutOfMemory(ctx);

  ret = JS_NewString(ctx, dst);
  sqlite3_free(dst);
  return ret;
}

static JSValue
js_sqlite_quote_string(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  const char* src;
  char* dst;
  size_t len;

  if(!(src = JS_ToCStringLen(ctx, &len, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be string");

  dst = sqlite3_mprintf("%Q", src);
  JS_FreeCString(ctx, src);

  if(!dst)
    return JS_ThrowOutOfMemory(ctx);

  ret = JS_NewString(ctx, dst);
  sqlite3_free(dst);
  return ret;
}

static JSValue
js_sqlite_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = js_sqlite_wrap(ctx, proto, 0);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  if(argc > 0 && JS_IsString(argv[0])) {
    SQLiteConnection* db = JS_GetOpaque(obj, js_sqlite_class_id);
    const char* filename = JS_ToCString(ctx, argv[0]);
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;

    if(argc > 1)
      JS_ToInt32(ctx, &flags, argv[1]);

    if(sqlite3_open_v2(filename, &db->db, flags, NULL) != SQLITE_OK) {
      JSValue err = js_sqliteerror_new(ctx, sqlite_error(db));
      JS_FreeCString(ctx, filename);
      JS_FreeValue(ctx, obj);
      return JS_Throw(ctx, err);
    }

    JS_FreeCString(ctx, filename);
  }

  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_sqlite_open(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  SQLiteConnection* db;
  const char* filename;
  int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
  int ret;

  if(!(db = js_sqlite_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(db->db) {
    sqlite3_close_v2(db->db);
    db->db = 0;
  }

  if(!(filename = JS_ToCString(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a filename string");

  if(argc > 1)
    JS_ToInt32(ctx, &flags, argv[1]);

  ret = sqlite3_open_v2(filename, &db->db, flags, NULL);
  JS_FreeCString(ctx, filename);

  if(ret != SQLITE_OK)
    return JS_Throw(ctx, js_sqliteerror_new(ctx, sqlite_error(db)));

  return JS_NewBool(ctx, TRUE);
}

static JSValue
js_sqlite_query(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  SQLiteConnection* db;
  const char* sql;
  size_t sql_len;
  sqlite3_stmt* stmt = NULL;
  JSValue ret;

  if(!(db = js_sqlite_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!db->db)
    return JS_Throw(ctx, js_sqliteerror_new(ctx, "no database"));

  if(!(sql = JS_ToCStringLen(ctx, &sql_len, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be string");

  if(sqlite3_prepare_v2(db->db, sql, (int)sql_len, &stmt, NULL) != SQLITE_OK) {
    JSValue err = js_sqliteerror_new(ctx, sqlite_error(db));
    JS_FreeCString(ctx, sql);
    return JS_Throw(ctx, err);
  }

  JS_FreeCString(ctx, sql);

  if(stmt == NULL)
    return JS_NULL;

  if(sqlite3_column_count(stmt) == 0) {
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if(rc != SQLITE_DONE && rc != SQLITE_ROW)
      return JS_Throw(ctx, js_sqliteerror_new(ctx, sqlite_error(db)));

    return JS_NewInt32(ctx, sqlite3_changes(db->db));
  }

  ret = sqlite_result(db, stmt, ctx);
  JS_DefinePropertyValueStr(ctx, ret, "handle", JS_DupValue(ctx, this_val), JS_PROP_CONFIGURABLE);

  return ret;
}

static JSValue
js_sqlite_exec(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  SQLiteConnection* db;
  const char* sql;
  char* err = NULL;

  if(!(db = js_sqlite_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!db->db)
    return JS_Throw(ctx, js_sqliteerror_new(ctx, "no database"));

  if(!(sql = JS_ToCString(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be string");

  int rc = sqlite3_exec(db->db, sql, NULL, NULL, &err);
  JS_FreeCString(ctx, sql);

  if(rc != SQLITE_OK) {
    JSValue ex = js_sqliteerror_new(ctx, err ? err : "exec failed");

    if(err)
      sqlite3_free(err);

    return JS_Throw(ctx, ex);
  }

  return JS_NewInt32(ctx, sqlite3_changes(db->db));
}

static JSValue
js_sqlite_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  SQLiteConnection* db;

  if(!(db = js_sqlite_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(db->db) {
    sqlite3_close_v2(db->db);
    db->db = 0;
  }

  return JS_UNDEFINED;
}

static void
js_sqlite_finalizer(JSRuntime* rt, JSValue val) {
  SQLiteConnection* db;

  if((db = JS_GetOpaque(val, js_sqlite_class_id)))
    sqlite_free(db, rt);
}

static JSClassDef js_sqlite_class = {
    .class_name = "SQLite3",
    .finalizer = js_sqlite_finalizer,
};

static const JSCFunctionListEntry js_sqlite_funcs[] = {
    JS_CGETSET_MAGIC_DEF("affectedRows", js_sqlite_get, 0, PROP_AFFECTED_ROWS),
    JS_CGETSET_MAGIC_DEF("changes", js_sqlite_get, 0, PROP_AFFECTED_ROWS),
    JS_CGETSET_MAGIC_DEF("insertId", js_sqlite_get, 0, PROP_INSERT_ID),
    JS_CGETSET_MAGIC_DEF("lastInsertRowid", js_sqlite_get, 0, PROP_INSERT_ID),
    JS_CGETSET_MAGIC_DEF("totalChanges", js_sqlite_get, 0, PROP_TOTAL_CHANGES),
    JS_CGETSET_MAGIC_DEF("errorMessage", js_sqlite_get, 0, PROP_ERROR_MESSAGE),
    JS_CGETSET_MAGIC_DEF("errorCode", js_sqlite_get, 0, PROP_ERROR_CODE),
    JS_CGETSET_MAGIC_DEF("filename", js_sqlite_get, 0, PROP_FILENAME),
    JS_CFUNC_DEF("open", 1, js_sqlite_open),
    JS_CFUNC_DEF("query", 1, js_sqlite_query),
    JS_CFUNC_DEF("exec", 1, js_sqlite_exec),
    JS_CFUNC_DEF("close", 0, js_sqlite_close),
    JS_ALIAS_DEF("execute", "query"),
    JS_CFUNC_DEF("escapeString", 1, js_sqlite_escape_string),
    JS_CFUNC_DEF("quoteString", 1, js_sqlite_quote_string),
    JS_CFUNC_DEF("valueString", 0, js_sqlite_value_string),
    JS_CFUNC_DEF("valuesString", 1, js_sqlite_values_string),
    JS_CFUNC_DEF("insertQuery", 2, js_sqlite_insert_query),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SQLite3", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_sqlite_static[] = {
    JS_CFUNC_DEF("escapeString", 1, js_sqlite_escape_string),
    JS_CFUNC_DEF("quoteString", 1, js_sqlite_quote_string),
    JS_CFUNC_DEF("valueString", 0, js_sqlite_value_string),
};

static const JSCFunctionListEntry js_sqlite_defines[] = {
    JS_PROP_INT32_DEF("RESULT_OBJECT", RESULT_OBJECT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("RESULT_STRING", RESULT_STRING, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPEN_READONLY", SQLITE_OPEN_READONLY, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPEN_READWRITE", SQLITE_OPEN_READWRITE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPEN_CREATE", SQLITE_OPEN_CREATE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPEN_URI", SQLITE_OPEN_URI, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPEN_MEMORY", SQLITE_OPEN_MEMORY, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPEN_NOMUTEX", SQLITE_OPEN_NOMUTEX, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPEN_FULLMUTEX", SQLITE_OPEN_FULLMUTEX, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPEN_SHAREDCACHE", SQLITE_OPEN_SHAREDCACHE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPEN_PRIVATECACHE", SQLITE_OPEN_PRIVATECACHE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("INTEGER", SQLITE_INTEGER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("FLOAT", SQLITE_FLOAT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TEXT", SQLITE_TEXT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("BLOB", SQLITE_BLOB, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("NULL", SQLITE_NULL, JS_PROP_CONFIGURABLE),
};

/* ---- SQLiteError ---- */

static JSValue
js_sqliteerror_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  JSAtom prop;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_sqliteerror_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

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

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_sqliteerror_new(JSContext* ctx, const char* msg) {
  JSValue obj;
  JSValue argv[1];

  obj = JS_NewObjectProtoClass(ctx, sqliteerror_proto, js_sqliteerror_class_id);
  if(JS_IsException(obj))
    return JS_EXCEPTION;

  argv[0] = JS_NewString(ctx, msg ? msg : "");
  obj = js_sqliteerror_constructor(ctx, sqliteerror_ctor, 1, argv);

  JS_FreeValue(ctx, argv[0]);

  return obj;
}

static JSClassDef js_sqliteerror_class = {
    .class_name = "SQLite3Error",
};

static const JSCFunctionListEntry js_sqliteerror_funcs[] = {
    JS_PROP_STRING_DEF("name", "SQLite3Error", JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("type", 0, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SQLite3Error", JS_PROP_CONFIGURABLE),
};

/* ---- SQLite3Result ---- */

static void
result_blob_free(JSRuntime* rt, void* opaque, void* ptr) {
  js_free_rt(rt, ptr);
}

static JSValue
result_column_value(JSContext* ctx, SQLiteResult* res, int col, int rtype) {
  sqlite3_stmt* stmt = res->stmt;
  int type = sqlite3_column_type(stmt, col);

  if(rtype & RESULT_STRING) {
    if(type == SQLITE_NULL)
      return JS_NewString(ctx, "NULL");

    const char* text = (const char*)sqlite3_column_text(stmt, col);
    int len = sqlite3_column_bytes(stmt, col);

    return JS_NewStringLen(ctx, text ? text : "", text ? len : 0);
  }

  switch(type) {
    case SQLITE_NULL: return JS_NULL;

    case SQLITE_INTEGER: {
      sqlite3_int64 v = sqlite3_column_int64(stmt, col);

      if(v >= INT32_MIN && v <= INT32_MAX)
        return JS_NewInt32(ctx, (int32_t)v);

      return JS_NewInt64(ctx, v);
    }

    case SQLITE_FLOAT: return JS_NewFloat64(ctx, sqlite3_column_double(stmt, col));

    case SQLITE_TEXT: {
      const char* text = (const char*)sqlite3_column_text(stmt, col);
      int len = sqlite3_column_bytes(stmt, col);

      return JS_NewStringLen(ctx, text ? text : "", text ? len : 0);
    }

    case SQLITE_BLOB: {
      const void* blob = sqlite3_column_blob(stmt, col);
      int len = sqlite3_column_bytes(stmt, col);

      if(!blob || len == 0)
        return JS_NewArrayBuffer(ctx, NULL, 0, NULL, NULL, FALSE);

      uint8_t* copy = js_malloc(ctx, len);

      if(!copy)
        return JS_EXCEPTION;

      memcpy(copy, blob, len);
      return JS_NewArrayBuffer(ctx, copy, len, &result_blob_free, NULL, FALSE);
    }
  }

  return JS_UNDEFINED;
}

static JSValue
result_array(JSContext* ctx, SQLiteResult* res, int rtype) {
  JSValue ret = JS_NewArray(ctx);
  int i, n = sqlite3_column_count(res->stmt);

  for(i = 0; i < n; i++)
    JS_SetPropertyUint32(ctx, ret, i, result_column_value(ctx, res, i, rtype));

  return ret;
}

static JSValue
result_object(JSContext* ctx, SQLiteResult* res, int rtype) {
  JSValue ret = JS_NewObjectProto(ctx, JS_NULL);
  int i, n = sqlite3_column_count(res->stmt);

  for(i = 0; i < n; i++) {
    const char* name = sqlite3_column_name(res->stmt, i);

    if(name)
      JS_SetPropertyStr(ctx, ret, name, result_column_value(ctx, res, i, rtype));
  }

  return ret;
}

static JSValue
result_row(JSContext* ctx, SQLiteResult* res, int rtype) {
  return (rtype & RESULT_OBJECT) ? result_object(ctx, res, rtype) : result_array(ctx, res, rtype);
}

static SQLiteResult*
sqliteresult_new(JSContext* ctx) {
  SQLiteResult* res;

  if(!(res = js_malloc(ctx, sizeof(SQLiteResult))))
    return 0;

  *res = (SQLiteResult){1, NULL, NULL, 0, FALSE, FALSE};

  return res;
}

static SQLiteResult*
sqliteresult_dup(SQLiteResult* ptr) {
  ++ptr->ref_count;
  return ptr;
}

static void
sqliteresult_free(JSRuntime* rt, void* ptr, void* mem) {
  SQLiteResult* res = ptr;

  if(--res->ref_count == 0) {
    if(res->stmt) {
      sqlite3_finalize(res->stmt);
      res->stmt = 0;
    }

    if(res->conn) {
      sqlite_free(res->conn, rt);
      res->conn = 0;
    }

    js_free_rt(rt, res);
  }
}

static void
sqliteresult_set_conn(SQLiteResult* res, SQLiteConnection* conn, JSContext* ctx) {
  if(res->conn) {
    sqlite_free(res->conn, JS_GetRuntime(ctx));
    res->conn = 0;
  }

  res->conn = conn ? sqlite_dup(conn) : 0;
}

SQLiteResult*
js_sqliteresult_opaque2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_sqliteresult_class_id);
}

sqlite3_stmt*
js_sqliteresult_data(JSContext* ctx, JSValueConst value) {
  SQLiteResult* res;

  if((res = JS_GetOpaque2(ctx, value, js_sqliteresult_class_id)))
    return res->stmt;

  return 0;
}

sqlite3*
js_sqliteresult_handle(JSContext* ctx, JSValueConst value) {
  SQLiteResult* res;

  if((res = JS_GetOpaque2(ctx, value, js_sqliteresult_class_id)))
    return res->conn ? res->conn->db : 0;

  return 0;
}

static int
sqliteresult_rtype(JSContext* ctx, JSValueConst this_val) {
  return js_get_propertystr_int32(ctx, this_val, "resultType");
}

enum {
  METHOD_FETCH_FIELD,
  METHOD_FETCH_FIELDS,
  METHOD_FETCH_ROW,
  METHOD_FETCH_ASSOC,
  METHOD_RESET,
};

static JSValue
field_array(JSContext* ctx, sqlite3_stmt* stmt, int i) {
  JSValue arr = JS_NewArray(ctx);
  const char* name = sqlite3_column_name(stmt, i);
  const char* decltype = sqlite3_column_decltype(stmt, i);

  JS_SetPropertyUint32(ctx, arr, 0, name ? JS_NewString(ctx, name) : JS_NULL);
  JS_SetPropertyUint32(ctx, arr, 1, decltype ? JS_NewString(ctx, decltype) : JS_NULL);

  return arr;
}

static JSValue
js_sqliteresult_next(JSContext* ctx, SQLiteResult* res, BOOL* pdone, int rtype) {
  if(!res->stmt || res->done) {
    *pdone = TRUE;
    return JS_UNDEFINED;
  }

  int rc = sqlite3_step(res->stmt);

  if(rc == SQLITE_ROW) {
    *pdone = FALSE;
    res->row_index++;
    return result_row(ctx, res, rtype);
  }

  res->done = TRUE;
  *pdone = TRUE;

  if(rc != SQLITE_DONE && res->conn && res->conn->db)
    return JS_Throw(ctx, js_sqliteerror_new(ctx, sqlite3_errmsg(res->conn->db)));

  return JS_UNDEFINED;
}

static JSValue
js_sqliteresult_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  SQLiteResult* res;
  JSValue ret = JS_UNDEFINED;

  if(!(res = js_sqliteresult_opaque2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_FETCH_FIELD: {
      uint32_t index;

      if(JS_ToUint32(ctx, &index, argv[0]))
        return JS_ThrowTypeError(ctx, "argument 1 must be a positive index");

      if((int)index >= sqlite3_column_count(res->stmt))
        return JS_ThrowRangeError(ctx, "argument 1 must be smaller than total fields (%d)", sqlite3_column_count(res->stmt));

      ret = field_array(ctx, res->stmt, (int)index);
      break;
    }

    case METHOD_FETCH_FIELDS: {
      int n = sqlite3_column_count(res->stmt);

      ret = JS_NewArray(ctx);

      for(int i = 0; i < n; i++)
        JS_SetPropertyUint32(ctx, ret, i, field_array(ctx, res->stmt, i));
      break;
    }

    case METHOD_FETCH_ROW:
    case METHOD_FETCH_ASSOC: {
      BOOL done = FALSE;
      int rtype = sqliteresult_rtype(ctx, this_val);

      if(magic == METHOD_FETCH_ASSOC)
        rtype |= RESULT_OBJECT;

      ret = js_sqliteresult_next(ctx, res, &done, rtype);

      if(done && JS_IsUndefined(ret))
        ret = JS_NULL;

      break;
    }

    case METHOD_RESET: {
      if(res->stmt) {
        sqlite3_reset(res->stmt);
        res->row_index = 0;
        res->done = FALSE;
      }
      break;
    }
  }

  return ret;
}

static JSValue
js_sqliteresult_new(JSContext* ctx, JSValueConst proto, sqlite3_stmt* stmt) {
  JSValue obj;
  SQLiteResult* opaque;

  if(!(opaque = sqliteresult_new(ctx)))
    return JS_EXCEPTION;

  if(js_sqliteresult_class_id == 0)
    js_sqlite_init(ctx, 0);

  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = sqliteresult_proto;

  obj = JS_NewObjectProtoClass(ctx, proto, js_sqliteresult_class_id);
  if(JS_IsException(obj)) {
    js_free(ctx, opaque);
    return JS_EXCEPTION;
  }

  opaque->stmt = stmt;
  JS_SetOpaque(obj, opaque);

  return obj;
}

static JSValue
js_sqliteresult_get(JSContext* ctx, JSValueConst this_val, int magic) {
  SQLiteResult* res;
  JSValue ret = JS_UNDEFINED;

  if(!(res = js_sqliteresult_opaque2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_EOF: {
      ret = JS_NewBool(ctx, res->done);
      break;
    }

    case PROP_NUM_ROWS: {
      ret = JS_NewInt64(ctx, res->row_index);
      break;
    }

    case PROP_NUM_FIELDS: {
      ret = JS_NewInt32(ctx, res->stmt ? sqlite3_column_count(res->stmt) : 0);
      break;
    }
  }

  return ret;
}

static JSValue
js_sqliteresult_iterator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  SQLiteResultIterator* it = ptr;
  SQLiteResult* res = it->result;
  int rtype = 0;
  BOOL done = FALSE;
  JSValue val = js_sqliteresult_next(ctx, res, &done, rtype);

  if(JS_IsException(val))
    return val;

  JSValue ret = js_iterator_result(ctx, JS_IsUndefined(val) ? JS_NULL : val, done);
  JS_FreeValue(ctx, val);
  return ret;
}

static void
js_sqliteresult_iterator_free(JSRuntime* rt, void* ptr) {
  SQLiteResultIterator* it = ptr;

  sqliteresult_free(rt, it->result, 0);
  js_free_rt(rt, it);
}

static JSValue
js_sqliteresult_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_NewObject(ctx);
  SQLiteResultIterator* it;
  SQLiteResult* res;

  if(!(res = js_sqliteresult_opaque2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(it = js_malloc(ctx, sizeof(SQLiteResultIterator))))
    return JS_EXCEPTION;

  *it = (SQLiteResultIterator){ctx, sqliteresult_dup(res)};

  JS_SetPropertyStr(ctx, ret, "next", js_function_cclosure(ctx, js_sqliteresult_iterator_next, 0, 0, it, js_sqliteresult_iterator_free));

  return ret;
}

static JSValue
js_sqliteresult_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");

  if(JS_IsException(proto))
    goto fail;

  obj = js_sqliteresult_new(ctx, proto, 0);
  JS_FreeValue(ctx, proto);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_sqliteresult_finalizer(JSRuntime* rt, JSValue val) {
  SQLiteResult* opaque;

  if((opaque = JS_GetOpaque(val, js_sqliteresult_class_id)))
    sqliteresult_free(rt, opaque, 0);
}

static JSClassDef js_sqliteresult_class = {
    .class_name = "SQLite3Result",
    .finalizer = js_sqliteresult_finalizer,
};

static const JSCFunctionListEntry js_sqliteresult_funcs[] = {
    JS_CGETSET_MAGIC_DEF("eof", js_sqliteresult_get, 0, PROP_EOF),
    JS_CGETSET_MAGIC_FLAGS_DEF("numRows", js_sqliteresult_get, 0, PROP_NUM_ROWS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("numFields", js_sqliteresult_get, 0, PROP_NUM_FIELDS, JS_PROP_ENUMERABLE),
    JS_CFUNC_MAGIC_DEF("fetchField", 1, js_sqliteresult_functions, METHOD_FETCH_FIELD),
    JS_CFUNC_MAGIC_DEF("fetchFields", 0, js_sqliteresult_functions, METHOD_FETCH_FIELDS),
    JS_CFUNC_MAGIC_DEF("fetchRow", 0, js_sqliteresult_functions, METHOD_FETCH_ROW),
    JS_CFUNC_MAGIC_DEF("fetchAssoc", 0, js_sqliteresult_functions, METHOD_FETCH_ASSOC),
    JS_CFUNC_MAGIC_DEF("reset", 0, js_sqliteresult_functions, METHOD_RESET),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_sqliteresult_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SQLite3Result", JS_PROP_CONFIGURABLE),
};

int
js_sqlite_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_sqlite_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_sqlite_class_id, &js_sqlite_class);

  sqlite_ctor = JS_NewCFunction2(ctx, js_sqlite_constructor, "SQLite3", 1, JS_CFUNC_constructor, 0);
  sqlite_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, sqlite_proto, js_sqlite_funcs, countof(js_sqlite_funcs));
  JS_SetPropertyFunctionList(ctx, sqlite_proto, js_sqlite_defines, countof(js_sqlite_defines));
  JS_SetPropertyFunctionList(ctx, sqlite_ctor, js_sqlite_static, countof(js_sqlite_static));
  JS_SetPropertyFunctionList(ctx, sqlite_ctor, js_sqlite_defines, countof(js_sqlite_defines));
  JS_SetClassProto(ctx, js_sqlite_class_id, sqlite_proto);
  JS_SetConstructor(ctx, sqlite_ctor, sqlite_proto);

  JSValue error = JS_NewError(ctx);
  JSValue error_proto = JS_GetPrototype(ctx, error);
  JSValue error_ctor = JS_GetPropertyStr(ctx, error_proto, "constructor");
  JS_FreeValue(ctx, error);

  JS_NewClassID(&js_sqliteerror_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_sqliteerror_class_id, &js_sqliteerror_class);

  sqliteerror_ctor = JS_NewCFunction2(ctx, js_sqliteerror_constructor, "SQLite3Error", 1, JS_CFUNC_constructor, 0);
  JS_SetPrototype(ctx, sqliteerror_ctor, error_ctor);
  JS_FreeValue(ctx, error_ctor);
  sqliteerror_proto = JS_NewObjectProto(ctx, error_proto);
  JS_FreeValue(ctx, error_proto);

  JS_SetPropertyFunctionList(ctx, sqliteerror_proto, js_sqliteerror_funcs, countof(js_sqliteerror_funcs));

  JS_SetClassProto(ctx, js_sqliteerror_class_id, sqliteerror_proto);
  JS_SetConstructor(ctx, sqliteerror_ctor, sqliteerror_proto);

  JS_NewClassID(&js_sqliteresult_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_sqliteresult_class_id, &js_sqliteresult_class);

  sqliteresult_ctor = JS_NewCFunction2(ctx, js_sqliteresult_constructor, "SQLite3Result", 1, JS_CFUNC_constructor, 0);
  sqliteresult_proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, sqliteresult_proto, js_sqliteresult_funcs, countof(js_sqliteresult_funcs));
  JS_SetClassProto(ctx, js_sqliteresult_class_id, sqliteresult_proto);
  JS_SetConstructor(ctx, sqliteresult_ctor, sqliteresult_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "SQLite3", sqlite_ctor);
    JS_SetModuleExport(ctx, m, "SQLite3Error", sqliteerror_ctor);
    JS_SetModuleExport(ctx, m, "SQLite3Result", sqliteresult_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_sqlite
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_sqlite_init))) {
    JS_AddModuleExport(ctx, m, "SQLite3");
    JS_AddModuleExport(ctx, m, "SQLite3Error");
    JS_AddModuleExport(ctx, m, "SQLite3Result");
  }

  return m;
}

/**
 * @}
 */
