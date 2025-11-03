/*
 * QuickJS: binary JSON module (test only)
 *
 * Copyright (c) 2017-2019 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <quickjs-libc.h>
#include "utils.h"
#include "defines.h"
#include "buffer-utils.h"
#include "debug.h"

/**
 * \defgroup quickjs-bjson quickjs-bjson: Binary JSON
 * @{
 */

static JSValue
js_bjson_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t flags = 0;
  InputBuffer input = js_input_args(ctx, argc, argv);

  if(!input.data)
    return JS_ThrowTypeError(ctx, "argument 1 must be < ArrayBuffer | TypedArray | String >");

  if(argc > 3) {
    if(JS_IsBool(argv[3])) {
      if(JS_ToBool(ctx, argv[3]))
        flags |= JS_READ_OBJ_REFERENCE;
    } else {
      JS_ToInt32(ctx, &flags, argv[3]);
    }
  }

  JSValue obj = JS_ReadObject(ctx, inputbuffer_data(&input), inputbuffer_length(&input), flags);
  inputbuffer_free(&input, ctx);
  return obj;
}

static JSValue
js_bjson_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t flags = 0;
  size_t len;
  uint8_t* buf;

  if(argc > 1) {
    if(JS_IsBool(argv[1])) {
      if(JS_ToBool(ctx, argv[1]))
        flags |= JS_WRITE_OBJ_REFERENCE;
    } else {
      JS_ToInt32(ctx, &flags, argv[1]);
    }
  }

  if(!(buf = JS_WriteObject(ctx, &len, argv[0], flags)))
    return JS_EXCEPTION;

  return JS_NewArrayBuffer(ctx, buf, len, js_arraybuffer_freeptr, 0, FALSE);
}

static const JSCFunctionListEntry js_bjson_funcs[] = {
    JS_CFUNC_DEF("read", 4, js_bjson_read),
    JS_CONSTANT(JS_READ_OBJ_BYTECODE),
    JS_CONSTANT(JS_READ_OBJ_ROM_DATA),
    JS_CONSTANT(JS_READ_OBJ_SAB),
    JS_CONSTANT(JS_READ_OBJ_REFERENCE),
    JS_CFUNC_DEF("write", 2, js_bjson_write),
    JS_CONSTANT(JS_WRITE_OBJ_BYTECODE),
    JS_CONSTANT(JS_WRITE_OBJ_BSWAP),
    JS_CONSTANT(JS_WRITE_OBJ_SAB),
    JS_CONSTANT(JS_WRITE_OBJ_REFERENCE),
};

static int
js_bjson_init(JSContext* ctx, JSModuleDef* m) {
  return JS_SetModuleExportList(ctx, m, js_bjson_funcs, countof(js_bjson_funcs));
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_bjson
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_bjson_init)))
    JS_AddModuleExportList(ctx, m, js_bjson_funcs, countof(js_bjson_funcs));

  return m;
}

/**
 * @}
 */
