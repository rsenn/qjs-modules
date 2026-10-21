#include "defines.h"
#include "quickjs-textcode.h"
#include "utils.h"
#include "buffer-utils.h"
#include "stream-utils.h"
#include "debug.h"

#include "tutf8e/include/tutf8e.h"

#include <sys/types.h>
#include <libutf.h>

/**
 * \addtogroup quickjs-textdecoder
 * @{
 */

VISIBLE JSClassID js_decoder_class_id = 0, js_encoder_class_id = 0;
static JSValue textdecoder_proto, textdecoder_ctor, textencoder_proto, textencoder_ctor;

const TUTF8encoder* tutf8e_coders[] = {
    /* 0, 0, 0, 0, 0, 0, 0, 0, */
    &tutf8e_encoder_iso_8859_1,   &tutf8e_encoder_iso_8859_2,   &tutf8e_encoder_iso_8859_3,   &tutf8e_encoder_iso_8859_4,   &tutf8e_encoder_iso_8859_5,
    &tutf8e_encoder_iso_8859_6,   &tutf8e_encoder_iso_8859_7,   &tutf8e_encoder_iso_8859_8,   &tutf8e_encoder_iso_8859_9,   &tutf8e_encoder_iso_8859_10,
    &tutf8e_encoder_iso_8859_11,  &tutf8e_encoder_iso_8859_13,  &tutf8e_encoder_iso_8859_14,  &tutf8e_encoder_iso_8859_15,  &tutf8e_encoder_iso_8859_16,
    &tutf8e_encoder_windows_1250, &tutf8e_encoder_windows_1251, &tutf8e_encoder_windows_1252, &tutf8e_encoder_windows_1253, &tutf8e_encoder_windows_1254,
    &tutf8e_encoder_windows_1255, &tutf8e_encoder_windows_1256, &tutf8e_encoder_windows_1257, &tutf8e_encoder_windows_1258,
};

const char* const textcode_encodings[] = {
    "unknown",      "utf-8",        "utf-16le",     "utf-32le",     "unknown",      "utf-8",        "utf-16be",     "utf-32be",     "iso-8859-1",
    "iso-8859-2",   "iso-8859-3",   "iso-8859-4",   "iso-8859-5",   "iso-8859-6",   "iso-8859-7",   "iso-8859-8",   "iso-8859-9",   "iso-8859-10",
    "iso-8859-11",  "iso-8859-13",  "iso-8859-14",  "iso-8859-15",  "iso-8859-16",  "windows-1250", "windows-1251", "windows-1252", "windows-1253",
    "windows-1254", "windows-1255", "windows-1256", "windows-1257", "windows-1258", "windows-874",  "iso-8859-8-i",
};

/* WHATWG encoding labels, sorted for bsearch(), plus utf-32 as an extension.
 * generated from https://encoding.spec.whatwg.org/encodings.json */
