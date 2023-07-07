#define _IN_DEBUG_C 1
#include "debug.h"
#include <quickjs.h>
#include <list.h>
#include <cutils.h>
#include <assert.h>
#include "defines.h"
#include <string.h>
#include <stdlib.h>

/**
 * \addtogroup debug
 * @{
 */
struct alloc_block {
  struct list_head link;
  const char* file;
  int line;
  size_t size;
};

#define ALLOC_BLOCK_SIZE sizeof(struct alloc_block)
#define ALLOC_BLOCK(p) (((struct alloc_block*)(p)) - 1)
#define ALLOC_PTR struct alloc_block*

#undef malloc
#undef calloc
#undef realloc
#undef strdup
#undef free
#undef js_malloc
#undef js_mallocz
#undef js_realloc
#undef js_strdup
#undef js_strndup
#undef js_free
#undef js_malloc_usable_size
#undef js_malloc_rt
#undef js_mallocz_rt
#undef js_realloc_rt
#undef js_free_rt
#undef js_malloc_usable_size_rt

thread_local struct list_head alloc_block_list = {0, 0};

static inline void
add_to_list(struct list_head* el, struct list_head* head) {
  if(alloc_block_list.prev == 0 && alloc_block_list.next == 0)
    init_list_head(&alloc_block_list);

  list_add_tail(el, head);
}

int64_t
check_pointer(void* p) {
  ALLOC_PTR ptr = ALLOC_BLOCK(p);
  struct list_head* link;
  int64_t ret = 0;
  list_for_each(link, &alloc_block_list) {
    if(link == &ptr->link)
      return ret;
    ret++;
  }
  // assert(0);
  return -1;
}

void*
debug_malloc(size_t n, const char* file, int line) {
  ALLOC_PTR ptr;

  if((ptr = malloc(n + ALLOC_BLOCK_SIZE))) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = n;

    add_to_list(&ptr->link, &alloc_block_list);
    return &ptr[1];
  }
  return 0;
}

void*
debug_calloc(size_t m, size_t n, const char* file, int line) {
  ALLOC_PTR ptr;

  m *= n;
  n = 1;

  if((ptr = calloc(m + ALLOC_BLOCK_SIZE, 1))) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = n;

    add_to_list(&ptr->link, &alloc_block_list);
    return &ptr[1];
  }
  return 0;
}

void*
debug_realloc(void* p, size_t n, const char* file, int line) {
  ALLOC_PTR ptr;

  if(p) {

    check_pointer(p);

    ptr = ALLOC_BLOCK(p);
    list_del(&ptr->link);

    if(n == 0) {
      free(ptr);
      return 0;
    }

    ptr = realloc(ptr, n + ALLOC_BLOCK_SIZE);
  } else {
    ptr = malloc(n + ALLOC_BLOCK_SIZE);
  }

  if(ptr) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = n;

    add_to_list(&ptr->link, &alloc_block_list);
    return &ptr[1];
  }
  return 0;
}

void*
debug_strdup(const char* s, const char* file, int line) {
  ALLOC_PTR ptr;
  size_t len = strlen(s);

  if((ptr = malloc(len + 1 + ALLOC_BLOCK_SIZE))) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = len + 1;

    add_to_list(&ptr->link, &alloc_block_list);

    memcpy(&ptr[1], s, len + 1);
    return &ptr[1];
  }
  return 0;
}

void
debug_free(void* p, const char* file, int line) {
  ALLOC_PTR ptr = ALLOC_BLOCK(p);

  list_del(&ptr->link);

  memset(ptr, 0xff, ALLOC_BLOCK_SIZE);

  free(ptr);
}

void*
debug_js_malloc(JSContext* ctx, size_t n, const char* file, int line) {
  ALLOC_PTR ptr;

  if((ptr = js_malloc(ctx, n + ALLOC_BLOCK_SIZE))) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = n;

    add_to_list(&ptr->link, &alloc_block_list);
    return &ptr[1];
  }
  return 0;
}

