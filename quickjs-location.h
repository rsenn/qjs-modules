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
/* js_syntax_error_new: a SyntaxError with own, non-enumerable `loc` (a Location
 * clone), `length` and `code`; message is "<text> at <file>:<line>:<column>".
 *
 *   const Location*  at      start of the offending token
 *   size_t           length  characters it spans (0 at end of input)
 *   const char*      code    stable kebab-case name
 *
 *   returns JSValue  new reference to the error, or JS_EXCEPTION */
VISIBLE JSValue js_syntax_error_new(JSContext*, const Location* at, size_t length, const char* code, const char* fmt, ...);

/* js_throw_syntax_error: as above, and throws it; returns JS_EXCEPTION. */
VISIBLE JSValue js_throw_syntax_error(JSContext*, const Location* at, size_t length, const char* code, const char* fmt, ...);

/* js_syntax_error_at: as js_syntax_error_new(), for an engine-free SourcePos of the
 * input called name (NULL: <input>).
 *
 *   const char*       name  input name for the message and loc.file ("f.json")
 *   const SourcePos*  pos   start of the offending token (1-based)
 *
 * returns the error (refcount: a new reference), or JS_EXCEPTION. */
VISIBLE JSValue js_syntax_error_at(JSContext*, const char* name, const SourcePos* pos, size_t length, const char* code, const char* msg);

/* js_throw_syntax_error_at: as above, and throws it; returns JS_EXCEPTION. */
VISIBLE JSValue js_throw_syntax_error_at(JSContext*, const char* name, const SourcePos* pos, size_t length, const char* code, const char* msg);

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
