#ifndef JSERIALIZER_H
#define JSERIALIZER_H

#include <quickjs.h>
#include "json/jserial.h"
#include "json/jwalk.h"
#include "json/jopts.h"
#include "location.h"

/**
 * \addtogroup json
 * @{
 */

/* JsonSerializer: a JwalkSource (JSON.stringify's rules) pulled through a JsonSerial;
 * read() takes events from the walk until the bytes asked for are buffered.
 * rule: an event is taken once, so toJSON and the replacer run exactly once per property. */
typedef struct json_serializer {
  JwalkSource src;
  JsonSerial ser;
  JsonSpace space;
  unsigned started : 1, error : 1, failed : 1;
  JSValue root;
  Location* loc;
} JsonSerializer;

/* jserializer_new: a serializer of argv[0], with the replacer and space of JSON.stringify as argv[1] and argv[2].
 * returns it, or NULL with an exception pending. */
JsonSerializer* jserializer_new(JSContext*, int argc, JSValueConst argv[]);

/* jserializer_free: releases js and what it holds. */
void jserializer_free(JsonSerializer* js, JSRuntime* rt);

/* jserializer_read: JsonSerializer.read(): argv[0] is a buffer to fill and gives the count of bytes
 * written, else a count of characters (-1 or none for all) and gives the text, undefined at the end.
 * returns the result, or JS_EXCEPTION. */
JSValue jserializer_read(JSContext*, JsonSerializer* js, int argc, JSValueConst argv[]);

/** @} */
#endif /* JSERIALIZER_H */
