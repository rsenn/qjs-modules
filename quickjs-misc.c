#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "defines.h"
#include <quickjs.h>
#include <quickjs-libc.h>
#include <quickjs-config.h>
#include "quickjs-misc.h"
#include "quickjs-internal.h"
#include "quickjs-location.h"
#include "quickjs-textcode.h"
#include "quickjs-syscallerror.h"
#include "utils.h"
#include "path.h"
#include "base64.h"
#include <time.h>
#ifndef _WIN32
#include <sys/utsname.h>
#endif
#include <errno.h>
#ifdef HAVE_FNMATCH_H
#include <fnmatch.h>
#endif
#ifdef HAVE_GLOB
#include <glob.h>
#ifndef GLOB_MAGCHAR
#define GLOB_MAGCHAR 256
#endif
#ifndef GLOB_ALTDIRFUNC
#define GLOB_ALTDIRFUNC 512
#endif
#ifndef GLOB_BRACE
#define GLOB_BRACE 1024
#endif
#ifndef GLOB_NOMAGIC
#define GLOB_NOMAGIC 2048
#endif
#ifndef GLOB_TILDE
#define GLOB_TILDE 4096
#endif
#ifndef GLOB_ONLYDIR
#define GLOB_ONLYDIR 8192
#endif
#ifndef GLOB_TILDE_CHECK
#define GLOB_TILDE_CHECK 16384
#endif
#endif
#ifdef HAVE_WORDEXP
#include "wordexp.h"
#endif
#ifdef HAVE_INOTIFY_INIT1
#include <sys/inotify.h>
#endif
#include "buffer-utils.h"
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include "debug.h"
#include "js-utils.h"

#ifndef _WIN32
#define FOREGROUND_BLUE (1 << 0)
#define FOREGROUND_GREEN (1 << 1)
#define FOREGROUND_RED (1 << 2)
#define FOREGROUND_INTENSITY (1 << 3)
#define BACKGROUND_BLUE (1 << 4)
#define BACKGROUND_GREEN (1 << 5)
#define BACKGROUND_RED (1 << 6)
#define BACKGROUND_INTENSITY (1 << 7)
#define COMMON_LVB_REVERSE_VIDEO (1 << 14)
#endif

#define TextAttrColor(n) (((n)&1) * FOREGROUND_RED + (((n) >> 1) & 1) * FOREGROUND_GREEN + (((n) >> 2) & 1) * FOREGROUND_BLUE + (((n) >> 3) & 1) * FOREGROUND_INTENSITY)

#define ColorIsBG(c) ((c) >= 100 ? TRUE : (c) >= 90 ? FALSE : (c) >= 40 ? TRUE : FALSE)
#define ColorIsBold(c) ((c) >= 90)
#define ColorIndex(c) (((c) >= 30 ? (c) % 10 : (c)) & 7)
#define ColorRed(c) (ColorIndex(c) & 1)
#define ColorGreen(c) ((ColorIndex(c) >> 1) & 1)
#define ColorBlue(c) ((ColorIndex(c) >> 2) & 1)

#define ColorToBits(c) ((ColorIsBG(c) << 4) | (ColorIsBold(c) << 3) | ColorBlue(c) | ColorGreen(c) << 1 | ColorRed(c) << 2)

/**
 * \addtogroup quickjs-misc
 * @{
 */

#ifndef HAVE_MEMMEM
void* memmem(const void*, size_t, const void*, size_t);
#endif

enum {
  FUNC_GETEXECUTABLE = 0,
  FUNC_GETCWD,
  FUNC_GETROOT,
  FUNC_GETFD,
  FUNC_GETCOMMANDLINE,
  FUNC_GETPROCMAPS,
  FUNC_GETPROCMOUNTS,
  FUNC_GETPROCSTAT,
  FUNC_GETTID,
  FUNC_GETPID,
  FUNC_GETPPID,
  FUNC_GETSID,
  FUNC_GETUID,
  FUNC_GETGID,
  FUNC_GETEUID,
  FUNC_GETEGID,
  FUNC_SETUID,
  FUNC_SETGID,
  FUNC_SETEUID,
  FUNC_SETEGID
};

// static thread_local int inotify_fd = -1;

typedef struct pcg_state_setseq_64 {
  uint64_t state, inc;
} pcg32_random_t;

static pcg32_random_t pcg32_global = {0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL};

static inline uint32_t
pcg32_random_r(pcg32_random_t* rng) {
  uint64_t oldstate = rng->state;
  rng->state = oldstate * 6364136223846793005ULL + rng->inc;
  uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
  uint32_t rot = oldstate >> 59u;
  return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

static uint32_t
pcg32_random(void) {
  return pcg32_random_r(&pcg32_global);
}

static void
pcg32_init_state(uint32_t state) {
  pcg32_global.state = ~(((uint64_t)state) << 32) | state;
}

static uint32_t
pcg32_random_bounded_divisionless(uint32_t range) {
  uint64_t random32bit, multiresult;
  uint32_t leftover;
  uint32_t threshold;
  random32bit = pcg32_random();
  multiresult = random32bit * range;
  leftover = (uint32_t)multiresult;
  if(leftover < range) {
    threshold = -range % range;
    while(leftover < threshold) {
      random32bit = pcg32_random();
      multiresult = random32bit * range;
      leftover = (uint32_t)multiresult;
    }
  }
  return multiresult >> 32; // [0, range)
}

typedef enum {
  CLEAR_TO_END = 0,
  CLEAR_TO_BEGIN,
  CLEAR_ENTIRE,
} ClearMode;

#ifdef _WIN32
static BOOL
clear_screen(HANDLE h, ClearMode mode, BOOL line) {
  COORD coords = {0, 0};
  DWORD w, n;
  CONSOLE_SCREEN_BUFFER_INFO sbi;

  if(!GetConsoleScreenBufferInfo(h, &sbi))
    return FALSE;

#define CHAR_POS(c) (((c).Y * sbi.dwSize.X) + (c).X)

  switch(mode) {
    case CLEAR_TO_END: {
      coords = sbi.dwCursorPosition;
      n = line ? sbi.dwSize.X - sbi.dwCursorPosition.X : (sbi.dwSize.X * sbi.dwSize.Y) - CHAR_POS(sbi.dwCursorPosition);
      break;
    }
    case CLEAR_TO_BEGIN: {
      if(line)
        coords.Y = sbi.dwCursorPosition.Y;
      n = line ? sbi.dwCursorPosition.X : CHAR_POS(sbi.dwCursorPosition);
      break;
    }
    case CLEAR_ENTIRE: {
      if(line)
        coords.Y = sbi.dwCursorPosition.Y;
      n = line ? sbi.dwSize.X : sbi.dwSize.X * sbi.dwSize.Y;
      break;
    }
  }

  if(!FillConsoleOutputCharacter(h, (TCHAR)' ', n, coords, &w))
    return FALSE;

  if(!GetConsoleScreenBufferInfo(h, &sbi))
    return FALSE;

  if(!FillConsoleOutputAttribute(h, sbi.wAttributes, n, coords, &w))
    return FALSE;

  // SetConsoleCursorPosition(h, coords);
  return TRUE;
}

#else
static BOOL
clear_screen(int fd, ClearMode mode, BOOL line) {
  char buf[] = {27, '[', mode + '0', line ? 'K' : 'J'};
  return write(fd, buf, sizeof(buf)) > 0;
}
#endif

#ifdef _WIN32
static BOOL
set_cursor_position(HANDLE h, int x, int y) {
  COORD coords = {0, 0};
  CONSOLE_SCREEN_BUFFER_INFO sbi;

  if(!GetConsoleScreenBufferInfo(h, &sbi))
    return FALSE;

  coords.X = x == -1 ? sbi.dwCursorPosition.X : x;
  coords.Y = y == -1 ? sbi.dwCursorPosition.Y : y;

  return !!SetConsoleCursorPosition(h, coords);
}
#else
static BOOL
set_cursor_position(int fd, int x, int y) {
  char buf[2 + (FMT_ULONG + 1) * 2] = {27, '['};
  size_t pos = 2;

  if(y == -1 && x >= 0) {
    pos += fmt_ulong(&buf[pos], x + 1);
    buf[pos++] = 'G';
  } else {

    if(y >= 0 && x >= 0) {
      pos += fmt_ulong(&buf[pos], y + 1);
      buf[pos++] = ';';
      pos += fmt_ulong(&buf[pos], x + 1);
    }

    buf[pos++] = 'H';
  }

  return write(fd, buf, pos) > 0;
}
#endif

#ifdef _WIN32
static BOOL
move_cursor(HANDLE h, int x, int y) {
  COORD coords = {0, 0};
  CONSOLE_SCREEN_BUFFER_INFO sbi;

  if(!GetConsoleScreenBufferInfo(h, &sbi))
    return FALSE;

  coords.X = sbi.dwCursorPosition.X + x;
  coords.Y = sbi.dwCursorPosition.Y + y;

  return !!SetConsoleCursorPosition(h, coords);
}
#else
static BOOL
move_cursor(int fd, int x, int y) {
  char buf[(2 + (FMT_ULONG) + 1) * 2];
  size_t pos = 0;

  if(y) {
    buf[pos++] = 27;
    buf[pos++] = '[';
    if(ABS_NUM(y) != 1)
      pos += fmt_ulong(&buf[pos], ABS_NUM(y));
    buf[pos++] = y < 0 ? 'A' : 'B';
  }

  if(x) {
    buf[pos++] = 27;
    buf[pos++] = '[';
    if(ABS_NUM(x) != 1)
      pos += fmt_ulong(&buf[pos], ABS_NUM(x));
    buf[pos++] = x < 0 ? 'D' : 'C';
  }

  return write(fd, buf, pos) > 0;
}
#endif

#ifdef _WIN32
static BOOL
set_text_attributes(HANDLE h, uint32_t attr) {
  return !!SetConsoleTextAttribute(h, attr);
}

static BOOL
get_text_attributes(HANDLE h, uint32_t* attr) {
  CONSOLE_SCREEN_BUFFER_INFO sbi;

  if(GetConsoleScreenBufferInfo(h, &sbi)) {
    *attr = sbi.wAttributes;
    return TRUE;
  }

  return FALSE;
}

#else
static BOOL
set_text_color(intptr_t fd, int intc, int32_t intv[]) {
  DynBuf dbuf;

  dbuf_init2(&dbuf, 0, 0);

  dbuf_putstr(&dbuf, "\x1b[");

  for(int i = 0; i < intc; i++) {
    uint8_t* ptr;

    if(!(ptr = dbuf_reserve(&dbuf, FMT_ULONG))) {
      dbuf_free(&dbuf);
      return FALSE;
    }

    dbuf.size += fmt_ulong((void*)ptr, intv[i]);

    dbuf_putc(&dbuf, i < (intc - 1) ? ';' : 'm');
  }

  dbuf_0(&dbuf);

  ssize_t r = dbuf.size > 0 ? write(fd, dbuf.buf, dbuf.size) : 0;

#if 1 // DEBUG_OUTPUT
  for(size_t i = 0; i < dbuf.size; i++)
    if(dbuf.buf[i] == '\x1b')
      dbuf.buf[i] = 0xac;

  printf("%s intc = %d, buf = '%s'\n", __func__, intc, dbuf.buf);
#endif

  dbuf_free(&dbuf);

  return r > 0;
}

static BOOL
set_text_attributes(intptr_t fd, uint32_t attr) {
  char buf[(2 + (FMT_ULONG) + 1) * 3];
  size_t pos = 0;

  buf[pos++] = 27;
  buf[pos++] = '[';

  int fg = ((attr & FOREGROUND_RED) ? 1 : 0) + ((attr & FOREGROUND_GREEN) ? 2 : 0) + ((attr & FOREGROUND_BLUE) ? 4 : 0) + ((attr & FOREGROUND_INTENSITY) ? 90 : 30);

  pos += fmt_ulong(&buf[pos], fg);
  buf[pos++] = ';';

  int bg = ((attr & BACKGROUND_RED) ? 1 : 0) + ((attr & BACKGROUND_GREEN) ? 2 : 0) + ((attr & BACKGROUND_BLUE) ? 4 : 0) + ((attr & BACKGROUND_INTENSITY) ? 100 : 40);

  pos += fmt_ulong(&buf[pos], bg);

  buf[pos++] = ';';
  pos += fmt_ulong(&buf[pos], (attr & COMMON_LVB_REVERSE_VIDEO) ? 7 : 27);

  buf[pos++] = 'm';

  return write(fd, buf, pos) > 0;
}
#endif

static void
js_arraybuffer_free_pointer(JSRuntime* rt, void* opaque, void* ptr) {
  js_free_rt(rt, ptr);
}

static void
js_arraybuffer_free_cstring(JSRuntime* rt, void* opaque, void* ptr) {
  JSValue value = js_cstring_value(opaque);

  JS_FreeValueRT(rt, value);
}

static void
js_arraybuffer_free_object(JSRuntime* rt, void* opaque, void* ptr) {
  JSValue value = JS_MKPTR(JS_TAG_OBJECT, opaque);

  JS_FreeValueRT(rt, value);
}

static void*
js_get_pointer(JSContext* ctx, JSValueConst value) {
  void* ptr;
  size_t len;
  int64_t i64;

  if((ptr = JS_GetArrayBuffer(ctx, &len, value)))
    return ptr;

  if(!JS_ToInt64Ext(ctx, &i64, value)) {
    return (void*)(ptrdiff_t)i64;
  }

  /*if((str = JS_ToCString(ctx, value))) {
    uint64_t u64;
    if(str_start(str, "0x")) {
      if(scan_xlonglong(str + 2, &u64) > 0)
        return (void*)(ptrdiff_t)u64;
    } else {
      if(scan_ulonglong(str + 2, &u64) > 0)
        return (void*)(ptrdiff_t)u64;
    }
  }*/

  return 0;
}

static JSValue
js_misc_getrelease(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;

  ret = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, ret, "name", JS_NewString(ctx, "quickjs"));
  JS_SetPropertyStr(ctx, ret, "sourceUrl", JS_NewString(ctx, "https://bellard.org/quickjs/quickjs-" CONFIG_VERSION ".tar.xz"));

  return ret;
}

