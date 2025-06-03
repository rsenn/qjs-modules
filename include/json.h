#ifndef JSON_H
#define JSON_H

#include "stream-utils.h"

/**
 * \defgroup json json: JSON parser
 * @{
 */

typedef struct {
  Reader reader;
} JsonParser;

BOOL json_init(JsonParser*, JSValueConst, JSContext*);

/**
 * @}
 */
#endif /* defined(JSON_H) */
