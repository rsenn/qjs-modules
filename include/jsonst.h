#ifndef JSONST_H
#define JSONST_H

/* jsonst.{h,c}: The main implementation. */

#include <stdbool.h>
#include <stddef.h>

/**
 * \defgroup jsonst jsonst: JSON parser
 * @{
 */

/* Possible types of objects in a JSON document. */
typedef enum {
  Type_Doc = 'd',
  Type_Null = 'n',
  Type_True = 't',
  Type_False = 'f',
  Type_Number = 'x',
  Type_String = 's',

  Type_Array = '[',
  Type_ArrayElm = 'e',
  Type_ArrayEnd = ']',

  Type_Object = '{',
  Type_ObjectKey = 'k',
  Type_ObjectEnd = '}',
} JsonSt_Type;

/* A JSON value. */
typedef struct {
  JsonSt_Type type;

  /* Set only if type == Type_String or type == Type_Number.
   *
   * In case of Type_Number, you have to parse the number yourself - it is provided here exactly as
   * it is in the document. It is guaranteed to be a valid number as per JSON spec and strtod()
   * should always work on it. Parsing example:
   *
   *   #include <stdlib.h>
   *   char *endptr;
   *   double num = strtod(value->val_str.str, &endptr);
   *      You might want to do real error handling here instead.
   *   assert(endptr == value->val_str.str + value->val_str.str_len);
   */
  struct {
    char* str;         /**< This is NULL byte terminated for compatibility with C strings. */
    ptrdiff_t str_len; /**< Length of str _without_ the NULL byte. */
  } val_str;
} JsonSt_Value;

/* A JSON path describing the location of a value in a document. */
typedef struct jsonst_path JsonSt_Path;

struct jsonst_path {
  JsonSt_Type type;  /**< Valid options are only Type_ArrayElm and Type_ObjectKey. */
  JsonSt_Path* next; /**< Is NULL for the last path segment. */
  union {
    unsigned arry_ix; /**< Set if type == Type_ArrayElm. */
    struct {
      char* str;         /**< This is NULL byte terminated for compatibility with the C stdlib. */
      ptrdiff_t str_len; /**< Length of str without NULL byte. */
    } obj_key;           /**< Set if type == Type_ObjectKey. */
  } props;
};

/*
 * Callback signature.
 * The callback arguments and memory locations they point to have valid lifetime only until the
 * callback returns. Excepted from that is user_data.
 */
typedef void (*JsonSt_Callback)(void* user_data, const JsonSt_Value* value, const JsonSt_Path* p);

/*
 * Opaque handle to an instance.
 * Use jsonst_new() to allocate one.
 */
typedef struct jsonst_internal* JsonSt;

/* Default values for JsonSt_Config. */
#define JSONST_DEFAULT_STR_ALLOC_BYTES (ptrdiff_t)128
#define JSONST_DEFAULT_OBJ_KEY_ALLOC_BYTES (ptrdiff_t)128
#define JSONST_DEFAULT_NUM_ALLOC_BYTES (ptrdiff_t)128

/*
 * Configuration values for jsonst.
 * A zero-initialized struct is valid and means using the defaults.
 */
typedef struct {
  ptrdiff_t str_alloc_bytes;     /**< Max size in bytes for string values. */
  ptrdiff_t obj_key_alloc_bytes; /**< Max size in bytes for object keys. */
  ptrdiff_t num_alloc_bytes;     /**< Max size in bytes for numbers before parsing. */
} JsonSt_Config;

/*
 * Create an instance.
 *
 * - The instance will take ownership of the memory buffer mem (with size memsz) and use it as
 * allocation jsonst_arena.
 * - It will not allocate memory by any other means (e.g. malloc).
 * - To destroy the instance, simply free(mem).
 * - An instance is good to parse one single JSON document.
 * - To parse a new one, it needs to be reset. To do that, simply call jsonst_new() again with the
 *   same mem argument.
 *   Calling jsonst_new() with the same mem argument will always return the same instance struct
 *   value, so you you can ignore it if all you want is to reset the instance.
 * - Context can be passed to the callback by optionally passing non-NULL cb_user_data here.
 *   It will be forwarded to the callback as first argument.
 * - If the memory region mem passed in here is too small to allocate an instance, NULL is returned.
 */
JsonSt
jsonst_new(void* mem, const ptrdiff_t memsz, const JsonSt_Callback cb, void* cb_user_data, const JsonSt_Config conf);

/* Error codes. */
typedef enum {
  Error_Success = 0,

  Error_Oom,
  Error_StrBufferFull,
  Error_PreviousError,
  Error_InternalBug,
  Error_EndOfDoc,
  Error_InvalidEof,

  Error_ExpectedNewValue,
  Error_ExpectedNewKey,
  Error_UnexpectedChar,
  Error_InvalidLiteral,
  Error_InvalidControlChar,
  Error_InvalidQuotedChar,
  Error_InvalidHexDigit,
  Error_InvalidUtf8Encoding,
  Error_InvalidNumber,
  Error_InvalidUnicodeCodepoint,
  Error_InvalidUtf16Surrogate,
} JsonSt_Error;

#define JSONST_EOF (-1)

/* Feed the next byte to the parser.
 * This can cause zero, one or multiple invocations of the callback function.
 * At the end of your input, you must call this method once with c = JSONST_EOF.
 * Returns Error_Success or an error code.
 */
JsonSt_Error jsonst_feed(JsonSt j, const char c);

/* Return value of jsonst_feed_doc. */
typedef struct {
  JsonSt_Error err;
  size_t parsed_bytes;
} JsonSt_FeedDocRet;

/* Feed an entire document to the parser.
 * Will process until the document is entirely parsed, or an error is met.
 * Returns Error_Success or an error code, as well as the number of bytes the
 * parser has read from the doc.
 */
JsonSt_FeedDocRet jsonst_feed_doc(JsonSt j, const char* doc, const size_t docsz);

/**
 * @}
 */

#endif /* defined(JSONST_H) */