static const struct textcode_label {
  const char* label;
  uint8_t code;
} textcode_labels[] = {
    {"ansi_x3.4-1968", WINDOWS_1252},
    {"arabic", ISO_8859_6},
    {"ascii", WINDOWS_1252},
    {"asmo-708", ISO_8859_6},
    {"cp1250", WINDOWS_1250},
    {"cp1251", WINDOWS_1251},
    {"cp1252", WINDOWS_1252},
    {"cp1253", WINDOWS_1253},
    {"cp1254", WINDOWS_1254},
    {"cp1255", WINDOWS_1255},
    {"cp1256", WINDOWS_1256},
    {"cp1257", WINDOWS_1257},
    {"cp1258", WINDOWS_1258},
    {"cp819", WINDOWS_1252},
    {"csiso88596e", ISO_8859_6},
    {"csiso88596i", ISO_8859_6},
    {"csiso88598e", ISO_8859_8},
    {"csiso88598i", ISO_8859_8_I},
    {"csisolatin1", WINDOWS_1252},
    {"csisolatin2", ISO_8859_2},
    {"csisolatin3", ISO_8859_3},
    {"csisolatin4", ISO_8859_4},
    {"csisolatin5", WINDOWS_1254},
    {"csisolatin6", ISO_8859_10},
    {"csisolatin9", ISO_8859_15},
    {"csisolatinarabic", ISO_8859_6},
    {"csisolatincyrillic", ISO_8859_5},
    {"csisolatingreek", ISO_8859_7},
    {"csisolatinhebrew", ISO_8859_8},
    {"csunicode", UTF16LE},
    {"cyrillic", ISO_8859_5},
    {"dos-874", WINDOWS_874},
    {"ecma-114", ISO_8859_6},
    {"ecma-118", ISO_8859_7},
    {"elot_928", ISO_8859_7},
    {"greek", ISO_8859_7},
    {"greek8", ISO_8859_7},
    {"hebrew", ISO_8859_8},
    {"ibm819", WINDOWS_1252},
    {"iso-10646-ucs-2", UTF16LE},
    {"iso-8859-1", WINDOWS_1252},
    {"iso-8859-10", ISO_8859_10},
    {"iso-8859-11", WINDOWS_874},
    {"iso-8859-13", ISO_8859_13},
    {"iso-8859-14", ISO_8859_14},
    {"iso-8859-15", ISO_8859_15},
    {"iso-8859-16", ISO_8859_16},
    {"iso-8859-2", ISO_8859_2},
    {"iso-8859-3", ISO_8859_3},
    {"iso-8859-4", ISO_8859_4},
    {"iso-8859-5", ISO_8859_5},
    {"iso-8859-6", ISO_8859_6},
    {"iso-8859-6-e", ISO_8859_6},
    {"iso-8859-6-i", ISO_8859_6},
    {"iso-8859-7", ISO_8859_7},
    {"iso-8859-8", ISO_8859_8},
    {"iso-8859-8-e", ISO_8859_8},
    {"iso-8859-8-i", ISO_8859_8_I},
    {"iso-8859-9", WINDOWS_1254},
    {"iso-ir-100", WINDOWS_1252},
    {"iso-ir-101", ISO_8859_2},
    {"iso-ir-109", ISO_8859_3},
    {"iso-ir-110", ISO_8859_4},
    {"iso-ir-126", ISO_8859_7},
    {"iso-ir-127", ISO_8859_6},
    {"iso-ir-138", ISO_8859_8},
    {"iso-ir-144", ISO_8859_5},
    {"iso-ir-148", WINDOWS_1254},
    {"iso-ir-157", ISO_8859_10},
    {"iso8859-1", WINDOWS_1252},
    {"iso8859-10", ISO_8859_10},
    {"iso8859-11", WINDOWS_874},
    {"iso8859-13", ISO_8859_13},
    {"iso8859-14", ISO_8859_14},
    {"iso8859-15", ISO_8859_15},
    {"iso8859-2", ISO_8859_2},
    {"iso8859-3", ISO_8859_3},
    {"iso8859-4", ISO_8859_4},
    {"iso8859-5", ISO_8859_5},
    {"iso8859-6", ISO_8859_6},
    {"iso8859-7", ISO_8859_7},
    {"iso8859-8", ISO_8859_8},
    {"iso8859-9", WINDOWS_1254},
    {"iso88591", WINDOWS_1252},
    {"iso885910", ISO_8859_10},
    {"iso885911", WINDOWS_874},
    {"iso885913", ISO_8859_13},
    {"iso885914", ISO_8859_14},
    {"iso885915", ISO_8859_15},
    {"iso88592", ISO_8859_2},
    {"iso88593", ISO_8859_3},
    {"iso88594", ISO_8859_4},
    {"iso88595", ISO_8859_5},
    {"iso88596", ISO_8859_6},
    {"iso88597", ISO_8859_7},
    {"iso88598", ISO_8859_8},
    {"iso88599", WINDOWS_1254},
    {"iso_8859-1", WINDOWS_1252},
    {"iso_8859-15", ISO_8859_15},
    {"iso_8859-1:1987", WINDOWS_1252},
    {"iso_8859-2", ISO_8859_2},
    {"iso_8859-2:1987", ISO_8859_2},
    {"iso_8859-3", ISO_8859_3},
    {"iso_8859-3:1988", ISO_8859_3},
    {"iso_8859-4", ISO_8859_4},
    {"iso_8859-4:1988", ISO_8859_4},
    {"iso_8859-5", ISO_8859_5},
    {"iso_8859-5:1988", ISO_8859_5},
    {"iso_8859-6", ISO_8859_6},
    {"iso_8859-6:1987", ISO_8859_6},
    {"iso_8859-7", ISO_8859_7},
    {"iso_8859-7:1987", ISO_8859_7},
    {"iso_8859-8", ISO_8859_8},
    {"iso_8859-8:1988", ISO_8859_8},
    {"iso_8859-9", WINDOWS_1254},
    {"iso_8859-9:1989", WINDOWS_1254},
    {"l1", WINDOWS_1252},
    {"l2", ISO_8859_2},
    {"l3", ISO_8859_3},
    {"l4", ISO_8859_4},
    {"l5", WINDOWS_1254},
    {"l6", ISO_8859_10},
    {"l9", ISO_8859_15},
    {"latin1", WINDOWS_1252},
    {"latin2", ISO_8859_2},
    {"latin3", ISO_8859_3},
    {"latin4", ISO_8859_4},
    {"latin5", WINDOWS_1254},
    {"latin6", ISO_8859_10},
    {"logical", ISO_8859_8_I},
    {"sun_eu_greek", ISO_8859_7},
    {"tis-620", WINDOWS_874},
    {"ucs-2", UTF16LE},
    {"unicode", UTF16LE},
    {"unicode-1-1-utf-8", UTF8},
    {"unicode11utf8", UTF8},
    {"unicode20utf8", UTF8},
    {"unicodefeff", UTF16LE},
    {"unicodefffe", UTF16BE},
    {"us-ascii", WINDOWS_1252},
    {"utf-16", UTF16LE},
    {"utf-16be", UTF16BE},
    {"utf-16le", UTF16LE},
    {"utf-32", UTF32LE},
    {"utf-32be", UTF32BE},
    {"utf-32le", UTF32LE},
    {"utf-8", UTF8},
    {"utf8", UTF8},
    {"visual", ISO_8859_8},
    {"windows-1250", WINDOWS_1250},
    {"windows-1251", WINDOWS_1251},
    {"windows-1252", WINDOWS_1252},
    {"windows-1253", WINDOWS_1253},
    {"windows-1254", WINDOWS_1254},
    {"windows-1255", WINDOWS_1255},
    {"windows-1256", WINDOWS_1256},
    {"windows-1257", WINDOWS_1257},
    {"windows-1258", WINDOWS_1258},
    {"windows-874", WINDOWS_874},
    {"x-cp1250", WINDOWS_1250},
    {"x-cp1251", WINDOWS_1251},
    {"x-cp1252", WINDOWS_1252},
    {"x-cp1253", WINDOWS_1253},
    {"x-cp1254", WINDOWS_1254},
    {"x-cp1255", WINDOWS_1255},
    {"x-cp1256", WINDOWS_1256},
    {"x-cp1257", WINDOWS_1257},
    {"x-cp1258", WINDOWS_1258},
    {"x-unicode20utf8", UTF8},
};

/* windows-874 bytes 0x80..0x97, the rest is the iso-8859-11 table */
static const uint16_t textdecoder_windows_874[] = {
    0x20ac, 0x0081, 0x0082, 0x0083, 0x0084, 0x2026, 0x0086, 0x0087, 0x0088, 0x0089, 0x008a, 0x008b,
    0x008c, 0x008d, 0x008e, 0x008f, 0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
};

enum {
  DECODER_DECODE,
  DECODER_END,
};
enum {
  DECODER_ENCODING,
  DECODER_ENDIANNESS,
  DECODER_BUFFERED,
  DECODER_FATAL,
  DECODER_IGNOREBOM,
};

/* throws a TypeError, or a RangeError if range, with Node's code property.
 * returns JS_EXCEPTION with an exception pending. */
static JSValue
textcode_throw(JSContext* ctx, BOOL range, const char* code, const char* msg) {
  JSValue err;

  if(range)
    JS_ThrowRangeError(ctx, "%s", msg);
  else
    JS_ThrowTypeError(ctx, "%s", msg);

  err = JS_GetException(ctx);
  JS_SetPropertyStr(ctx, err, "code", JS_NewString(ctx, code));
  return JS_Throw(ctx, err);
}

static int
textcode_label_cmp(const void* key, const void* entry) {
  return strcmp(key, ((const struct textcode_label*)entry)->label);
}

