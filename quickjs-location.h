#ifndef QUICKJS_LOCATION_H
#define QUICKJS_LOCATION_H

#include "location.h"
#include "utils.h"

/**
 * \defgroup quickjs-location QuickJS module: location - Text location object
 * @{
 */
extern thread_local JSClassID js_location_class_id;
extern thread_local JSValue location_proto, location_ctor;

JSValue js_location_wrap(JSContext*, Location*);
JSValue js_location_tostring(JSContext*, JSValue, int, JSValue argv[]);
BOOL js_is_location(JSContext*, JSValue);
Location* js_location_from(JSContext*, JSValue);
JSValue js_location_toprimitive(JSContext*, JSValue, int, JSValue argv[]);
JSValue js_location_constructor(JSContext*, JSValue, int, JSValue argv[]);
void js_location_finalizer(JSRuntime*, JSValue);

static inline Location*
js_location_data(JSValueConst value) {
  Location* loc;
  assert(js_location_class_id);
  if((loc = JS_GetOpaque(value, js_location_class_id)))
    if(loc == (Location*)-1)
      loc = 0;
  return loc;
}

static inline Location*
js_location_data2(JSContext* ctx, JSValueConst value) {
  Location* loc;
  assert(js_location_class_id);
  if((loc = JS_GetOpaque2(ctx, value, js_location_class_id)))
    if(loc == (Location*)-1)
      loc = 0;
  return loc;
}

/**
 * @}
 */

#endif /* defined(QUICKJS_LOCATION_H) */
