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
#include "quickjs-location.h"
#include "quickjs-textcode.h"
#include "quickjs-syscallerror.h"
#include "utils.h"
#include "path.h"
#include "base64.h"
#include <time.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/utsname.h>
#endif
#include <errno.h>
#ifdef HAVE_FNMATCH_H
#include <fnmatch.h>
#endif

#if defined(HAVE_GLOB) && defined(HAVE_GLOB_H)
#include <glob.h>
#ifndef GLOB_APPEND
#define GLOB_APPEND 0x0001
#endif
#ifndef GLOB_DOOFFS
#define GLOB_DOOFFS 0x0002
#endif
#ifndef GLOB_ERR
#define GLOB_ERR 0x0004
#endif
#ifndef GLOB_MARK
#define GLOB_MARK 0x0008
#endif
#ifndef GLOB_NOCHECK
#define GLOB_NOCHECK 0x0010
#endif
#ifndef GLOB_NOSORT
#define GLOB_NOSORT 0x0020
#endif
#ifndef GLOB_ALTDIRFUNC
#define GLOB_ALTDIRFUNC 0x0040
#endif
#ifndef GLOB_BRACE
#define GLOB_BRACE 0x0080
#endif
#ifndef GLOB_MAGCHAR
#define GLOB_MAGCHAR 0x0100
#endif
#ifndef GLOB_NOMAGIC
#define GLOB_NOMAGIC 0x0200
#endif
#ifndef GLOB_QUOTE
#define GLOB_QUOTE 0x0400
#endif
#ifndef GLOB_TILDE
#define GLOB_TILDE 0x0800
#endif
#ifndef GLOB_NOESCAPE
#define GLOB_NOESCAPE 0x1000
#endif
#ifndef GLOB_NOSPACE
#define GLOB_NOSPACE (-1)
#endif
#ifndef GLOB_ABORTED
#define GLOB_ABORTED (-2)
#endif
#ifndef GLOB_NOMATCH
#define GLOB_NOMATCH (-3)
#endif
#ifndef GLOB_NOSYS
#define GLOB_NOSYS (-4)
#endif
#else
#define glob openbsd_glob
#define globfree openbsd_globfree
#include "glob.h"
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
#include "../libbcrypt/bcrypt.h"

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#if defined(HAVE_UTIME) || defined(HAVE_UTIMES) || defined(HAVE_FUTIMES) || defined(HAVE_LUTIMES)
#include <sys/types.h>
#include <utime.h>

#ifdef __ANDROID__
#if !(__ANDROID_API__ >= 26)
int futimes(int, const struct timeval[2]);
int lutimes(const char*, const struct timeval[2]);
#endif
#endif

#endif

#ifndef USE_TEMPNAM
#define USE_TEMPNAM
#endif

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

#define TextAttrColor(n) \
  (((n)&1) * FOREGROUND_RED + (((n) >> 1) & 1) * FOREGROUND_GREEN + (((n) >> 2) & 1) * FOREGROUND_BLUE + \
   (((n) >> 3) & 1) * FOREGROUND_INTENSITY)

#define ColorIsBG(c) ((c) >= 100 ? TRUE : (c) >= 90 ? FALSE : (c) >= 40 ? TRUE : FALSE)
#define ColorIsBold(c) ((c) >= 90)
#define ColorIndex(c) (((c) >= 30 ? (c) % 10 : (c)) & 7)
#define ColorRed(c) (ColorIndex(c) & 1)
#define ColorGreen(c) ((ColorIndex(c) >> 1) & 1)
#define ColorBlue(c) ((ColorIndex(c) >> 2) & 1)

#define ColorToBits(c) \
  ((ColorIsBG(c) << 4) | (ColorIsBold(c) << 3) | ColorBlue(c) | ColorGreen(c) << 1 | ColorRed(c) << 2)

BOOL JS_IsUncatchableError(JSContext* ctx, JSValueConst val);

/**
 * \addtogroup quickjs-misc
 * @{
 */

#ifndef HAVE_MEMMEM
void* memmem(const void*, size_t, const void*, size_t);
#endif

// static thread_local int inotify_fd = -1;

typedef struct pcg_state_setseq_64 {
  uint64_t state, inc;
} pcg32_random_t;

static thread_local pcg32_random_t pcg32_global = {0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL};

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
  uint32_t leftover, threshold;

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
clear_screen(intptr_t h, ClearMode mode, BOOL line) {
  COORD coords = {0, 0};
  DWORD w, n;
  CONSOLE_SCREEN_BUFFER_INFO sbi;

  if(!GetConsoleScreenBufferInfo((HANDLE)h, &sbi))
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

  if(!FillConsoleOutputCharacter((HANDLE)h, (TCHAR)' ', n, coords, &w))
    return FALSE;

  if(!GetConsoleScreenBufferInfo((HANDLE)h, &sbi))
    return FALSE;

  if(!FillConsoleOutputAttribute((HANDLE)h, sbi.wAttributes, n, coords, &w))
    return FALSE;

  // SetConsoleCursorPosition((HANDLE)h, coords);
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
set_cursor_position(intptr_t h, int x, int y) {
  COORD coords = {0, 0};
  CONSOLE_SCREEN_BUFFER_INFO sbi;

  if(!GetConsoleScreenBufferInfo((HANDLE)h, &sbi))
    return FALSE;

  coords.X = x == -1 ? sbi.dwCursorPosition.X : x;
  coords.Y = y == -1 ? sbi.dwCursorPosition.Y : y;

  return !!SetConsoleCursorPosition((HANDLE)h, coords);
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
move_cursor(intptr_t h, int x, int y) {
  COORD coords = {0, 0};
  CONSOLE_SCREEN_BUFFER_INFO sbi;

  if(!GetConsoleScreenBufferInfo((HANDLE)h, &sbi))
    return FALSE;

  coords.X = sbi.dwCursorPosition.X + x;
  coords.Y = sbi.dwCursorPosition.Y + y;

  return !!SetConsoleCursorPosition((HANDLE)h, coords);
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
set_text_attributes(intptr_t h, uint32_t attr) {
  return !!SetConsoleTextAttribute((HANDLE)h, attr);
}

static BOOL
get_text_attributes(intptr_t h, uint32_t* attr) {
  CONSOLE_SCREEN_BUFFER_INFO sbi;

  if(GetConsoleScreenBufferInfo((HANDLE)h, &sbi)) {
    *attr = sbi.wAttributes;
    return TRUE;
  }

  return FALSE;
}

#else
static BOOL
set_text_color(intptr_t fd, int intc, int32_t intv[]) {
  ssize_t r;
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
  r = dbuf.size > 0 ? write(fd, dbuf.buf, dbuf.size) : 0;

#ifdef DEBUG_OUTPUT
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
  int fg, bg;
  char buf[(2 + (FMT_ULONG) + 1) * 3];
  size_t pos = 0;

  buf[pos++] = 27;
  buf[pos++] = '[';

  fg = ((attr & FOREGROUND_RED) ? 1 : 0) + ((attr & FOREGROUND_GREEN) ? 2 : 0) + ((attr & FOREGROUND_BLUE) ? 4 : 0) +
       ((attr & FOREGROUND_INTENSITY) ? 90 : 30);

  pos += fmt_ulong(&buf[pos], fg);
  buf[pos++] = ';';

  bg = ((attr & BACKGROUND_RED) ? 1 : 0) + ((attr & BACKGROUND_GREEN) ? 2 : 0) + ((attr & BACKGROUND_BLUE) ? 4 : 0) +
       ((attr & BACKGROUND_INTENSITY) ? 100 : 40);

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

  if(!JS_ToInt64Ext(ctx, &i64, value))
    return (void*)(ptrdiff_t)i64;

  return 0;
}

static JSValue
js_misc_getrelease(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, ret, "name", JS_NewString(ctx, "quickjs"));
  JS_SetPropertyStr(ctx,
                    ret,
                    "sourceUrl",
                    JS_NewString(ctx, "https://bellard.org/quickjs/quickjs-" CONFIG_VERSION ".tar.xz"));

  return ret;
}

static JSValue
js_misc_charlen(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer input = js_input_args(ctx, argc, argv);
  size_t size = input_buffer_length(&input);
  const uint8_t* data = input_buffer_data(&input);
  int64_t len = 0;

  if(size)
    len = utf8_charlen((const char*)data, size);

  input_buffer_free(&input, ctx);

  return JS_NewInt64(ctx, len);
}

static JSValue
js_misc_charcode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer input = js_input_args(ctx, argc, argv);
  size_t size = input_buffer_length(&input);
  const uint8_t* data = input_buffer_data(&input);
  int32_t code = -1;

  if(size)
    code = utf8_charcode((const char*)data, size);

  input_buffer_free(&input, ctx);

  return JS_NewInt32(ctx, code);
}

static JSValue
js_misc_u8dec(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer input = js_input_args(ctx, argc, argv);
  size_t size = input_buffer_length(&input);
  const uint8_t* data = input_buffer_data(&input);
  int32_t code = -1;
  int64_t len = 0;

  if(size) {
    code = utf8_charcode((const char*)data, size);
    len = utf8_charlen((const char*)data, size);
  }

  input_buffer_free(&input, ctx);

  if(code == -1)
    return JS_NULL;

  JSValue ret = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, ret, 0, JS_NewInt32(ctx, code));
  JS_SetPropertyUint32(ctx, ret, 1, JS_NewInt64(ctx, len));

  return ret;
}

