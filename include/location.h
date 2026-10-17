#ifndef LOCATION_H
#define LOCATION_H

#include "char-utils.h"
#include "utils.h"
#include "srcpos.h"

#ifdef JS_LOCATION_MODULE
#define LOCATION_API VISIBLE
#else
#define LOCATION_API
#endif

/**
 * \defgroup location location: Source text location
 * @{
 */
typedef struct {
  int ref_count;
  union {
    int32_t file;
    char* filename;
  };
  int32_t line, column;
  int64_t char_offset, byte_offset;
  BOOL read_only : 1, has_filename : 1, cr : 1; /* cr: last byte counted was "\r" */
} Location;

#define LOCATION() (Location){0, {-1}, 0, 0, 0, 0, FALSE, FALSE};
#define LOCATION_FILENAME(fn) (Location){0, {.filename = (fn)}, 0, 0, 0, 0, FALSE, TRUE};
#define LOCATION_FILE(atom) (Location){0, {(atom)}, 0, 0, 0, 0, FALSE, FALSE};
#define LOCATION_POS(l) ((l)->byte_offset)

LOCATION_API void location_print(const Location*, DynBuf*, JSContext*);
LOCATION_API char* location_tostring(const Location*, JSContext*);
LOCATION_API char* location_file(const Location*, JSContext*);
LOCATION_API JSValue location_tovalue(const Location*, JSContext*);
LOCATION_API void location_init(Location*);
LOCATION_API void location_zero(Location*);
LOCATION_API void location_release(Location*, JSRuntime*);
LOCATION_API void location_free(Location*, JSRuntime*);
LOCATION_API size_t location_count(Location*, const uint8_t*, size_t);
LOCATION_API size_t location_nextchar(Location*, int c);
/* location_from_srcpos: copies the position of an engine-free SourcePos into `loc`
 * (Location is 0-based, SourcePos 1-based); file and ref_count are left alone. */
static inline void
location_from_srcpos(Location* loc, const SourcePos* pos) {
  loc->line = pos->line - 1;
  loc->column = pos->column - 1;
  loc->char_offset = pos->char_offset;
  loc->byte_offset = pos->byte_offset;
  loc->cr = pos->cr;
}

LOCATION_API BOOL location_equal(const Location*, const Location*);
LOCATION_API void location_copy(Location*, const Location*, JSContext*);
LOCATION_API void location_copy_file(Location*, const Location*, JSContext*);
LOCATION_API Location* location_clone(const Location*, JSContext*);
LOCATION_API Location* location_new(JSContext*);
LOCATION_API Location* location_dup(Location*);
LOCATION_API void location_set_file(Location*, JSAtom, JSContext*);
LOCATION_API void location_set_filename(Location*, const char*, JSContext*);

static inline int64_t
location_byteoffset(const Location* loc) {
  if(loc)
    return loc->byte_offset;
  return -1;
}

static inline int64_t
location_charoffset(const Location* loc) {
  if(loc)
    return loc->char_offset;
  return -1;
}

/**
 * @}
 */
#endif /* defined(LOCATION_H) */
