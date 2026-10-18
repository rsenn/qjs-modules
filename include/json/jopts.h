#ifndef JOPTS_H
#define JOPTS_H

#include <quickjs.h>
#include "json/jwrite.h"

/**
 * \addtogroup jwrite
 * @{
 */

/* JsonSpace: JSON.stringify's space argument, in the form the writer takes it. */
typedef struct {
  int32_t indent;    /* spaces per level, 0..10 */
  unsigned char len; /* bytes of str, when > 0 str is used instead of spaces */
  char str[10];
} JsonSpace;

/* jsonspace_parse: a JSON.stringify space as a JsonSpace.
 *
 *   JS value          means
 *   number            that many spaces, clamped to 0..10
 *   string            its first 10 bytes (whole characters) per level, e.g. "\t"
 *   anything else     no indentation
 *
 * returns 0, or -1 with an exception pending. */
int jsonspace_parse(JSContext*, JSValueConst space, JsonSpace* out);

/* jsonspace_apply: puts sp into the writer options, no indent means minify
 * as JSON.stringify writes no whitespace then. never throws. */
void jsonspace_apply(const JsonSpace* sp, JsonWriteOptions* opts);

/* jsonspace_args: the replacer and space of write(value, ...) and new JsonSerializer(value, ...).
 *
 *   (v, null, 2)   (v, fn)   (v, [..], 2)   JSON.stringify's own forms
 *   (v, 2)         (v, '\t')                 a number or string alone is the space
 *
 * never throws, a replacer that is neither a function nor an array is ignored. */
void jsonspace_args(JSContext*, int argc, JSValueConst argv[], JSValueConst* replacer, JSValueConst* space);

/* jwrite_options_from_js: formatting options from a JS value.
 *
 *   jwrite_options_from_js(ctx, 2, &o);                     o.indent = 2
 *   jwrite_options_from_js(ctx, {indent: 2, minify: true}, &o);
 *
 *   JSValueConst       obj   a number (the indent), or an object with any of
 *                            indent, bareKeys, singleQuotes, hexNumbers, minify
 *   JsonWriteOptions*  opts  set from obj, unset keys become 0/false
 *
 * never throws, any other kind of obj leaves all options 0. */
void jwrite_options_from_js(JSContext*, JSValueConst obj, JsonWriteOptions* opts);

/** @} */
#endif /* JOPTS_H */
