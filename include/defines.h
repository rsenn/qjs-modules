#ifndef DEFINES_H
#define DEFINES_H

/**
 * \defgroup defines defines: Preprocessor definitions
 * @{
 */

#ifndef QUICKJS_DBUF_REALLOC
#define dbuf_realloc dbuf_claim
#endif

#ifdef _WIN32
#include <io.h>
#define FD_TO_SOCKET(fd) ((SOCKET)_get_osfhandle((fd)))
#define SOCKET_TO_FD(fh) (_open_osfhandle((intptr_t)(fh), O_RDWR | O_BINARY))
#else
#define FD_TO_SOCKET(fd) (fd)
#define SOCKET_TO_FD(fh) (fh)
#endif

#ifndef offsetof
#define offsetof(type, field) ((size_t) & ((type*)0)->field)
#endif

#ifndef inrange
#define inrange(value, min, max) ((value) >= (min) && (value) <= (max))
#endif

#define trim_dotslash(str) (!strncmp((str), "./", 2) ? (str) + 2 : (str))

#ifndef thread_local
#if defined(__TINYC__)
#define thread_local
#elif defined(_Thread_local) || defined(__TINYC__)
#define thread_local _Thread_local
#elif defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__SUNPRO_CC) || defined(__IBMCPP__)
#define thread_local __thread
#elif defined(_WIN32)
#define thread_local __declspec(thread)
#else
#error No TLS implementation found.
#endif
#endif

#ifdef _MSC_VER
#define PACK #pragma pack(push, 1)
#define ENDPACK #pragma pack(pop)
#else
#define PACK __attribute__((packed))
#define ENDPACK
#endif

#define JS_CGETSET_ENUMERABLE_DEF(prop_name, fgetter, fsetter, magic_num) \
  { \
    .name = prop_name, .prop_flags = JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE, .def_type = JS_DEF_CGETSET_MAGIC, .magic = magic_num, .u = { \
      .getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}} \
    } \
  }

#define JS_CGETSET_MAGIC_FLAGS_DEF(prop_name, fgetter, fsetter, magic_num, flags) \
  { \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CGETSET_MAGIC, .magic = magic_num, .u = { \
      .getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}} \
    } \
  }

#define JS_CFUNC_DEF_FLAGS(prop_name, length, func1, flags) \
  { \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CFUNC, .magic = 0, .u = {.func = {length, JS_CFUNC_generic, {.generic = func1}} } \
  }

#define JS_CONSTANT_FLAGS(name, flags) JS_PROP_INT32_DEF(#name, name, (flags))
#define JS_CONSTANT_U32(name) JS_PROP_INT64_DEF(#name, (uint32_t)name, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE)
#define JS_CONSTANT(name) JS_PROP_INT32_DEF(#name, name, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE)
#define JS_CONSTANT_NONENUMERABLE(name) JS_PROP_INT32_DEF(#name, name, JS_PROP_CONFIGURABLE)

#if defined(_WIN32) || defined(__MINGW32__)
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

#ifdef JS_SHARED_LIBRARY
#if defined(_WIN32) || defined(__MINGW32__)
#define VISIBLE DLLEXPORT
#define HIDDEN
#else
#define VISIBLE __attribute__((visibility("default")))
#define HIDDEN __attribute__((visibility("hidden")))
#endif
#else
#define VISIBLE DLLEXPORT
#define HIDDEN
#endif

#ifndef MAX_NUM
#define MAX_NUM(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN_NUM
#define MIN_NUM(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef ABS_NUM
#define ABS_NUM(n) ((n) < 0 ? -(n) : (n))
#endif
#ifndef CLAMP_NUM
#define CLAMP_NUM(a, min, max) MAX_NUM(MIN_NUM(a, max), min)
#endif
#ifndef MOD_NUM
#define MOD_NUM(n, divisor) ((((n) % (divisor)) + (divisor)) % (divisor))
#endif
#ifndef WRAP_NUM
#define WRAP_NUM(n, len) ((n) < 0 ? (n) + (len) : (n))
#endif
#ifndef RANGE_NUM
#define RANGE_NUM(n, len) CLAMP_NUM(WRAP_NUM(n, (len)), 0, (len))
#endif
#ifndef SIGN_NUM
#define SIGN_NUM(n) ((n) < 0)
#endif

#define CONST_STRARRAY(x...) \
  (const char* const[]) { \
    x \
  }