static JSValue
js_misc_u8enc(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer input = js_input_args(ctx, argc - 1, argv + 1);
  uint32_t code = 0;

  JS_ToUint32(ctx, &code, argv[0]);

  size_t size = input_buffer_length(&input);
  uint8_t* data = input_buffer_data(&input);

  int len = unicode_to_utf8(data, code);

  input_buffer_free(&input, ctx);

  return JS_NewInt32(ctx, len);
}

static JSValue
js_misc_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  if(js_is_arraybuffer(ctx, argv[0]) || js_is_sharedarraybuffer(ctx, argv[0])) {
    uint8_t* data;
    size_t len;

    if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
      OffsetLength ol = {0, -1};
      uint8_t* s;
      size_t n;

      js_offset_length(ctx, -1, argc - 1, argv + 1, &ol);

      s = offset_data(ol, data);
      n = offset_size(ol, len);

      if(ol.length < 0 && memchr(s, '\0', n))
        ret = JS_NewString(ctx, (const char*)s);
      else
        ret = JS_NewStringLen(ctx, (const char*)s, n);
    }
  } else {
    ret = js_value_tostring(ctx, "Object", argc > 0 ? argv[0] : this_val);
  }

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
  uint8_t* ptr;
  char str[64];
  InputBuffer buf = js_input_chars(ctx, argv[0]);

  if(JS_IsException(buf.value))
    return JS_EXCEPTION;

  if((ptr = input_buffer_data(&buf)))
    ret = JS_NewStringLen(ctx, str, snprintf(str, sizeof(str), "%p", ptr));

  input_buffer_free(&buf, ctx);
  return ret;
}

static JSValue
js_misc_toarraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  uintptr_t addr = 0, len = 0;
  BOOL is_bigint = JS_IsBigInt(ctx, argv[0]);

  if(argc >= 2 && (JS_IsNumber(argv[0]) || is_bigint)) {
    addr = (uintptr_t)js_topointer(ctx, argv[0]);

    if(addr == 0)
      return JS_NULL;

    len = (uintptr_t)js_topointer(ctx, argv[1]);

    if(len == 0)
      return JS_ThrowInternalError(ctx, "zero length given");

    return JS_NewArrayBuffer(ctx, (void*)(size_t)addr, len, 0, 0, 0);
  }

  InputBuffer input = js_input_chars(ctx, argv[0]);

  if(!input.block.size && !input.block.base)
    return JS_NULL;

  /*if(!input.size) {
    size_t len=0;
    const char* str = JS_ToCStringLen(ctx,&len, argv[0]);

    ret = JS_ThrowInternalError(ctx, "zero size: '%s' %lu", str, (unsigned long)len);

    JS_FreeCString(ctx, str);
    return ret;
  }*/

  if(!JS_IsException(input.value)) {
    OffsetLength o = OFFSET_INIT();
    js_offset_length(ctx, input.size, argc - 1, argv + 1, &o);
    MemoryBlock b = offset_block(o, input_buffer_block(&input));
    ret = js_arraybuffer_fromvalue(ctx, b.base, b.size, argv[0]);
  }

  return ret;
}

static JSValue
js_misc_slicearraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  uint8_t* data;
  size_t len;

  if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
    IndexRange ir = {0, INT64_MAX};

    js_index_range(ctx, len, argc - 1, argv + 1, &ir);

    return JS_NewArrayBuffer(ctx,
                             indexrange_data(ir, data, len),
                             indexrange_size(ir, len),
                             js_arraybuffer_free_object,
                             js_value_obj2(ctx, argv[0]),
                             js_is_sharedarraybuffer(ctx, argv[0]));
  }

  return JS_ThrowTypeError(ctx, "argument 1 must be an ArrayBuffer");
}

static JSValue
js_misc_duparraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  uint8_t* data;
  size_t len;

  if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
    OffsetLength ol = {0, -1};

    js_offset_length(ctx, len, argc - 1, argv + 1, &ol);

    return JS_NewArrayBuffer(ctx,
                             offset_data(ol, data),
                             offset_size(ol, len),
                             js_arraybuffer_free_object,
                             js_value_obj2(ctx, argv[0]),
                             js_is_sharedarraybuffer(ctx, argv[0]));
  }

  return JS_ThrowTypeError(ctx, "argument 1 must be an ArrayBuffer");
}

static JSValue
js_misc_concat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  size_t total_len = 0, pos = 0;
  uint8_t* buf;
  InputBuffer* buffers = js_mallocz(ctx, sizeof(InputBuffer) * argc);

  for(int i = 0; i < argc; i++) {
    buffers[i] = js_input_buffer(ctx, argv[i]);

    if(!buffers[i].data) {
      ret = JS_ThrowTypeError(ctx, "argument %d is not ArrayBuffer", i + 1);
      goto fail;
    }

    total_len += buffers[i].size;
  }

  buf = js_malloc(ctx, total_len);

  for(int i = 0; i < argc; i++) {
    memcpy(&buf[pos], buffers[i].data, buffers[i].size);
    pos += buffers[i].size;
  }

  ret = JS_NewArrayBuffer(ctx, buf, total_len, js_arraybuffer_free_pointer, 0, FALSE);

