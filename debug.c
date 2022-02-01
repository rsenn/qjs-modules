#undef DEBUG_ALLOC
#include "debug.h"
#include <quickjs.h>
#include <list.h>
#include <cutils.h>
#include "defines.h"

#if 0
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
#endif

thread_local struct list_head alloc_block_list = {0, 0};

static inline void
add_to_list(struct list_head* el, struct list_head* head) {
  if(alloc_block_list.prev == 0 && alloc_block_list.next == 0)
    init_list_head(&alloc_block_list);
  list_add_tail(el, head);
}

void*
debug_malloc(size_t n, const char* file, int line) {
  struct alloc_block* ptr;

  if((ptr = malloc(n + sizeof(struct alloc_block)))) {
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
  struct alloc_block* ptr;

  m *= n;
  n = 1;

  if((ptr = calloc(m + sizeof(struct alloc_block), 1))) {
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
  struct alloc_block* ptr = &((struct alloc_block*)p)[-1];

  list_del(&ptr->link);

  if((ptr = realloc(ptr, n + sizeof(struct alloc_block)))) {
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
  struct alloc_block* ptr;
  size_t len = strlen(s);

  if((ptr = malloc(len + 1 + sizeof(struct alloc_block)))) {
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
  struct alloc_block* ptr = &((struct alloc_block*)p)[-1];
  list_del(&ptr->link);
  free(ptr);
  return 0;
}

void*
debug_js_malloc(JSContext* ctx, size_t n, const char* file, int line) {
  struct alloc_block* ptr;

  if((ptr = js_malloc(ctx, n + sizeof(struct alloc_block)))) {
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
  struct alloc_block* ptr;

  if((ptr = js_malloc(ctx, n + sizeof(struct alloc_block)))) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = n;

    add_to_list(&ptr->link, &alloc_block_list);

    memset(&ptr[1], 0, n);
    return &ptr[1];
  }
  return 0;
}

void*
debug_js_realloc(JSContext* ctx, void* p, size_t n, const char* file, int line) {
  struct alloc_block* ptr = &((struct alloc_block*)p)[-1];

  list_del(&ptr->link);

  if((ptr = js_realloc(ctx, ptr, n + sizeof(struct alloc_block)))) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = n;

    add_to_list(&ptr->link, &alloc_block_list);
    return &ptr[1];
  }
  return 0;
}

void*
debug_js_strdup(JSContext* ctx, const char* s, const char* file, int line) {
  struct alloc_block* ptr;
  size_t len = strlen(s);

  if((ptr = js_malloc(ctx, len + 1 + sizeof(struct alloc_block)))) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = len + 1;

    add_to_list(&ptr->link, &alloc_block_list);

    memcpy(&ptr[1], s, len + 1);
    return &ptr[1];
  }
  return 0;
}

void*
debug_js_strndup(JSContext* ctx, const char* s, size_t len, const char* file, int line) {
  struct alloc_block* ptr;
  char* p;

  if((ptr = js_malloc(ctx, len + 1 + sizeof(struct alloc_block)))) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = len + 1;

    add_to_list(&ptr->link, &alloc_block_list);

    p = (char*)&ptr[1];
    memcpy(p, s, len);
    p[len] = '\0';

    return p;
  }
  return 0;
}

size_t
debug_js_malloc_usable_size(JSContext* ctx, const void* p, const char* file, int line) {
  struct alloc_block* ptr = &((struct alloc_block*)p)[-1];

  return malloc_usable_size(ptr);
}

void
debug_js_free(JSContext* ctx, void* p, const char* file, int line) {
  struct alloc_block* ptr = &((struct alloc_block*)p)[-1];
  list_del(&ptr->link);
  js_free(ctx, ptr);
}

void*
debug_js_malloc_rt(JSRuntime* rt, size_t n, const char* file, int line) {
  struct alloc_block* ptr;

  if((ptr = js_malloc_rt(rt, n + sizeof(struct alloc_block)))) {
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
  struct alloc_block* ptr;

  if((ptr = js_malloc_rt(rt, n + sizeof(struct alloc_block)))) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = n;

    add_to_list(&ptr->link, &alloc_block_list);
    memset(&ptr[1], 0, n);
    return &ptr[1];
  }
  return 0;
}

void*
debug_js_realloc_rt(JSRuntime* rt, void* p, size_t n, const char* file, int line) {
  struct alloc_block* ptr = &((struct alloc_block*)p)[-1];

  list_del(&ptr->link);

  if((ptr = js_realloc_rt(rt, ptr, n + sizeof(struct alloc_block)))) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = n;

    add_to_list(&ptr->link, &alloc_block_list);
    return &ptr[1];
  }
  return 0;
}

void
debug_js_free_rt(JSRuntime* rt, void* p, const char* file, int line) {
  struct alloc_block* ptr = &((struct alloc_block*)p)[-1];
  list_del(&ptr->link);
  js_free_rt(rt, ptr);
}