static inline BOOL
textcode_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

/* the encoding a WHATWG label names, case and surrounding space ignored.
 *
 *   " Latin1 "  gives WINDOWS_1252
 *
 * never throws: returns the TextEncoding, or -1 for an unknown label. */
static int
textcode_lookup(const char* s, size_t len) {
  const struct textcode_label* found;
  char name[32];

  while(len && textcode_is_space(*s))
    s++, len--;

  while(len && textcode_is_space(s[len - 1]))
    len--;

  if(len >= sizeof(name) || memchr(s, '\0', len))
    return -1;

  for(size_t i = 0; i < len; i++)
    name[i] = s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i];

  name[len] = '\0';
  found = bsearch(name, textcode_labels, countof(textcode_labels), sizeof(textcode_labels[0]), textcode_label_cmp);

  return found ? found->code : -1;
}

/* the decoder state of a fresh stream */
static void
textdecoder_reset(TextDecoder* dec) {
  dec->buflen = dec->needed = dec->seen = 0;
  dec->cp = 0;
  dec->lower = 0x80;
  dec->upper = 0xbf;
  dec->bom_seen = 0;
}

static inline void
textdecoder_put(DynBuf* db, uint32_t cp) {
  uint8_t* p;

  if(cp < 0x80)
    dbuf_putc(db, cp);
  else if((p = dbuf_reserve(db, UTF8_CHAR_LEN_MAX)))
    db->size += unicode_to_utf8(p, cp);
}

/* one decode error: U+FFFD, or a TypeError when the decoder is fatal.
 * returns 0, or -1 with an exception pending. */
static int
textdecoder_error(TextDecoder* dec, DynBuf* db, JSContext* ctx) {
  char msg[64];

  if(!dec->fatal) {
    dbuf_put(db, (const uint8_t*)"\xef\xbf\xbd", 3);
    return 0;
  }

  snprintf(msg, sizeof(msg), "The encoded data was not valid for encoding %s", textcode_encodings[dec->type_code]);
  textcode_throw(ctx, FALSE, "ERR_ENCODING_INVALID_ENCODED_DATA", msg);
  return -1;
}

/**
 * WHATWG utf-8 decode: one U+FFFD per maximal invalid subpart.
 *
 * valid input is not copied: the run of valid bytes after the last error
 * stays in p for the caller, as [*pstart, *pend).
 *
 *   TextDecoder*    dec     decoder state, kept across calls
 *   const uint8_t*  p       input bytes
 *   size_t          n       input length
 *   BOOL            flush   end of stream, an open sequence is an error
 *   DynBuf*         db      output before the valid run
 *   size_t*         pstart  start of the valid run
 *   size_t*         pend    end of the valid run
 *
 *   returns 0, or -1 with an exception pending (fatal).
 */
static int
textdecoder_utf8(TextDecoder* dec, const uint8_t* p, size_t n, BOOL flush, DynBuf* db, size_t* pstart, size_t* pend, JSContext* ctx) {
  size_t i = 0, start = 0;
  ssize_t seq = -1; /* where the open sequence starts, -1 if before p */

  while(i < n) {
    uint8_t b = p[i];

    if(!dec->needed) {
      if(b < 0x80) {
        uint64_t word;

        /* skip ASCII 8 bytes at a time */
        for(i++; i + 8 <= n; i += 8) {
          memcpy(&word, p + i, 8);

          if(word & 0x8080808080808080ULL)
            break;
        }

        continue;
      }

      if(b >= 0xc2 && b <= 0xdf) {
        dec->needed = 1;
        dec->cp = b & 0x1f;
      } else if(b >= 0xe0 && b <= 0xef) {
        dec->lower = b == 0xe0 ? 0xa0 : 0x80;
        dec->upper = b == 0xed ? 0x9f : 0xbf;
        dec->needed = 2;
        dec->cp = b & 0xf;
      } else if(b >= 0xf0 && b <= 0xf4) {
        dec->lower = b == 0xf0 ? 0x90 : 0x80;
        dec->upper = b == 0xf4 ? 0x8f : 0xbf;
        dec->needed = 3;
        dec->cp = b & 0x7;
      } else {
        dbuf_put(db, p + start, i - start);

        if(textdecoder_error(dec, db, ctx))
          return -1;

        start = ++i;
        continue;
      }

      seq = i++;
      continue;
    }

    if(b < dec->lower || b > dec->upper) {
      /* the open sequence is one error, b is read again */
      if(seq >= 0)
        dbuf_put(db, p + start, seq - start);

      dec->needed = dec->seen = 0;
      dec->lower = 0x80;
      dec->upper = 0xbf;

      if(textdecoder_error(dec, db, ctx))
        return -1;

      start = i;
      seq = -1;
      continue;
    }

    dec->lower = 0x80;
    dec->upper = 0xbf;
    dec->cp = (dec->cp << 6) | (b & 0x3f);
    i++;

    if(++dec->seen == dec->needed) {
      if(seq < 0) {
        textdecoder_put(db, dec->cp);
        start = i;
      }

      dec->needed = dec->seen = 0;
      seq = -1;
    }
  }

  *pstart = start;
  *pend = n;
  dec->buflen = dec->needed ? dec->seen + 1 : 0;

  if(dec->needed) {
    *pend = seq >= 0 ? (size_t)seq : start;

    if(flush) {
      dbuf_put(db, p + start, *pend - start);
      *pstart = *pend = n;
      textdecoder_reset(dec);
      return textdecoder_error(dec, db, ctx);
    }
  }

  return 0;
}

/* WHATWG utf-16le/be decode, a lone surrogate or odd byte is U+FFFD.
 * returns 0, or -1 with an exception pending (fatal). */
static int
textdecoder_utf16(TextDecoder* dec, const uint8_t* p, size_t n, BOOL flush, DynBuf* db, JSContext* ctx) {
  for(size_t i = 0; i < n; i++) {
    uint32_t u;

    if(!dec->buflen) {
      dec->buf[0] = p[i];
      dec->buflen = 1;
      continue;
    }

    u = dec->endian == BIG ? (dec->buf[0] << 8) | p[i] : (p[i] << 8) | dec->buf[0];
    dec->buflen = 0;

    if(dec->cp) {
      uint32_t lead = dec->cp;

      dec->cp = 0;

      if(u >= 0xdc00 && u <= 0xdfff) {
        textdecoder_put(db, 0x10000 + ((lead - 0xd800) << 10) + (u - 0xdc00));
        continue;
      }

      if(textdecoder_error(dec, db, ctx))
        return -1;
    }

    if(u >= 0xd800 && u <= 0xdbff)
      dec->cp = u;
    else if(u >= 0xdc00 && u <= 0xdfff) {
      if(textdecoder_error(dec, db, ctx))
        return -1;
    } else
      textdecoder_put(db, u);
  }

  if(flush && (dec->cp || dec->buflen)) {
    dec->cp = dec->buflen = 0;
    return textdecoder_error(dec, db, ctx);
  }

  return 0;
}

