#ifndef DEBUG_H
#define DEBUG_H

#include <quickjs.h>
#include <list.h>
#include <cutils.h>
#include "defines.h"

struct alloc_block {
  const char* file;
  int line;
  size_t size;
  struct list_head link;
};

extern thread_local struct list_head alloc_block_list;

void* debug_malloc(size_t, const char*, int);
void* debug_calloc(size_t, size_t, const char*, int line);
void* debug_realloc(void*, size_t, const char*, int line);
void* debug_strdup(const char*, const char*, int);
void debug_free(void*, const char*, int);
void* debug_js_malloc(JSContext*, size_t, const char*, int line);
void* debug_js_mallocz(JSContext*, size_t, const char*, int line);
void* debug_js_realloc(JSContext*, void*, size_t, const char* file, int line);
void* debug_js_strdup(JSContext*, const char*, const char*, int line);
void* debug_js_strndup(JSContext*, const char*, size_t, const char* file, int line);
size_t debug_js_malloc_usable_size(JSContext*, const void*, const char* file, int line);
void debug_js_free(JSContext*, void*, const char*, int line);
void* debug_js_malloc_rt(JSRuntime*, size_t, const char*, int line);
void* debug_js_mallocz_rt(JSRuntime*, size_t, const char*, int line);
void* debug_js_realloc_rt(JSRuntime*, void*, size_t, const char* file, int line);
void debug_js_free_rt(JSRuntime*, void*, const char*, int line);

#ifdef DEBUG_ALLOC
#define malloc(size) debug_malloc(size, __FILE__, __LINE__)
#define calloc(nelem, size) debug_calloc(nelem, size, __FILE__, __LINE__)
#define realloc(ptr, size) debug_realloc(ptr, size, __FILE__, __LINE__)
#define strdup(str) debug_strdup(str, __FILE__, __LINE__)
#define free(ptr) debug_free(ptr, __FILE__, __LINE__)
#define js_malloc(ctx, size) debug_js_malloc(ctx, size, __FILE__, __LINE__)
#define js_mallocz(ctx, size) debug_js_mallocz(ctx, size, __FILE__, __LINE__)
#define js_realloc(ctx, ptr, size) debug_js_realloc(ctx, ptr, size, __FILE__, __LINE__)
#define js_strdup(ctx, str) debug_js_strdup(ctx, str, __FILE__, __LINE__)
#define js_strndup(ctx, str, len) debug_js_strndup(ctx, str, len, __FILE__, __LINE__)
#define js_free(ctx, ptr) debug_js_free(ctx, ptr, __FILE__, __LINE__)
#define js_malloc_usable_size(ctx, ptr) debug_js_malloc_usable_size(ctx, ptr, __FILE__, __LINE__)
#define js_malloc_rt(rt, size) debug_js_malloc_rt(rt, size, __FILE__, __LINE__)
#define js_mallocz_rt(rt, size) debug_js_mallocz_rt(rt, size, __FILE__, __LINE__)
#define js_realloc_rt(rt, ptr, size) debug_js_realloc_rt(rt, ptr, size, __FILE__, __LINE__)
#define js_free_rt(rt, ptr) debug_js_free_rt(rt, ptr, __FILE__, __LINE__)
#endif

#endif /* defined(DEBUG_H) */
