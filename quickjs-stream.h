#ifndef QUICKJS_STREAM_H
#define QUICKJS_STREAM_H

#include "stream-utils.h"
#include <quickjs.h>

/**
 * \defgroup quickjs-stream quickjs-stream: Streams
 * @{
 */

int js_stream_init(JSContext*, JSModuleDef*);
JSModuleDef* js_init_module_stream(JSContext*, const char*);

/* Wraps a stream-utils.h Reader as a ReadableStream; see quickjs-stream.c.
 * For other C modules that already hold a Reader and want to hand it to JS
 * without writing their own pull-loop shim. Takes ownership of `reader`. */
JSValue js_readable_stream_from_reader(JSContext*, Reader, size_t chunk_size);

/**
 * @}
 */

#endif /* defined(QUICKJS_STREAM_H) */
