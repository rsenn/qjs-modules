#ifndef LOCATION_H
#define LOCATION_H

#include "utils.h"
#include "char-utils.h"

#ifdef JS_LOCATION_MODULE
#define LOCATION_API VISIBLE
#else
#define LOCATION_API
#endif

/**
 * \defgroup location Source text location
 * @{
 */
typedef struct {
  int ref_count;
  int32_t file;
  uint32_t line;
  int32_t column;
  int64_t char_offset, byte_offset;
  char* str;
  BOOL read_only : 1;
} Location;

#define LOCATION() (Location){0, -1, 0, 0, 0, 0, 0, FALSE};
#define LOCATION_FILE(atom) (Location){0, (atom), 0, 0, 0, 0, 0, FALSE};

LOCATION_API void location_print(const Location*, DynBuf*, JSContext*);
LOCATION_API char* location_tostring(const Location*, JSContext*);
LOCATION_API char* location_file(const Location*, JSContext*);
LOCATION_API JSValue location_tovalue(const Location*, JSContext*);
LOCATION_API void location_init(Location*);
LOCATION_API void location_zero(Location*);
LOCATION_API void location_add(Location*, const Location*);
LOCATION_API void location_sub(Location*, const Location*);
LOCATION_API BOOL location_release(Location*, JSRuntime*);
LOCATION_API Location* location_free(Location*, JSRuntime*);
LOCATION_API size_t location_count(Location*, const uint8_t*, size_t);
LOCATION_API Location* location_copy(Location*, const Location*, JSContext*);
LOCATION_API Location* location_clone(const Location*, JSContext*);
LOCATION_API Location* location_new(JSContext*);
LOCATION_API Location* location_dup(Location*);

/**
 * @}
 */
#endif /* defined(LOCATION_H) */
