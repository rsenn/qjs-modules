#ifndef QUICKJS_LOCATION_H
#define QUICKJS_LOCATION_H

#include "location.h"
#include "utils.h"

extern thread_local JSClassID js_location_class_id;
extern thread_local JSValue location_proto, location_ctor;

Location* js_location_data(JSContext*, JSValue value);
JSValue js_location_new_proto(JSContext*, JSValue proto, const Location* location);
JSValue js_location_new(JSContext*, const Location* location);
JSValue js_location_tostring(JSContext*, JSValue this_val, int argc, JSValue* argv);
BOOL js_is_location(JSContext*, JSValue obj);
Location js_location_from(JSContext*, JSValue this_val);
JSValue js_location_toprimitive(JSContext*, JSValue this_val, int argc, JSValue* argv);
JSValue js_location_constructor(JSContext*, JSValue new_target, int argc, JSValue* argv);
void js_location_finalizer(JSRuntime*, JSValue val);
int js_location_init(JSContext*, JSModuleDef* m);
JSModuleDef* js_init_module_location(JSContext*, const char* module_name);

#endif /* defined(QUICKJS_LOCATION_H) */
