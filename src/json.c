#include "json.h"

BOOL
json_init(JsonParser* json, JSValueConst input, JSContext* ctx) {
  json->reader = reader_from_js(input, ctx);

  if(!json->reader.read)
    return FALSE;

  return TRUE;
}
