#ifndef QUICKJS_ARCHIVE_H
#define QUICKJS_ARCHIVE_H

struct archive;

#include <quickjs.h>
#include <cutils.h>

extern thread_local JSClassID js_archive_class_id;

struct archive* js_archive_data(JSContext*, JSValue value);
JSValue js_archive_new_proto(JSContext*, JSValue proto, const struct archive* archive);
JSValue js_archive_new(JSContext*, const struct archive* archive);
JSValue js_archive_tostring(JSContext*, JSValue this_val, int argc, JSValue* argv);
BOOL js_is_archive(JSContext*, JSValue obj);
struct archive js_archive_from(JSContext*, JSValue this_val);
JSValue js_archive_toprimitive(JSContext*, JSValue this_val, int argc, JSValue* argv);
JSValue js_archive_constructor(JSContext*, JSValue new_target, int argc, JSValue* argv);
void js_archive_finalizer(JSRuntime*, JSValue val);
int js_archive_init(JSContext*, JSModuleDef* m);
JSModuleDef* js_init_module_archive(JSContext*, const char* module_name);

#endif /* defined(QUICKJS_ARCHIVE_H) */