fail:
  for(int i = 0; i < argc; i++)
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
        printf("@(%lu + %lu); ", (unsigned long)i, (unsigned long)j);
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
  OffsetLength s_offs = {0, -1}, d_offs = {0, -1};
  size_t n;
  int i = 0;

  if(!block_arraybuffer(&dst, argv[0], ctx))
    return JS_ThrowTypeError(ctx, "argument 1 (dst) must be an ArrayBuffer");

  ++i;
  i += js_offset_length(ctx, dst.size, argc - i, argv + i, &s_offs);

  /*dst.base += s_offs.offset;
  dst.size -= s_offs.offset;
  dst.size = MIN_NUM(dst.size, s_offs.length);*/

  if(i == argc || !block_arraybuffer(&src, argv[i], ctx))
    return JS_ThrowTypeError(ctx, "argument %d (src) must be an ArrayBuffer", i + 1);

  ++i;
  i += js_offset_length(ctx, dst.size, argc - i, argv + i, &d_offs);

  /*src.base += d_offs.offset;
  src.size -= d_offs.offset;
  src.size = MIN_NUM(src.size, d_offs.length);*/

  if((n = MIN_NUM(offset_size(d_offs, block_length(&dst)), offset_size(s_offs, block_length(&src)))))
    memcpy(offset_data(d_offs, block_data(&dst)), offset_data(s_offs, block_data(&src)), n);

  return JS_NewInt64(ctx, n);
}

static JSValue
js_misc_memcmp(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MemoryBlock s1 = {0, 0}, s2 = {0, 0};
  OffsetLength o1 = {0, -1}, o2 = {0, -1};
  size_t n;
  int i = 0;

  if(!block_arraybuffer(&s1, argv[0], ctx))
    return JS_ThrowTypeError(ctx, "argument 1 (s1) must be an ArrayBuffer");

  ++i;
  i += js_offset_length(ctx, s1.size, argc - i, argv + i, &o1);

  if(i == argc || !block_arraybuffer(&s2, argv[i], ctx))
    return JS_ThrowTypeError(ctx, "argument %d (s2) must be an ArrayBuffer", i + 1);

  ++i;
  i += js_offset_length(ctx, s2.size, argc - i, argv + i, &o2);

  if((n = MIN_NUM(offset_size(o1, block_length(&s1)), offset_size(o2, block_length(&s2)))))
    return JS_NewInt32(ctx, memcmp(offset_data(o1, block_data(&s1)), offset_data(o2, block_data(&s2)), n));

  return JS_NULL;
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

  ++i;

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

  mode = JS_ToCString(ctx, argv[0]);
  FILE* f = fmemopen(ptr, len, mode);
  JS_FreeCString(ctx, mode);

  return f ? js_std_file(ctx, f) : JS_NULL;
}
#endif

static JSValue
js_misc_getperformancecounter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  return JS_NewFloat64(ctx, (double)ts.tv_sec * 1000 + ((double)ts.tv_nsec / 1e06));
}

enum {
  FUNC_GETEXECUTABLE,
  FUNC_GETCWD,
  FUNC_GETROOT,
  FUNC_GETFD,
};

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

  if((r = path_readlink2(path, &dbuf)) > 0)
    ret = dbuf_tostring_free(&dbuf, ctx);

  return ret;
}

enum {
  FUNC_GETCOMMANDLINE,
  FUNC_GETPROCMAPS,
  FUNC_GETPROCMOUNTS,
  FUNC_GETPROCSTAT,
};