/* utf-32le/be decode, an extension: a surrogate or a value above
 * U+10FFFF is U+FFFD. returns 0, or -1 with an exception pending. */
static int
textdecoder_utf32(TextDecoder* dec, const uint8_t* p, size_t n, BOOL flush, DynBuf* db, JSContext* ctx) {
  for(size_t i = 0; i < n; i++) {
    uint32_t cp;

    dec->buf[dec->buflen++] = p[i];

    if(dec->buflen < 4)
      continue;

    cp = uint32_get_endian(dec->buf, dec->endian);
    dec->buflen = 0;

    if(cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
      if(textdecoder_error(dec, db, ctx))
        return -1;
    } else
      textdecoder_put(db, cp);
  }

  if(flush && dec->buflen) {
    dec->buflen = 0;
    return textdecoder_error(dec, db, ctx);
  }

  return 0;
}

/* WHATWG single-byte decode over a tutf8e table: an unmapped byte is
 * U+FFFD, in windows-125x an unmapped 0x80..0x9f is the C1 control.
 * returns 0, or -1 with an exception pending (fatal). */
static int
textdecoder_single(TextDecoder* dec, const uint8_t* p, size_t n, DynBuf* db, JSContext* ctx) {
  TextEncoding code = dec->type_code == WINDOWS_874 ? ISO_8859_11 : dec->type_code == ISO_8859_8_I ? ISO_8859_8 : dec->type_code;
  const uint16_t* table = (const uint16_t*)*tutf8e_coders[code - ISO_8859_1];
  BOOL windows = dec->type_code >= WINDOWS_1250 && dec->type_code <= WINDOWS_1258;

  for(size_t i = 0; i < n; i++) {
    uint32_t c = p[i];

    if(c >= 0x80) {
      c = dec->type_code == WINDOWS_874 && c < 0x98 ? textdecoder_windows_874[c - 0x80] : table[c];

      if(c == 0xffff && dec->type_code == WINDOWS_1255 && p[i] == 0xca)
        c = 0x05ba; /* WHATWG maps it, the unicode.org table does not */

      if(c == 0xffff) {
        if(!(windows && p[i] <= 0x9f)) {
          if(textdecoder_error(dec, db, ctx))
            return -1;

          continue;
        }

        c = p[i];
      }
    }

    textdecoder_put(db, c);
  }

  return 0;
}

/* the bytes of an ArrayBuffer, SharedArrayBuffer, typed array or DataView,
 * none for undefined. returns the buffer to free, JS_UNDEFINED for none,
 * or JS_EXCEPTION with an exception pending. */
static JSValue
textdecoder_input(JSContext* ctx, JSValueConst value, const uint8_t** pptr, size_t* plen) {
  size_t offset = 0, length = 0, size = 0;
  JSValue buf;
  uint8_t* data;

  *pptr = 0;
  *plen = 0;

  if(JS_IsUndefined(value))
    return JS_UNDEFINED;

  if(js_is_arraybuffer(ctx, value) || js_is_sharedarraybuffer(ctx, value)) {
    buf = JS_DupValue(ctx, value);
    length = SIZE_MAX;
  } else if(js_is_typedarray(ctx, value)) {
    buf = JS_GetTypedArrayBuffer(ctx, value, &offset, &length, 0);
  } else if(js_is_dataview(ctx, value)) {
    buf = JS_GetPropertyStr(ctx, value, "buffer");
    offset = js_get_propertystr_uint64(ctx, value, "byteOffset");
    length = js_get_propertystr_uint64(ctx, value, "byteLength");
  } else
    return textcode_throw(ctx, FALSE, "ERR_INVALID_ARG_TYPE", "The \"input\" argument must be an instance of ArrayBuffer or ArrayBufferView");

  if(JS_IsException(buf))
    return buf;

  if(!(data = JS_GetArrayBuffer(ctx, &size, buf)) && JS_HasException(ctx)) {
    JS_FreeValue(ctx, buf);
    return JS_EXCEPTION;
  }

  if(offset > size)
    offset = size;

  *pptr = data + offset;
  *plen = MIN_NUM(length, size - offset);
  return buf;
}

/* the decoded string: db, then the n bytes at tail, BOM removed at the
 * start of a stream. returns the string, or JS_EXCEPTION. */
static JSValue
textdecoder_string(TextDecoder* dec, DynBuf* db, const uint8_t* tail, size_t n, JSContext* ctx) {
  const uint8_t* s = tail;

  if(db->size) {
    dbuf_put(db, tail, n);
    s = db->buf;
    n = db->size;
  }

  if(db->error)
    return JS_ThrowOutOfMemory(ctx);

  if(n && !dec->bom_seen) {
    dec->bom_seen = 1;

    if(!dec->ignore_bom && n >= 3 && !memcmp(s, "\xef\xbb\xbf", 3))
      s += 3, n -= 3;
  }

  return JS_NewStringLen(ctx, (const char*)s, n);
}

