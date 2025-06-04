/*
 * Copyright (c) 2021 Firmware Modules Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this softwareand associated documentation files(the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions :
 *
 * The above copyright noticeand this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 * OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __SJSON_H
#define __SJSON_H

#include <stdint.h>

#define SJSON_STATUS_IN_PROGRESS 1
#define SJSON_STATUS_OK 0
#define SJSON_STATUS_ERROR -1
#define SJSON_STATUS_INVALID_ARGS -2
#define SJSON_STATUS_MALFORMED_KEY -3
#define SJSON_STATUS_MALFORMED_VALUE -4
#define SJSON_STATUS_INVALID_STATE -5
#define SJSON_STATUS_OVERFLOW -6
#define SJSON_STATUS_UNEXPECTED_INPUT -7

typedef enum { SJSON_TYPE_NONE = 0, SJSON_TYPE_STRING, SJSON_TYPE_PRIMITIVE } sjson_type_t;

/*
 * Callback for handling the matched key token's value characters.
 * The raw characters comprising the token are provided followed by a \0.
 * The user is required to convert as expected.  The type of the detected value
 * as well as the JSON depth of detection is provided to aid in conversion.
 *
 * The handler can return an error condition (value < 0) to halt the parsing.
 * The error is forwarded to the caller of sjson_parse().
 *
 * @param buf - The parser's JSON token buffer containing the parsed value.
 * @param len - Number of characters in the parsed value token (excluding \0).
 * @param type - Type of the token parsed.
 * @param depth - Depth within the JSON object or array that the token was found in.  1 - first level, 2 - second level,
 * etc. Objects and arrays are treated the same so each '{' or '[' increases the depth.
 */
typedef int (*sjson_cb_handler_t)(const char* buf, uint16_t len, sjson_type_t type, uint8_t depth, void* opaque);

typedef struct {
  /* Key to match */
  const char* key;

  /* User's token value callback handler to invoke */
  /*const*/ sjson_cb_handler_t value_handler;
  void* opaque;

} sjson_cb_t;

/* Various non-functional statistics about the parsing. */
typedef struct {
  int char_count;
  uint16_t num_keys;
  uint16_t num_strings;
  uint16_t num_primitives;
  uint16_t num_callbacks;
  int status; /* overall status - holding most negative value detected */

} sjson_stats_t;

/* Parsing context structure */
typedef struct {
  /* User-provided token buffer */
  char* buf;
  uint16_t buf_len;

  /* position in the buffer */
  uint16_t pos;
  union {
    uint8_t str_uni_cnt;    /* counting unicode chars */
    uint8_t test_multi_end; /* test for end of comment block */
  };

  uint32_t is_array; /* flag for being in an array at depth = bit position (a kind of stack) */
  int8_t depth;      /* depth within JSON (1 - first level) */

  uint8_t parse_state;

  union {
    uint8_t parse_state_str;
    uint8_t comment_style;
  };

  /* Active value handler */
  sjson_cb_handler_t value_handler;
  void* opaque;

  sjson_stats_t stats;

  /*
   * Run-time linkage to user token match value handler callbacks array.
   * This array is read-only and may be located in flash.
   *
   * The user can re-configure the parser for different JSON payloads.
   *
   * **There must be a null key record at the end.**
   *
   * Example:
   *
   *    const sjson_cb_t my_sjson_callbacks[] = {
   *        { "key1", my_key1_value_handler },
   *        // more handlers,
   *        { 0 }
   *    };
   */
  const sjson_cb_t* callbacks;

} sjson_ctx_t;

/*
 * Init SJSON context with buffer.
 *
 * This buffer must be large enough to contain the largest expected key *or* value token in the stream.
 *
 * The context state is reset and ready for parsing after this call.
 *
 * @param ctx - SJSON context structure.
 * @param buf - user token buffer.
 * @param len - user token buffer length.
 * @param callbacks - user token callbacks array **must be null-terminated**.
 */
int sjson_init(sjson_ctx_t* ctx, char* buf, uint16_t len, const sjson_cb_t* callbacks);

/*
 * Parse the buffer for JSON.
 *
 * Any amount of sequential JSON characters can be offered at each invocation.
 *
 * A properly formatted JSON character sequence is required. Any unexpected characters in the stream,
 * or key or value tokens too large for the initialized buffer, will return an error.
 *
 * @param ctx - SJSON context
 * @param buf - Buffer containing some JSON characters to parse within the context of the
 *              current stream.
 * @param len - Number of characters in the provided buffer.
 *
 * Returns SJSON_STATUS error codes: >= 0 if parsing is working; < 0 on any error as soon as it is detected.
 */
int sjson_parse(sjson_ctx_t* ctx, const char* buf, int len);

/*
 * Reset the parser.
 *
 * Keeps the user token buffer.
 * Parser can be re-used for another JSON stream.
 */
void sjson_reset(sjson_ctx_t* ctx);

/*
 * Get a constant pointer to the string representing
 * the given JSON status code.
 *
 * Will always return a valid pointer.
 */
const char* sjson_status_to_str(int code);

/*
 * Utility function:
 *
 * Pack a hex (base-16) string into binary form.
 *
 *   hex_str = "CAFE1234"
 *
 *   hex_bin = { 0xCA, 0xFE, 0x12, 0x34 }
 *
 * @param hex_str input string in hexadecimal notation, e.g. "6546cabaf079a6221d"
 * @param hex_bin output array of packed hex bytes
 * @param len length of the output array
 *
 * Returns OK on success, UNEXPECTED_INPUT if input is not divisible by 2
 *  or OVERFLOW if packing is truncated and output buffer would overflow.
 */
int sjson_pack_hexstr2bin(const char* hex_str, uint8_t* hex_bin, int len);

#endif /* __SJSON_H */
