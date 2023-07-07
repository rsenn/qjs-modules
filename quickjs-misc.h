#ifndef QUICKJS_MISC_H
#define QUICKJS_MISC_H

#include "utils.h"

/**
 * \defgroup quickjs-misc quickjs-misc: Miscellaneous
 * @{
 */

JSValue js_misc_escape(JSContext*, JSValue this_val, int argc, JSValue argv[]);
JSValue js_misc_quote(JSContext*, JSValue this_val, int argc, JSValue argv[]);
JSValue js_misc_error(JSContext*, JSValue this_val, int argc, JSValue argv[]);

/**
 * @}
 */

#endif /* defined(QUICKJS_MISC_H) */
