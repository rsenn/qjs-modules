#ifndef QUICKJS_LOCATION_H
#define QUICKJS_LOCATION_H

#include "location.h"
#include "utils.h"

/**
 * \defgroup quickjs-location quickjs-location: Text location object
 * @{
 */
extern VISIBLE JSClassID js_location_class_id;

VISIBLE JSValue js_location_wrap(JSContext*, Location*);
VISIBLE void js_location_from2(JSContext*, JSValueConst, Location*);
VISIBLE Location* js_location_from(JSContext*, JSValueConst);
VISIBLE Location* js_location_copy(JSContext*, JSValueConst);
VISIBLE int js_location_init(JSContext*, JSModuleDef*);

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