#if __SIZEOF_POINTER__ == 8
#define DEF6432(v64, v32) v64
#else
#define DEF6432(v64, v32) v32
#endif

#if defined(_WIN32) && defined(__GNUC__)
/* mingw printf is used */
#define FORMAT_STRING(a, b) __attribute__((format(gnu_printf, a, b)))
#else
#define FORMAT_STRING(a, b) __attribute__((format(printf, a, b)))
#endif

#define COLOR_BLACK "\x1b[0;30m"
#define COLOR_RED "\x1b[0;31m"
#define COLOR_GREEN "\x1b[0;32m"
#define COLOR_BROWN "\x1b[0;33m"
#define COLOR_BLUE "\x1b[0;34m"
#define COLOR_PURPLE "\x1b[0;35m"
#define COLOR_MARINE "\x1b[0;36m"
#define COLOR_LIGHTGRAY "\x1b[0;37m"
#define COLOR_GRAY "\x1b[38;5;242m"
#define COLOR_NONE "\x1b[0m"

#define COLOR_LIGHTRED "\x1b[1;31m"

#define COLOR_LIGHTGREEN "\x1b[1;32m"
#define COLOR_YELLOW "\x1b[1;33m"
#define COLOR_LIGHTBLUE "\x1b[1;34m"
#define COLOR_MAGENTA "\x1b[1;35m"
#define COLOR_CYAN "\x1b[1;36m"
#define COLOR_WHITE "\x1b[1;37m"

#define BGCOLOR_RED "\x1b[48;5;124m"
#define BGCOLOR_BLUE "\x1b[48;5;20m"
#define BGCOLOR_YELLOW "\x1b[48;5;214m"
#define BGCOLOR_GREEN "\x1b[48;5;28m"
#define BGCOLOR_PINK "\x1b[48;5;165m"

#define JS_VALUE_FREE(ctx, value) \
  do { \
    JS_FreeValue((ctx), (value)); \
    (value) = JS_UNDEFINED; \
  } while(0);
#define JS_VALUE_FREE_RT(ctx, value) \
  do { \
    JS_FreeValueRT((ctx), (value)); \
    (value) = JS_UNDEFINED; \
  } while(0);

#if 0
#define js_object_tmpmark_set(value) \
  do { \
    ((uint8_t*)JS_VALUE_GET_PTR((value)))[5] |= 0x40; \
  } while(0);
#define js_object_tmpmark_clear(value) \
  do { \
    ((uint8_t*)JS_VALUE_GET_PTR((value)))[5] &= ~0x40; \
  } while(0);
#define js_object_tmpmark_isset(value) (((uint8_t*)JS_VALUE_GET_PTR((value)))[5] & 0x40)
#else
#define js_object_tmpmark_set(value) \
  do { \
    JS_VALUE_GET_PTR((value))->tmp_mark |= 0x40; \
  } while(0);
#define js_object_tmpmark_clear(value) \
  do { \
    JS_VALUE_GET_PTR((value))->tmp_mark &= ~0x40; \
  } while(0);
#define js_object_tmpmark_isset(value) (JS_VALUE_GET_PTR((value))->tmp_mark & 0x40)
#endif

#define js_runtime_exception_set(rt, value) \
  do { \
    *(JSValue*)((uint8_t*)(rt) + 216) = value; \
  } while(0);
#define js_runtime_exception_get(rt) (*(JSValue*)((uint8_t*)(rt) + 216))
#define js_runtime_exception_clear(rt) \
  do { \
    if(!JS_IsNull(js_runtime_exception_get(rt))) \
      JS_FreeValueRT((rt), js_runtime_exception_get(rt)); \
    js_runtime_exception_set(rt, JS_NULL); \
  } while(0)

#define JS_ATOM_TAG_INT (1U << 31)
#define JS_ATOM_MAX_INT (JS_ATOM_TAG_INT - 1)

#define JS_ATOM_ISINT(i) ((JSAtom)((i) & JS_ATOM_TAG_INT))
#define JS_ATOM_FROMINT(i) ((JSAtom)((i) & JS_ATOM_MAX_INT) | JS_ATOM_TAG_INT)
#define JS_ATOM_TOINT(i) (unsigned int)(((JSAtom)(i) & (~(JS_ATOM_TAG_INT))))

/**
 * @}
 */
#endif /* defined(DEFINES_H) */
