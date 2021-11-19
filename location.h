#ifndef LOCATION_H
#define LOCATION_H

#include "utils.h"
#include "char-utils.h"

/**
 * \defgroup location Source text location
 * @{
 */
typedef struct source_location {
  char* file;
  uint32_t line;
  uint32_t column;
  int64_t pos;
  char* str;
} Location;

void location_print(const Location*, DynBuf* dbuf);
char* location_tostring(const Location*, JSContext* ctx);
JSValue location_tovalue(const Location*, JSContext* ctx);
Location location_clone(const Location*, JSContext* ctx);
void location_init(Location*);
void location_zero(Location*);
void location_add(Location*, const Location* other);
void location_sub(Location*, const Location* other);
/*void location_count(Location*, const char* x, size_t n);*/
void location_free(Location*, JSContext* ctx);
void location_free_rt(Location*, JSRuntime* rt);

static inline int
location_isnull(const Location* loc) {
  return !loc->file && !loc->line && !loc->column && !loc->pos;
}

static inline void
location_count(Location* loc, const char* x, size_t n) {
  size_t i;
  for(i = 0; i < n;) {
    size_t bytes = byte_charlen(&x[i], n - i);

    if(x[i] == '\n') {
      loc->line++;
      loc->column = 0;
    } else {
      loc->column++;
    }

    loc->pos++;
    i += bytes;
  }
}

/**
 * @}
 */
#endif /* defined(LOCATION_H) */