static JSValue
js_misc_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  //  JSValue arraybuffer_ctor = js_global_get_str(ctx, "ArrayBuffer");

  if(js_value_isclass(ctx, argv[0], JS_CLASS_ARRAY_BUFFER) || js_is_arraybuffer(ctx, argv[0]) /* || JS_IsInstanceOf(ctx, argv[0], arraybuffer_ctor)*/) {
    uint8_t* data;
    size_t len;

    if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
      OffsetLength ol;

      js_offset_length(ctx, len, argc - 1, argv + 1, &ol);

      ret = JS_NewStringLen(ctx, offset_data(&ol, data), offset_size(&ol, len));
    }
  } else {
    ret = js_value_tostring(ctx, "Object", argc > 0 ? argv[0] : this_val);
  }

  //  JS_FreeValue(ctx, arraybuffer_ctor);

  return ret;
}

static JSValue
js_misc_strcmp(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  const char *a, *b;
  size_t alen, blen;

  a = JS_ToCStringLen(ctx, &alen, argv[0]);
  b = JS_ToCStringLen(ctx, &blen, argv[1]);

  ret = JS_NewInt32(ctx, byte_diff2(a, alen, b, blen));

  JS_FreeCString(ctx, a);
  JS_FreeCString(ctx, b);

  return ret;
}

static JSValue
js_misc_topointer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_NULL;
  InputBuffer buf;
  uint8_t* ptr;

  buf = js_input_chars(ctx, argv[0]);

  if(JS_IsException(buf.value))
    return JS_EXCEPTION;

  if((ptr = input_buffer_data(&buf))) {
    char str[64];
    ret = JS_NewStringLen(ctx, str, snprintf(str, sizeof(str), "%p", ptr));
  }

  input_buffer_free(&buf, ctx);

  return ret;
}

static JSValue
js_misc_toarraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  MemoryBlock b;
  OffsetLength o;

  /*  if(JS_IsString(argv[0])) {
      JSValueConst value = argv[0]; // JS_DupValue(ctx, argv[0]);
      b.base = JS_ToCStringLen(ctx, &b.size, value);
      f = &js_arraybuffer_free_cstring;
      opaque = b.base;
      ret =
          JS_NewArrayBuffer(ctx, b.base + o.offset, MIN_NUM(b.size, o.length),
    js_arraybuffer_free_cstring, (void*)b.base, FALSE); } else*/

  InputBuffer input = js_input_chars(ctx, argv[0]);
  js_offset_length(ctx, input.size, argc - 1, argv + 1, &o);
  b = input_buffer_block(&input);
  //    b = block_range(&b, &input.range);
  b = block_range(&b, &o);
  ret = js_arraybuffer_fromvalue(ctx, b.base, b.size, argv[0]);

  return ret;
}

static JSValue
js_misc_slice(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  uint8_t* data;
  size_t len;

  if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
    IndexRange ir;
    JSArrayBuffer* ab;
    if(!(ab = JS_GetOpaque(argv[0], JS_CLASS_ARRAY_BUFFER)))
      ab = JS_GetOpaque(argv[0], JS_CLASS_SHARED_ARRAY_BUFFER);

    js_index_range(ctx, len, argc - 1, argv + 1, &ir);

    JSValue value = JS_DupValue(ctx, argv[0]);
    JSObject* obj = JS_VALUE_GET_OBJ(value);
    return JS_NewArrayBuffer(ctx, data + ir.start, ir.end - ir.start, js_arraybuffer_free_object, (void*)obj, ab && ab->shared ? TRUE : FALSE);
  }

  return JS_ThrowTypeError(ctx, "argument 1 must be an ArrayBuffer");
}

static JSValue
js_misc_duparraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  uint8_t* data;
  size_t len;

  if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
    OffsetLength ol;
    JSArrayBuffer* ab;
    if(!(ab = JS_GetOpaque(argv[0], JS_CLASS_ARRAY_BUFFER)))
      ab = JS_GetOpaque(argv[0], JS_CLASS_SHARED_ARRAY_BUFFER);

    js_offset_length(ctx, len, argc - 1, argv + 1, &ol);

    JSValue value = JS_DupValue(ctx, argv[0]);
    JSObject* obj = JS_VALUE_GET_OBJ(value);
    return JS_NewArrayBuffer(ctx, data + ol.offset, ol.length, js_arraybuffer_free_object, (void*)obj, ab && ab->shared ? TRUE : FALSE);
  }

  return JS_ThrowTypeError(ctx, "argument 1 must be an ArrayBuffer");
}

static JSValue
js_misc_resizearraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  JSObject* obj;
  JSArrayBuffer* arraybuf;
  uint64_t newlen;

  if(!js_is_arraybuffer(ctx, argv[0]))
    return JS_ThrowTypeError(ctx, "argument 1 must be an ArrayBuffer");

  obj = JS_VALUE_GET_OBJ(argv[0]);
  arraybuf = obj->u.array_buffer;
  JS_ToIndex(ctx, &newlen, argv[1]);

  if(arraybuf->shared)
    ret = JS_ThrowTypeError(ctx, "ArrayBuffer must not be shared");
  else if(arraybuf->shared)
    ret = JS_ThrowTypeError(ctx, "ArrayBuffer must have opaque == 0");
  else {
    arraybuf->data = js_realloc(ctx, arraybuf->data, newlen);
    arraybuf->byte_length = newlen;

    ret = JS_MKPTR(JS_TAG_OBJECT, obj);
  }

  return ret;
}

static JSValue
js_misc_concat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  int i;
  size_t total_len = 0, pos = 0;
  uint8_t* buf;
  InputBuffer* buffers = js_mallocz(ctx, sizeof(InputBuffer) * argc);
  for(i = 0; i < argc; i++) {
    buffers[i] = js_input_buffer(ctx, argv[i]);
    if(!buffers[i].data) {
      ret = JS_ThrowTypeError(ctx, "argument %d is not ArrayBuffer", i + 1);
      goto fail;
    }
    total_len += buffers[i].size;
  }
  buf = js_malloc(ctx, total_len);
  for(i = 0; i < argc; i++) {
    memcpy(&buf[pos], buffers[i].data, buffers[i].size);
    pos += buffers[i].size;
  }
  ret = JS_NewArrayBuffer(ctx, buf, total_len, js_arraybuffer_free_pointer, 0, FALSE);
fail:
  for(i = 0; i < argc; i++)
    if(buffers[i].data)
      input_buffer_free(&buffers[i], ctx);
  return ret;
}

static JSValue
js_misc_searcharraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MemoryBlock haystack, needle, mask;
  size_t n_size, h_end;

  if(!block_arraybuffer(&haystack, argv[0], ctx))
    return JS_ThrowTypeError(ctx, "argument 1 (haystack) must be an ArrayBuffer");
  
  if(!block_arraybuffer(&needle, argv[1], ctx))
    return JS_ThrowTypeError(ctx, "argument 2 (needle) must be an ArrayBuffer");

  if(argc < 3 || (JS_IsNumber(argv[2]) || JS_IsBigInt(ctx, argv[2]))) {
    uint8_t* ptr;
    ptrdiff_t ofs;
    int64_t start_pos = 0;

    if(argc >= 3) {
      JS_ToInt64Ext(ctx, &start_pos, argv[2]);

      if(start_pos >= (int64_t)haystack.size)
        return JS_NULL;

      if(start_pos > 0) {
        haystack.base += start_pos;
        haystack.size -= start_pos;
      }
    }

    if(needle.size <= haystack.size && (ptr = memmem(haystack.base, haystack.size, needle.base, needle.size))) {
      ofs = ptr - haystack.base;
      ofs += start_pos;

      if(ofs > MAX_SAFE_INTEGER || (argc > 2 && JS_IsBigInt(ctx, argv[2])))
        return JS_NewBigUint64(ctx, ofs);

      return JS_NewInt64(ctx, ofs);
    }

    return JS_NULL;
  }

  if(!block_arraybuffer(&mask, argv[2], ctx))
    return JS_ThrowTypeError(ctx, "argument 3 (mask) must be an ArrayBuffer");

  n_size = MIN_NUM(needle.size, mask.size);
  h_end = haystack.size - n_size;

  // naive searching algorithm (slow)
  for(size_t i = 0; i < h_end; i++) {
    int found = 1;
    for(size_t j = 0; j < n_size; j++) {
      if((haystack.base[i + j] ^ needle.base[j]) & mask.base[j]) {
        found = 0;
        break;
      }
    }
    if(found) {
      /*for(size_t j = 0; j < n_size; j++) {
        uint8_t xorval = haystack.base[i + j] ^ needle.base[j];
        printf("@(%zu + %zu); ", i, j);
        printf("%02x XOR %02x = %02x; ", haystack.base[i + j], needle.base[j], xorval);
        printf("%02x AND %02x = %02x\n", xorval, mask.base[j], xorval & mask.base[j]);
      }*/

      return JS_NewInt64(ctx, (int64_t)i);
    }
  }

  return JS_NULL;
}

static JSValue
js_misc_memcpy(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MemoryBlock dst = {0, 0}, src = {0, 0};
  OffsetLength s_offs, d_offs;
  size_t n;
  int i = 0;

  if(!block_arraybuffer(&dst, argv[0], ctx))
    return JS_ThrowTypeError(ctx, "argument 1 (dst) must be an ArrayBuffer");

  i++;

  i += js_offset_length(ctx, dst.size, argc - i, argv + i, &s_offs);

  /* dst.base += s_offs.offset;
   dst.size -= s_offs.offset;
   dst.size = MIN_NUM(dst.size, s_offs.length);*/

  if(i == argc || !block_arraybuffer(&src, argv[i], ctx))
    return JS_ThrowTypeError(ctx, "argument %d (src) must be an ArrayBuffer", i + 1);

  i++;

  i += js_offset_length(ctx, dst.size, argc - i, argv + i, &d_offs);

  /* src.base += d_offs.offset;
   src.size -= d_offs.offset;
   src.size = MIN_NUM(src.size, d_offs.length);*/

  if((n = MIN_NUM(offset_size(&d_offs, block_length(&dst)), offset_size(&s_offs, block_length(&src)))))
    memcpy(offset_data(&d_offs, block_data(&dst)), offset_data(&s_offs, block_data(&src)), n);

  return JS_NewInt64(ctx, n);
}

