#ifndef QUICKJS_PGSQL_H
#define QUICKJS_PGSQL_H

/**
 * \defgroup quickjs-pgsql QuickJS module: pgsql - libpgsqlclient bindings
 * @{
 */

#include "defines.h"
#include <quickjs.h>
#include <cutils.h>
#include <list.h>

#include <postgresql/libpq-fe.h>

PGconn* js_pgsql_data(JSContext*, JSValueConst);
PGresult* js_pgsqlresult_data(JSContext*, JSValueConst);
PGconn* js_pgsqlresult_handle(JSContext*, JSValueConst);
BOOL js_pgsqlresult_nonblock(JSContext*, JSValueConst);
int js_pgsql_init(JSContext*, JSModuleDef*);
JSModuleDef* js_init_module_pgsql(JSContext*, const char* module_name);

extern thread_local VISIBLE JSClassID js_pgsql_class_id, js_pgsqlresult_class_id;

/**
 * @}
 */

#endif /* defined(QUICKJS_PGSQL_H) */