void*
debug_js_mallocz(JSContext* ctx, size_t n, const char* file, int line) {
  ALLOC_PTR ptr;

  if((ptr = js_mallocz(ctx, n + ALLOC_BLOCK_SIZE))) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = n;

    add_to_list(&ptr->link, &alloc_block_list);
    return &ptr[1];
  }
  return 0;
}

void*
debug_js_realloc(JSContext* ctx, void* p, size_t n, const char* file, int line) {
  ALLOC_PTR ptr;

  if(p) {

    check_pointer(p);

    ptr = ALLOC_BLOCK(p);
    list_del(&ptr->link);

    if(n == 0) {
      js_free(ctx, ptr);
      return 0;
    }

    ptr = js_realloc(ctx, ptr, n + ALLOC_BLOCK_SIZE);
  } else {
    ptr = js_malloc(ctx, n + ALLOC_BLOCK_SIZE);
  }

  if(ptr) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = n;

    add_to_list(&ptr->link, &alloc_block_list);
    return &ptr[1];
  }
  return 0;
}

void*
debug_js_realloc2(JSContext* ctx, void* p, size_t size, size_t* pslack, const char* file, int line) {
  void* ptr;

  if((ptr = debug_js_realloc(ctx, p, size, file, line))) {
    if(pslack) {
      size_t new_size = debug_js_malloc_usable_size(ctx, ptr, file, line);
      *pslack = (new_size > size) ? new_size - size : 0;
    }
  }
  return ptr;
}

void*
debug_js_strdup(JSContext* ctx, const char* s, const char* file, int line) {
  ALLOC_PTR ptr;
  size_t len = strlen(s);

  if((ptr = js_malloc(ctx, len + 1 + ALLOC_BLOCK_SIZE))) {
    char* p = (char*)&ptr[1];
    ptr->file = file;
    ptr->line = line;
    ptr->size = len + 1;

    add_to_list(&ptr->link, &alloc_block_list);

    memcpy(p, s, len + 1);
    return p;
  }
  return 0;
}

void*
debug_js_strndup(JSContext* ctx, const char* s, size_t len, const char* file, int line) {
  ALLOC_PTR ptr;

  if((ptr = js_malloc(ctx, len + 1 + ALLOC_BLOCK_SIZE))) {
    char* p = (char*)&ptr[1];
    ptr->file = file;
    ptr->line = line;
    ptr->size = len + 1;

    add_to_list(&ptr->link, &alloc_block_list);

    memcpy(p, s, len);
    p[len] = '\0';

    return p;
  }
  return 0;
}

size_t
debug_js_malloc_usable_size(JSContext* ctx, const void* p, const char* file, int line) {
  ALLOC_PTR ptr = ALLOC_BLOCK(p);

  return js_malloc_usable_size(ctx, ptr) - ALLOC_BLOCK_SIZE;
}

void
debug_js_free(JSContext* ctx, void* p, const char* file, int line) {
  ALLOC_PTR ptr;

  check_pointer(p);
  ptr = ALLOC_BLOCK(p);
  list_del(&ptr->link);

  memset(ptr, 0xff, ALLOC_BLOCK_SIZE);

  js_free(ctx, ptr);
}

void*
debug_js_malloc_rt(JSRuntime* rt, size_t n, const char* file, int line) {
  ALLOC_PTR ptr;

  if((ptr = js_malloc_rt(rt, n + ALLOC_BLOCK_SIZE))) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = n;

    add_to_list(&ptr->link, &alloc_block_list);
    return &ptr[1];
  }
  return 0;
}

void*
debug_js_mallocz_rt(JSRuntime* rt, size_t n, const char* file, int line) {
  ALLOC_PTR ptr;

  if((ptr = js_mallocz_rt(rt, n + ALLOC_BLOCK_SIZE))) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = n;

    add_to_list(&ptr->link, &alloc_block_list);
    return &ptr[1];
  }
  return 0;
}