#ifdef HAVE_FMEMOPEN
static JSValue
js_misc_fmemopen(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  uint8_t* ptr;
  size_t len;
  int i = 0;
  const char* mode;

  if(!(ptr = JS_GetArrayBuffer(ctx, &len, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 (dst) must be an ArrayBuffer");

  i++;

  if(i + 1 < argc && JS_IsNumber(argv[i])) {
    int64_t offset = 0;
    JS_ToInt64(ctx, &offset, argv[i++]);
    offset = MIN_NUM((int64_t)len, offset);

    ptr += offset;
    len -= offset;
  }

  if(i + 1 < argc && JS_IsNumber(argv[i])) {
    int64_t length = 0;

    if(!JS_ToInt64(ctx, &length, argv[i++]))
      len = MIN_NUM((int64_t)len, length);
  }

  {
    JSClassID class_id = js_class_find(ctx, "FILE");
    JSValue obj, proto = JS_GetClassProto(ctx, class_id);
    JSSTDFile* file;
    mode = JS_ToCString(ctx, argv[0]);

    file = js_malloc(ctx, sizeof(JSSTDFile));
    *file = (JSSTDFile){0, TRUE, FALSE};

    file->f = fmemopen(ptr, len, mode);

    obj = JS_NewObjectProtoClass(ctx, proto, class_id);

    JS_SetOpaque(obj, file);

    return obj;
  }
}
#endif

static JSValue
js_misc_getperformancecounter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  return JS_NewFloat64(ctx, (double)ts.tv_sec * 1000 + ((double)ts.tv_nsec / 1e06));
}

static JSValue
js_misc_proclink(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  DynBuf dbuf = {0};
  const char* link;
  char path[256];
  size_t n;
  ssize_t r;

  switch(magic) {
    case FUNC_GETEXECUTABLE: link = "exe"; break;
    case FUNC_GETCWD: link = "cwd"; break;
    case FUNC_GETROOT: link = "root"; break;
    case FUNC_GETFD: link = "fd/"; break;
  }

  n = snprintf(path, sizeof(path), "/proc/self/%s", link);

  if(magic == FUNC_GETFD) {
    int32_t fd;
    if(argc < 1 || !JS_IsNumber(argv[0]))
      return JS_ThrowTypeError(ctx, "argument 1 must be Number");

    JS_ToInt32(ctx, &fd, argv[0]);
    snprintf(&path[n], sizeof(path) - n, "%d", fd);
  }

  js_dbuf_init(ctx, &dbuf);

  if((r = path_readlink2(path, &dbuf)) > 0) {
    ret = dbuf_tostring_free(&dbuf, ctx);
  } else if(r < 0) {
  }

  return ret;
}

static JSValue
js_misc_procread(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  DynBuf dbuf = {0};
  ssize_t i, j = 0, size, n;
  const char* file;
  char sep = '\n';

  switch(magic) {
    case FUNC_GETCOMMANDLINE:
      file = "/proc/self/cmdline";
      sep = '\0';
      break;
    case FUNC_GETPROCMAPS:
      file = "/proc/self/maps";
      sep = '\n';
      break;
    case FUNC_GETPROCMOUNTS:
      file = "/proc/self/mounts";
      sep = '\n';
      break;
    case FUNC_GETPROCSTAT:
      file = "/proc/self/stat";
      sep = ' ';
      break;
  }

  js_dbuf_init(ctx, &dbuf);

  if((size = dbuf_load(&dbuf, file)) > 0) {

    while(size > 0 && dbuf.buf[size - 1] == '\n')
      size--;

    ret = JS_NewArray(ctx);
    for(i = 0; i < size; i += n + 1) {
      size_t len;
      len = n = byte_chr(&dbuf.buf[i], size - i, sep);
      while(len > 0 && is_whitespace_char(dbuf.buf[i + len - 1]))
        len--;
      JS_SetPropertyUint32(ctx, ret, j++, JS_NewStringLen(ctx, (const char*)&dbuf.buf[i], len));
    }
  }

  dbuf_free(&dbuf);

  return ret;
}

static JSValue
js_misc_getprototypechain(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue proto, prev = JS_UNDEFINED, ret;
  int64_t i = -1, j = 0, limit = -1, start = 0, end;

  if(argc < 1 || !JS_IsObject(argv[0]))
    return JS_ThrowTypeError(ctx, "argument 1 object excepted");

  if(argc >= 2 && !js_is_null_or_undefined(argv[1]))
    JS_ToInt64(ctx, &limit, argv[1]);

  if(argc >= 3 && !js_is_null_or_undefined(argv[2]))
    JS_ToInt64(ctx, &start, argv[2]);

  ret = JS_NewArray(ctx);
  end = limit >= 0 ? start + limit : -1;

  for(proto = JS_DupValue(ctx, argv[0]); !JS_IsException(proto) && !JS_IsNull(proto) && JS_IsObject(proto); proto = JS_GetPrototype(ctx, proto)) {
    BOOL circular = (JS_VALUE_GET_OBJ(proto) == JS_VALUE_GET_OBJ(prev));
    JS_FreeValue(ctx, prev);
    if(circular)
      break;
    if(i >= start && (end == -1 || i < end))
      JS_SetPropertyUint32(ctx, ret, j++, proto);
    ++i;
    prev = proto;
  }

  JS_FreeValue(ctx, proto);
  return ret;
}

static JSValue
js_misc_hrtime(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
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

#ifndef __wasi__
/*static JSValue
js_misc_realpath(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  char resolved[PATH_MAX];
  const char* path = JS_ToCString(ctx, argv[0]);
  char* result;

#ifndef __wasi__
  if((result = realpath(path, resolved)))
#endif
    return JS_NewString(ctx, result);
  return JS_NULL;
}*/
#ifdef USE_TEMPNAM
static JSValue
js_misc_tempnam(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const char *dir = 0, *pfx = 0;
  char* nam;
  JSValue ret = JS_NULL;

  if(argc >= 1 && JS_IsString(argv[0]))
    dir = JS_ToCString(ctx, argv[0]);
  if(argc >= 2 && JS_IsString(argv[1]))
    pfx = JS_ToCString(ctx, argv[1]);

  if((nam = tempnam(dir, pfx))) {
    ret = JS_NewString(ctx, nam);
    free(nam);
  }
  return ret;
}
#endif

static JSValue
js_misc_mkstemp(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const char* tmp = 0;
  char* template;
  int fd;

  if(argc >= 1 && JS_IsString(argv[0]))
    tmp = JS_ToCString(ctx, argv[0]);

  template = js_strdup(ctx, tmp ? tmp : "/tmp/fileXXXXXX");

  if(tmp)
    JS_FreeCString(ctx, tmp);

  if(!template)
    return JS_EXCEPTION;

  fd = mkstemp(template);

  js_free(ctx, template);

  if(fd < 0) {
    fd = -errno;
    errno = 0;
  }

  return JS_NewInt32(ctx, fd);
}
#endif

static JSValue
js_misc_fnmatch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  size_t plen, slen;
  int32_t flags = 0, ret;

  const char* pattern = JS_ToCStringLen(ctx, &plen, argv[0]);
  const char* string = JS_ToCStringLen(ctx, &slen, argv[1]);

  if(argc >= 3)
    JS_ToInt32(ctx, &flags, argv[2]);

#ifdef HAVE_FNMATCH
  ret = fnmatch(pattern, string, flags);
#else
  ret = path_fnmatch5(pattern, plen, string, slen, flags);
#endif
  JS_FreeCString(ctx, pattern);
  JS_FreeCString(ctx, string);
  return JS_NewInt32(ctx, ret);
}

#ifdef HAVE_GLOB
static JSContext* js_misc_glob_errfunc_ctx;
static JSValueConst js_misc_glob_errfunc_fn;

static int
js_misc_glob_errfunc(const char* epath, int eerrno) {
  JSContext* ctx;

  if((ctx = js_misc_glob_errfunc_ctx)) {
    JSValueConst argv[2] = {JS_NewString(ctx, epath), JS_NewInt32(ctx, eerrno)};

    JS_FreeValue(ctx, JS_Call(ctx, js_misc_glob_errfunc_fn, JS_NULL, 2, argv));

    JS_FreeValue(ctx, argv[0]);
    JS_FreeValue(ctx, argv[1]);
  }
  return 0;
}

static JSValue
js_misc_glob(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  size_t start = 0, i;
  int32_t flags = 0;
  JSValue ret = JS_UNDEFINED;
  glob_t g = {0, 0, 0, 0, 0};
  int result;
  BOOL array_arg = FALSE;
  const char* pattern = JS_ToCString(ctx, argv[0]);

  if(argc >= 2)
    JS_ToInt32(ctx, &flags, argv[1]);

  if((array_arg = (argc >= 4 && JS_IsArray(ctx, argv[3])))) {
    ret = JS_DupValue(ctx, argv[3]);

    if(flags & GLOB_APPEND)
      start = js_array_length(ctx, ret);
  } else {
    ret = JS_NewArray(ctx);
  }

  js_misc_glob_errfunc_ctx = ctx;
  js_misc_glob_errfunc_fn = argc >= 3 ? argv[2] : JS_UNDEFINED;

  if((result = glob(pattern, flags & (~(GLOB_APPEND | GLOB_DOOFFS)), js_misc_glob_errfunc, &g)) == 0) {
    for(i = 0; i < g.gl_pathc; i++)
      JS_SetPropertyUint32(ctx, ret, i + start, JS_NewString(ctx, g.gl_pathv[i]));

    globfree(&g);
  }

  if(array_arg || result) {
    JS_FreeValue(ctx, ret);
    ret = JS_NewInt32(ctx, result);
  }

  JS_FreeValue(ctx, js_misc_glob_errfunc_fn);
  js_misc_glob_errfunc_ctx = 0;
  JS_FreeCString(ctx, pattern);
  return ret;
}
#endif

#ifdef HAVE_WORDEXP
static JSValue
js_misc_wordexp(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  size_t start = 0, i;
  int32_t flags = 0;
  JSValue ret = JS_UNDEFINED;
  wordexp_t we = {0, 0, 0};
  int result;
  BOOL array_arg = FALSE;
  const char* s = JS_ToCString(ctx, argv[0]);

  if(argc >= 3)
    JS_ToInt32(ctx, &flags, argv[2]);

  if((array_arg = (argc >= 2 && JS_IsArray(ctx, argv[1])))) {
    ret = JS_DupValue(ctx, argv[1]);

    if(flags & WRDE_APPEND)
      start = js_array_length(ctx, ret);
  } else {
    ret = JS_NewArray(ctx);
  }

  if((result = wordexp(s, &we, flags & (~(WRDE_APPEND | WRDE_DOOFFS | WRDE_REUSE)))) == 0) {
    for(i = 0; i < we.we_wordc; i++)
      JS_SetPropertyUint32(ctx, ret, i + start, JS_NewString(ctx, we.we_wordv[i]));

    wordfree(&we);
  }

  if(array_arg || result) {
    JS_FreeValue(ctx, ret);
    ret = JS_NewInt32(ctx, result);
  }

  JS_FreeCString(ctx, s);
  return ret;
}
#endif

static JSValue
js_misc_uname(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
#ifdef HAVE_UNAME
  struct utsname un;

  if(uname(&un) != -1) {
    ret = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, ret, "sysname", JS_NewString(ctx, un.sysname));
    JS_SetPropertyStr(ctx, ret, "nodename", JS_NewString(ctx, un.nodename));
    JS_SetPropertyStr(ctx, ret, "release", JS_NewString(ctx, un.release));
    JS_SetPropertyStr(ctx, ret, "version", JS_NewString(ctx, un.version));
    JS_SetPropertyStr(ctx, ret, "machine", JS_NewString(ctx, un.machine));
  }
#else
  ret = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,
                    ret,
                    "sysname",
                    JS_NewString(ctx,
#ifdef __MINGW32__
                                 "mingw"
#elif defined(__MSYS__)
                                 "msys"
#elif defined(_WIN32)
                                 "unknown"
#endif
                                 ));
  JS_SetPropertyStr(ctx,
                    ret,
                    "machine",
                    JS_NewString(ctx,
#ifdef __x86_64__
                                 "x86_64"
#elif defined(_X86_)
                                 "i686"
#else
                                 "unknown"
#endif
                                 ));
#endif

  return ret;
}

#ifndef _WIN32
static JSValue
js_misc_ioctl(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t fd = -1, args[2] = {-1, -1};
  int64_t request = -1LL;

  JS_ToInt32(ctx, &fd, argv[0]);
  JS_ToInt64(ctx, &request, argv[1]);

  if(argc >= 3)
    JS_ToInt32(ctx, &args[0], argv[2]);
  if(argc >= 4)
    JS_ToInt32(ctx, &args[1], argv[3]);

  return JS_NewInt32(ctx, ioctl(fd, request, args[0], args[1]));
}
#endif

static JSValue
js_misc_screensize(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  int size[2] = {-1, -1};

  if(argc >= 1 && JS_IsObject(argv[0]))
    ret = JS_DupValue(ctx, argv[0]);

  if(!screen_size(size)) {
    JSValue width, height;
    if(JS_IsUndefined(ret))
      ret = argc >= 1 && JS_IsArray(ctx, argv[0]) ? JS_DupValue(ctx, argv[0]) : JS_NewArray(ctx);
    width = JS_NewInt32(ctx, size[0]);
    height = JS_NewInt32(ctx, size[1]);
    if(JS_IsArray(ctx, ret)) {
      JS_SetPropertyUint32(ctx, ret, 0, width);
      JS_SetPropertyUint32(ctx, ret, 1, height);
    } else if(JS_IsObject(ret)) {
      JS_SetPropertyStr(ctx, ret, "width", width);
      JS_SetPropertyStr(ctx, ret, "height", height);
    } else {
      JS_FreeValue(ctx, width);
      JS_FreeValue(ctx, height);
    }
  }

  return ret;
}

enum {
  ERASE_IN_DISPLAY,
  ERASE_IN_LINE,
};

static JSValue
js_misc_clearscreen(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int32_t fd = 1, mode = 0;
  intptr_t h;
  JSValue ret = JS_UNDEFINED;

  if(argc >= 1)
    JS_ToInt32(ctx, &fd, argv[0]);
  if(argc >= 2)
    JS_ToInt32(ctx, &mode, argv[1]);

#ifdef _WIN32
  if(-1 == (h = (intptr_t)_get_osfhandle(fd)))
    return JS_ThrowInternalError(ctx, "argument 1 must be file descriptor");

#else
  h = fd;
#endif
  ret = JS_NewBool(ctx, clear_screen(h, mode, magic == ERASE_IN_LINE));

  return ret;
}

enum { SET_CURSOR_POSITION, MOVE_CURSOR };

static JSValue
js_misc_cursorposition(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int32_t fd = 1, x = magic == MOVE_CURSOR ? 0 : -1, y = magic == MOVE_CURSOR ? 0 : -1;
  intptr_t h;

  if(argc >= 1)
    JS_ToInt32(ctx, &fd, argv[0]);
  if(argc >= 2 && JS_IsNumber(argv[1]))
    JS_ToInt32(ctx, &x, argv[1]);
  if(argc >= 3 && JS_IsNumber(argv[2]))
    JS_ToInt32(ctx, &y, argv[2]);

#ifdef _WIN32
  if(-1 == (h = (intptr_t)_get_osfhandle(fd)))
    return JS_ThrowInternalError(ctx, "argument 1 must be file descriptor");
#else
  h = fd;
#endif

  return JS_NewBool(ctx, magic == MOVE_CURSOR ? move_cursor(h, x, y) : set_cursor_position(h, x, y));
}

enum {
  SET_TEXT_ATTRIBUTES,
  SET_TEXT_COLOR,
};

static JSValue
js_misc_settextattr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int32_t fd = 1;
  intptr_t h;
  JSValue ret = JS_UNDEFINED;

  if(argc >= 1)
    JS_ToInt32(ctx, &fd, argv[0]);

#ifdef _WIN32
  if(-1 == (h = (intptr_t)_get_osfhandle(fd)))
    return JS_ThrowInternalError(ctx, "argument 1 must be file descriptor");

#else
  h = fd;
#endif

  switch(magic) {
    case SET_TEXT_ATTRIBUTES: {
      uint32_t attr = 0;

      if(argc >= 2)
        JS_ToUint32(ctx, &attr, argv[1]);

      ret = JS_NewBool(ctx, set_text_attributes(h, attr));
      break;
    }

    case SET_TEXT_COLOR: {

#ifdef _WIN32
      uint32_t attr = 0;

      get_text_attributes(h, &attr);

      for(int i = 0; i < argc; i++) {
        int32_t color = 0;
        JS_ToInt32(ctx, &color, argv[i]);

        uint8_t bits = ColorToBits(color) & 0x0f;
        uint8_t shift = ColorIsBG(color) ? 4 : 0;
        uint8_t mask = ColorIsBG(color) ? 0xf0 : 0x0f;

        attr &= ~mask;
        attr |= bits << shift;
      }

      if(set_text_attributes(h, attr))
        ret = JS_NewUint32(ctx, attr);
#else
      int32_t* intarray;

      if((intarray = js_argv_to_int32v(ctx, argc, argv))) {
        ret = JS_NewBool(ctx, set_text_color(h, argc, intarray));

        js_free(ctx, intarray);
      }
#endif

      break;
    }
  }

  return ret;
}

#ifdef _WIN32
enum {
  SET_CONSOLE_MODE,
  GET_CONSOLE_MODE,
};

static JSValue
js_misc_consolemode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int32_t fd = 1;
  JSValue ret = JS_UNDEFINED;
  intptr_t h;

  if(argc >= 1)
    JS_ToInt32(ctx, &fd, argv[0]);

  if(-1 == (h = (intptr_t)_get_osfhandle(fd)))
    return JS_ThrowInternalError(ctx, "argument 1 must be file descriptor");

  switch(magic) {
    case SET_CONSOLE_MODE: {
      uint32_t mode = 0;
      JS_ToUint32(ctx, &mode, argv[1]);

      if(!SetConsoleMode(h, mode))
        ret = JS_Throw(ctx, js_syscallerror_new(ctx, "SetConsoleMode", GetLastError()));

      break;
    }
    case GET_CONSOLE_MODE: {
      DWORD mode = 0;
      if(!GetConsoleMode(h, &mode))
        ret = JS_Throw(ctx, js_syscallerror_new(ctx, "GetConsoleMode", GetLastError()));
      else
        ret = JS_NewUint32(ctx, mode);

      break;
    }
  }

  return ret;
}
#endif

