#define _IN_DEBUG_C 1
#include "debug.h"
#include <quickjs.h>
#include <list.h>
#include <cutils.h>
#include <assert.h>
#include "defines.h"

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
  struct alloc_block* ptr = &(((struct alloc_block*)p)[-1]);
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
  struct alloc_block* ptr = &(((struct alloc_block*)p)[-1]);

  if(p)
    list_del(&ptr->link);

  if((ptr = realloc(p ? ptr : 0, n ? n + sizeof(struct alloc_block) : 0))) {
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
  struct alloc_block* ptr = &(((struct alloc_block*)p)[-1]);

  list_del(&ptr->link);

  memset(ptr, 0xff, sizeof(struct alloc_block));

  free(ptr);
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

  if((ptr = js_mallocz(ctx, n + sizeof(struct alloc_block)))) {
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
  struct alloc_block* ptr;

  if(p) {
    if(n == 0) {
      debug_js_free(ctx, p, file, line);
      return;
    }
    check_pointer(p);

    ptr = p ? &(((struct alloc_block*)p)[-1]) : 0;
    list_del(&ptr->link);
  } else {
    return debug_js_malloc(ctx, n, file, line);
  }
  n = n ? n + sizeof(struct alloc_block) : 0;

  if((ptr = js_realloc(ctx, ptr, n))) {
    ptr->file = file;
    ptr->line = line;
    ptr->size = n;

    add_to_list(&ptr->link, &alloc_block_list);
    return &ptr[1];
  }
  return 0;
}

void*
debug_js_realloc2(JSContext* ctx, void* p, size_t n, size_t* pslack, const char* file, int line) {
  struct alloc_block* ptr;

  if(p) {
    if(n == 0) {
      debug_js_free(ctx, p, file, line);
      return;
    }
    check_pointer(p);

    ptr = p ? &(((struct alloc_block*)p)[-1]) : 0;
    list_del(&ptr->link);
  } else {
    return debug_js_malloc(ctx, n, file, line);
  }
  n = n ? n + sizeof(struct alloc_block) : 0;

  if((ptr = js_realloc2(ctx, ptr, n, pslack))) {
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
  struct alloc_block* ptr;

  if((ptr = js_malloc(ctx, len + 1 + sizeof(struct alloc_block)))) {
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
  struct alloc_block* ptr = &(((struct alloc_block*)p)[-1]);

  return js_malloc_usable_size(ctx, ptr);
}

void
debug_js_free(JSContext* ctx, void* p, const char* file, int line) {
  struct alloc_block* ptr;

  check_pointer(p);
  ptr = &(((struct alloc_block*)p)[-1]);
  list_del(&ptr->link);

  memset(ptr, 0xff, sizeof(struct alloc_block));

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

  if((ptr = js_mallocz_rt(rt, n + sizeof(struct alloc_block)))) {
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
  struct alloc_block* ptr;

  if(p) {
    if(n == 0) {
      debug_js_free_rt(rt, p, file, line);
      return;
    }
    check_pointer(p);

    ptr = p ? &(((struct alloc_block*)p)[-1]) : 0;
    list_del(&ptr->link);
  } else {
    return debug_js_malloc_rt(rt, n, file, line);
  }
  n = n ? n + sizeof(struct alloc_block) : 0;

  if((ptr = js_realloc_rt(rt, ptr, n))) {
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
  struct alloc_block* ptr = &(((struct alloc_block*)p)[-1]);

  return js_malloc_usable_size_rt(rt, ptr);
}

void
debug_js_free_rt(JSRuntime* rt, void* p, const char* file, int line) {
  struct alloc_block* ptr;
  check_pointer(p);

  ptr = &(((struct alloc_block*)p)[-1]);

  // printf("debug_js_free_rt %p\n", p);

  list_del(&ptr->link);

  memset(ptr, 0xff, sizeof(struct alloc_block));

  js_free_rt(rt, ptr);
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