static JSValue
js_misc_procread(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  DynBuf dbuf = {0};
  ssize_t size, n;
  const char* file;
  char sep = '\n';

  switch(magic) {
    case FUNC_GETCOMMANDLINE: {
      file = "/proc/self/cmdline";
      sep = '\0';
      break;
    }
    case FUNC_GETPROCMAPS: {
      file = "/proc/self/maps";
      sep = '\n';
      break;
    }
    case FUNC_GETPROCMOUNTS: {
      file = "/proc/self/mounts";
      sep = '\n';
      break;
    }
    case FUNC_GETPROCSTAT: {
      file = "/proc/self/stat";
      sep = ' ';
      break;
    }
  }

  js_dbuf_init(ctx, &dbuf);

  if((size = dbuf_load(&dbuf, file)) > 0) {
    while(size > 0 && dbuf.buf[size - 1] == '\n')
      size--;

    ret = JS_NewArray(ctx);

    for(size_t i = 0, j = 0; i < size; i += n + 1) {
      size_t len = n = byte_chr(&dbuf.buf[i], size - i, sep);

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

  for(proto = JS_DupValue(ctx, argv[0]); !JS_IsException(proto) && !JS_IsNull(proto) && JS_IsObject(proto);
      proto = JS_GetPrototype(ctx, proto)) {
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
  int start = 0, i;
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

#ifdef GLOB_APPEND
    if(flags & GLOB_APPEND)
      start = js_array_length(ctx, ret);
#endif

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

  struct glob_state* gs;
  gs = js_mallocz(ctx, sizeof(struct glob_state));
  *gs = (struct glob_state){.flags = GLOB_TILDE | GLOB_BRACE};

  result = my_glob(pattern, gs);

  JS_FreeValue(ctx, ret);
  ret = JS_NewArray(ctx);

  for(size_t i = 0; i < gs->paths.len; i++)
    JS_SetPropertyUint32(ctx, ret, i, JS_NewString(ctx, gs->paths.ptr[i]));

  if(array_arg || result) {
    JS_FreeValue(ctx, ret);
    ret = JS_NewInt32(ctx, result);
  }

  JS_FreeValue(ctx, js_misc_glob_errfunc_fn);
  js_misc_glob_errfunc_ctx = 0;
  JS_FreeCString(ctx, pattern);
  return ret;
}

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
#ifdef EMSCRIPTEN
                                 "emscripten"
#elif defined(__ANDROID__)
                                 "android"
#elif defined(__MINGW32__)
                                 "mingw"
#elif defined(__MSYS__)
                                 "msys"
#elif defined(__CYGWIN__)
                                 "cygwin"
#elif defined(__linux__)
                                 "linux"
#elif defined(_WIN64)
                                 "win64"
#elif defined(_WIN32)
                                 "win32"
#endif
                                 ));
  JS_SetPropertyStr(ctx,
                    ret,
                    "machine",
                    JS_NewString(ctx,
#ifdef __aarch64__
                                 "aarch64"
#elif defined(__arm__)
                                 "arm"
#elif defined(__x86_64__)
                                 "x86_64"
#elif defined(_X86_)
                                 "i686"
#elif defined(__wasm32__)
                                 "wasm32"
#elif defined(__wasm__)
                                 "wasm"
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

  return JS_NewBool(ctx, clear_screen(h, mode, magic == ERASE_IN_LINE));
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
        uint8_t bits, shift, mask;
        int32_t color = 0;

        JS_ToInt32(ctx, &color, argv[i]);

        bits = ColorToBits(color) & 0x0f;
        shift = ColorIsBG(color) ? 4 : 0;
        mask = ColorIsBG(color) ? 0xf0 : 0x0f;

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

      if(!SetConsoleMode((HANDLE)h, mode))
        ret = JS_Throw(ctx, js_syscallerror_new(ctx, "SetConsoleMode", GetLastError()));

      break;
    }
    case GET_CONSOLE_MODE: {
      DWORD mode = 0;

      if(!GetConsoleMode((HANDLE)h, &mode))
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

  if(argc > 1 && JS_ToBool(ctx, argv[1]))
    b64url_encode(input.data, input.size, encbuf);
  else
    b64_encode(input.data, input.size, encbuf);

  ret = JS_NewStringLen(ctx, (const char*)encbuf, byte_chr(encbuf, enclen, '\0'));
  js_free(ctx, encbuf);
  return ret;
}

static JSValue
js_misc_atob(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret;
  InputBuffer input = js_input_chars(ctx, argv[0]);
  size_t outlen, declen = b64_get_decoded_buffer_size(input.size);
  uint8_t* decbuf = js_malloc(ctx, declen);
  BOOL output_string = magic > 0;

  if(argc > 1 && JS_ToBool(ctx, argv[1]))
    outlen = b64url_decode(input.data, input.size, decbuf);
  else
    outlen = b64_decode(input.data, input.size, decbuf);

  ret = output_string ? JS_NewStringLen(ctx, (const char*)decbuf, outlen)
                      : JS_NewArrayBufferCopy(ctx, (const uint8_t*)decbuf, outlen);

  js_free(ctx, decbuf);
  return ret;
}

static JSValue
js_misc_bcrypt(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret;
  InputBuffer input = js_input_chars(ctx, argv[0]);

  if(!JS_IsString(argv[0])) {
    uint32_t wf = 12;

    JS_ToUint32(ctx, &wf, argv[0]);

    if(argc > 1) {
      InputBuffer salt = js_input_buffer(ctx, argv[1]);

      if(salt.size < BCRYPT_HASHSIZE)
        return JS_ThrowInternalError(ctx, "supplied buffer size (%lu) < %d", (unsigned long)salt.size, BCRYPT_HASHSIZE);

      ret = JS_NewInt32(ctx, bcrypt_gensalt(wf, (void*)salt.data));

      input_buffer_free(&salt, ctx);
    } else {
      char s[BCRYPT_HASHSIZE + 1];

      memset(s, 0, sizeof(s));

      ret = bcrypt_gensalt(wf, s) ? JS_NULL : JS_NewStringLen(ctx, s, strlen(s));
    }
  } else if(argc >= 3) {
    const char* pw = JS_ToCString(ctx, argv[0]);
    InputBuffer salt = js_input_chars(ctx, argv[1]);
    InputBuffer buf = js_input_buffer(ctx, argv[2]);
    char s[BCRYPT_HASHSIZE];

    memset(s, 0, sizeof(s));

    if(!salt.size)
      bcrypt_gensalt(12, s);
    else
      memcpy(s, salt.data, salt.size);

    input_buffer_free(&salt, ctx);

    if(buf.size < BCRYPT_HASHSIZE)
      return JS_ThrowInternalError(ctx, "supplied buffer size (%lu) < %d", (unsigned long)buf.size, BCRYPT_HASHSIZE);

    ret = JS_NewInt32(ctx, bcrypt_hashpw(pw, s, (char*)buf.data));

    input_buffer_free(&buf, ctx);
    JS_FreeCString(ctx, pw);
  } else {
    const char* pw = JS_ToCString(ctx, argv[0]);
    InputBuffer buf = js_input_chars(ctx, argv[1]);
    char x[BCRYPT_HASHSIZE];

    if(!buf.size)
      return JS_ThrowInternalError(ctx, "supplied buffer size 0");

    memset(x, 0, sizeof(x));
    memcpy(x, buf.data, buf.size);

    input_buffer_free(&buf, ctx);

    ret = JS_NewInt32(ctx, bcrypt_checkpw(pw, x));

    JS_FreeCString(ctx, pw);
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
js_misc_immutable_constructor(
    JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic, void* ptr) {
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

  if((bytecode = JS_WriteObject(ctx, &size, argv[0], JS_WRITE_OBJ_BYTECODE)))
    ret = JS_NewArrayBuffer(ctx, bytecode, size, js_arraybuffer_free_pointer, 0, FALSE);

  return ret;
}

static JSValue
js_misc_read_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer input = js_input_chars(ctx, argv[0]);

  return JS_ReadObject(ctx, input.data, input.size, JS_READ_OBJ_BYTECODE);
}

enum {
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
  FUNC_SETEGID,
};

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
#ifndef __ANDROID__
      int32_t uid;

      JS_ToInt32(ctx, &uid, argv[0]);
      ret = setuid(uid);
#else
      ret = -1;
#endif
      break;
    }
    case FUNC_SETGID: {
#ifndef __ANDROID__
      int32_t gid;

      JS_ToInt32(ctx, &gid, argv[0]);
      ret = setgid(gid);
#else
      ret = -1;
#endif
      break;
    }
    case FUNC_SETEUID: {
#ifndef __ANDROID__
      int32_t euid;

      JS_ToInt32(ctx, &euid, argv[0]);
      ret = seteuid(euid);
#else
      ret = -1;
#endif
      break;
    }
    case FUNC_SETEGID: {
#ifndef __ANDROID__
      int32_t egid;

      JS_ToInt32(ctx, &egid, argv[0]);
      ret = setegid(egid);
#else
      ret = -1;
#endif
      break;
    }
#endif
  }

  if(ret == -1)
    return JS_ThrowInternalError(ctx,
                                 "%s() failed: %s",
                                 ((const char* const[]){"getpid",
                                                        "getppid",
                                                        "getsid",
                                                        "getuid",
                                                        "getgid",
                                                        "geteuid",
                                                        "getegid",
                                                        "setuid",
                                                        "setgid",
                                                        "seteuid",
                                                        "setegid"})[magic - FUNC_GETPID],
                                 strerror(errno));

  return JS_NewInt32(ctx, ret);
}

enum {
  VALUE_TYPE = 0,
  VALUE_TAG,
  VALUE_POINTER,
  OBJECT_REFCOUNT,
  OBJECT_CLASSID,
  OBJECT_OPAQUE,
  CLASS_ATOM,
  CLASS_NAME,
  CLASS_ID,
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
      ret = js_newpointer(ctx, js_value_ptr(argv[0]));
      break;
    }
    case OBJECT_REFCOUNT: {
      ret = JS_NewInt32(ctx, js_object_refcount(argv[0]));
      break;
    }
    case OBJECT_CLASSID: {
      ret = JS_NewInt32(ctx, js_object_classid(argv[0]));
      break;
    }
    case OBJECT_OPAQUE: {
      ret = js_newpointer(ctx, js_object_opaque(argv[0]));
      break;
    }
    case CLASS_ATOM: {
      int32_t id = js_toint32(ctx, argv[0]);
      uint32_t count = js_class_count(JS_GetRuntime(ctx));

      if(id < 0 || id >= count)
        ret = JS_ThrowRangeError(ctx, "id %d out of range (max: %u)", (int)id, (unsigned)count);
      else if(js_class_id(ctx, id))
        ret = JS_NewInt32(ctx, js_class_atom(ctx, id));

      break;
    }
    case CLASS_NAME: {
      int32_t id = js_toint32(ctx, argv[0]);
      ret = js_class_value(ctx, id);
      break;
    }
    case CLASS_ID: {
      JSAtom name = JS_IsNumber(argv[0]) ? js_toint32(ctx, argv[0]) : JS_ValueToAtom(ctx, argv[0]);

      int32_t id = js_class_find(ctx, name);
      ret = id == -1 ? JS_UNDEFINED : JS_NewInt32(ctx, id);

      if(!JS_IsNumber(argv[0]))
        JS_FreeAtom(ctx, name);
      break;
    }
    case STRING_POINTER: {
      if(JS_IsString(argv[0])) {
        ret = js_newpointer(ctx, js_cstring_ptr(argv[0]));
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
      if(JS_IsString(argv[0]))
        ret = js_arraybuffer_fromstring(ctx, argv[0]);

      break;
    }
  }

  return ret;
}