static JSValue
js_misc_btoa(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  InputBuffer input = js_input_chars(ctx, argv[0]);
  size_t enclen = b64_get_encoded_buffer_size(input.size);
  uint8_t* encbuf = js_malloc(ctx, enclen);

  b64_encode(input.data, input.size, encbuf);

  ret = JS_NewStringLen(ctx, (const char*)encbuf, enclen);
  js_free(ctx, encbuf);
  return ret;
}

static JSValue
js_misc_atob(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  InputBuffer input = js_input_chars(ctx, argv[0]);
  size_t declen = b64_get_decoded_buffer_size(input.size);
  uint8_t* decbuf = js_malloc(ctx, declen);

  b64_decode(input.data, input.size, decbuf);

  ret = JS_NewArrayBufferCopy(ctx, (const uint8_t*)decbuf, declen);
  js_free(ctx, decbuf);
  return ret;
}

static JSValue
js_misc_compile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  const char* file = JS_ToCString(ctx, argv[0]);
  BOOL is_mod = FALSE;
  uint8_t* buf;
  size_t len;
  int32_t flags = JS_EVAL_TYPE_GLOBAL;

  if(argc >= 2 && JS_IsNumber(argv[1])) {
    JS_ToInt32(ctx, &flags, argv[1]);
  } else if(argc >= 2 && JS_IsBool(argv[1])) {
    if(JS_ToBool(ctx, argv[1]))
      flags |= JS_EVAL_TYPE_MODULE;
  }
  is_mod = !!(flags & JS_EVAL_TYPE_MODULE);
  if(str_ends(file, ".jsm"))
    is_mod = TRUE;
  if((buf = js_load_file(ctx, &len, file))) {
    if(!is_mod && JS_DetectModule((const char*)buf, len))
      is_mod = TRUE;
    flags |= (is_mod ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL);
    ret = JS_Eval(ctx, (const char*)buf, len, file, flags | (is_mod ? JS_EVAL_FLAG_COMPILE_ONLY : 0));

    if(is_mod && !(flags & JS_EVAL_FLAG_COMPILE_ONLY)) {
      ret = JS_EvalFunction(ctx, ret);
    }
  } else {
    ret = JS_ThrowReferenceError(ctx, "could not load '%s': %s", file, strerror(errno));
  }
  return ret;
}

struct ImmutableClosure {
  JSRuntime* rt;
  JSValue ctor, proto;
};

static void
js_misc_immutable_free(void* ptr) {
  struct ImmutableClosure* closure = ptr;
  JS_FreeValueRT(closure->rt, closure->ctor);
  JS_FreeValueRT(closure->rt, closure->proto);
  free(ptr);
}

static JSValue
js_misc_immutable_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic, void* ptr) {
  JSValue ret = JS_UNDEFINED;

  if(ptr) {
    struct ImmutableClosure* closure = ptr;

    ret = JS_CallConstructor2(ctx, closure->ctor, new_target, argc, argv);
  } else {
  }
  return ret;
}

static JSValue
js_misc_immutable_class(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  struct ImmutableClosure* closure;
  JSValue ret, proto;
  char *name, *new_name;

  if(argc == 0 || !JS_IsConstructor(ctx, argv[0]))
    return JS_ThrowTypeError(ctx, "argument 1 must be a constructor");

  if(!(closure = malloc(sizeof(struct ImmutableClosure))))
    return JS_ThrowOutOfMemory(ctx);

  closure->rt = JS_GetRuntime(ctx);
  closure->ctor = JS_DupValue(ctx, argv[0]);
  closure->proto = JS_GetPropertyStr(ctx, closure->ctor, "prototype");

  if(JS_IsException(closure->proto)) {
    js_misc_immutable_free(closure);
    return JS_ThrowTypeError(ctx, "argument 1 must have a 'prototype' property");
  }

  name = js_object_classname(ctx, closure->proto);
  new_name = alloca(strlen(name) + sizeof("Immutable"));

  str_copy(&new_name[str_copy(new_name, "Immutable")], name);

  proto = JS_NewObject(ctx);
  JS_SetPrototype(ctx, proto, closure->proto);

  /* {
     JSCFunctionListEntry entries[] = {JS_PROP_STRING_DEF("[Symbol.toStringTag]", new_name,
   JS_PROP_CONFIGURABLE)}; JS_SetPropertyFunctionList(ctx, proto, entries, countof(entries));
   }*/
  js_set_tostringtag_value(ctx, proto, JS_NewString(ctx, new_name));

  ret = js_function_cclosure(ctx, js_misc_immutable_constructor, 0, 0, closure, js_misc_immutable_free);

  if(!JS_IsConstructor(ctx, ret))
    JS_SetConstructorBit(ctx, ret, TRUE);

  JS_SetConstructor(ctx, ret, proto);

  return ret;
}

static JSValue
js_misc_write_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  size_t size;
  uint8_t* bytecode;

  if((bytecode = JS_WriteObject(ctx, &size, argv[0], JS_WRITE_OBJ_BYTECODE))) {
    ret = JS_NewArrayBuffer(ctx, bytecode, size, js_arraybuffer_free_pointer, 0, FALSE);
  }
  return ret;
}

static JSValue
js_misc_read_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer input = js_input_chars(ctx, argv[0]);

  return JS_ReadObject(ctx, input.data, input.size, JS_READ_OBJ_BYTECODE);
}

static JSValue
js_misc_getx(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int32_t ret = 0;

  switch(magic) {
#ifdef HAVE_GETTID
    case FUNC_GETTID: {
      ret = gettid();
      break;
    }
#endif
#ifndef __wasi__
    case FUNC_GETPID: {
      ret = getpid();
      break;
    }
#endif
#if !defined(__wasi__) && !defined(_WIN32)
    case FUNC_GETPPID: {
      ret = getppid();
      break;
    }
#endif
    case FUNC_GETSID: {
      // sret = getsid();
      break;
    }
#if !defined(__wasi__) && !defined(_WIN32)
    case FUNC_GETUID: {
      ret = getuid();
      break;
    }
    case FUNC_GETGID: {
      ret = getgid();
      break;
    }
    case FUNC_GETEUID: {
      ret = geteuid();
      break;
    }
    case FUNC_GETEGID: {
      ret = getegid();
      break;
    }
    case FUNC_SETUID: {
      int32_t uid;
      JS_ToInt32(ctx, &uid, argv[0]);
      ret = setuid(uid);
      break;
    }
    case FUNC_SETGID: {
      int32_t gid;
      JS_ToInt32(ctx, &gid, argv[0]);
      ret = setgid(gid);
      break;
    }
    case FUNC_SETEUID: {
      int32_t euid;
      JS_ToInt32(ctx, &euid, argv[0]);
      ret = seteuid(euid);
      break;
    }
    case FUNC_SETEGID: {
      int32_t egid;
      JS_ToInt32(ctx, &egid, argv[0]);
      ret = setegid(egid);
      break;
    }
#endif
  }
  if(ret == -1)
    return JS_ThrowInternalError(ctx,
                                 "%s() failed: %s",
                                 ((const char* const[]){
                                     "getpid",
                                     "getppid",
                                     "getsid",
                                     "getuid",
                                     "getgid",
                                     "geteuid",
                                     "getegid",
                                     "setuid",
                                     "setgid",
                                     "seteuid",
                                     "setegid",
                                 })[magic - FUNC_GETPID],
                                 strerror(errno));

  return JS_NewInt32(ctx, ret);
}

enum {
  VALUE_TYPE = 0,
  VALUE_TAG,
  VALUE_POINTER,
  STRING_POINTER,
  STRING_LENGTH,
  STRING_BUFFER,
};

static JSValue
js_misc_valuetype(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case VALUE_TYPE: {
      const char* typestr = js_value_typestr(ctx, argv[0]);
      ret = JS_NewString(ctx, typestr);
      break;
    }
    case VALUE_TAG: {
      ret = JS_NewInt32(ctx, JS_VALUE_GET_TAG(argv[0]));
      break;
    }
    case VALUE_POINTER: {
      void* ptr = JS_VALUE_GET_PTR(argv[0]);
      char buf[128];

      snprintf(buf, sizeof(buf), "%p", ptr);
      ret = JS_NewString(ctx, buf);
      break;
    }
    case STRING_POINTER: {
      if(JS_IsString(argv[0])) {
        /*char buf[128];

        snprintf(buf, sizeof(buf), "%p", js_cstring_ptr(argv[0]));
        ret = JS_NewString(ctx, buf);*/

        uint64_t ptr = (ptrdiff_t)js_cstring_ptr(argv[0]);

        if(ptr < (1ll << 53))
          ret = JS_NewInt64(ctx, ptr);
        else
          ret = JS_NewBigUint64(ctx, ptr);
      } else {
        void* ptr;

        if((ptr = js_get_pointer(ctx, argv[0])))
          ret = JS_NewString(ctx, ptr);
      }

      break;
    }
    case STRING_LENGTH: {
      if(JS_IsString(argv[0])) {
        ret = JS_NewInt64(ctx, strlen(js_cstring_ptr(argv[0])));
      } else {
        void* ptr;

        if((ptr = js_get_pointer(ctx, argv[0])))
          ret = JS_NewInt64(ctx, strlen(ptr));
      }

      break;
    }
    case STRING_BUFFER: {
      if(JS_IsString(argv[0])) {
        ret = js_arraybuffer_fromstring(ctx, argv[0]);
      }

      break;
    }
  }
  return ret;
}

static JSValue
js_misc_getopaque(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  if(JS_IsObject(argv[0])) {
    JSClassID id;
    char buf[128];

    id = JS_GetClassID(argv[0]);

    snprintf(buf, sizeof(buf), "%p", JS_GetOpaque(argv[0], id));
    ret = JS_NewString(ctx, buf);
  }

  return ret;
}

static JSValue
js_misc_evalstring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  int32_t flags = JS_EVAL_TYPE_MODULE;
  InputBuffer input = js_input_chars(ctx, argv[0]);
  const char* filename = 0;

  if(argc > 1)
    filename = JS_ToCString(ctx, argv[1]);

  if(argc > 2)
    JS_ToInt32(ctx, &flags, argv[2]);

  ret = js_eval_buf(ctx, input.data, input.size, filename ? filename : "<input>", flags);

  if(filename)
    JS_FreeCString(ctx, filename);

  return ret;
}

static JSValue
js_misc_evalbinary(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  BOOL load_only = FALSE;
  JSValueConst obj;
  int tag = JS_VALUE_GET_TAG(argv[0]);
  if(argc >= 2)
    load_only = JS_ToBool(ctx, argv[1]);

  if(tag != JS_TAG_MODULE && tag != JS_TAG_FUNCTION_BYTECODE)
    obj = js_misc_read_object(ctx, this_val, argc, argv);
  else
    obj = argv[0];
  if(JS_IsException(obj))
    return obj;

  tag = JS_VALUE_GET_TAG(obj);

  if(tag != JS_TAG_MODULE && tag != JS_TAG_FUNCTION_BYTECODE)
    return JS_ThrowTypeError(ctx, "obj is not MODULE nor BYTECODE");

  if(load_only) {
    if(tag == JS_TAG_MODULE)
      js_module_set_import_meta(ctx, obj, FALSE, FALSE);
  } else {
    if(tag == JS_TAG_MODULE) {
      if(JS_ResolveModule(ctx, obj) < 0) {
        JSModuleDef* m = JS_VALUE_GET_PTR(obj);
        const char* name = JS_AtomToCString(ctx, m->module_name);
        ret = JS_ThrowInternalError(ctx, "Failed resolving module '%s'", name);
        JS_FreeCString(ctx, name);
        JS_FreeValue(ctx, obj);
        return ret;
      }
      js_module_set_import_meta(ctx, obj, FALSE, TRUE);
    }
    ret = JS_EvalFunction(ctx, obj);
  }
  return ret;
}

static JSValue
js_misc_opcode_array(JSContext* ctx, const JSOpCode* opcode) {
  JSValue ret = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, ret, 0, JS_NewUint32(ctx, opcode->size));
  JS_SetPropertyUint32(ctx, ret, 1, JS_NewUint32(ctx, opcode->n_pop));
  JS_SetPropertyUint32(ctx, ret, 2, JS_NewUint32(ctx, opcode->n_push));
  JS_SetPropertyUint32(ctx, ret, 3, JS_NewUint32(ctx, opcode->fmt));
  JS_SetPropertyUint32(ctx, ret, 4, JS_NewString(ctx, opcode->name));
  return ret;
}

static JSValue
js_misc_opcode_object(JSContext* ctx, const JSOpCode* opcode) {
  JSValue ret = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, ret, "size", JS_NewUint32(ctx, opcode->size));
  JS_SetPropertyStr(ctx, ret, "n_pop", JS_NewUint32(ctx, opcode->n_pop));
  JS_SetPropertyStr(ctx, ret, "n_push", JS_NewUint32(ctx, opcode->n_push));
  JS_SetPropertyStr(ctx, ret, "fmt", JS_NewUint32(ctx, opcode->fmt));
  JS_SetPropertyStr(ctx, ret, "name", JS_NewString(ctx, opcode->name));
  return ret;
}

static JSValue
js_misc_opcodes(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_NewArray(ctx);
  size_t i, j, len = countof(js_opcodes);
  BOOL as_object = FALSE;

  if(argc >= 1)
    as_object = JS_ToBool(ctx, argv[0]);

  for(i = 0, j = 0; i < len; i++) {

    if(i >= OP_TEMP_START && i < OP_TEMP_END)
      continue;

    JS_SetPropertyUint32(ctx, ret, j++, (as_object ? js_misc_opcode_object : js_misc_opcode_array)(ctx, &js_opcodes[i]));
  }

  return ret;
}

static JSValue
js_misc_get_bytecode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  if(JS_IsFunction(ctx, argv[0])) {
    JSObject* obj = JS_VALUE_GET_OBJ(argv[0]);
    JSFunctionBytecode* fnbc;

    if((fnbc = obj->u.func.function_bytecode)) {
      ret = JS_NewArrayBufferCopy(ctx, fnbc->byte_code_buf, fnbc->byte_code_len);
    }
  }

  return ret;
}