static JSValue
js_decoder_get(JSContext* ctx, JSValueConst this_val, int magic) {
  TextDecoder* dec;
  JSValue ret = JS_UNDEFINED;

  if(!(dec = js_decoder_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case DECODER_ENCODING: {
      ret = JS_NewString(ctx, textcode_encodings[dec->type_code]);
      break;
    }
    case DECODER_ENDIANNESS: {
      ret = JS_NewBool(ctx, dec->endian == BIG);
      break;
    }
    case DECODER_BUFFERED: {
      ret = JS_NewUint32(ctx, dec->buflen);
      break;
    }
    case DECODER_FATAL: {
      ret = JS_NewBool(ctx, dec->fatal);
      break;
    }
    case DECODER_IGNOREBOM: {
      ret = JS_NewBool(ctx, dec->ignore_bom);
      break;
    }
  }

  return ret;
}

/* TextDecoder: WHATWG TextDecoder, plus utf-32 and the endian and
 * buffered getters.
 *
 *   new TextDecoder(label = 'utf-8', { fatal, ignoreBOM })
 *
 *   throws RangeError ERR_ENCODING_NOT_SUPPORTED for an unknown label,
 *   TypeError ERR_INVALID_ARG_TYPE when options is not an object */
static JSValue
js_decoder_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  TextDecoder* dec;
  int code = UTF8;

  if(!(dec = js_mallocz(ctx, sizeof(TextDecoder))))
    return JS_EXCEPTION;

  if(argc > 0 && !JS_IsUndefined(argv[0])) {
    const char* s;
    size_t len;

    if(!(s = JS_ToCStringLen(ctx, &len, argv[0])))
      goto fail;

    if((code = textcode_lookup(s, len)) < 0) {
      char msg[96];

      snprintf(msg, sizeof(msg), "The \"%.64s\" encoding is not supported", s);
      JS_FreeCString(ctx, s);
      textcode_throw(ctx, TRUE, "ERR_ENCODING_NOT_SUPPORTED", msg);
      goto fail;
    }

    JS_FreeCString(ctx, s);
  }

  if(argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
    if(!JS_IsObject(argv[1])) {
      textcode_throw(ctx, FALSE, "ERR_INVALID_ARG_TYPE", "The \"options\" argument must be of type object");
      goto fail;
    }

    dec->fatal = js_get_propertystr_bool(ctx, argv[1], "fatal");
    dec->ignore_bom = js_get_propertystr_bool(ctx, argv[1], "ignoreBOM");
  }

  dec->type_code = code;
  textdecoder_reset(dec);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, textdecoder_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_decoder_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, dec);
  return obj;

fail:
  js_free(ctx, dec);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

/* decode(input, { stream }): the string for input, as in WHATWG.
 *
 * without stream: true the next call starts a new stream, and an open
 * sequence at the end is an error.
 *
 *   throws TypeError ERR_INVALID_ARG_TYPE for input other than bytes,
 *   TypeError ERR_ENCODING_INVALID_ENCODED_DATA when fatal */
static JSValue
js_decoder_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  TextDecoder* dec;
  JSValue ret = JS_UNDEFINED;

  if(!(dec = js_decoder_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case DECODER_DECODE: {
      const uint8_t* ptr;
      size_t len, start = 0, end = 0;
      BOOL stream = FALSE;
      DynBuf db = DBUF_INIT_CTX(ctx);
      JSValue buf;
      int r;

      if(argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        if(!JS_IsObject(argv[1]))
          return textcode_throw(ctx, FALSE, "ERR_INVALID_ARG_TYPE", "The \"options\" argument must be of type object");

        stream = js_get_propertystr_bool(ctx, argv[1], "stream");
      }

      buf = textdecoder_input(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &ptr, &len);
      if(JS_IsException(buf))
        return buf;

      if(!dec->do_not_flush)
        textdecoder_reset(dec);

      dec->do_not_flush = stream;

      switch((int)dec->type_code) {
        case UTF8: r = textdecoder_utf8(dec, ptr, len, !stream, &db, &start, &end, ctx); break;
        case UTF16LE:
        case UTF16BE: r = textdecoder_utf16(dec, ptr, len, !stream, &db, ctx); break;
        case UTF32LE:
        case UTF32BE: r = textdecoder_utf32(dec, ptr, len, !stream, &db, ctx); break;
        default: r = textdecoder_single(dec, ptr, len, &db, ctx); break;
      }

      if(r < 0) {
        dec->do_not_flush = 0;
        ret = JS_EXCEPTION;
      } else
        ret = textdecoder_string(dec, &db, ptr + start, end - start, ctx);

      dbuf_free(&db);
      JS_FreeValue(ctx, buf);
      break;
    }
  }

  return ret;
}

static void
js_decoder_finalizer(JSRuntime* rt, JSValue val) {
  TextDecoder* dec;

  if((dec = JS_GetOpaque(val, js_decoder_class_id))) {
    js_free_rt(rt, dec);
  }
}

static JSClassDef js_decoder_class = {
    .class_name = "TextDecoder",
    .finalizer = js_decoder_finalizer,
};

