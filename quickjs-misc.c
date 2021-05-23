#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "quickjs-internal.h"
#include "utils.h"
#include <time.h>
#include <sys/utsname.h>

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

static void
js_string_free_func(JSRuntime* rt, void* opaque, void* ptr) {
  JSValue value = js_cstring_value(opaque);

  JS_FreeValueRT(rt, value);
}

typedef struct OffsetLength {
  int64_t offset;
  int64_t length;
} OffsetLength;

static OffsetLength
get_offset_length(JSContext* ctx, int64_t len, int argc, JSValueConst* argv) {
  int64_t offset = 0, length = len;
  if(argc >= 2)
    JS_ToInt64(ctx, &offset, argv[1]);
  if(argc >= 3)
    JS_ToInt64(ctx, &length, argv[2]);

  if(offset >= 0)
    offset = min(offset, len);
  else
    offset = ((offset % len) + offset) % len;

  if(length >= 0)
    length = min(length, len - offset);
  else
    length = len - offset;

  return (OffsetLength){.offset = offset, .length = length};
}

static JSValue
js_misc_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;
  if(js_is_arraybuffer(ctx, argv[0])) {
    uint8_t* data;
    size_t len;

    if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
      OffsetLength ol;

      ol = get_offset_length(ctx, len, argc, argv);

      ret = JS_NewStringLen(ctx, (const char*)data + ol.offset, ol.length);
    }
  }
  return ret;
}

static JSValue
js_misc_toarraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;

  if(JS_IsString(argv[0])) {
    JSValue value = JS_DupValue(ctx, argv[0]);
    size_t len;
    const char* str;
    if((str = JS_ToCStringLen(ctx, &len, value))) {
      OffsetLength ol;

      ol = get_offset_length(ctx, len, argc, argv);

      ret = JS_NewArrayBuffer(ctx, (uint8_t*)str + ol.offset, ol.length, js_string_free_func, (void*)str, FALSE);
    }
  }

  return ret;
}

static JSValue
js_misc_getperformancecounter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  return JS_NewFloat64(ctx, (double)ts.tv_sec * 1000 + ((double)ts.tv_nsec / 1e06));
}

static JSValue
js_misc_hrtime(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  struct timespec ts;
  JSValue ret;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  if(argc >= 1 && JS_IsArray(ctx, argv[0])) {
    int64_t sec, nsec;
    JSValue psec, pnsec;

    psec = JS_GetPropertyUint32(ctx, argv[0], 0);
    pnsec = JS_GetPropertyUint32(ctx, argv[0], 1);

    JS_ToInt64(ctx, &sec, psec);
    JS_ToInt64(ctx, &nsec, pnsec);
    JS_FreeValue(ctx, psec);
    JS_FreeValue(ctx, pnsec);

    if(nsec > ts.tv_nsec) {
      ts.tv_sec -= 1;
      ts.tv_nsec += 1000000000;
    }

    ts.tv_sec -= sec;
    ts.tv_nsec -= nsec;
  }

  ret = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, ret, 0, JS_NewInt64(ctx, ts.tv_sec));
  JS_SetPropertyUint32(ctx, ret, 1, JS_NewInt64(ctx, ts.tv_nsec));

  return ret;
}

static JSValue
js_misc_uname(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  struct utsname un;
  JSValue ret = JS_UNDEFINED;

  if(uname(&un) != -1) {
    ret = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, ret, "sysname", JS_NewString(ctx, un.sysname));
    JS_SetPropertyStr(ctx, ret, "nodename", JS_NewString(ctx, un.nodename));
    JS_SetPropertyStr(ctx, ret, "release", JS_NewString(ctx, un.release));
    JS_SetPropertyStr(ctx, ret, "version", JS_NewString(ctx, un.version));
    JS_SetPropertyStr(ctx, ret, "machine", JS_NewString(ctx, un.machine));
  }

  return ret;
}
static const JSCFunctionListEntry js_misc_funcs[] = {
    JS_CFUNC_DEF("toString", 1, js_misc_tostring),
    JS_CFUNC_DEF("toArrayBuffer", 1, js_misc_toarraybuffer),
    JS_CFUNC_DEF("getPerformanceCounter", 0, js_misc_getperformancecounter),
    JS_CFUNC_DEF("hrtime", 0, js_misc_hrtime),
    JS_CFUNC_DEF("uname", 0, js_misc_uname),
};

static int
js_misc_init(JSContext* ctx, JSModuleDef* m) {

  if(m) {
    JS_SetModuleExportList(ctx, m, js_misc_funcs, countof(js_misc_funcs));
  }

  return 0;
}

#if defined(JS_SHARED_LIBRARY) && defined(JS_MISC_MODULE)
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_misc
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_misc_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_misc_funcs, countof(js_misc_funcs));
  return m;
}
