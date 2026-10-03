#ifndef QUICKJS_SQLITE_H
#define QUICKJS_SQLITE_H

/**
 * \defgroup quickjs-sqlite quickjs-sqlite: SQLite3 bindings
 * @{
 */

#include "defines.h"
#include <quickjs.h>
#include <cutils.h>
#include <list.h>

#include <sqlite3.h>

int js_sqlite_init(JSContext*, JSModuleDef*);
JSModuleDef* js_init_module_sqlite(JSContext*, const char* module_name);

extern VISIBLE JSClassID js_sqlite_class_id, js_sqliteresult_class_id;

/**
 * @}
 */

#endif /* defined(QUICKJS_SQLITE_H) */