static const JSCFunctionListEntry js_decoder_methods[] = {
    JS_CFUNC_MAGIC_DEF("decode", 1, js_decoder_method, DECODER_DECODE),
    JS_CGETSET_ENUMERABLE_DEF("encoding", js_decoder_get, 0, DECODER_ENCODING),
    JS_CGETSET_ENUMERABLE_DEF("fatal", js_decoder_get, 0, DECODER_FATAL),
    JS_CGETSET_ENUMERABLE_DEF("ignoreBOM", js_decoder_get, 0, DECODER_IGNOREBOM),
    JS_CGETSET_MAGIC_DEF("endian", js_decoder_get, 0, DECODER_ENDIANNESS),
    JS_CGETSET_MAGIC_DEF("buffered", js_decoder_get, 0, DECODER_BUFFERED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TextDecoder", JS_PROP_CONFIGURABLE),
};

enum {
  ENCODER_ENCODING,
  ENCODER_ENDIANNESS,
  ENCODER_BUFFERED,
};

static JSValue
textencoder_typedarray(TextEncoding type_code, const void* x, size_t n, JSContext* ctx) {
  JSValue ret, buf;
  int bits;

  switch(type_code & 0x3) {
    case UTF8: bits = 8; break;
    case UTF16: bits = 16; break;
    case UTF32: bits = 32; break;
    default: return JS_ThrowInternalError(ctx, "%s: TextEncoder: invalid encoding: %d", __func__, type_code);
  }

  buf = JS_NewArrayBufferCopy(ctx, x, n);
  ret = js_typedarray_new(ctx, bits, FALSE, FALSE, buf);
  JS_FreeValue(ctx, buf);
  return ret;
}

static ssize_t
textencoder_encode(TextEncoder* enc, InputBuffer in, Writer wr, JSContext* ctx) {
  uint8_t u8[UTF8_CHAR_LEN_MAX];
  size_t i = 0, written = 0;
  const uint8_t *ptr = inputbuffer_begin(&in), *end = inputbuffer_end(&in), *next;
  int cp;

  /* i counts UTF-16 units, as encodeInto() reports read */
  for(i = 0; ptr < end; ptr = next, i += cp > 0xffff ? 2 : 1) {
    cp = unicode_from_utf8(ptr, end - ptr, &next);

    if(cp == -1)
      break;

    if(enc->char_set == UTF8) {
      if(writer_write(&wr, ptr, next - ptr) < 0)
        break;

      continue;
    }

    if(enc->char_set == UTF16) {
      uint16_t u16[2];
      int len;

      if(!libutf_c32_to_c16(cp, &len, u16)) {
        JS_ThrowInternalError(ctx, "TextEncoder.encode: not a valid code point at (%" PRIu64 ") [%" PRId64 "]: %i", (uint64_t)i, (int64_t)(next - ptr), cp);
        return -1;
      }

      for(int j = 0; j < len; j++)
        uint16_put_endian(u8 + j * 2, u16[j], enc->endian);

      if(writer_write(&wr, u8, len * sizeof(uint16_t)) < 0)
        break;

      continue;
    }

    if(enc->char_set == UTF32) {
      uint32_put_endian(u8, cp, enc->endian);

      if(writer_write(&wr, u8, sizeof(uint32_t)) < 0)
        break;

      continue;
    }

    JS_ThrowInternalError(ctx, "TextEncoder.encode: unknown encoding");
    return -1;
  }

  return i;
}

static JSValue
js_encoder_get(JSContext* ctx, JSValueConst this_val, int magic) {
  TextEncoder* enc;
  JSValue ret = JS_UNDEFINED;

  if(!(enc = js_encoder_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case ENCODER_ENCODING: {
      ret = JS_NewString(ctx, textcode_encodings[enc->type_code]);
      break;
    }
    case ENCODER_ENDIANNESS: {
      ret = JS_NewBool(ctx, enc->endian == BIG);
      break;
    }
    case ENCODER_BUFFERED: {
      ret = JS_NewUint32(ctx, enc->buflen);
      break;
    }
  }

  return ret;
}

static JSValue
js_encoder_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  TextEncoder* enc;

  if(!(enc = js_mallocz(ctx, sizeof(TextEncoder))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = textencoder_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_encoder_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  uint32_t defaultSize = 1024;

  if(argc > 1)
    JS_ToUint32(ctx, &defaultSize, argv[1]);

  if(argc >= 1) {
    const char* s = JS_ToCString(ctx, argv[0]);

    if(s[case_finds(s, "utf32")] || s[case_finds(s, "utf-32")])
      enc->char_set = UTF32;
    else if(s[case_finds(s, "utf16")] || s[case_finds(s, "utf-16")])
      enc->char_set = UTF16;
    else if(s[case_finds(s, "utf8")] || s[case_finds(s, "utf-8")])
      enc->char_set = UTF8;
    else
      return JS_ThrowInternalError(ctx, "TextEncoder '%s' is invalid s", s);

    if((int)enc->char_set > (int)UTF8)
      if(s[case_finds(s, "be")] || s[case_finds(s, "be")])
        enc->endian = BIG;

    JS_FreeCString(ctx, s);
  } else {
    enc->char_set = (UTFCharset)UTF8;
  }

  JS_SetOpaque(obj, enc);
  return obj;

fail:
  js_free(ctx, enc);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  ENCODER_ENCODE,
  ENCODER_ENCODE_INTO,
};

static JSValue
js_encoder_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  TextEncoder* enc;
  JSValue ret = JS_UNDEFINED;

  if(!(enc = js_encoder_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case ENCODER_ENCODE: {
      InputBuffer in = js_input_chars(ctx, argv[0]);
      DynBuf db = DBUF_INIT_CTX(ctx);

      if(textencoder_encode(enc, in, writer_from_dynbuf(&db), ctx) < 0) {
        ret = JS_EXCEPTION;
        break;
      }

      ret = textencoder_typedarray(enc->type_code, db.buf, db.size, ctx);
      dbuf_free(&db);
      break;
    }
    case ENCODER_ENCODE_INTO: {
      InputBuffer in = js_input_chars(ctx, argv[0]);
      OutputBuffer out = js_output_typedarray(ctx, argv[1]);
      ssize_t read;

      if((read = textencoder_encode(enc, in, writer_from_buf(&out), ctx)) < 0) {
        ret = JS_EXCEPTION;
        break;
      }

      ret = JS_NewObjectProto(ctx, JS_NULL);
      JS_SetPropertyStr(ctx, ret, "read", JS_NewUint32(ctx, read));
      JS_SetPropertyStr(ctx, ret, "written", JS_NewUint32(ctx, out.pos));

      inputbuffer_free(&in, ctx);
      outputbuffer_free(&out, ctx);
      break;
    }
  }

  return ret;
}

/* length of the ASCII run at the start of p, 8 bytes at a time */
static size_t
ascii_span(const uint8_t* p, size_t n) {
  size_t i;
  uint64_t word;

  for(i = 0; i + 8 <= n; i += 8) {
    memcpy(&word, p + i, 8);

    if(word & 0x8080808080808080ULL)
      break;
  }

  while(i < n && p[i] < 0x80)
    i++;

  return i;
}

/* length of the valid UTF-8 sequence at p, 0 when it is invalid or cut
 * off. valid as in WHATWG: no overlong form, no surrogate, <= U+10FFFF.
 *
 *   E2 82 AC  ->  3
 *   ED A0 80  ->  0  (a surrogate)
 */
static size_t
utf8_seqlen(const uint8_t* p, size_t n) {
  uint8_t b = p[0], lower = 0x80, upper = 0xbf;
  size_t i, len;

  if(b < 0x80)
    return 1;

  if(b >= 0xc2 && b <= 0xdf) {
    len = 2;
  } else if(b >= 0xe0 && b <= 0xef) {
    len = 3;
    lower = b == 0xe0 ? 0xa0 : 0x80;
    upper = b == 0xed ? 0x9f : 0xbf;
  } else if(b >= 0xf0 && b <= 0xf4) {
    len = 4;
    lower = b == 0xf0 ? 0x90 : 0x80;
    upper = b == 0xf4 ? 0x8f : 0xbf;
  } else {
    return 0;
  }

  if(len > n || p[1] < lower || p[1] > upper)
    return 0;

  for(i = 2; i < len; i++)
    if((p[i] & 0xc0) != 0x80)
      return 0;

  return len;
}

enum {
  TEXTCODE_IS_ASCII,
  TEXTCODE_IS_UTF8,
};

/**
 * isAscii(view), isUtf8(view): whether the bytes are 7-bit ASCII or
 * valid UTF-8, the base of Node's buffer.isAscii() and buffer.isUtf8().
 *
 *   isUtf8(new Uint8Array([0xe2, 0x82, 0xac]))   -> true
 *   isUtf8(new Uint8Array([0xc0, 0xaf]))         -> false, overlong
 *
 *   ArrayBuffer|view  view  the bytes, a view counts from its offset
 *
 *   returns boolean
 *
 * throws TypeError when view is not an ArrayBuffer or a view.
 * wired in as JS_CFUNC_MAGIC_DEF("isAscii", 1, js_textcode_is, ...).
 */
static JSValue
js_textcode_is(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  InputBuffer in = js_input_buffer(ctx, argv[0]);
  const uint8_t* p = inputbuffer_data(&in);
  size_t i, len, n = inputbuffer_length(&in);

  if(JS_IsException(in.value))
    return JS_EXCEPTION;

  i = ascii_span(p, n);

  if(magic == TEXTCODE_IS_UTF8)
    while(i < n && (len = utf8_seqlen(p + i, n - i)))
      i += len + ascii_span(p + i + len, n - i - len);

  inputbuffer_free(&in, ctx);
  return JS_NewBool(ctx, i == n);
}

/**
 * utf8Offsets(bytes, indices): converts UTF-16 indices into the text
 * that bytes encodes to byte offsets, in place, in one pass.
 *
 *   utf8Offsets(new TextEncoder().encode('aé😀b'), new Uint32Array([1, 2, 4]))
 *     -> Uint32Array [1, 3, 7]
 *
 *   ArrayBuffer|view  bytes    UTF-8, as TextEncoder makes it
 *   Uint32Array       indices  UTF-16 indices, ascending for one pass,
 *                              each one becomes a byte offset
 *
 *   returns Uint32Array        indices
 *
 * an index inside a surrogate pair maps to the start of its code point,
 * one past the end maps to the length of bytes.
 * throws TypeError when bytes is no buffer or indices has no 4-byte
 * elements. wired in as JS_CFUNC_DEF("utf8Offsets", 2, ...).
 */
static JSValue
js_textcode_utf8offsets(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer in = js_input_buffer(ctx, argv[0]);
  const uint8_t* p = inputbuffer_data(&in);
  size_t k, offset, length, bpe, size, n = inputbuffer_length(&in), pos = 0;
  uint32_t* idx;
  uint32_t u16 = 0;
  JSValue buf;

  if(JS_IsException(in.value))
    return JS_EXCEPTION;

  buf = JS_GetTypedArrayBuffer(ctx, argv[1], &offset, &length, &bpe);

  if(JS_IsException(buf) || bpe != 4 || !(idx = (uint32_t*)JS_GetArrayBuffer(ctx, &size, buf))) {
    JS_FreeValue(ctx, buf);
    inputbuffer_free(&in, ctx);
    return JS_IsException(buf) ? JS_EXCEPTION : JS_ThrowTypeError(ctx, "argument 2 must be a Uint32Array");
  }

  idx = (uint32_t*)((uint8_t*)idx + offset);

  for(k = 0; k < length / 4; k++) {
    uint32_t target = idx[k];

    /* a smaller index than the last one starts over */
    if(target < u16)
      pos = u16 = 0;

    while(u16 < target && pos < n) {
      size_t len, run;

      if((run = ascii_span(p + pos, MIN_NUM(n - pos, target - u16)))) {
        pos += run;
        u16 += run;
        continue;
      }

      /* an invalid byte counts as one U+FFFD */
      if(!(len = utf8_seqlen(p + pos, n - pos)))
        len = 1;

      if(u16 + (len == 4 ? 2 : 1) > target)
        break;

      pos += len;
      u16 += len == 4 ? 2 : 1;
    }

    idx[k] = pos;
  }

  JS_FreeValue(ctx, buf);
  inputbuffer_free(&in, ctx);
  return JS_DupValue(ctx, argv[1]);
}

static void
textcode_buffer_free(JSRuntime* rt, void* opaque, void* ptr) {
  js_free_rt(rt, ptr);
}

/**
 * encode(string, encoding): the UTF-16 units of string as bytes, for the
 * Node encodings no standard API has, Buffer.from(string, 'latin1').
 *
 *   encode('AéĀ', 'latin1')   -> ArrayBuffer [0x41, 0xe9, 0x00]
 *   encode('A\ud800', 'utf-16le')       -> ArrayBuffer [0x41, 0, 0x00, 0xd8]
 *
 *   string  string    the text, a lone surrogate is kept as its unit
 *   string  encoding  'utf-16le', or 'latin1' and 'ascii' (unit & 255)
 *
 *   returns ArrayBuffer
 *
 * throws RangeError for another encoding.
 * wired in as JS_CFUNC_DEF("encode", 2, js_textcode_encode).
 */
static JSValue
js_textcode_encode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const uint8_t *s, *p, *end, *next;
  const char* enc;
  size_t len, units = 0, k = 0;
  uint8_t* out;
  BOOL wide;

  if(!(enc = JS_ToCString(ctx, argc > 1 ? argv[1] : JS_UNDEFINED)))
    return JS_EXCEPTION;

  wide = !strcmp(enc, "utf-16le");

  if(!wide && strcmp(enc, "latin1") && strcmp(enc, "ascii")) {
    JS_ThrowRangeError(ctx, "encode: unknown encoding '%s'", enc);
    JS_FreeCString(ctx, enc);
    return JS_EXCEPTION;
  }

  JS_FreeCString(ctx, enc);

  /* cesu8: every UTF-16 unit, lone surrogates too, is its own sequence */
  if(!(s = (const uint8_t*)JS_ToCStringLen2(ctx, &len, argv[0], TRUE)))
    return JS_EXCEPTION;

  for(p = s, end = s + len; p < end; p++)
    units += (*p & 0xc0) != 0x80;

  if(!(out = js_malloc(ctx, (units << wide) + 1))) {
    JS_FreeCString(ctx, (const char*)s);
    return JS_EXCEPTION;
  }

  for(p = s; p < end; p = next) {
    int unit = unicode_from_utf8(p, end - p, &next);

    if(unit < 0) {
      unit = 0xfffd;
      next = p + 1;
    }

    out[k++] = unit & 0xff;

    if(wide)
      out[k++] = unit >> 8;
  }

  JS_FreeCString(ctx, (const char*)s);
  return JS_NewArrayBuffer(ctx, out, k, textcode_buffer_free, 0, FALSE);
}

/**
 * decode(view, encoding): bytes as code points, for the Node encodings
 * whose names WHATWG uses for windows-1252, buffer.toString('latin1').
 *
 *   decode(new Uint8Array([0x41, 0xe9]), 'latin1')   -> 'Aé'
 *   decode(new Uint8Array([0x41, 0xe9]), 'ascii')    -> 'Ai'
 *
 *   ArrayBuffer|view  view      the bytes
 *   string            encoding  'latin1' (byte = code point) or 'ascii'
 *                               (byte & 0x7f)
 *
 *   returns string
 *
 * throws RangeError for another encoding.
 * wired in as JS_CFUNC_DEF("decode", 2, js_textcode_decode).
 */
static JSValue
js_textcode_decode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer in = js_input_buffer(ctx, argv[0]);
  const uint8_t* p = inputbuffer_data(&in);
  size_t i, k = 0, n = inputbuffer_length(&in);
  const char* enc;
  uint8_t mask, *out;
  JSValue ret = JS_EXCEPTION;

  if(JS_IsException(in.value))
    return JS_EXCEPTION;

  if(!(enc = JS_ToCString(ctx, argc > 1 ? argv[1] : JS_UNDEFINED)))
    goto end;

  mask = !strcmp(enc, "ascii") ? 0x7f : 0xff;

  if(mask == 0xff && strcmp(enc, "latin1")) {
    JS_ThrowRangeError(ctx, "decode: unknown encoding '%s'", enc);
    JS_FreeCString(ctx, enc);
    goto end;
  }

  JS_FreeCString(ctx, enc);

  /* as UTF-8 for JS_NewStringLen, a byte >= 0x80 takes 2 */
  if(!(out = js_malloc(ctx, n * 2 + 1)))
    goto end;

  for(i = 0; i < n; i++) {
    uint8_t c = p[i] & mask;

    if(c < 0x80) {
      out[k++] = c;
    } else {
      out[k++] = 0xc0 | (c >> 6);
      out[k++] = 0x80 | (c & 0x3f);
    }
  }

  ret = JS_NewStringLen(ctx, (const char*)out, k);
  js_free(ctx, out);

end:
  inputbuffer_free(&in, ctx);
  return ret;
}

static void
js_encoder_finalizer(JSRuntime* rt, JSValue val) {
  TextEncoder* enc;

  if((enc = JS_GetOpaque(val, js_encoder_class_id))) {
    js_free_rt(rt, enc);
  }
}

static JSClassDef js_encoder_class = {
    .class_name = "TextEncoder",
    .finalizer = js_encoder_finalizer,
};

static const JSCFunctionListEntry js_encoder_methods[] = {
    JS_CFUNC_MAGIC_DEF("encode", 1, js_encoder_method, ENCODER_ENCODE),
    JS_CFUNC_MAGIC_DEF("encodeInto", 2, js_encoder_method, ENCODER_ENCODE_INTO),
    JS_CGETSET_ENUMERABLE_DEF("encoding", js_encoder_get, 0, ENCODER_ENCODING),
    JS_CGETSET_MAGIC_DEF("endian", js_encoder_get, 0, ENCODER_ENDIANNESS),
    JS_CGETSET_MAGIC_DEF("buffered", js_encoder_get, 0, ENCODER_BUFFERED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TextEncoder", JS_PROP_CONFIGURABLE),
};

/* module exports besides the two classes, one line per name:
 *   isAscii      7-bit ASCII check, buffer.isAscii()
 *   isUtf8       UTF-8 validity check, buffer.isUtf8()
 *   utf8Offsets  UTF-16 indices to UTF-8 byte offsets
 *   encode       a string as utf-16le or latin1 bytes
 *   decode       latin1 or ascii bytes as a string
 */
static const JSCFunctionListEntry js_textcode_funcs[] = {
    JS_CFUNC_MAGIC_DEF("isAscii", 1, js_textcode_is, TEXTCODE_IS_ASCII),
    JS_CFUNC_MAGIC_DEF("isUtf8", 1, js_textcode_is, TEXTCODE_IS_UTF8),
    JS_CFUNC_DEF("utf8Offsets", 2, js_textcode_utf8offsets),
    JS_CFUNC_DEF("encode", 2, js_textcode_encode),
    JS_CFUNC_DEF("decode", 2, js_textcode_decode),
};

int
js_code_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_decoder_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_decoder_class_id, &js_decoder_class);

  textdecoder_ctor = JS_NewCFunction2(ctx, js_decoder_constructor, "TextDecoder", 1, JS_CFUNC_constructor, 0);
  textdecoder_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, textdecoder_proto, js_decoder_methods, countof(js_decoder_methods));
  JS_SetClassProto(ctx, js_decoder_class_id, textdecoder_proto);

  JS_SetConstructor(ctx, textdecoder_ctor, textdecoder_proto);

  JS_NewClassID(&js_encoder_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_encoder_class_id, &js_encoder_class);

  textencoder_ctor = JS_NewCFunction2(ctx, js_encoder_constructor, "TextEncoder", 1, JS_CFUNC_constructor, 0);
  textencoder_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, textencoder_proto, js_encoder_methods, countof(js_encoder_methods));
  JS_SetClassProto(ctx, js_encoder_class_id, textencoder_proto);

  JS_SetConstructor(ctx, textencoder_ctor, textencoder_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "TextDecoder", textdecoder_ctor);
    JS_SetModuleExport(ctx, m, "TextEncoder", textencoder_ctor);
    JS_SetModuleExportList(ctx, m, js_textcode_funcs, countof(js_textcode_funcs));
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_textcode
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_code_init))) {
    JS_AddModuleExport(ctx, m, "TextDecoder");
    JS_AddModuleExport(ctx, m, "TextEncoder");
    JS_AddModuleExportList(ctx, m, js_textcode_funcs, countof(js_textcode_funcs));
  }

  return m;
}

/**
 * @}
 */
