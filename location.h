#ifndef LOCATION_H
#define LOCATION_H

#include "utils.h"
#include "char-utils.h"

/**
 * \defgroup location Source text location
 * @{
 */
typedef struct source_location {
  int ref_count;
  int32_t file;
  uint32_t line, column;
  int64_t char_offset, byte_offset;
  char* str;
  BOOL read_only : 1;
} Location;

void location_print(const Location*, DynBuf*, JSContext*);
char* location_tostring(const Location*, JSContext*);
char* location_file(const Location*, JSContext*);
JSValue location_tovalue(const Location*, JSContext*);
void location_init(Location*);
void location_zero(Location*);
void location_add(Location*, const Location*);
void location_sub(Location*, const Location*);
BOOL location_release_rt(Location*, JSRuntime*);
Location* location_free_rt(Location*, JSRuntime*);

void location_count(Location*, const char*, size_t);
Location* location_copy(Location*, const Location*, JSContext*);
Location* location_clone(const Location*, JSContext*);
Location* location_new(JSContext*);

static inline BOOL
location_release(Location* loc, JSContext* ctx) {
  return location_release_rt(loc, JS_GetRuntime(ctx));
}

static inline Location*
location_free(Location* loc, JSContext* ctx) {
  return location_free_rt(loc, JS_GetRuntime(ctx));
}

static inline int
location_isnull(const Location* loc) {
  return loc->file == -1 && !loc->line && !loc->column && !loc->char_offset;
}

static inline Location*
location_dup(Location* loc) {
  loc->ref_count++;
  return loc;
}

/**
 * @}
 */
#endif /* defined(LOCATION_H) */
