/* quickjs-digest.c: digest() and algorithms, one-shot message digests.
 * depends on: third_party/Hash-Algorithms (md5, sha1, sha2), buffer-utils.h.
 * rule: the C side hashes a whole message, chunking is the caller's job.
 */
#include "buffer-utils.h"
#include "defines.h"

#include <quickjs.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "Hash-Algorithms/md5.h"
#include "Hash-Algorithms/sha1.h"
#include "Hash-Algorithms/sha224.h"
#include "Hash-Algorithms/sha256.h"
#include "Hash-Algorithms/sha512.h"

/* the headers of sha384, sha512-224 and sha512-256 are empty */
void sha384_digest(uint8_t* message, size_t message_len, uint64_t digest[], bool debug);
void sha512_224_digest(uint8_t* message, size_t message_len, uint64_t digest[], bool debug);
void sha512_256_digest(uint8_t* message, size_t message_len, uint64_t digest[], bool debug);

/**
 * \addtogroup quickjs-digest
 * @{
 */

typedef void HashFunction(uint8_t* message, size_t len, uint8_t* out);

typedef struct {
  const char* name; /* lowercase, as Node's getHashes() lists it */
  uint32_t size;    /* digest bytes */
  uint32_t block;   /* block bytes, what HMAC needs */
  HashFunction* hash;
} Algorithm;

/* the first n bytes of the state words, each word big-endian.
 *
 *   uint8_t*     out    n bytes
 *   const void*  words  uint32_t or uint64_t state
 *   size_t       w      word size, 4 or 8
 */
static void
put_words(uint8_t* out, size_t n, const void* words, size_t w) {
  for(size_t i = 0; i < n; i++) {
    uint64_t v = w == 4 ? ((const uint32_t*)words)[i / 4] : ((const uint64_t*)words)[i / 8];

    out[i] = v >> (8 * (w - 1 - i % w));
  }
}

/* one wrapper per library function: <fn>_digest into <type> state words, the
 * first <bytes> of them are the digest. the state array holds 8 words, the
 * widest digest-state any of the functions writes. */
#define WRAP(fn, type, bytes) \
  static void hash_##fn(uint8_t* message, size_t len, uint8_t* out) { \
    type words[8] = {0}; \
\
    fn##_digest(message, len, words, false); \
    put_words(out, bytes, words, sizeof(type)); \
  }

WRAP(md5, uint32_t, 16)
WRAP(sha1, uint32_t, 20)
WRAP(sha224, uint32_t, 28)
WRAP(sha256, uint32_t, 32)
WRAP(sha384, uint64_t, 48)
WRAP(sha512_224, uint64_t, 28)
WRAP(sha512_256, uint64_t, 32)

/* sha512 has an object API instead of sha512_digest() */
static void
hash_sha512(uint8_t* message, size_t len, uint8_t* out) {
  sha512 h = SHA512();

  h.digest(&h, message, len);
  put_words(out, 64, h.digests, 8);
}

static const Algorithm algorithms[] = {
    {"md5", 16, 64, hash_md5},
    {"sha1", 20, 64, hash_sha1},
    {"sha224", 28, 64, hash_sha224},
    {"sha256", 32, 64, hash_sha256},
    {"sha384", 48, 128, hash_sha384},
    {"sha512", 64, 128, hash_sha512},
    {"sha512-224", 28, 128, hash_sha512_224},
    {"sha512-256", 32, 128, hash_sha512_256},
};

/* digest: one-shot message digest, as the engine under node:crypto.
 *
 *   import { digest } from 'digest';
 *   digest('sha256', new TextEncoder().encode('abc'))   // ArrayBuffer, 32 bytes
 *
 *   string                  algorithm  lowercase name, one of algorithms
 *   ArrayBuffer | view      data       the whole message
 *
 *   returns ArrayBuffer     the digest, size bytes
 *
 * throws TypeError when data is no buffer or view, RangeError for an unknown
 * algorithm. wired in: JS_CFUNC_DEF("digest", 2, js_digest).
 */
static JSValue
js_digest(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const Algorithm* algorithm = 0;
  const char* name;
  InputBuffer in;
  uint8_t out[64], *copy = 0, *message;
  size_t len;

  if(argc < 2)
    return JS_ThrowTypeError(ctx, "digest(algorithm, data) takes 2 arguments");

  if(!(name = JS_ToCString(ctx, argv[0])))
    return JS_EXCEPTION;

  for(size_t i = 0; i < countof(algorithms); i++)
    if(!strcmp(name, algorithms[i].name))
      algorithm = &algorithms[i];

  if(!algorithm) {
    JSValue ret = JS_ThrowRangeError(ctx, "unknown digest algorithm '%s'", name);

    JS_FreeCString(ctx, name);
    return ret;
  }

  JS_FreeCString(ctx, name);

  in = js_input_buffer(ctx, argv[1]);

  if(JS_IsException(in.value))
    return JS_EXCEPTION;

  message = (uint8_t*)inputbuffer_begin(&in);
  len = inputbuffer_length(&in);

  /* the library reads the message as 32 or 64 bit words: align it */
  if((uintptr_t)message & 7) {
    if(!(copy = js_malloc(ctx, len ? len : 1))) {
      inputbuffer_free(&in, ctx);
      return JS_EXCEPTION;
    }

    memcpy(copy, message, len);
    message = copy;
  }

  algorithm->hash(message, len, out);

  if(copy)
    js_free(ctx, copy);

  inputbuffer_free(&in, ctx);

  return JS_NewArrayBufferCopy(ctx, out, algorithm->size);
}

static const JSCFunctionListEntry js_digest_funcs[] = {
    JS_CFUNC_DEF("digest", 2, js_digest),
};

static int
js_digest_init(JSContext* ctx, JSModuleDef* m) {
  JSValue list = JS_NewArray(ctx);

  for(size_t i = 0; i < countof(algorithms); i++) {
    JSValue entry = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, entry, "name", JS_NewString(ctx, algorithms[i].name));
    JS_SetPropertyStr(ctx, entry, "size", JS_NewUint32(ctx, algorithms[i].size));
    JS_SetPropertyStr(ctx, entry, "blockSize", JS_NewUint32(ctx, algorithms[i].block));
    JS_SetPropertyUint32(ctx, list, i, entry);
  }

  JS_SetModuleExportList(ctx, m, js_digest_funcs, countof(js_digest_funcs));
  JS_SetModuleExport(ctx, m, "algorithms", list);

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_digest
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_digest_init))) {
    JS_AddModuleExportList(ctx, m, js_digest_funcs, countof(js_digest_funcs));
    JS_AddModuleExport(ctx, m, "algorithms");
  }

  return m;
}

/**
 * @}
 */
