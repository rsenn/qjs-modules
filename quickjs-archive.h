#ifndef QUICKJS_ARCHIVE_H
#define QUICKJS_ARCHIVE_H

/**
 * \defgroup quickjs-archive quickjs-archive: libarchive bindings
 * @{
 */

struct archive;

#include <quickjs.h>
#include <cutils.h>

struct archive* js_archive_data(JSContext*, JSValue value);
struct archive_entry* js_archiveentry_data(JSContext*, JSValue value);
int js_archive_init(JSContext*, JSModuleDef* m);
JSModuleDef* js_init_module_archive(JSContext*, const char* module_name);

extern VISIBLE JSClassID js_archive_class_id, js_archiveentry_class_id;
extern VISIBLE JSValue archive_proto, archive_ctor, archiveentry_proto, archiveentry_ctor;
/**
 * @}
 */

#endif /* defined(QUICKJS_ARCHIVE_H) */
