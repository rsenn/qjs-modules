#include <quickjs.h>
#include "libbcrypt/bcrypt.h"
#include "buffer-utils.h"

static const unsigned int BCRYPT_SALTSIZE = 29;

enum {
  BCRYPT_GENSALT,
  BCRYPT_CHECKPW,
  BCRYPT_HASHPW,
};

static JSValue
js_bcrypt_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret;
  InputBuffer input = js_input_chars(ctx, argv[0]);

  switch(magic) {
    case BCRYPT_GENSALT: {
      uint32_t wf = 12;

      if(JS_IsNumber(argv[0])) {
        JS_ToUint32(ctx, &wf, argv[0]);
        --argc;
        ++argv;
      }

      if(argc > 0) {
        InputBuffer salt = js_input_buffer(ctx, argv[0]);

        if(salt.size < BCRYPT_HASHSIZE)
          return JS_ThrowInternalError(ctx,
                                       "supplied buffer size (%lu) < %d",
                                       (unsigned long)salt.size,
                                       BCRYPT_HASHSIZE);

        ret = JS_NewInt32(ctx, bcrypt_gensalt(wf, (void*)salt.data));

        inputbuffer_free(&salt, ctx);
      } else {
        char s[BCRYPT_HASHSIZE + 1];

        memset(s, 0, sizeof(s));

        if(bcrypt_gensalt(wf, s))
          ret = JS_ThrowInternalError(ctx, "bcrypt_gensalt() failed");
        else
          ret = JS_NewStringLen(ctx, s, strlen(s));
      }

      break;
    }
    case BCRYPT_HASHPW: {
      const char* pw;
      InputBuffer salt = js_input_chars(ctx, argv[1]);
      // InputBuffer buf = js_input_buffer(ctx, argv[2]);
      char tmp[BCRYPT_HASHSIZE], out[BCRYPT_HASHSIZE], *s;

      memset(tmp, 0, sizeof(tmp));
      memset(out, 0, sizeof(out));

      if(!salt.size) {
        int32_t workfactor = 12;
        JS_ToInt32(ctx, &workfactor, argv[1]);
        bcrypt_gensalt(workfactor, s = tmp);
      } else if(salt.size < BCRYPT_SALTSIZE) {
        JS_ThrowInternalError(ctx, "supplied salt size (%lu) < %d", (unsigned long)salt.size, BCRYPT_SALTSIZE);
        inputbuffer_free(&salt, ctx);
        // inputbuffer_free(&buf, ctx);
        return JS_EXCEPTION;
      } else if(salt.size >= BCRYPT_SALTSIZE) {
        s = (void*)salt.data;
      }

      pw = JS_ToCString(ctx, argv[0]);

      if(bcrypt_hashpw(pw, s, out))
        ret = JS_ThrowInternalError(ctx, "bcrypt_hashpw() failed");
      else
        ret = JS_NewStringLen(ctx, out, strlen(out));

      inputbuffer_free(&salt, ctx);
      JS_FreeCString(ctx, pw);
      break;
    }

    case BCRYPT_CHECKPW: {
      InputBuffer buf = js_input_chars(ctx, argv[1]);
      char x[BCRYPT_HASHSIZE];

      if(!buf.size) {
        inputbuffer_free(&buf, ctx);
        return JS_ThrowInternalError(ctx, "supplied buffer size 0");
      }

      if(buf.size < (BCRYPT_HASHSIZE - 4)) {
        inputbuffer_free(&buf, ctx);
        return JS_ThrowInternalError(ctx,
                                     "supplied buffer size %lu < %u",
                                     (unsigned long)buf.size,
                                     BCRYPT_HASHSIZE - 4);
      }

      memset(x, 0, sizeof(x));
      memcpy(x, buf.data, MIN_NUM(buf.size, BCRYPT_HASHSIZE));
      inputbuffer_free(&buf, ctx);

      const char* pw = JS_ToCString(ctx, argv[0]);

      int result = bcrypt_checkpw(pw, x);
      if(result == -1)
        ret = JS_ThrowInternalError(ctx, "bcrypt_checkpw() returned -1");
      else
        ret = JS_NewBool(ctx, !result);

      JS_FreeCString(ctx, pw);
      break;
    }
  }

  return ret;
}

static const JSCFunctionListEntry js_bcrypt_functions[] = {
    JS_CFUNC_MAGIC_DEF("genSalt", 0, js_bcrypt_function, BCRYPT_GENSALT),
    JS_CFUNC_MAGIC_DEF("hash", 1, js_bcrypt_function, BCRYPT_HASHPW),
    JS_CFUNC_MAGIC_DEF("compare", 2, js_bcrypt_function, BCRYPT_CHECKPW),
    JS_PROP_INT32_DEF("HASHSIZE", BCRYPT_HASHSIZE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SALTSIZE", BCRYPT_SALTSIZE, JS_PROP_ENUMERABLE),
};

static int
js_bcrypt_init(JSContext* ctx, JSModuleDef* m) {
  if(m)
    JS_SetModuleExportList(ctx, m, js_bcrypt_functions, countof(js_bcrypt_functions));

  return 0;
}

#if defined(JS_SHARED_LIBRARY) && defined(JS_BCRYPT_MODULE)
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_bcrypt
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_bcrypt_init)))
    JS_AddModuleExportList(ctx, m, js_bcrypt_functions, countof(js_bcrypt_functions));

  return m;
}

/**
 * @}
 */
