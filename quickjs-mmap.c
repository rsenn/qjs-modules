#include "cutils.h"
#include "quickjs.h"
#include "utils.h"
#include <sys/mman.h>

static void
js_mmap_free_func(JSRuntime* rt, void* opaque, void* ptr) {
  munmap(ptr, (size_t)opaque);
}

static JSValue
js_mmap_map(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  uint64_t addr, length, offset;
  int32_t prot, flags, fd;
  void* ptr;

  if(JS_ToIndex(ctx, &addr, argv[0]))
    return JS_EXCEPTION;
  if(JS_ToIndex(ctx, &length, argv[1]))
    return JS_EXCEPTION;
  if(argc <= 2 || JS_ToInt32(ctx, &prot, argv[2]))
    prot = PROT_READ | PROT_WRITE;
  if(argc <= 3 || JS_ToInt32(ctx, &flags, argv[3]))
    flags = MAP_ANONYMOUS;
  if(argc <= 4 || JS_ToInt32(ctx, &fd, argv[4]))
    fd = -1;
  if(argc <= 5 || JS_ToIndex(ctx, &offset, argv[5]))
    offset = 0;

  ptr = mmap((void*)addr, length, prot, flags, fd, offset);

  if(ptr == 0)
    return JS_EXCEPTION;

  return JS_NewArrayBuffer(ctx, ptr, length, &js_mmap_free_func, (void*)length, !!(flags & MAP_SHARED));
}

static JSValue
js_mmap_unmap(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JS_DetachArrayBuffer(ctx, argv[0]);
  return JS_UNDEFINED;
}

static JSValue
js_mmap_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  if(js_is_arraybuffer(ctx, argv[0])) {
    uint8_t* data;
    size_t len;
    if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
      ret = JS_NewStringLen(ctx, (const char*)data, len);
    }
  }
  return ret;
}

static const JSCFunctionListEntry js_mmap_funcs[] = {
    JS_CFUNC_DEF("mmap", 2, js_mmap_map),
    JS_CFUNC_DEF("munmap", 1, js_mmap_unmap),
    JS_CFUNC_DEF("toString", 1, js_mmap_tostring),
    JS_PROP_INT32_DEF("PROT_READ", 0x01, 0),
    JS_PROP_INT32_DEF("PROT_WRITE", 0x02, 0),
    JS_PROP_INT32_DEF("PROT_EXEC", 0x04, 0),
    JS_PROP_INT32_DEF("PROT_SEM", 0x08, 0),
    JS_PROP_INT32_DEF("PROT_NONE", 0x00, 0),
    JS_PROP_INT32_DEF("PROT_GROWSDOWN", 0x01000000, 0),
    JS_PROP_INT32_DEF("PROT_GROWSUP", 0x02000000, 0),
    JS_PROP_INT32_DEF("MAP_SHARED", 0x01, 0),
    JS_PROP_INT32_DEF("MAP_PRIVATE", 0x02, 0),
    JS_PROP_INT32_DEF("MAP_TYPE", 0x0f, 0),
    JS_PROP_INT32_DEF("MAP_FIXED", 0x10, 0),
    JS_PROP_INT32_DEF("MAP_ANONYMOUS", 0x20, 0),
    JS_PROP_INT32_DEF("MAP_GROWSDOWN", 0x0100, 0),
    JS_PROP_INT32_DEF("MAP_DENYWRITE", 0x0800, 0),
    JS_PROP_INT32_DEF("MAP_EXECUTABLE", 0x1000, 0),
    JS_PROP_INT32_DEF("MAP_LOCKED", 0x2000, 0),
    JS_PROP_INT32_DEF("MAP_NORESERVE", 0x4000, 0),
    JS_PROP_INT32_DEF("MAP_POPULATE", 0x8000, 0),
    JS_PROP_INT32_DEF("MAP_NONBLOCK", 0x10000, 0),
    JS_PROP_INT32_DEF("MAP_STACK", 0x20000, 0),
    JS_PROP_INT32_DEF("MAP_HUGETLB", 0x40000, 0),
};

static int
js_mmap_init(JSContext* ctx, JSModuleDef* m) {
  return JS_SetModuleExportList(ctx, m, js_mmap_funcs, countof(js_mmap_funcs));
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_mmap
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_mmap_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_mmap_funcs, countof(js_mmap_funcs));
  return m;
}
