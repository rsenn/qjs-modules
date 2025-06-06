#ifndef QUICKJS_ARCHIVE_H
#define QUICKJS_ARCHIVE_H

/**
 * \defgroup quickjs-archive quickjs-archive: libarchive bindings
 * @{
 */

struct archive;

#include <quickjs.h>
#include <cutils.h>

int js_archive_init(JSContext*, JSModuleDef* m);
JSModuleDef* js_init_module_archive(JSContext*, const char* module_name);

extern VISIBLE JSClassID js_archive_class_id;

/**
 * @}
 */

#endif /* defined(QUICKJS_ARCHIVE_H) */
