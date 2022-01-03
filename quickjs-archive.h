#ifndef QUICKJS_ARCHIVE_H
#define QUICKJS_ARCHIVE_H

/**
 * \defgroup quickjs-archive QuickJS module: archive - libarchive bindings
 * @{
 */

struct archive;

#include <quickjs.h>
#include <cutils.h>

struct archive* js_archive_data(JSContext*, JSValue value);
struct archive_entry* js_archiveentry_data(JSContext*, JSValue value);
int js_archive_init(JSContext*, JSModuleDef* m);
JSModuleDef* js_init_module_archive(JSContext*, const char* module_name);

/**
 * @}
 */

#endif /* defined(QUICKJS_ARCHIVE_H) */
