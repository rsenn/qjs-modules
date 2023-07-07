#ifndef DEBUG_H
#define DEBUG_H

#include <quickjs.h>
#include <cutils.h>
#include "defines.h"

#ifndef QUICKJS_H
#error "quickjs.h not included"
#endif

/**
 * \defgroup debug debug: Debugging helpers
 * @{
 */

extern thread_local struct list_head alloc_block_list;

int64_t check_pointer(void*);
void* debug_malloc(size_t, const char*, int);
void* debug_calloc(size_t, size_t, const char*, int line);
void* debug_realloc(void*, size_t, const char*, int line);
void* debug_strdup(const char*, const char*, int);
void debug_free(void*, const char*, int);
void* debug_js_malloc(JSContext*, size_t, const char*, int line);
void* debug_js_mallocz(JSContext*, size_t, const char*, int line);
void* debug_js_realloc(JSContext*, void*, size_t, const char* file, int line);
void* debug_js_realloc2(JSContext*, void*, size_t, size_t* pslack, const char* file, int line);
void* debug_js_strdup(JSContext*, const char*, const char*, int line);
void* debug_js_strndup(JSContext*, const char*, size_t, const char* file, int line);
size_t debug_js_malloc_usable_size(JSContext*, const void*, const char*, int line);
void debug_js_free(JSContext*, void*, const char*, int line);
void* debug_js_malloc_rt(JSRuntime*, size_t, const char*, int line);
void* debug_js_mallocz_rt(JSRuntime*, size_t, const char*, int line);
void* debug_js_realloc_rt(JSRuntime*, void*, size_t, const char* file, int line);
size_t debug_js_malloc_usable_size_rt(JSRuntime*, const void*, const char*, int line);
void debug_js_free_rt(JSRuntime*, void*, const char*, int line);

#if !defined(_IN_DEBUG_C)

#if defined(DEBUG_ALLOC)
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
#define js_malloc_usable_size_rt(rt, ptr) debug_js_malloc_usable_size_rt(rt, ptr, __FILE__, __LINE__)
#define js_free_rt(rt, ptr) debug_js_free_rt(rt, ptr, __FILE__, __LINE__)
#endif

#ifdef DEBUG_ALLOC
#define realloc_helper(name) \
  void* name(void* ptr, size_t size) { \
    if(ptr == 0) \
      return debug_malloc(size, __FILE__, __LINE__); \
    if(size == 0) \
      return debug_free(ptr, __FILE__, __LINE__); \
    return debug_realloc(ptr, size, __FILE__, __LINE__); \
  }
#define realloc2_helper(name) \
  void* name(void* opaque, void* ptr, size_t size) { \
    if(ptr == 0) \
      return debug_malloc(size, __FILE__, __LINE__); \
    if(size == 0) { \
      debug_free(ptr, __FILE__, __LINE__); \
      return 0; \
    } \
    return debug_realloc(ptr, size, __FILE__, __LINE__); \
  }
#define js_realloc_helper(name) \
  void* name(JSContext* ctx, void* ptr, size_t size) { \
    if(ptr == 0) \
      return debug_js_malloc(ctx, size, __FILE__, __LINE__); \
    if(size == 0) { \
      debug_js_free(ctx, ptr, __FILE__, __LINE__); \
      return 0; \
    } \
    return debug_js_realloc(ctx, ptr, size, __FILE__, __LINE__); \
  }
#define js_realloc_rt_helper(name) \
  void* name(JSRuntime* rt, void* ptr, size_t size) { \
    if(ptr == 0) \
      return debug_js_malloc_rt(rt, size, __FILE__, __LINE__); \
    if(size == 0) { \
      debug_js_free_rt(rt, ptr, __FILE__, __LINE__); \
      return 0; \
    } \
    return debug_js_realloc_rt(rt, ptr, size, __FILE__, __LINE__); \
  }
#else
#define realloc_helper(name) \
  void* name(void* ptr, size_t size) { \
    if(ptr == 0) \
      return malloc(size); \
    if(size == 0) { \
      free(ptr); \
      return 0; \
    } \
    return realloc(ptr, size); \
  }
#define realloc2_helper(name) \
  void* name(void* opaque, void* ptr, size_t size) { \
    if(ptr == 0) \
      return malloc(size); \
    if(size == 0) { \
      free(ptr); \
      return 0; \
    } \
    return realloc(ptr, size); \
  }
#define js_realloc_helper(name) \
  void* name(JSContext* ctx, void* ptr, size_t size) { \
    if(ptr == 0) \
      return orig_js_malloc(ctx, size); \
    if(size == 0) { \
      orig_js_free(ctx, ptr); \
      return 0; \
    } \
    return orig_js_realloc(ctx, ptr, size); \
  }
#define js_realloc_rt_helper(name) \
  void* name(JSRuntime* rt, void* ptr, size_t size) { \
    if(ptr == 0) \
      return orig_js_malloc_rt(rt, size); \
    if(size == 0) { \
      orig_js_free_rt(rt, ptr); \
      return 0; \
    } \
    return orig_js_realloc_rt(rt, ptr, size); \
  }
#endif
#endif

void* orig_malloc(size_t);
void* orig_calloc(size_t, size_t);
void* orig_realloc(void*, size_t);
void* orig_strdup(const char*);
void orig_free(void*);
void* orig_js_malloc(JSContext*, size_t);
void* orig_js_mallocz(JSContext*, size_t);
void* orig_js_realloc(JSContext*, void*, size_t);
void* orig_js_strdup(JSContext*, const char*);
void* orig_js_strndup(JSContext*, const char*, size_t);
size_t orig_js_malloc_usable_size(JSContext*, const void*);
void orig_js_free(JSContext*, void*);
void* orig_js_malloc_rt(JSRuntime*, size_t);
void* orig_js_mallocz_rt(JSRuntime*, size_t);
void* orig_js_realloc_rt(JSRuntime*, void*, size_t);
size_t orig_js_malloc_usable_size_rt(JSRuntime*, const void*);
void orig_js_free_rt(JSRuntime*, void*);

/**
 * @}
 */
#endif /* defined(DEBUG_H) */