enum {
  ATOM_TO_STRING = 0,
  ATOM_TO_VALUE,
  VALUE_TO_ATOM,
};

static JSValue
js_misc_atom(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case ATOM_TO_STRING: {
      int32_t atom;
      JS_ToInt32(ctx, &atom, argv[0]);
      ret = JS_AtomToString(ctx, atom);
      break;
    }
    case ATOM_TO_VALUE: {
      int32_t atom;
      JS_ToInt32(ctx, &atom, argv[0]);
      ret = JS_AtomToValue(ctx, atom);
      break;
    }
    case VALUE_TO_ATOM: {
      JSAtom atom = JS_ValueToAtom(ctx, argv[0]);
      ret = JS_NewUint32(ctx, atom);
      break;
    }
  }
  return ret;
}

enum {
  GET_CLASS_ID = 0,
  GET_CLASS_NAME,
  GET_CLASS_ATOM,
  GET_CLASS_COUNT,
  GET_CLASS_PROTO,
  GET_CLASS_CONSTRUCTOR,
  GET_TYPE_ID,
  GET_TYPE_STR,
  GET_TYPE_NAME,
};

static JSValue
js_misc_classid(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  JSObject* obj;
  int32_t class_id = 0;

  if(argc >= 1) {
    if(JS_IsNumber(argv[0]))
      JS_ToInt32(ctx, &class_id, argv[0]);
    else if((obj = js_value_obj(argv[0])))
      class_id = obj->class_id;
    else if(JS_IsObject(argv[0]))
      class_id = JS_GetClassID(argv[0]);
  }

  switch(magic) {
    case GET_CLASS_ID: {
      if(class_id > 0)
        ret = JS_NewUint32(ctx, class_id);
      break;
    }
    case GET_CLASS_NAME: {
      if(class_id > 0) {
        JSAtom atom;
        if((atom = js_class_atom(ctx, class_id)))
          ret = JS_AtomToValue(ctx, atom);
      }
      break;
    }
    case GET_CLASS_ATOM: {
      if(class_id > 0) {
        JSAtom atom;
        if((atom = js_class_atom(ctx, class_id)))
          ret = JS_NewInt32(ctx, atom);
      }
      break;
    }
    case GET_CLASS_COUNT: {
      uint32_t i, class_count = ctx->rt->class_count;
      for(i = 1; i < class_count; i++)
        if(!JS_IsRegisteredClass(ctx->rt, i))
          break;

      ret = JS_NewUint32(ctx, i);
      break;
    }
    case GET_CLASS_PROTO: {
      if(class_id > 0)
        ret = JS_GetClassProto(ctx, class_id);
      break;
    }
    case GET_CLASS_CONSTRUCTOR: {
      if(class_id > 0) {
        JSValue proto = JS_GetClassProto(ctx, class_id);
        if(JS_IsObject(proto))
          ret = JS_GetPropertyStr(ctx, proto, "constructor");
        JS_FreeValue(ctx, proto);
      }
      break;
    }
  }
  return ret;
}

static JSValue
js_misc_type(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  int32_t type_id = 0;

  if(argc >= 1) {
    /*  if(JS_IsNumber(argv[0]))
          JS_ToInt32(ctx, &type_id, argv[0]);
        else*/
    type_id = js_value_type(ctx, argv[0]);
  }

  switch(magic) {
    case GET_TYPE_ID: {
      ret = JS_NewInt32(ctx, type_id);
      break;
    }
    case GET_TYPE_STR: {
      const char* type;
      if((type = js_value_type_name(type_id)))
        ret = JS_NewString(ctx, type);
      break;
    }
    case GET_TYPE_NAME: {
      const char* type;
      if((type = (const char*)js_object_classname(ctx, argv[0]))) {
        ret = JS_NewString(ctx, type);
        js_free(ctx, (void*)type);
      } else if((type = js_value_type_name(type_id))) {
        ret = JS_NewString(ctx, type);
      }
      break;
    }
  }
  return ret;
}

enum {
  BITFIELD_SET,
  BITFIELD_BITS,
  BITFIELD_FROMARRAY,
  BITFIELD_TOARRAY,
};

static JSValue
js_misc_bitfield(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  size_t len;
  int64_t offset = 0;
  switch(magic) {
    case BITFIELD_SET: {
      const uint8_t* buf;

      if(argc >= 2)
        JS_ToInt64(ctx, &offset, argv[1]);

      if((buf = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
        size_t i, j = 0, bits = len * 8;
        ret = JS_NewArray(ctx);

        for(i = 0; i < bits; i++) {
          if(buf[i >> 3] & (1u << (i & 0x7))) {
            JS_SetPropertyUint32(ctx, ret, j++, JS_NewInt64(ctx, i + offset));
          }
        }
      }
      break;
    }

    case BITFIELD_BITS: {
      const uint8_t* buf;

      if(argc >= 2)
        JS_ToInt64(ctx, &offset, argv[1]);

      if(argc >= 1 && (buf = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
        size_t i, j = 0, bits = len * 8;
        ret = JS_NewArray(ctx);

        for(i = 0; i < bits; i++) {
          BOOL value = !!(buf[i >> 3] & (1u << (i & 0x7)));
          JS_SetPropertyUint32(ctx, ret, j++, JS_NewInt32(ctx, value));
        }
      } else if(argc >= 1 && JS_IsArray(ctx, argv[0])) {

        size_t i, len = js_array_length(ctx, argv[0]);
        uint8_t* bufptr;
        size_t bufsize = (len + 7) >> 3;

        if((bufptr = js_mallocz(ctx, bufsize)) == 0)
          return JS_EXCEPTION;

        for(i = 0; i < len; i++) {
          JSValue element = JS_GetPropertyUint32(ctx, argv[0], i);
          BOOL value = JS_ToBool(ctx, element);
          JS_FreeValue(ctx, element);

          if(value)
            bufptr[i >> 3] |= 1u << (i & 0x7);
        }
        ret = JS_NewArrayBuffer(ctx, bufptr, bufsize, js_arraybuffer_free_pointer, bufptr, FALSE);
      }
      break;
    }
    case BITFIELD_TOARRAY: {
      const uint8_t* buf;

      if(argc >= 2)
        JS_ToInt64(ctx, &offset, argv[1]);

      if((buf = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
        size_t i, bits = len * 8;
        ret = JS_NewArray(ctx);

        for(i = 0; i < bits; i++) {
          BOOL value = buf[i >> 3] & (1u << (i & 0x7));

          JS_SetPropertyUint32(ctx, ret, i, JS_NewBool(ctx, value));
        }
      }
      break;
    }
    case BITFIELD_FROMARRAY: {
      JSValue prop;
      if(argc >= 2)
        JS_ToInt64(ctx, &offset, argv[1]);

      if(!JS_IsArray(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "argument must be an array");

      prop = JS_GetPropertyUint32(ctx, argv[0], 0);
      len = js_array_length(ctx, argv[0]);
      if(len) {
        uint8_t* bufptr;
        size_t bufsize;

        if(JS_IsBool(prop)) {
          size_t i;
          bufsize = (len + 7) >> 3;
          if((bufptr = js_mallocz(ctx, bufsize)) == 0)
            return JS_EXCEPTION;

          for(i = 0; i < len; i++) {
            JSValue value = JS_GetPropertyUint32(ctx, argv[0], i);
            BOOL b = JS_ToBool(ctx, value);
            JS_FreeValue(ctx, value);

            bufptr[i >> 3] |= (b ? 1 : 0) << (i & 0x7);
          }

        } else {

          size_t i;
          int64_t max = -1;

          for(i = 0; i < len; i++) {
            JSValue value = JS_GetPropertyUint32(ctx, argv[0], i);
            uint32_t number;
            JS_ToUint32(ctx, &number, value);
            JS_FreeValue(ctx, value);

            if(max < number)
              max = number;
          }
          bufsize = ((max + 1) + 7) >> 3;
          if((bufptr = js_mallocz(ctx, bufsize)) == 0)
            return JS_EXCEPTION;

          for(i = 0; i < len; i++) {
            JSValue value = JS_GetPropertyUint32(ctx, argv[0], i);
            uint32_t number;
            JS_ToUint32(ctx, &number, value);
            JS_FreeValue(ctx, value);

            number -= offset;

            bufptr[number >> 3] |= 1u << (number & 0x7);
          }
        }
        ret = JS_NewArrayBuffer(ctx, bufptr, bufsize, js_arraybuffer_free_pointer, bufptr, FALSE);
      }
      break;
    }
  }
  return ret;
}

enum {
  BITOP_NOT,
  BITOP_XOR,
  BITOP_AND,
  BITOP_OR,
};

static JSValue
js_misc_bitop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  size_t i;
  struct {
    uint8_t* buf;
    size_t len;
  } ab[2] = {{0, 0}, {0, 0}};

  if(argc >= 1) {
    ab[0].buf = JS_GetArrayBuffer(ctx, &ab[0].len, argv[0]);

    if(argc >= 2)
      ab[1].buf = JS_GetArrayBuffer(ctx, &ab[1].len, argv[1]);
  }

  if(ab[0].buf == 0)
    return JS_ThrowTypeError(ctx, "argument 1 must be an ArrayBuffer");

  if(magic > BITOP_NOT && ab[1].buf == 0)
    return JS_ThrowTypeError(ctx, "argument 2 must be an ArrayBuffer");

  ret = JS_DupValue(ctx, argv[0]);

  switch(magic) {
    case BITOP_NOT: {
      for(i = 0; i < ab[0].len; i++)
        ab[0].buf[i] ^= 0xffu;

      break;
    }
    case BITOP_XOR: {

      for(i = 0; i < ab[0].len; i++)
        ab[0].buf[i] ^= ab[1].buf[i % ab[1].len];

      break;
    }
    case BITOP_AND: {
      break;
    }
    case BITOP_OR: {
      break;
    }
  }
  return ret;
}

enum {
  RANDOM_RAND,
  RANDOM_RANDI,
  RANDOM_RANDF,
  RANDOM_SRAND,
};

static JSValue
js_misc_random(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  uint32_t bound = 0;
  JSValue ret = JS_UNDEFINED;

  if(argc > 0 && JS_IsNumber(argv[0]))
    JS_ToUint32(ctx, &bound, argv[0]);

  switch(magic) {
    case RANDOM_RAND: {
      uint32_t num = argc > 0 ? pcg32_random_bounded_divisionless(bound) : pcg32_random();
      ret = JS_NewUint32(ctx, num);
      break;
    }
    case RANDOM_RANDI: {
      int32_t num = argc > 0 ? pcg32_random_bounded_divisionless(bound * 2) - bound : pcg32_random();
      ret = JS_NewInt32(ctx, num);
      break;
    }
    case RANDOM_RANDF: {
      uint32_t num = pcg32_random();
      ret = JS_NewFloat64(ctx, (double)num / UINT32_MAX);
      break;
    }
    case RANDOM_SRAND: {
      uint32_t st = 0;
      JS_ToUint32(ctx, &st, argv[0]);
      pcg32_init_state(st);
      ret = JS_UNDEFINED;
      break;
    }
  }

  return ret;
}

/*static const uint8_t js_misc_escape_sq_tab[256] = {
    'x', 'x',  'x', 'x', 'x', 'x', 'x', 'x', 0x62, 0x74, 0x6e, 0x76, 0x66, 0x72, 'x', 'x', 'x',  'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 0,   0, 0, 0, 0, 0,
    0,   0x27, 0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0,
    0,   0,    0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0x5c, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0,
    0,   0,    0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    'x',  0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0,
    0,   0,    0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0,
    0,   'u',  'u', 'u', 'u', 'u', 'u', 'u', 'u',  'u',  'u',  'u',  'u',  'u',  'u', 'u', 'u',  'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u',
};*/
static const uint8_t js_misc_escape_dq_tab[256] = {
    'x', 'x', 'x', 'x', 'x', 'x', 'x',  'x', 0x62, 0x74, 0x6e, 0x76, 0x66, 0x72, 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',  'x', 'x', 'x', 'x',

    'x', 'x', 'x', 'x', 0,   0,   0x22, 0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,   0,   0,   0,   0,   0,   0,    0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0x5c, 0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,   0,   0,   0,   0,   0,   0,    0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   'x', 0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,   0,   0,   0,   0,   0,   0,    0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 'u',
    'u', 'u', 'u', 'u', 'u', 'u', 'u',  'u', 'u',  'u',  'u',  'u',  'u',  'u',  'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u',  'u', 'u', 'u', 'u', 'u', 'u', 'u',
};

JSValue
js_misc_escape(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer input = js_input_chars(ctx, argv[0]);
  if(input.data) {
    uint8_t escape_tab[256];
    const uint8_t* tab = js_misc_escape_dq_tab;
    int32_t* intv = 0;
    size_t i, nelems;

    if(argc > 1 && (intv = js_array_to_int32v(ctx, &nelems, argv[1]))) {
      for(i = 0; i < nelems; i++) {
        escape_tab[i] = intv[i];
      }
      while(i < 256) {
        escape_tab[i++] = '\0';
      }
      tab = escape_tab;
      js_free(ctx, intv);
    }

    DynBuf output;
    js_dbuf_init(ctx, &output);
    dbuf_put_escaped_table(&output, (const char*)input.data, input.size, tab);

    return dbuf_tostring_free(&output, ctx);
  }
  return JS_DupValue(ctx, argv[0]);
}

static int
js_misc_unescape_pred(const char* s, size_t* lenp) {
  uint32_t l;
  int val = -1;
  size_t len = 1;

  if(*s == '\\')
    val = (int)(unsigned int)(unsigned char)'\\';
  else if(*s == 'n')
    val = (int)(unsigned int)(unsigned char)'\n';
  else if(*s == 'r')
    val = (int)(unsigned int)(unsigned char)'\r';
  else if(*s == 't')
    val = (int)(unsigned int)(unsigned char)'\t';
  else if(*s == 'v')
    val = (int)(unsigned int)(unsigned char)'\v';
  else if(*s == 'b')
    val = (int)(unsigned int)(unsigned char)'\b';
  else if((len = scan_8long(s, &l)) >= 3)
    val = (int)(unsigned int)(unsigned char)l;

  if(val != -1) {
    if(lenp)
      *lenp += len;
    return val;
  }

  return 0;
}

JSValue
js_misc_unescape(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer input = js_input_chars(ctx, argv[0]);
  if(input.data) {
    DynBuf output;
    js_dbuf_init(ctx, &output);
    dbuf_put_unescaped_pred(&output, (const char*)input.data, input.size, &js_misc_unescape_pred);
    return dbuf_tostring_free(&output, ctx);
  }
  return JS_DupValue(ctx, argv[0]);
}

JSValue
js_misc_quote(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer input = js_input_chars(ctx, argv[0]);
  DynBuf output;
  char quote = '"';
  uint8_t table[256] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 'b',  't',  'n',  'v',  'f',  'r',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, '\\', 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75,
      0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75,
  };

  js_dbuf_init(ctx, &output);
  if(argc >= 2) {
    const char* str = JS_ToCString(ctx, argv[1]);
    if(str[0])
      quote = str[0];
    JS_FreeCString(ctx, str);
  }

  table[(unsigned)quote] = quote;

  if(quote == '`') {
    table[(unsigned)'\r'] = 0;
    table[(unsigned)'\n'] = 0;
    table[(unsigned)'$'] = '$';
  }

  dbuf_putc(&output, quote);
  dbuf_put_escaped_table(&output, (const char*)input.data, input.size, table);
  dbuf_putc(&output, quote);
  return dbuf_tostring_free(&output, ctx);
}

JSValue
js_misc_error(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t errnum = errno;
  const char* syscall = 0;
  JSValue err;
  if(argc >= 1)
    JS_ToInt32(ctx, &errnum, argv[0]);
  if(argc >= 2)
    syscall = JS_ToCString(ctx, argv[1]);

  err = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, err, "errno", JS_NewInt32(ctx, errnum));
  if(errnum)
    JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, strerror(errnum)));
  if(syscall)
    JS_SetPropertyStr(ctx, err, "syscall", JS_NewString(ctx, syscall));

  if(syscall)
    JS_FreeCString(ctx, syscall);

  return err;
}

