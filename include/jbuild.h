#ifndef JBUILD_H
#define JBUILD_H

#include <stddef.h>
#include <quickjs.h>
#include "jread.h"

typedef struct JsonBuilderFrame JsonBuilderFrame; /* private to jbuild.c */

/* JsonBuilder: builds a JS value from a stream of JSON events.
 *
 * ```c
 * JsonBuilder b;
 * jbuild_init(&b, ctx);
 * jbuild_push(&b, jr_type_object_start);
 * jbuild_key(&b, "a", 1);
 * jbuild_value(&b, jr_type_number, "1", 1);
 * jbuild_pop(&b);
 * JSValue v = jbuild_root(&b);   // {a: 1}
 * jbuild_free(&b, rt);
 * ```
 *
 * Fed from jread callbacks (JsonPushParser); the event types are jr_type_t. */
typedef struct JsonBuilder {
  JSContext* ctx;
  JsonBuilderFrame* top;
  JSValue root;
  unsigned has_root : 1;
} JsonBuilder;

void jbuild_init(JsonBuilder*, JSContext*);
void jbuild_push(JsonBuilder*, jr_type_t type);
void jbuild_pop(JsonBuilder*);
void jbuild_key(JsonBuilder*, const char* name, size_t len);
void jbuild_value(JsonBuilder*, jr_type_t type, const char* data, size_t len);
JSValue jbuild_path(JsonBuilder*);
JSValue jbuild_root(JsonBuilder*);
void jbuild_free(JsonBuilder*, JSRuntime*);

#endif
