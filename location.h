#ifndef LOCATION_H
#define LOCATION_H

#include "utils.h"

typedef struct Location {
  char* file;
  uint32_t line;
  uint32_t column;
  int64_t pos;
  char* str;
} Location;

void location_print(const Location*, DynBuf* dbuf);
Location location_clone(const Location*, JSContext* ctx);
void location_init(Location*);
void location_zero(Location*);
void location_add(Location*, const Location* other);
void location_sub(Location*, const Location* other);
void location_count(Location*, const char* x, size_t n);
void location_free(Location*, JSContext* ctx);
void location_free_rt(Location*, JSRuntime* rt);

static inline int
location_isnull(const Location* loc) {
  return !loc->file && !loc->line && !loc->column && !loc->pos;
}

#endif /* defined(LOCATION_H) */