enum {
  IS_ARRAY,
  IS_BIGDECIMAL,
  IS_BIGFLOAT,
  IS_BIGINT,
  IS_BOOL,
  IS_CFUNCTION,
  IS_BOUNDFUNCTION,
  IS_JSFUNCTION,
  IS_CONSTRUCTOR,
  IS_EMPTYSTRING,
  IS_ERROR,
  IS_EXCEPTION,
  IS_EXTENSIBLE,
  IS_FUNCTION,
  IS_HTMLDDA,
  IS_INSTANCEOF,
  IS_INTEGER,
  IS_JOBPENDING,
  IS_LIVEOBJECT,
  IS_NULL,
  IS_NUMBER,
  IS_OBJECT,
  IS_REGISTEREDCLASS,
  IS_STRING,
  IS_SYMBOL,
  IS_UNCATCHABLEERROR,
  IS_UNDEFINED,
  IS_UNINITIALIZED,
  IS_ARRAYBUFFER,
};

JSValue
js_misc_is(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int32_t r = -1;
  JSValueConst arg = argc >= 1 ? argv[0] : JS_UNDEFINED;

  switch(magic) {
    case IS_ARRAY: r = JS_IsArray(ctx, arg); break;
    case IS_BIGDECIMAL: r = JS_IsBigDecimal(arg); break;
    case IS_BIGFLOAT: r = JS_IsBigFloat(arg); break;
    case IS_BIGINT: r = JS_IsBigInt(ctx, arg); break;
    case IS_BOOL: r = JS_IsBool(arg); break;
    case IS_CFUNCTION: r = JS_GetClassID(arg) == JS_CLASS_C_FUNCTION; break;
    case IS_BOUNDFUNCTION: r = JS_GetClassID(arg) == JS_CLASS_BOUND_FUNCTION; break;
    case IS_JSFUNCTION: r = JS_GetClassID(arg) == JS_CLASS_BYTECODE_FUNCTION; break;
    case IS_CONSTRUCTOR: r = JS_IsConstructor(ctx, arg); break;
    case IS_EMPTYSTRING: r = JS_VALUE_GET_TAG(arg) == JS_TAG_STRING && JS_VALUE_GET_STRING(arg)->len == 0; break;
    case IS_ERROR: r = JS_IsError(ctx, arg); break;
    case IS_EXCEPTION: r = JS_IsException(arg); break;
    case IS_EXTENSIBLE: r = JS_IsExtensible(ctx, arg); break;
    case IS_FUNCTION: r = JS_IsFunction(ctx, arg); break;
    case IS_HTMLDDA: r = JS_VALUE_GET_TAG(arg) == JS_TAG_OBJECT && JS_VALUE_GET_OBJ(arg)->is_HTMLDDA; break;
    case IS_INSTANCEOF: r = JS_IsInstanceOf(ctx, arg, argv[1]); break;
    case IS_INTEGER: r = JS_IsNumber(arg) && JS_VALUE_GET_TAG(arg) != JS_TAG_FLOAT64; break;
    case IS_JOBPENDING: r = JS_IsJobPending(JS_GetRuntime(ctx)); break;
    case IS_LIVEOBJECT: r = JS_IsLiveObject(JS_GetRuntime(ctx), arg); break;
    case IS_NULL: r = JS_IsNull(arg); break;
    case IS_NUMBER: r = JS_IsNumber(arg); break;
    case IS_OBJECT: r = JS_IsObject(arg); break;
    case IS_REGISTEREDCLASS: r = !JS_ToInt32(ctx, &r, arg) && JS_IsRegisteredClass(JS_GetRuntime(ctx), r); break;
    case IS_STRING: r = JS_IsString(arg); break;
    case IS_SYMBOL: r = JS_IsSymbol(arg); break;
    case IS_UNCATCHABLEERROR: r = JS_IsUncatchableError(ctx, arg); break;
    case IS_UNDEFINED: r = JS_IsUndefined(arg); break;
    case IS_UNINITIALIZED: r = JS_IsUninitialized(arg); break;
    case IS_ARRAYBUFFER: r = js_is_arraybuffer(ctx, arg); break;
  }
  if(r == -1)
    return JS_ThrowInternalError(ctx, "js_misc_is %d", magic);
  return JS_NewBool(ctx, r >= 1);
}

#ifdef HAVE_INOTIFY_INIT1
static JSValue
js_misc_watch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  int32_t fd = -1;

  if(argc >= 1) {
    JS_ToInt32(ctx, &fd, argv[0]);
  }

  if(js_is_arraybuffer(ctx, argv[0])) {
    InputBuffer buf = js_output_args(ctx, argc, argv);
    uint32_t count = 0, reclen;

    if(buf.range.length >= (int64_t)sizeof(struct inotify_event)) {
      size_t end = buf.pos + buf.range.offset + buf.range.length;
      ret = JS_NewArray(ctx);

      for(size_t i = buf.pos + buf.range.offset; i + sizeof(struct inotify_event) <= end; i += reclen) {
        struct inotify_event* ev = (struct inotify_event*)&buf.data[i];
        size_t namelen = byte_chr(ev->name, '\0', ev->len);
        reclen = sizeof(struct inotify_event) + ev->len;

        JSValue obj = JS_NewObject(ctx);

        JS_SetPropertyStr(ctx, obj, "wd", JS_NewInt32(ctx, ev->wd));
        if(ev->mask)
          JS_SetPropertyStr(ctx, obj, "mask", JS_NewUint32(ctx, ev->mask));
        if(ev->cookie)
          JS_SetPropertyStr(ctx, obj, "cookie", JS_NewUint32(ctx, ev->cookie));

        if(namelen)
          JS_SetPropertyStr(ctx, obj, "name", JS_NewStringLen(ctx, ev->name, namelen));
        // JS_SetPropertyStr(ctx, obj, "offset", JS_NewUint32(ctx, i));

        JS_SetPropertyUint32(ctx, ret, count++, obj);
      }
    }

  } else if(argc >= 2 && JS_IsString(argv[1])) {
    int wd;
    int32_t flags = IN_ALL_EVENTS;
    const char* filename;

    filename = JS_ToCString(ctx, argv[1]);
    if(argc >= 3)
      JS_ToInt32(ctx, &flags, argv[2]);

    if((wd = inotify_add_watch(fd, filename, flags)) == -1)
      return JS_ThrowInternalError(ctx, "inotify_add_watch(%d, %s, %08x) = %d (%s)", fd, filename, flags, wd, strerror(errno));

    // printf("inotify_add_watch(%d, %s, %08x) = %d\n", fd, filename, flags, wd);

    ret = JS_NewInt32(ctx, wd);
  } else if(argc >= 2 && JS_IsNumber(argv[1])) {
    int r;
    int32_t wd = -1;

    JS_ToInt32(ctx, &wd, argv[1]);

    if((r = inotify_rm_watch(fd, wd)) == -1)
      return JS_ThrowInternalError(ctx, "inotify_rm_watch(%d, %d) = %d (%s)", fd, wd, r, strerror(errno));
    // printf("inotify_add_watch(%d, %d) = %d\n", fd, wd, r);

    ret = JS_NewInt32(ctx, r);
  } else {
    int fd;
    if((fd = inotify_init1(IN_NONBLOCK)) == -1)
      return JS_ThrowInternalError(ctx, "inotify_init1(IN_NONBLOCK) failed (%s)", strerror(errno));

    // printf("inotify_init1() = %d\n", fd);
    ret = JS_NewInt32(ctx, fd);
  }

  return ret;
}
#endif

#ifdef HAVE_DAEMON
static JSValue
js_misc_daemon(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  BOOL nochdir, noclose;

  nochdir = argc >= 1 && JS_ToBool(ctx, argv[0]);

  noclose = argc >= 2 && JS_ToBool(ctx, argv[0]);

  return JS_NewInt32(ctx, daemon(nochdir, noclose));
}
#endif

#ifdef HAVE_FORK
static JSValue
js_misc_fork(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_NewInt32(ctx, fork());
}
#endif

#ifdef HAVE_VFORK
static JSValue
js_misc_vfork(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_NewInt32(ctx, vfork());
}
#endif

