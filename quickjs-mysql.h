#ifndef QUICKJS_MYSQL_H
#define QUICKJS_MYSQL_H

/**
 * \defgroup quickjs-mysql QuickJS module: mysql - libmysqlclient bindings
 * @{
 */

#include "defines.h"
#include <quickjs.h>
#include <cutils.h>
#include <list.h>
#define list_add mariadb_list_add
#include <mariadb/mysql.h>

MYSQL* js_mysql_data(JSValueConst);
MYSQL* js_mysql_data2(JSContext*, JSValueConst);
MYSQL_RES* js_mysqlresult_data2(JSContext*, JSValueConst);
MYSQL* js_mysqlresult_handle(JSContext*, JSValueConst);
int js_mysql_init(JSContext*, JSModuleDef*);

JSModuleDef* js_init_module_mysql(JSContext*, const char* module_name);

extern thread_local VISIBLE JSClassID js_mysql_class_id, js_mysqlresult_class_id;

/**
 * @}
 */

#endif /* defined(QUICKJS_MYSQL_H) */
