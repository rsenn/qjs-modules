#include <quickjs.h>
#include "libbcrypt/bcrypt.h"
#include "buffer-utils.h"

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

        input_buffer_free(&salt, ctx);
      } else {
        char s[BCRYPT_HASHSIZE + 1];

        memset(s, 0, sizeof(s));

        ret = bcrypt_gensalt(wf, s) ? JS_NULL : JS_NewStringLen(ctx, s, strlen(s));
      }

      break;
    }
    case BCRYPT_HASHPW: {
      const char* pw;
      InputBuffer salt = js_input_chars(ctx, argv[1]);
      InputBuffer buf = js_input_buffer(ctx, argv[2]);
      char tmp[BCRYPT_HASHSIZE], *s;

      if(!salt.size) {
        bcrypt_gensalt(12, s = tmp);
      }  else if(salt.size < BCRYPT_HASHSIZE) {
        JS_ThrowInternalError(ctx, "supplied salt size (%lu) < %d", (unsigned long)salt.size, BCRYPT_HASHSIZE);
        input_buffer_free(&salt, ctx);
        input_buffer_free(&buf, ctx);
        return JS_EXCEPTION;
      } else if(salt.size >= BCRYPT_HASHSIZE) {
        s = salt.data;
      }

      if(buf.size < BCRYPT_HASHSIZE) {
        JS_ThrowInternalError(ctx, "supplied buffer size (%lu) < %d", (unsigned long)buf.size, BCRYPT_HASHSIZE);
        input_buffer_free(&salt, ctx);
        input_buffer_free(&buf, ctx);
        return JS_EXCEPTION;
      }

      pw = JS_ToCString(ctx, argv[0]);

      ret = JS_NewInt32(ctx, bcrypt_hashpw(pw, s, (char*)buf.data));

      input_buffer_free(&salt, ctx);
      input_buffer_free(&buf, ctx);
      JS_FreeCString(ctx, pw);
      break;
    }

    case BCRYPT_CHECKPW: {
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
      break;
    }
  }

  return ret;
}

static const JSCFunctionListEntry js_bcrypt_functions[] = {
    JS_CFUNC_MAGIC_DEF("gensalt", 0, js_bcrypt_function, BCRYPT_GENSALT),
    JS_CFUNC_MAGIC_DEF("hashpw", 1, js_bcrypt_function, BCRYPT_HASHPW),
    JS_CFUNC_MAGIC_DEF("checkpw", 2, js_bcrypt_function, BCRYPT_CHECKPW),
    JS_CONSTANT(BCRYPT_HASHSIZE),
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
