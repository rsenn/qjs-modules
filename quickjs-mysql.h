#ifndef QUICKJS_MYSQL_H
#define QUICKJS_MYSQL_H

/**
 * \defgroup quickjs-mysql QuickJS module: mysql - libmysqlclient bindings
 * @{
 */

struct mysql;

#include <quickjs.h>
#include <cutils.h>
#include <mariadb/mysql.h>

struct MYSQL* js_mysql_data(JSContext*, JSValue value);
struct MYSQL_RES* js_mysqlresult_data(JSContext*, JSValue value);
int js_mysql_init(JSContext*, JSModuleDef* m);
JSModuleDef* js_init_module_mysql(JSContext*, const char* module_name);

/**
 * @}
 */

#endif /* defined(QUICKJS_MYSQL_H) */