static JSValue
js_misc_evalbinary(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  BOOL load_only = FALSE;
  JSValue obj;
  int tag = JS_VALUE_GET_TAG(argv[0]);

  if(argc >= 2)
    load_only = JS_ToBool(ctx, argv[1]);

  if(tag != JS_TAG_MODULE && tag != JS_TAG_FUNCTION_BYTECODE)
    obj = js_misc_read_object(ctx, this_val, argc, argv);
  else
    obj = JS_DupValue(ctx, argv[0]);

  if(JS_IsException(obj))
    return obj;

  if((tag = JS_VALUE_GET_TAG(obj)) != JS_TAG_MODULE && tag != JS_TAG_FUNCTION_BYTECODE)
    return JS_ThrowTypeError(ctx, "obj is not MODULE nor BYTECODE");

  if(load_only) {
    if(tag == JS_TAG_MODULE)
      js_module_set_import_meta(ctx, obj, FALSE, FALSE);
  } else {
    if(tag == JS_TAG_MODULE) {
      if(JS_ResolveModule(ctx, obj) < 0) {
        JSModuleDef* m = JS_VALUE_GET_PTR(obj);
        const char* name = module_namecstr(ctx, m);

        ret = JS_ThrowInternalError(ctx, "Failed resolving module '%s'", name);
        JS_FreeCString(ctx, name);
        JS_FreeValue(ctx, obj);
        return ret;
      }

      js_module_set_import_meta(ctx, obj, FALSE, TRUE);
    }

    ret = JS_EvalFunction(ctx, obj);
  }

  JS_FreeValue(ctx, obj);
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
  GET_TYPE_ID,
  GET_TYPE_STR,
  GET_TYPE_NAME,
};