#ifdef HAVE_EXECVE
static JSValue
js_misc_exec(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  size_t nargs;
  char** args;
  const char* file;

  if(!(file = JS_ToCString(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a string");

  if(!(args = js_array_to_argv(ctx, &nargs, argv[1]))) {
    JS_FreeCString(ctx, file);
    return JS_ThrowTypeError(ctx, "argument 2 must be an array");
  }

  ret = JS_NewInt32(ctx, execve(file, args, environ));

  JS_FreeCString(ctx, file);
  js_strv_free(ctx, args);

  return ret;
}
#endif

static JSValue
js_misc_kill(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  uint64_t handle;
  uint32_t exitcode = 0;

  JS_ToIndex(ctx, &handle, argv[0]);
  if(argc > 1)
    JS_ToUint32(ctx, &exitcode, argv[1]);

#if !(defined(_WIN32) && !defined(__MSYS__))
  else
    exitcode = SIGTERM;
  ret = JS_NewInt32(ctx, kill(handle, exitcode));
#else
  ret = JS_NewBool(ctx, TerminateProcess((HANDLE)handle, exitcode));
#endif

  return ret;
}
#ifdef HAVE_SETSID
static JSValue
js_misc_setsid(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_NewInt32(ctx, setsid());
}
#endif

typedef struct {
  JSContext* ctx;
  JSValue fn;
} JSAtExitEntry;

thread_local Vector js_misc_atexit_functions;
thread_local BOOL js_misc_atexit_called = FALSE;

static void
js_misc_atexit_handler() {
  JSAtExitEntry* entry;

  if(js_misc_atexit_called)
    return;
  js_misc_atexit_called = TRUE;

  vector_foreach_t(&js_misc_atexit_functions, entry) {
    JSValue ret = JS_Call(entry->ctx, entry->fn, JS_UNDEFINED, 0, 0);
    JS_FreeValue(entry->ctx, ret);
  }
}

static JSValue
js_misc_atexit(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSAtExitEntry entry;

  if(argc < 1 || !JS_IsFunction(ctx, argv[0]))
    return JS_ThrowTypeError(ctx, "argument 1 must be function");

  entry.ctx = ctx;
  entry.fn = JS_DupValue(ctx, argv[0]);

  vector_push(&js_misc_atexit_functions, entry);
  return JS_UNDEFINED;
}

#ifdef HAVE_LINK
static JSValue
js_misc_link(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {

  const char *from, *to;

  if(!(from = JS_ToCString(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a string");

  if(!(to = JS_ToCString(ctx, argv[1])))
    return JS_ThrowTypeError(ctx, "argument 2 must be a string");

  return JS_NewInt32(ctx, link(from, to));
}
#endif

static JSValue
js_misc_unlink(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {

  const char* file;

  if(!(file = JS_ToCString(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a string");

  return JS_NewInt32(ctx, unlink(file));
}

#ifdef HAVE_ACCESS
static JSValue
js_misc_access(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t mode = -1;
  int ret;

  const char* pathname = JS_ToCString(ctx, argv[0]);

  JS_ToInt32(ctx, &mode, argv[1]);

  ret = access(pathname, mode);

  return JS_NewInt32(ctx, ret);
}
#endif

#ifdef HAVE_FCNTL
static JSValue
js_misc_fcntl(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t fd = -1, cmd = -1, arg = -1;
  int ret;

  JS_ToInt32(ctx, &fd, argv[0]);
  JS_ToInt32(ctx, &cmd, argv[1]);
  if(argc > 2)
    JS_ToInt32(ctx, &arg, argv[2]);

  ret = fcntl(fd, cmd, arg);

  return JS_NewInt32(ctx, ret);
}
#endif

#if !defined(_WIN32)
static int64_t
timespec_to_ms(const struct timespec* tv) {
  return (int64_t)tv->tv_sec * 1000 + (tv->tv_nsec / 1000000);
}
#endif

#ifdef HAVE_FSTAT
static JSValue
js_misc_fstat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t fd = -1;
  int res, err = 0;
  JSValue ret = JS_NewArray(ctx), obj = JS_NULL;
  struct stat st;

  JS_ToInt32(ctx, &fd, argv[0]);

  res = fstat(fd, &st);

  if(res < 0) {
    err = errno;

  } else {
    obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, obj, "dev", JS_NewInt64(ctx, st.st_dev));
    JS_SetPropertyStr(ctx, obj, "ino", JS_NewInt64(ctx, st.st_ino));
    JS_SetPropertyStr(ctx, obj, "mode", JS_NewInt32(ctx, st.st_mode));
    JS_SetPropertyStr(ctx, obj, "nlink", JS_NewInt64(ctx, st.st_nlink));
    JS_SetPropertyStr(ctx, obj, "uid", JS_NewInt64(ctx, st.st_uid));
    JS_SetPropertyStr(ctx, obj, "gid", JS_NewInt64(ctx, st.st_gid));
    JS_SetPropertyStr(ctx, obj, "rdev", JS_NewInt64(ctx, st.st_rdev));
    JS_SetPropertyStr(ctx, obj, "size", JS_NewInt64(ctx, st.st_size));
#if !defined(_WIN32)
    JS_SetPropertyStr(ctx, obj, "blocks", JS_NewInt64(ctx, st.st_blocks));
#endif
#if defined(_WIN32)
    JS_SetPropertyStr(ctx, obj, "atime", JS_NewInt64(ctx, (int64_t)st.st_atime * 1000));
    JS_SetPropertyStr(ctx, obj, "mtime", JS_NewInt64(ctx, (int64_t)st.st_mtime * 1000));
    JS_SetPropertyStr(ctx, obj, "ctime", JS_NewInt64(ctx, (int64_t)st.st_ctime * 1000));
#elif defined(__APPLE__)
    JS_SetPropertyStr(ctx, obj, "atime", JS_NewInt64(ctx, timespec_to_ms(&st.st_atimespec)));
    JS_SetPropertyStr(ctx, obj, "mtime", JS_NewInt64(ctx, timespec_to_ms(&st.st_mtimespec)));
    JS_SetPropertyStr(ctx, obj, "ctime", JS_NewInt64(ctx, timespec_to_ms(&st.st_ctimespec)));
#elif defined(__dietlibc__) || defined(__ANDROID__)
    JS_SetPropertyStr(ctx, obj, "atime", JS_NewInt64(ctx, 1000 * st.st_atime));
    JS_SetPropertyStr(ctx, obj, "mtime", JS_NewInt64(ctx, 1000 * st.st_mtime));
    JS_SetPropertyStr(ctx, obj, "ctime", JS_NewInt64(ctx, 1000 * st.st_ctime));
#else
    JS_SetPropertyStr(ctx, obj, "atime", JS_NewInt64(ctx, timespec_to_ms(&st.st_atim)));
    JS_SetPropertyStr(ctx, obj, "mtime", JS_NewInt64(ctx, timespec_to_ms(&st.st_mtim)));
    JS_SetPropertyStr(ctx, obj, "ctime", JS_NewInt64(ctx, timespec_to_ms(&st.st_ctim)));
#endif
  }

  JS_SetPropertyUint32(ctx, ret, 0, obj);
  JS_SetPropertyUint32(ctx, ret, 1, JS_NewInt32(ctx, err));

  return ret;
}
#endif

enum {
  FUNC_GET_OSFHANDLE,
  FUNC_OPEN_OSFHANDLE,
};

static JSValue
js_misc_osfhandle(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;

#ifdef _WIN32
  switch(magic) {
    case FUNC_GET_OSFHANDLE: {
      int32_t fd = -1;
      JS_ToInt32(ctx, &fd, argv[0]);
      ret = JS_NewInt64(ctx, _get_osfhandle(fd));
      break;
    }
    case FUNC_OPEN_OSFHANDLE: {
      int64_t hnd = -1;
      int32_t flags = 0;
      JS_ToInt64(ctx, &hnd, argv[0]);
      if(argc > 1)
        JS_ToInt32(ctx, &flags, argv[1]);

      ret = JS_NewInt32(ctx, _open_osfhandle((intptr_t)hnd, flags));
      break;
    }
  }
#else
  ret = JS_DupValue(ctx, argv[0]);
#endif

  return ret;
}

static const JSCFunctionListEntry js_misc_funcs[] = {
    JS_CFUNC_DEF("getRelease", 0, js_misc_getrelease),
#ifndef __wasi__
// JS_CFUNC_DEF("realpath", 1, js_misc_realpath),
#ifdef USE_TEMPNAM
    JS_CFUNC_DEF("tempnam", 0, js_misc_tempnam),
#endif
    JS_CFUNC_DEF("mkstemp", 1, js_misc_mkstemp),
#endif
    //#ifdef HAVE_FNMATCH
    JS_CFUNC_DEF("fnmatch", 3, js_misc_fnmatch),
//#endif
#ifdef HAVE_GLOB
    JS_CFUNC_DEF("glob", 2, js_misc_glob),
#endif
#ifdef HAVE_WORDEXP
    JS_CFUNC_DEF("wordexp", 2, js_misc_wordexp),
#endif
#ifdef HAVE_INOTIFY_INIT1
    JS_CFUNC_DEF("watch", 1, js_misc_watch),
#endif
#ifdef HAVE_DAEMON
    JS_CFUNC_DEF("daemon", 2, js_misc_daemon),
#endif
#ifdef HAVE_FORK
    JS_CFUNC_DEF("fork", 0, js_misc_fork),
#endif
#ifdef HAVE_VFORK
    JS_CFUNC_DEF("vfork", 0, js_misc_vfork),
#endif
#ifdef HAVE_EXECVE
    JS_CFUNC_DEF("exec", 2, js_misc_exec),
#endif
    JS_CFUNC_DEF("kill", 1, js_misc_kill),
#ifdef HAVE_SETSID
    JS_CFUNC_DEF("setsid", 0, js_misc_setsid),
#endif
    JS_CFUNC_DEF("unlink", 1, js_misc_unlink),
#ifdef HAVE_LINK
    JS_CFUNC_DEF("link", 2, js_misc_link),
#endif
#ifdef HAVE_ACCESS
    JS_CFUNC_DEF("access", 2, js_misc_access),
    JS_CONSTANT(F_OK),
    JS_CONSTANT(R_OK),
    JS_CONSTANT(W_OK),
    JS_CONSTANT(X_OK),

#endif
#ifdef HAVE_FCNTL
    JS_CFUNC_DEF("fcntl", 2, js_misc_fcntl),
    JS_CONSTANT(FD_CLOEXEC),
    JS_CONSTANT(F_DUPFD),
    JS_CONSTANT(F_DUPFD_CLOEXEC),
    JS_CONSTANT(F_GETFD),
    JS_CONSTANT(F_GETFL),
    JS_CONSTANT(F_GETLK),
    JS_CONSTANT(F_RDLCK),
    JS_CONSTANT(F_SETFD),
    JS_CONSTANT(F_SETFL),
    JS_CONSTANT(F_SETLK),
    JS_CONSTANT(F_SETLKW),
    JS_CONSTANT(F_UNLCK),
    JS_CONSTANT(F_WRLCK),
    JS_CONSTANT(O_APPEND),
    JS_CONSTANT(O_CLOEXEC),
    JS_CONSTANT(O_CREAT),
    JS_CONSTANT(O_DIRECT),
    JS_CONSTANT(O_DSYNC),
    JS_CONSTANT(O_EXCL),
    JS_CONSTANT(O_NOATIME),
    JS_CONSTANT(O_NOCTTY),
    JS_CONSTANT(O_NONBLOCK),
    JS_CONSTANT(O_RDONLY),
    JS_CONSTANT(O_RDWR),
    JS_CONSTANT(O_SYNC),
    JS_CONSTANT(O_TRUNC),
    JS_CONSTANT(O_WRONLY),
#endif
#ifdef HAVE_FSTAT
    JS_CFUNC_DEF("fstat", 1, js_misc_fstat),
#endif
    JS_CFUNC_MAGIC_DEF("_get_osfhandle", 1, js_misc_osfhandle, FUNC_GET_OSFHANDLE),
    JS_CFUNC_MAGIC_DEF("_open_osfhandle", 1, js_misc_osfhandle, FUNC_OPEN_OSFHANDLE),
    JS_CFUNC_DEF("toString", 1, js_misc_tostring),
    JS_CFUNC_DEF("strcmp", 2, js_misc_strcmp),
    JS_CFUNC_DEF("toPointer", 1, js_misc_topointer),
    JS_CFUNC_DEF("toArrayBuffer", 1, js_misc_toarraybuffer),
    JS_CFUNC_DEF("dupArrayBuffer", 1, js_misc_duparraybuffer),
    JS_CFUNC_DEF("sliceArrayBuffer", 1, js_misc_slice),
    JS_CFUNC_DEF("resizeArrayBuffer", 1, js_misc_resizearraybuffer),
    JS_CFUNC_DEF("concat", 1, js_misc_concat),
    JS_CFUNC_DEF("searchArrayBuffer", 2, js_misc_searcharraybuffer),
    // JS_ALIAS_DEF("search", "searchArrayBuffer"),
    JS_CFUNC_DEF("memcpy", 2, js_misc_memcpy),
#ifdef HAVE_FMEMOPEN
    JS_CFUNC_DEF("fmemopen", 2, js_misc_fmemopen),
#endif
    JS_CFUNC_DEF("getPerformanceCounter", 0, js_misc_getperformancecounter),
    JS_CFUNC_MAGIC_DEF("getExecutable", 0, js_misc_proclink, FUNC_GETEXECUTABLE),
    JS_CFUNC_MAGIC_DEF("getCurrentWorkingDirectory", 0, js_misc_proclink, FUNC_GETCWD),
    JS_CFUNC_MAGIC_DEF("getRootDirectory", 0, js_misc_proclink, FUNC_GETROOT),
    JS_CFUNC_MAGIC_DEF("getFileDescriptor", 0, js_misc_proclink, FUNC_GETFD),
    JS_CFUNC_MAGIC_DEF("getCommandLine", 0, js_misc_procread, FUNC_GETCOMMANDLINE),
    JS_CFUNC_MAGIC_DEF("getProcMaps", 0, js_misc_procread, FUNC_GETPROCMAPS),
    JS_CFUNC_MAGIC_DEF("getProcMounts", 0, js_misc_procread, FUNC_GETPROCMOUNTS),
    JS_CFUNC_MAGIC_DEF("getProcStat", 0, js_misc_procread, FUNC_GETPROCSTAT),
    JS_CFUNC_DEF("getPrototypeChain", 0, js_misc_getprototypechain),
#ifdef HAVE_GETTID
    JS_CFUNC_MAGIC_DEF("gettid", 0, js_misc_getx, FUNC_GETTID),
#endif
#ifndef __wasi__
    JS_CFUNC_MAGIC_DEF("getpid", 0, js_misc_getx, FUNC_GETPID),
    JS_CFUNC_MAGIC_DEF("getppid", 0, js_misc_getx, FUNC_GETPPID),
#endif
    JS_CFUNC_MAGIC_DEF("getsid", 0, js_misc_getx, FUNC_GETSID),
#if !defined(__wasi__) && !defined(_WIN32)
    JS_CFUNC_MAGIC_DEF("getuid", 0, js_misc_getx, FUNC_GETUID),
    JS_CFUNC_MAGIC_DEF("getgid", 0, js_misc_getx, FUNC_GETGID),
    JS_CFUNC_MAGIC_DEF("geteuid", 0, js_misc_getx, FUNC_GETEUID),
    JS_CFUNC_MAGIC_DEF("getegid", 0, js_misc_getx, FUNC_GETEGID),
    JS_CFUNC_MAGIC_DEF("setuid", 1, js_misc_getx, FUNC_SETUID),
    JS_CFUNC_MAGIC_DEF("setgid", 1, js_misc_getx, FUNC_SETGID),
#endif
    JS_CFUNC_MAGIC_DEF("seteuid", 1, js_misc_getx, FUNC_SETEUID),
    JS_CFUNC_MAGIC_DEF("setegid", 1, js_misc_getx, FUNC_SETEGID),
    JS_CFUNC_DEF("hrtime", 0, js_misc_hrtime),
    JS_CFUNC_DEF("uname", 0, js_misc_uname),
#ifdef HAVE_IOCTL_H
    JS_CFUNC_DEF("ioctl", 3, js_misc_ioctl),
#endif
#if defined(HAVE_TERMIOS_H) || defined(_WIN32)
    JS_CFUNC_DEF("getScreenSize", 0, js_misc_screensize),
#endif
    JS_CFUNC_MAGIC_DEF("clearScreen", 1, js_misc_clearscreen, ERASE_IN_DISPLAY),
    JS_CFUNC_MAGIC_DEF("clearLine", 1, js_misc_clearscreen, ERASE_IN_LINE),
    JS_CFUNC_MAGIC_DEF("setCursorPosition", 1, js_misc_cursorposition, SET_CURSOR_POSITION),
    JS_CFUNC_MAGIC_DEF("moveCursor", 1, js_misc_cursorposition, MOVE_CURSOR),
    JS_CFUNC_MAGIC_DEF("setTextAttribute", 2, js_misc_settextattr, SET_TEXT_ATTRIBUTES),
    JS_CFUNC_MAGIC_DEF("setTextColor", 2, js_misc_settextattr, SET_TEXT_COLOR),
#if defined(_WIN32)
    JS_CFUNC_MAGIC_DEF("setConsoleMode", 2, js_misc_consolemode, SET_CONSOLE_MODE),
    JS_CFUNC_MAGIC_DEF("getConsoleMode", 1, js_misc_consolemode, GET_CONSOLE_MODE),
#endif
    JS_CFUNC_DEF("btoa", 1, js_misc_btoa),
    JS_CFUNC_DEF("atob", 1, js_misc_atob),
    JS_CFUNC_MAGIC_DEF("not", 1, js_misc_bitop, BITOP_NOT),
    JS_CFUNC_MAGIC_DEF("xor", 2, js_misc_bitop, BITOP_XOR),
    JS_CFUNC_MAGIC_DEF("and", 2, js_misc_bitop, BITOP_AND),
    JS_CFUNC_MAGIC_DEF("or", 2, js_misc_bitop, BITOP_OR),
    JS_CFUNC_MAGIC_DEF("bitfieldSet", 1, js_misc_bitfield, BITFIELD_SET),
    JS_CFUNC_MAGIC_DEF("bits", 1, js_misc_bitfield, BITFIELD_BITS),
    JS_CFUNC_MAGIC_DEF("bitfieldToArray", 1, js_misc_bitfield, BITFIELD_TOARRAY),
    JS_CFUNC_MAGIC_DEF("arrayToBitfield", 1, js_misc_bitfield, BITFIELD_FROMARRAY),
    JS_CFUNC_MAGIC_DEF("compileScript", 1, js_misc_compile, 0),
    JS_CFUNC_MAGIC_DEF("evalScript", 1, js_misc_compile, 1),
    JS_CFUNC_MAGIC_DEF("immutableClass", 1, js_misc_immutable_class, 1),
    JS_CFUNC_DEF("writeObject", 1, js_misc_write_object),
    JS_CFUNC_DEF("readObject", 1, js_misc_read_object),
    JS_CFUNC_DEF("getOpCodes", 0, js_misc_opcodes),
    JS_CFUNC_DEF("getByteCode", 1, js_misc_get_bytecode),
    JS_CFUNC_MAGIC_DEF("valueType", 1, js_misc_valuetype, VALUE_TYPE),
    JS_CFUNC_MAGIC_DEF("valueTag", 1, js_misc_valuetype, VALUE_TAG),
    JS_CFUNC_MAGIC_DEF("valuePointer", 1, js_misc_valuetype, VALUE_POINTER),
    JS_CFUNC_MAGIC_DEF("stringPointer", 1, js_misc_valuetype, STRING_POINTER),
    JS_CFUNC_MAGIC_DEF("stringLength", 1, js_misc_valuetype, STRING_LENGTH),
    JS_CFUNC_MAGIC_DEF("stringBuffer", 1, js_misc_valuetype, STRING_BUFFER),
    JS_CFUNC_DEF("getOpaque", 1, js_misc_getopaque),
    JS_CFUNC_DEF("evalString", 1, js_misc_evalstring),
    JS_CFUNC_DEF("evalBinary", 1, js_misc_evalbinary),
    JS_CFUNC_MAGIC_DEF("atomToString", 1, js_misc_atom, ATOM_TO_STRING),
    JS_CFUNC_MAGIC_DEF("atomToValue", 1, js_misc_atom, ATOM_TO_VALUE),
    JS_CFUNC_MAGIC_DEF("valueToAtom", 1, js_misc_atom, VALUE_TO_ATOM),
    JS_CFUNC_MAGIC_DEF("getClassID", 1, js_misc_classid, GET_CLASS_ID),
    JS_CFUNC_MAGIC_DEF("getClassName", 1, js_misc_classid, GET_CLASS_NAME),
    JS_CFUNC_MAGIC_DEF("getClassAtom", 1, js_misc_classid, GET_CLASS_ATOM),
    JS_CFUNC_MAGIC_DEF("getClassCount", 1, js_misc_classid, GET_CLASS_COUNT),
    JS_CFUNC_MAGIC_DEF("getClassProto", 1, js_misc_classid, GET_CLASS_PROTO),
    JS_CFUNC_MAGIC_DEF("getClassConstructor", 1, js_misc_classid, GET_CLASS_CONSTRUCTOR),
    JS_CFUNC_MAGIC_DEF("getTypeId", 1, js_misc_type, GET_TYPE_ID),
    JS_CFUNC_MAGIC_DEF("getTypeStr", 1, js_misc_type, GET_TYPE_STR),
    JS_CFUNC_MAGIC_DEF("getTypeName", 1, js_misc_type, GET_TYPE_NAME),
    JS_CFUNC_MAGIC_DEF("rand", 0, js_misc_random, RANDOM_RAND),
    JS_CFUNC_MAGIC_DEF("randi", 0, js_misc_random, RANDOM_RANDI),
    JS_CFUNC_MAGIC_DEF("randf", 0, js_misc_random, RANDOM_RANDF),
    JS_CFUNC_MAGIC_DEF("srand", 1, js_misc_random, RANDOM_SRAND),
    JS_CFUNC_DEF("escape", 1, js_misc_escape),
    JS_CFUNC_DEF("unescape", 1, js_misc_unescape),
    JS_CFUNC_DEF("quote", 1, js_misc_quote),
    JS_CFUNC_DEF("error", 0, js_misc_error),
    JS_CFUNC_DEF("atexit", 1, js_misc_atexit),
    JS_CFUNC_MAGIC_DEF("isArray", 1, js_misc_is, IS_ARRAY),
    JS_CFUNC_MAGIC_DEF("isBigDecimal", 1, js_misc_is, IS_BIGDECIMAL),
    JS_CFUNC_MAGIC_DEF("isBigFloat", 1, js_misc_is, IS_BIGFLOAT),
    JS_CFUNC_MAGIC_DEF("isBigInt", 1, js_misc_is, IS_BIGINT),
    JS_CFUNC_MAGIC_DEF("isBool", 1, js_misc_is, IS_BOOL),
    JS_CFUNC_MAGIC_DEF("isCFunction", 1, js_misc_is, IS_CFUNCTION),
    JS_CFUNC_MAGIC_DEF("isBoundFunction", 1, js_misc_is, IS_BOUNDFUNCTION),
    JS_CFUNC_MAGIC_DEF("isJSFunction", 1, js_misc_is, IS_JSFUNCTION),
    JS_CFUNC_MAGIC_DEF("isConstructor", 1, js_misc_is, IS_CONSTRUCTOR),
    JS_CFUNC_MAGIC_DEF("isEmptyString", 1, js_misc_is, IS_EMPTYSTRING),
    JS_CFUNC_MAGIC_DEF("isError", 1, js_misc_is, IS_ERROR),
    JS_CFUNC_MAGIC_DEF("isException", 1, js_misc_is, IS_EXCEPTION),
    JS_CFUNC_MAGIC_DEF("isExtensible", 1, js_misc_is, IS_EXTENSIBLE),
    JS_CFUNC_MAGIC_DEF("isFunction", 1, js_misc_is, IS_FUNCTION),
    JS_CFUNC_MAGIC_DEF("isHTMLDDA", 1, js_misc_is, IS_HTMLDDA),
    JS_CFUNC_MAGIC_DEF("isInstanceOf", 1, js_misc_is, IS_INSTANCEOF),
    JS_CFUNC_MAGIC_DEF("isInteger", 1, js_misc_is, IS_INTEGER),
    JS_CFUNC_MAGIC_DEF("isJobPending", 1, js_misc_is, IS_JOBPENDING),
    JS_CFUNC_MAGIC_DEF("isLiveObject", 1, js_misc_is, IS_LIVEOBJECT),
    JS_CFUNC_MAGIC_DEF("isNull", 1, js_misc_is, IS_NULL),
    JS_CFUNC_MAGIC_DEF("isNumber", 1, js_misc_is, IS_NUMBER),
    JS_CFUNC_MAGIC_DEF("isObject", 1, js_misc_is, IS_OBJECT),
    JS_CFUNC_MAGIC_DEF("isRegisteredClass", 1, js_misc_is, IS_REGISTEREDCLASS),
    JS_CFUNC_MAGIC_DEF("isString", 1, js_misc_is, IS_STRING),
    JS_CFUNC_MAGIC_DEF("isSymbol", 1, js_misc_is, IS_SYMBOL),
    JS_CFUNC_MAGIC_DEF("isUncatchableError", 1, js_misc_is, IS_UNCATCHABLEERROR),
    JS_CFUNC_MAGIC_DEF("isUndefined", 1, js_misc_is, IS_UNDEFINED),
    JS_CFUNC_MAGIC_DEF("isUninitialized", 1, js_misc_is, IS_UNINITIALIZED),
    JS_CFUNC_MAGIC_DEF("isArrayBuffer", 1, js_misc_is, IS_ARRAYBUFFER),

    JS_CONSTANT(JS_EVAL_TYPE_GLOBAL),
    JS_CONSTANT(JS_EVAL_TYPE_MODULE),
    JS_CONSTANT(JS_EVAL_TYPE_DIRECT),
    JS_CONSTANT(JS_EVAL_TYPE_INDIRECT),
    JS_CONSTANT(JS_EVAL_TYPE_MASK),
    JS_CONSTANT(JS_EVAL_FLAG_STRICT),
    JS_CONSTANT(JS_EVAL_FLAG_STRIP),
    JS_CONSTANT(JS_EVAL_FLAG_COMPILE_ONLY),
    JS_CONSTANT(JS_EVAL_FLAG_BACKTRACE_BARRIER),
#ifdef HAVE_FNMATCH
    JS_CONSTANT(FNM_CASEFOLD),
#ifdef FNM_EXTMATCH
    JS_CONSTANT(FNM_EXTMATCH),
#endif
    JS_CONSTANT(FNM_FILE_NAME),
    JS_CONSTANT(FNM_LEADING_DIR),
    JS_CONSTANT(FNM_NOESCAPE),
    JS_CONSTANT(FNM_NOMATCH),
    JS_CONSTANT(FNM_PATHNAME),
    JS_CONSTANT(FNM_PERIOD),
#endif
#ifdef HAVE_GLOB
    JS_CONSTANT(GLOB_ERR),
    JS_CONSTANT(GLOB_MARK),
    JS_CONSTANT(GLOB_NOSORT),
    JS_CONSTANT(GLOB_NOCHECK),
    JS_CONSTANT(GLOB_NOMATCH),
    JS_CONSTANT(GLOB_NOESCAPE),
    // JS_CONSTANT(GLOB_PERIOD),
    JS_CONSTANT(GLOB_ALTDIRFUNC),
    JS_CONSTANT(GLOB_BRACE),
    JS_CONSTANT(GLOB_NOMAGIC),
    JS_CONSTANT(GLOB_TILDE),
    // JS_CONSTANT(GLOB_TILDE_CHECK),
    // JS_CONSTANT(GLOB_ONLYDIR),
    JS_CONSTANT(GLOB_MAGCHAR),
    JS_CONSTANT(GLOB_NOSPACE),
    JS_CONSTANT(GLOB_ABORTED),
#endif
#ifdef HAVE_WORDEXP
    JS_CONSTANT(WRDE_SHOWERR),
    JS_CONSTANT(WRDE_UNDEF),
    JS_CONSTANT(WRDE_BADCHAR),
    JS_CONSTANT(WRDE_BADVAL),
    JS_CONSTANT(WRDE_CMDSUB),
    JS_CONSTANT(WRDE_NOCMD),
    JS_CONSTANT(WRDE_NOSPACE),
    JS_CONSTANT(WRDE_SYNTAX),
#endif
#ifdef HAVE_INOTIFY_INIT1
    JS_CONSTANT(IN_ACCESS),
    JS_CONSTANT(IN_MODIFY),
    JS_CONSTANT(IN_ATTRIB),
    JS_CONSTANT(IN_CLOSE_WRITE),
    JS_CONSTANT(IN_CLOSE_NOWRITE),
    JS_CONSTANT(IN_CLOSE),
    JS_CONSTANT(IN_OPEN),
    JS_CONSTANT(IN_MOVED_FROM),
    JS_CONSTANT(IN_MOVED_TO),
    JS_CONSTANT(IN_MOVE),
    JS_CONSTANT(IN_CREATE),
    JS_CONSTANT(IN_DELETE),
    JS_CONSTANT(IN_DELETE_SELF),
    JS_CONSTANT(IN_MOVE_SELF),
    JS_CONSTANT(IN_UNMOUNT),
    JS_CONSTANT(IN_Q_OVERFLOW),
    JS_CONSTANT(IN_IGNORED),
    JS_CONSTANT(IN_ONLYDIR),
    JS_CONSTANT(IN_DONT_FOLLOW),
    JS_CONSTANT(IN_EXCL_UNLINK),
    JS_CONSTANT(IN_MASK_ADD),
    JS_CONSTANT(IN_ISDIR),
    JS_CONSTANT(IN_ONESHOT),
    JS_CONSTANT(IN_ALL_EVENTS),
    JS_CONSTANT(IN_NONBLOCK),
    JS_CONSTANT(IN_CLOEXEC),
    JS_PROP_INT32_DEF("inotify_event_size", sizeof(struct inotify_event), 0),
#endif
#ifdef HAVE_TERMIOS_H
    JS_CONSTANT(TIOCSCTTY),
    JS_CONSTANT(TIOCGPGRP),
    JS_CONSTANT(TIOCSPGRP),
    JS_CONSTANT(TIOCGWINSZ),
    JS_CONSTANT(TIOCSWINSZ),
    JS_CONSTANT(SIGWINCH),
    JS_CONSTANT(TIOCMGET),
    JS_CONSTANT(TIOCMBIS),
    JS_CONSTANT(TIOCMBIC),
    JS_CONSTANT(TIOCMSET),
    JS_CONSTANT(TIOCINQ),
    JS_CONSTANT(TIOCLINUX),
    JS_CONSTANT(TIOCPKT),
    JS_CONSTANT(TIOCSBRK),
    JS_CONSTANT(TIOCCBRK),
#endif
    JS_CONSTANT(FOREGROUND_BLUE),
    JS_CONSTANT(FOREGROUND_GREEN),
    JS_CONSTANT(FOREGROUND_RED),
    JS_CONSTANT(FOREGROUND_INTENSITY),
    JS_CONSTANT(BACKGROUND_BLUE),
    JS_CONSTANT(BACKGROUND_GREEN),
    JS_CONSTANT(BACKGROUND_RED),
    JS_CONSTANT(BACKGROUND_INTENSITY),
    JS_CONSTANT(COMMON_LVB_REVERSE_VIDEO),
};

static int
js_misc_init(JSContext* ctx, JSModuleDef* m) {

  /* if(!js_location_class_id)
     js_location_init(ctx, 0);*/

  vector_init(&js_misc_atexit_functions, ctx);
  atexit(&js_misc_atexit_handler);

  if(m) {
    JS_SetModuleExportList(ctx, m, js_misc_funcs, countof(js_misc_funcs));
    // JS_SetModuleExport(ctx, m, "Location", location_ctor);
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

  if((m = JS_NewCModule(ctx, module_name, js_misc_init))) {
    JS_AddModuleExportList(ctx, m, js_misc_funcs, countof(js_misc_funcs));
  }

  return m;
}

/**
 * @}
 */