void*
debug_js_realloc_rt(JSRuntime* rt, void* p, size_t n, const char* file, int line) {
  ALLOC_PTR ptr;

  if(p) {
    check_pointer(p);

    ptr = ALLOC_BLOCK(p);
    list_del(&ptr->link);
    if(n == 0) {
      js_free_rt(rt, ptr);
      return 0;
    }
    ptr = js_realloc_rt(rt, ptr, n + ALLOC_BLOCK_SIZE);
  } else {
    ptr = js_malloc_rt(rt, n + ALLOC_BLOCK_SIZE);
  }

  if(ptr) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = n;

    add_to_list(&ptr->link, &alloc_block_list);
    return &ptr[1];
  }
  return 0;
}

size_t
debug_js_malloc_usable_size_rt(JSRuntime* rt, const void* p, const char* file, int line) {
  ALLOC_PTR ptr = ALLOC_BLOCK(p);

  return js_malloc_usable_size_rt(rt, ptr) - ALLOC_BLOCK_SIZE;
}

void
debug_js_free_rt(JSRuntime* rt, void* p, const char* file, int line) {
  ALLOC_PTR ptr;
  check_pointer(p);

  ptr = ALLOC_BLOCK(p);

  // printf("debug_js_free_rt %p\n", p);

  list_del(&ptr->link);

  memset(ptr, 0xff, ALLOC_BLOCK_SIZE);

  js_free_rt(rt, ptr);
}

#undef malloc
#undef calloc
#undef realloc
#undef strdup
#undef free
#undef js_malloc
#undef js_mallocz
#undef js_realloc
#undef js_strdup
#undef js_strndup
#undef js_malloc_usable_size
#undef js_free
#undef js_malloc_rt
#undef js_mallocz_rt
#undef js_realloc_rt
#undef js_malloc_usable_size_rt
#undef js_free_rt

void*
orig_malloc(size_t size) {
  return malloc(size);
}

void*
orig_calloc(size_t nelem, size_t elemsz) {
  return calloc(nelem, elemsz);
}

void*
orig_realloc(void* ptr, size_t size) {
  return realloc(ptr, size);
}

void*
orig_strdup(const char* str) {
  return strdup(str);
}

void
orig_free(void* ptr) {
  free(ptr);
}

void*
orig_js_malloc(JSContext* ctx, size_t size) {
  return js_malloc(ctx, size);
}

void*
orig_js_mallocz(JSContext* ctx, size_t size) {
  return js_mallocz(ctx, size);
}

void*
orig_js_realloc(JSContext* ctx, void* p, size_t size) {
  return js_realloc(ctx, p, size);
}

void*
orig_js_strdup(JSContext* ctx, const char* str) {
  return js_strdup(ctx, str);
}

void*
orig_js_strndup(JSContext* ctx, const char* str, size_t size) {
  return js_strndup(ctx, str, size);
}

size_t
orig_js_malloc_usable_size(JSContext* ctx, const void* p) {
  return js_malloc_usable_size(ctx, p);
}

void
orig_js_free(JSContext* ctx, void* p) {
  return js_free(ctx, p);
}

void*
orig_js_malloc_rt(JSRuntime* rt, size_t size) {
  return js_malloc_rt(rt, size);
}

void*
orig_js_mallocz_rt(JSRuntime* rt, size_t size) {
  return js_mallocz_rt(rt, size);
}

void*
orig_js_realloc_rt(JSRuntime* rt, void* p, size_t size) {
  return js_realloc_rt(rt, p, size);
}

size_t
orig_js_malloc_usable_size_rt(JSRuntime* rt, const void* p) {
  return js_malloc_usable_size_rt(rt, p);
}

void
orig_js_free_rt(JSRuntime* rt, void* p) {
  return js_free_rt(rt, p);
}

/**
 * @}
 */