static JSValue
js_misc_type(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  int32_t type_id = 0;

  if(argc >= 1)
    type_id = js_value_type(ctx, argv[0]);

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

        for(i = 0; i < bits; i++)
          if(buf[i >> 3] & (1u << (i & 0x7)))
            JS_SetPropertyUint32(ctx, ret, j++, JS_NewInt64(ctx, i + offset));
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

      if((len = js_array_length(ctx, argv[0]))) {
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

      /*case RANDOM_RANDF: {
        uint32_t num = pcg32_random();

        ret = JS_NewFloat64(ctx, ((double)num / (UINT32_MAX >> 1)) - 1.0l);
        break;
      }*/

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

JSValue
js_misc_escape(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer input = js_input_chars(ctx, argv[0]);

  if(input.data) {
    uint8_t escape_tab[256];
    const uint8_t* tab = escape_noquote_tab;
    int32_t* intv = 0;
    size_t i, nelems;
    DynBuf output;

    if(argc > 1) {
      const char* str;

      if(JS_IsString(argv[1]) && (str = JS_ToCString(ctx, argv[1]))) {

        if(*str == '\'')
          tab = escape_singlequote_tab;
        else if(*str == '"')
          tab = escape_doublequote_tab;
        else if(*str == '`')
          tab = escape_backquote_tab;

        JS_FreeCString(ctx, str);

      } else if((intv = js_array_to_int32v(ctx, &nelems, argv[1]))) {
        for(i = 0; i < nelems; i++)
          escape_tab[i] = intv[i];

        while(i < 256)
          escape_tab[i++] = '\0';

        tab = escape_tab;
        js_free(ctx, intv);
      }
    }

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
    uint8_t escape_tab[256];
    const uint8_t* tab = escape_url_tab;
    int32_t* intv = 0;
    size_t i, nelems;
    DynBuf output;

    if(argc > 1) {
      const char* str;

      if(JS_IsString(argv[1]) && (str = JS_ToCString(ctx, argv[1]))) {

        if(*str == '\\')
          tab = escape_noquote_tab;
        else if(*str == '\'')
          tab = escape_singlequote_tab;
        else if(*str == '"')
          tab = escape_doublequote_tab;
        else if(*str == '`')
          tab = escape_backquote_tab;

        JS_FreeCString(ctx, str);

      } else if((intv = js_array_to_int32v(ctx, &nelems, argv[1]))) {
        for(i = 0; i < nelems; i++)
          escape_tab[i] = intv[i];

        while(i < 256)
          escape_tab[i++] = '\0';

        tab = escape_tab;
        js_free(ctx, intv);
      }
    }

    js_dbuf_init(ctx, &output);
    dbuf_put_unescaped_table(&output, (const char*)input.data, input.size, tab);

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
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 'b',  't',  'n',  'v',  'f',  'r',  0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, '\\', 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75,
      0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75, 0x75,
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
  /*IS_CFUNCTION,
  IS_BOUNDFUNCTION,
  IS_JSFUNCTION,*/
  IS_CONSTRUCTOR,
  IS_EMPTYSTRING,
  IS_ERROR,
  IS_EXCEPTION,
  IS_EXTENSIBLE,
  IS_FUNCTION,
  // IS_HTMLDDA,
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
    // case IS_CFUNCTION: r = JS_GetClassID(arg) == JS_CLASS_C_FUNCTION; break;
    // case IS_BOUNDFUNCTION: r = JS_GetClassID(arg) == JS_CLASS_BOUND_FUNCTION; break;
    // case IS_JSFUNCTION: r = JS_GetClassID(arg) == JS_CLASS_BYTECODE_FUNCTION; break;
    case IS_CONSTRUCTOR: r = JS_IsConstructor(ctx, arg); break;
    case IS_EMPTYSTRING: {
      const char* s = JS_ToCString(ctx, arg);
      r = JS_VALUE_GET_TAG(arg) == JS_TAG_STRING && (!s || !*s);
      if(s)
        JS_FreeCString(ctx, s);
      break;
    }
    case IS_ERROR: r = JS_IsError(ctx, arg); break;
    case IS_EXCEPTION: r = JS_IsException(arg); break;
    case IS_EXTENSIBLE: r = JS_IsExtensible(ctx, arg); break;
    case IS_FUNCTION: r = JS_IsFunction(ctx, arg); break;
    // case IS_HTMLDDA: r = JS_VALUE_GET_TAG(arg) == JS_TAG_OBJECT &&  JS_VALUE_GET_OBJ(arg)->is_HTMLDDA; break;
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

  if(argc >= 1)
    JS_ToInt32(ctx, &fd, argv[0]);

  if(js_is_arraybuffer(ctx, argv[0])) {
    InputBuffer buf = js_output_args(ctx, argc, argv);
    uint32_t count = 0, reclen;

    if(buf.range.length >= (int64_t)sizeof(struct inotify_event)) {
      size_t end = buf.pos + buf.range.offset + buf.range.length;
      ret = JS_NewArray(ctx);

      for(size_t i = buf.pos + buf.range.offset; i + sizeof(struct inotify_event) <= end; i += reclen) {
        struct inotify_event* ev = (struct inotify_event*)&buf.data[i];
        size_t namelen = byte_chr(ev->name, '\0', ev->len);
        JSValue obj = JS_NewObject(ctx);

        reclen = sizeof(struct inotify_event) + ev->len;

        JS_SetPropertyStr(ctx, obj, "wd", JS_NewInt32(ctx, ev->wd));
        if(ev->mask)
          JS_SetPropertyStr(ctx, obj, "mask", JS_NewUint32(ctx, ev->mask));
        if(ev->cookie)
          JS_SetPropertyStr(ctx, obj, "cookie", JS_NewUint32(ctx, ev->cookie));

        if(namelen)
          JS_SetPropertyStr(ctx, obj, "name", JS_NewStringLen(ctx, ev->name, namelen));

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
      return JS_ThrowInternalError(
          ctx, "inotify_add_watch(%d, %s, %08x) = %d (%s)", fd, filename, flags, wd, strerror(errno));

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
js_misc_chmod(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  uint32_t mode = 0;
  int32_t ret = -1;

  JS_ToUint32(ctx, &mode, argv[1]);

  switch(magic) {
#ifdef HAVE_FCHMOD
    case 1: {
      int32_t fd = -1;

      JS_ToInt32(ctx, &fd, argv[0]);

      ret = fchmod(fd, mode);
      break;
    }
#endif

#ifdef HAVE_CHMOD
    case 0: {
      const char* path;

      if(!(path = JS_ToCString(ctx, argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be a string");

      ret = chmod(path, mode);
      break;
    }
#endif
  }

  return JS_NewInt32(ctx, ret);
}

#ifdef HAVE_CHOWN
static JSValue
js_misc_chown(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int32_t ret = -1;
  uint32_t owner = 0, group = 0;

  JS_ToUint32(ctx, &owner, argv[1]);
  JS_ToUint32(ctx, &group, argv[2]);

  switch(magic) {
    case 1: {
      int32_t fd = -1;

      JS_ToInt32(ctx, &fd, argv[0]);

      ret = fchown(fd, owner, group);
      break;
    }
    case 0:
    case 2: {
      const char* path;

      if(!(path = JS_ToCString(ctx, argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be a string");

      ret = magic ? lchown(path, owner, group) : chown(path, owner, group);
      break;
    }
  }

  return JS_NewInt32(ctx, ret);
}
#endif

static JSValue
js_misc_fsync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int32_t ret = -1, fd = -1;

  JS_ToInt32(ctx, &fd, argv[0]);

  switch(magic) {
#ifdef HAVE_FSYNC
    case 0: {
      ret = fsync(fd);
      break;
    }
#endif

#ifdef HAVE_FDATASYNC
    case 1: {
      ret = fdatasync(fd);
      break;
    }
#endif
  }

  return JS_NewInt32(ctx, ret);
}

static JSValue
js_misc_truncate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int64_t len = -1;
  int32_t ret = -1;

  JS_ToInt64Ext(ctx, &len, argv[1]);

  if(len < 0)
    return JS_ThrowRangeError(ctx, "argument 2 must be positive-integer");

  switch(magic) {
#ifdef HAVE_TRUNCATE
    case 0: {
      const char* path;

      if(!(path = JS_ToCString(ctx, argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be a string");

      ret = truncate(path, len);
      break;
    }
#endif

#ifdef HAVE_FTRUNCATE
    case 1: {
      int32_t fd = -1;

      JS_ToInt32(ctx, &fd, argv[0]);

      ret = ftruncate(fd, len);
      break;
    }
#endif
  }

  return JS_NewInt32(ctx, ret);
}

static JSValue
js_misc_utime(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int32_t fd = -1, ret = -1;
  const char* path = 0;

  if(magic < 3) {
    if(!(path = JS_ToCString(ctx, argv[0])))
      return JS_ThrowTypeError(ctx, "argument 1 must be a string");
  } else {
    JS_ToInt32(ctx, &fd, argv[0]);
  }

  if(!JS_IsArray(ctx, argv[1]))
    return JS_ThrowTypeError(ctx, "argument 2 must be an array");

  switch(magic) {
#ifdef HAVE_UTIME
    case 0: {
      struct utimbuf tms;

      tms.actime = js_get_propertyint_int64(ctx, argv[1], 0);
      tms.modtime = js_get_propertyint_int64(ctx, argv[1], 1);

      ret = utime(path, &tms);
      break;
    }
#endif

#if defined(HAVE_UTIMES) || defined(HAVE_FUTIMES) || defined(HAVE_LUTIMES)
    default: {
      struct timeval tv[2];

      double atime = js_get_propertyint_float64(ctx, argv[1], 0);

      tv[0].tv_sec = atime / 1000;
      tv[0].tv_usec = (atime - tv[0].tv_sec * 1000) * 1000;

      double mtime = js_get_propertyint_float64(ctx, argv[1], 1);

      tv[1].tv_sec = mtime / 1000;
      tv[1].tv_usec = (mtime - tv[1].tv_sec * 1000) * 1000;

      switch(magic) {
#ifdef HAVE_FUTIMES
        case 3: ret = futimes(fd, tv); break;
#endif
#ifdef HAVE_LUTIMES
        case 2: ret = lutimes(path, tv); break;
#endif
#ifdef HAVE_UTIMES
        case 1: ret = utimes(path, tv); break;
#endif
      }

      break;
    }
#endif
  }

  return JS_NewInt32(ctx, ret);
}

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

static JSValue
js_misc_fstat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t fd = -1;
  int res, err = 0;
  JSValue ret = JS_NewArray(ctx), obj = JS_NULL;
  struct stat st;
  BOOL use_bigint = FALSE;
  struct {
    JSValue (*i)(JSContext*, int64_t);
    JSValue (*u)(JSContext*, uint64_t);
  } new64;

  JS_ToInt32(ctx, &fd, argv[0]);

  if(argc > 1 && JS_IsObject(argv[1])) {
    JSValue bi = JS_GetPropertyStr(ctx, argv[1], "bigint");
    use_bigint = JS_ToBool(ctx, bi);
    JS_FreeValue(ctx, bi);
  }

  new64.i = use_bigint ? JS_NewBigInt64 : JS_NewInt64;
  new64.u = use_bigint ? JS_NewBigUint64 : (JSValue(*)(JSContext*, uint64_t)) & JS_NewInt64;

#ifdef HAVE_FSTAT
  res = fstat(fd, &st);
#else
#warning Emulating fstat() using /proc/self/fd/<n>
  {
    char pbuf[32];
    strcpy(pbuf, "/proc/self/fd/");
    pbuf[fmt_ulong(&pbuf[14], fd)] = '\0';

    res = stat(pbuf, &st);
  }
#endif

  if(res < 0) {
    err = errno;

  } else {
    obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, obj, "dev", new64.u(ctx, st.st_dev));
    JS_SetPropertyStr(ctx, obj, "ino", new64.u(ctx, st.st_ino));
    JS_SetPropertyStr(ctx, obj, "mode", new64.u(ctx, st.st_mode));
    JS_SetPropertyStr(ctx, obj, "nlink", new64.u(ctx, st.st_nlink));
    JS_SetPropertyStr(ctx, obj, "uid", new64.u(ctx, st.st_uid));
    JS_SetPropertyStr(ctx, obj, "gid", new64.u(ctx, st.st_gid));
    JS_SetPropertyStr(ctx, obj, "rdev", new64.u(ctx, st.st_rdev));
    JS_SetPropertyStr(ctx, obj, "size", new64.u(ctx, st.st_size));
#if !defined(_WIN32)
    JS_SetPropertyStr(ctx, obj, "blocks", new64.u(ctx, st.st_blocks));
#endif

#if defined(_WIN32) || defined(__dietlibc__) || defined(__ANDROID__)
    JS_SetPropertyStr(ctx, obj, "atime", new64.u(ctx, (int64_t)st.st_atime * 1000));
    JS_SetPropertyStr(ctx, obj, "mtime", new64.u(ctx, (int64_t)st.st_mtime * 1000));
    JS_SetPropertyStr(ctx, obj, "ctime", new64.u(ctx, (int64_t)st.st_ctime * 1000));
#elif defined(__APPLE__)
    JS_SetPropertyStr(ctx, obj, "atime", new64.u(ctx, timespec_to_ms(&st.st_atimespec)));
    JS_SetPropertyStr(ctx, obj, "mtime", new64.u(ctx, timespec_to_ms(&st.st_mtimespec)));
    JS_SetPropertyStr(ctx, obj, "ctime", new64.u(ctx, timespec_to_ms(&st.st_ctimespec)));
#else
    JS_SetPropertyStr(ctx, obj, "atime", new64.u(ctx, timespec_to_ms(&st.st_atim)));
    JS_SetPropertyStr(ctx, obj, "mtime", new64.u(ctx, timespec_to_ms(&st.st_mtim)));
    JS_SetPropertyStr(ctx, obj, "ctime", new64.u(ctx, timespec_to_ms(&st.st_ctim)));
#endif
  }

  JS_SetPropertyUint32(ctx, ret, 0, obj);
  JS_SetPropertyUint32(ctx, ret, 1, JS_NewInt32(ctx, err));

  return ret;
}

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

#ifndef __wasi__
#if defined(_WIN32)

/* Windows 10 built-in VT100 emulation */
#define __ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#define __ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200

static JSValue
js_misc_ttysetraw(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int fd;
  HANDLE handle;

  if(JS_ToInt32(ctx, &fd, argv[0]))
    return JS_EXCEPTION;

  handle = (HANDLE)_get_osfhandle(fd);
  SetConsoleMode(handle, ENABLE_WINDOW_INPUT | __ENABLE_VIRTUAL_TERMINAL_INPUT);
  _setmode(fd, _O_BINARY);

  if(fd == 0) {
    handle = (HANDLE)_get_osfhandle(1); /* corresponding output */
    SetConsoleMode(handle, ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT | __ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }

  return JS_UNDEFINED;
}
#else

static struct termios oldtty;
static BOOL have_oldtty;

static void
term_exit(void) {
  tcsetattr(0, TCSANOW, &oldtty);
}

/* XXX: should add a way to go back to normal mode */
static JSValue
js_misc_ttysetraw(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct termios tty;
  int fd;
  BOOL restore = FALSE;

  if(JS_ToInt32(ctx, &fd, argv[0]))
    return JS_EXCEPTION;

  if(argc > 1)
    restore = JS_ToBool(ctx, argv[1]);

  if(restore) {
    if(!have_oldtty) {
      tcgetattr(fd, &tty);
      tty.c_iflag = IGNPAR | IMAXBEL | IUTF8;
      tty.c_oflag = OPOST | ONLCR;
      tty.c_cflag = B38400 | CSIZE | CREAD;
      tty.c_lflag = ISIG | ECHOE | ECHOK | ECHOCTL | ECHOKE;

      tty.c_cc[VMIN] = 1;
      tty.c_cc[VTIME] = 0;
    } else {
      tty = oldtty;
    }

    tcsetattr(0, TCSANOW, &tty);

  } else {

    memset(&tty, 0, sizeof(tty));

    tcgetattr(fd, &tty);
    oldtty = tty;
    have_oldtty = TRUE;

    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
    tty.c_cflag &= ~(CSIZE | PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr(fd, TCSANOW, &tty);

    atexit(term_exit);
  }

  return JS_UNDEFINED;
}

#endif /* !_WIN32 */
#endif

static JSValue
js_misc_opcodes(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  BOOL as_object = FALSE;

  if(argc >= 1)
    as_object = JS_ToBool(ctx, argv[0]);

  return js_opcode_list(ctx, as_object);
}

static JSValue
js_misc_get_bytecode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return js_get_bytecode(ctx, argv[0]);
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
    JS_CFUNC_DEF("fnmatch", 3, js_misc_fnmatch),
    JS_CFUNC_DEF("glob", 2, js_misc_glob),
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
#ifdef HAVE_CHMOD
    JS_CFUNC_MAGIC_DEF("chmod", 2, js_misc_chmod, 0),
#endif
#ifdef HAVE_FCHMOD
    JS_CFUNC_MAGIC_DEF("fchmod", 2, js_misc_chmod, 1),
#endif
#ifdef HAVE_CHOWN
    JS_CFUNC_MAGIC_DEF("chown", 3, js_misc_chown, 0),
#endif
#ifdef HAVE_FCHOWN
    JS_CFUNC_MAGIC_DEF("fchown", 3, js_misc_chown, 1),
#endif
#ifdef HAVE_LCHOWN
    JS_CFUNC_MAGIC_DEF("lchown", 3, js_misc_chown, 2),
#endif
#ifdef HAVE_FSYNC
    JS_CFUNC_MAGIC_DEF("fsync", 1, js_misc_fsync, 0),
#endif
#ifdef HAVE_FDATASYNC
    JS_CFUNC_MAGIC_DEF("fdatasync", 1, js_misc_fsync, 1),
#endif
#ifdef HAVE_TRUNCATE
    JS_CFUNC_MAGIC_DEF("truncate", 2, js_misc_truncate, 0),
#endif
#ifdef HAVE_FTRUNCATE
    JS_CFUNC_MAGIC_DEF("ftruncate", 2, js_misc_truncate, 1),
#endif
#ifdef HAVE_UTIME
    JS_CFUNC_MAGIC_DEF("utime", 2, js_misc_utime, 0),
#endif
#ifdef HAVE_UTIMES
    JS_CFUNC_MAGIC_DEF("utimes", 2, js_misc_utime, 1),
#endif
#ifdef HAVE_LUTIMES
    JS_CFUNC_MAGIC_DEF("lutimes", 2, js_misc_utime, 2),
#endif
#ifdef HAVE_FUTIMES
    JS_CFUNC_MAGIC_DEF("futimes", 2, js_misc_utime, 3),
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
    JS_CFUNC_DEF("fstat", 1, js_misc_fstat),
    JS_CFUNC_MAGIC_DEF("_get_osfhandle", 1, js_misc_osfhandle, FUNC_GET_OSFHANDLE),
    JS_CFUNC_MAGIC_DEF("_open_osfhandle", 1, js_misc_osfhandle, FUNC_OPEN_OSFHANDLE),
    JS_CFUNC_DEF("charLength", 1, js_misc_charlen),
    JS_CFUNC_DEF("charCode", 1, js_misc_charcode),
    JS_CFUNC_DEF("utf8Decode", 1, js_misc_u8dec),
    JS_CFUNC_DEF("utf8Encode", 1, js_misc_u8enc),
    JS_CFUNC_DEF("toString", 1, js_misc_tostring),
    JS_CFUNC_DEF("strcmp", 2, js_misc_strcmp),
    JS_CFUNC_DEF("toPointer", 1, js_misc_topointer),
    JS_CFUNC_DEF("toArrayBuffer", 1, js_misc_toarraybuffer),
    JS_CFUNC_DEF("dupArrayBuffer", 1, js_misc_duparraybuffer),
    JS_CFUNC_DEF("sliceArrayBuffer", 1, js_misc_slicearraybuffer),
    // JS_CFUNC_DEF("resizeArrayBuffer", 1, js_misc_resizearraybuffer),
    JS_CFUNC_DEF("concat", 1, js_misc_concat),
    JS_CFUNC_DEF("searchArrayBuffer", 2, js_misc_searcharraybuffer),
    // JS_ALIAS_DEF("search", "searchArrayBuffer"),
    JS_CFUNC_DEF("memcpy", 2, js_misc_memcpy),
    JS_CFUNC_DEF("memcmp", 2, js_misc_memcmp),
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
    JS_CFUNC_DEF("stoa", 1, js_misc_btoa),
    JS_CFUNC_MAGIC_DEF("atob", 1, js_misc_atob, 0),
    JS_CFUNC_MAGIC_DEF("atos", 1, js_misc_atob, 1),
    JS_CFUNC_MAGIC_DEF("bcrypt", 1, js_misc_bcrypt, 1),
    JS_CFUNC_MAGIC_DEF("not", 1, js_misc_bitop, BITOP_NOT),
    JS_CFUNC_MAGIC_DEF("xor", 2, js_misc_bitop, BITOP_XOR),
    JS_CFUNC_MAGIC_DEF("and", 2, js_misc_bitop, BITOP_AND),
    JS_CFUNC_MAGIC_DEF("or", 2, js_misc_bitop, BITOP_OR),
    JS_CFUNC_MAGIC_DEF("bitfieldSet", 1, js_misc_bitfield, BITFIELD_SET),
    JS_CFUNC_MAGIC_DEF("bits", 1, js_misc_bitfield, BITFIELD_BITS),
    JS_CFUNC_MAGIC_DEF("bitfieldToArray", 1, js_misc_bitfield, BITFIELD_TOARRAY),
    JS_CFUNC_MAGIC_DEF("arrayToBitfield", 1, js_misc_bitfield, BITFIELD_FROMARRAY),
    JS_CFUNC_MAGIC_DEF("immutableClass", 1, js_misc_immutable_class, 1),
    JS_CFUNC_DEF("writeObject", 1, js_misc_write_object),
    JS_CFUNC_DEF("readObject", 1, js_misc_read_object),
    JS_CFUNC_DEF("evalBinary", 1, js_misc_evalbinary),
    JS_CFUNC_DEF("getOpCodes", 0, js_misc_opcodes),
    JS_CFUNC_DEF("getByteCode", 1, js_misc_get_bytecode),
    JS_CFUNC_MAGIC_DEF("valueType", 1, js_misc_valuetype, VALUE_TYPE),
    JS_CFUNC_MAGIC_DEF("valueTag", 1, js_misc_valuetype, VALUE_TAG),
    JS_CFUNC_MAGIC_DEF("valuePointer", 1, js_misc_valuetype, VALUE_POINTER),
    JS_CFUNC_MAGIC_DEF("objectClassId", 1, js_misc_valuetype, OBJECT_CLASSID),
    JS_CFUNC_MAGIC_DEF("objectRefCount", 1, js_misc_valuetype, OBJECT_REFCOUNT),
    JS_CFUNC_MAGIC_DEF("objectOpaque", 1, js_misc_valuetype, OBJECT_OPAQUE),
    JS_CFUNC_MAGIC_DEF("classAtom", 1, js_misc_valuetype, CLASS_ATOM),
    JS_CFUNC_MAGIC_DEF("className", 1, js_misc_valuetype, CLASS_NAME),
    JS_CFUNC_MAGIC_DEF("classId", 1, js_misc_valuetype, CLASS_ID),
    JS_CFUNC_MAGIC_DEF("stringPointer", 1, js_misc_valuetype, STRING_POINTER),
    JS_CFUNC_MAGIC_DEF("stringLength", 1, js_misc_valuetype, STRING_LENGTH),
    JS_CFUNC_MAGIC_DEF("stringBuffer", 1, js_misc_valuetype, STRING_BUFFER),
    JS_CFUNC_MAGIC_DEF("atomToString", 1, js_misc_atom, ATOM_TO_STRING),
    JS_CFUNC_MAGIC_DEF("atomToValue", 1, js_misc_atom, ATOM_TO_VALUE),
    JS_CFUNC_MAGIC_DEF("valueToAtom", 1, js_misc_atom, VALUE_TO_ATOM),
    JS_CFUNC_MAGIC_DEF("getTypeId", 1, js_misc_type, GET_TYPE_ID),
    JS_CFUNC_MAGIC_DEF("getTypeStr", 1, js_misc_type, GET_TYPE_STR),
    JS_CFUNC_MAGIC_DEF("getTypeName", 1, js_misc_type, GET_TYPE_NAME),
    JS_CFUNC_MAGIC_DEF("rand", 0, js_misc_random, RANDOM_RAND),
    JS_CFUNC_MAGIC_DEF("randi", 0, js_misc_random, RANDOM_RANDI),
    // JS_CFUNC_MAGIC_DEF("randf", 0, js_misc_random, RANDOM_RANDF),
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
    /*JS_CFUNC_MAGIC_DEF("isCFunction", 1, js_misc_is, IS_CFUNCTION),
    JS_CFUNC_MAGIC_DEF("isBoundFunction", 1, js_misc_is, IS_BOUNDFUNCTION),
    JS_CFUNC_MAGIC_DEF("isJSFunction", 1, js_misc_is, IS_JSFUNCTION),*/
    JS_CFUNC_MAGIC_DEF("isConstructor", 1, js_misc_is, IS_CONSTRUCTOR),
    JS_CFUNC_MAGIC_DEF("isEmptyString", 1, js_misc_is, IS_EMPTYSTRING),
    JS_CFUNC_MAGIC_DEF("isError", 1, js_misc_is, IS_ERROR),
    JS_CFUNC_MAGIC_DEF("isException", 1, js_misc_is, IS_EXCEPTION),
    JS_CFUNC_MAGIC_DEF("isExtensible", 1, js_misc_is, IS_EXTENSIBLE),
    JS_CFUNC_MAGIC_DEF("isFunction", 1, js_misc_is, IS_FUNCTION),
    // JS_CFUNC_MAGIC_DEF("isHTMLDDA", 1, js_misc_is, IS_HTMLDDA),
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
    JS_CFUNC_DEF("ttySetRaw", 1, js_misc_ttysetraw),
    JS_CONSTANT(JS_EVAL_TYPE_GLOBAL),
    JS_CONSTANT(JS_EVAL_TYPE_MODULE),
    JS_CONSTANT(JS_EVAL_TYPE_DIRECT),
    JS_CONSTANT(JS_EVAL_TYPE_INDIRECT),
    JS_CONSTANT(JS_EVAL_TYPE_MASK),
    JS_CONSTANT(JS_EVAL_IS_MAIN),
    JS_CONSTANT(JS_EVAL_FLAG_STRICT),
    JS_CONSTANT(JS_EVAL_FLAG_STRIP),
    JS_CONSTANT(JS_EVAL_FLAG_COMPILE_ONLY),
    JS_CONSTANT(JS_EVAL_FLAG_BACKTRACE_BARRIER),
    JS_CONSTANT(JS_EVAL_FLAG_MASK),
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
    JS_CONSTANT(GLOB_APPEND),
    JS_CONSTANT(GLOB_DOOFFS),
    JS_CONSTANT(GLOB_ERR),
    JS_CONSTANT(GLOB_MARK),
    JS_CONSTANT(GLOB_NOCHECK),
    JS_CONSTANT(GLOB_NOSORT),
    JS_CONSTANT(GLOB_ALTDIRFUNC),
    JS_CONSTANT(GLOB_BRACE),
    JS_CONSTANT(GLOB_MAGCHAR),
    JS_CONSTANT(GLOB_NOMAGIC),
    JS_CONSTANT(GLOB_QUOTE),
    JS_CONSTANT(GLOB_TILDE),
    JS_CONSTANT(GLOB_NOESCAPE),
    JS_CONSTANT(GLOB_NOSPACE),
    JS_CONSTANT(GLOB_ABORTED),
    JS_CONSTANT(GLOB_NOMATCH),
    JS_CONSTANT(GLOB_NOSYS),
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
  vector_init(&js_misc_atexit_functions, ctx);
  atexit(&js_misc_atexit_handler);

  if(m)
    JS_SetModuleExportList(ctx, m, js_misc_funcs, countof(js_misc_funcs));

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

  if((m = JS_NewCModule(ctx, module_name, js_misc_init)))
    JS_AddModuleExportList(ctx, m, js_misc_funcs, countof(js_misc_funcs));

  return m;
}

/**
 * @}
 */
