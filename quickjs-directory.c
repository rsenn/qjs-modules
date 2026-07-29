#include "defines.h"
#include "getdents.h"
#include "utils.h"
#include "char-utils.h"
#include <errno.h>
#include <string.h>

/**
 * \defgroup quickjs-directory quickjs-directory: Directory reader
 * @{
 */

/*
 * src/getdents.c only implements the getdents_*() API declared in getdents.h
 * for Windows (FindFirstFile/FindNextFile) and Linux/Android (getdents64()/
 * getdents()/__dietlibc__/the raw syscall(SYS_getdents64, ...) fallback,
 * which needs Linux's syscall numbers and struct linux_dirent64 - see BUGS,
 * getdents-emscripten-no-syscall) - src/getdents.c is excluded from the
 * build on every other platform (CMakeLists.txt), so this module provides
 * its own portable opendir()/readdir()/fdopendir() backend instead, for
 * anything that isn't Windows/Linux/Android (macOS, BSD, WASI, Emscripten's
 * musl libc, ...). This is the *only* getdents_*() implementation linked in
 * on those platforms, so src/glob2.c (the other consumer of getdents.h)
 * resolves against it too.
 */
#if !(defined(_WIN32) || defined(__MSYS__) || defined(__CYGWIN__) || defined(__linux__) || defined(__ANDROID__))
#define DIRECTORY_PORTABLE_BACKEND 1
#include <dirent.h>
#include <stdlib.h>

struct getdents_reader {
  DIR* dirp;
  struct dirent* cur;
  int nread; /* number of readdir() calls made so far - mirrors the getdents.c
                Linux backend's d->nread==0 meaning "getdents_read() never
                called yet" (see getdents_initialized() below), since a plain
                DIR* / readdir() has no equivalent byte-buffer state */
};

size_t
getdents_size(void) {
  return sizeof(Directory);
}

void
getdents_clear(Directory* d) {
  d->dirp = 0;
  d->cur = 0;
  d->nread = 0;
}

intptr_t
getdents_handle(Directory* d) {
  return d->dirp ? dirfd(d->dirp) : -1;
}

int
getdents_open(Directory* d, const char* path) {
  getdents_clear(d);

  if(!(d->dirp = opendir(path)))
    return -1;

  return 0;
}

int
getdents_adopt(Directory* d, intptr_t fd) {
  getdents_clear(d);

  if(!(d->dirp = fdopendir((int)fd)))
    return -1;

  return 0;
}

int
getdents_initialized(Directory* d) {
  return d->nread == 0;
}

DirEntry*
getdents_read(Directory* d) {
  d->nread++;
  d->cur = readdir(d->dirp);

  return (DirEntry*)d->cur;
}

const void*
getdents_cname(const DirEntry* e) {
  return ((struct dirent*)e)->d_name;
}

char*
getdents_name(const DirEntry* e) {
  return strdup(getdents_cname(e));
}

const uint8_t*
getdents_namebuf(const DirEntry* e, size_t* len) {
  const char* name = ((struct dirent*)e)->d_name;

  if(len)
    *len = strlen(name);

  return (const uint8_t*)name;
}

void
getdents_close(Directory* d) {
  if(d->dirp)
    closedir(d->dirp);

  d->dirp = 0;
}

int
getdents_isblk(const DirEntry* e) {
  return ((struct dirent*)e)->d_type == DT_BLK;
}

int
getdents_ischr(const DirEntry* e) {
  return ((struct dirent*)e)->d_type == DT_CHR;
}

int
getdents_isdir(const DirEntry* e) {
  return ((struct dirent*)e)->d_type == DT_DIR;
}

int
getdents_isfifo(const DirEntry* e) {
  return ((struct dirent*)e)->d_type == DT_FIFO;
}

int
getdents_islnk(const DirEntry* e) {
  return ((struct dirent*)e)->d_type == DT_LNK;
}

int
getdents_isreg(const DirEntry* e) {
  return ((struct dirent*)e)->d_type == DT_REG;
}

int
getdents_issock(const DirEntry* e) {
  return ((struct dirent*)e)->d_type == DT_SOCK;
}

int
getdents_type(const DirEntry* e) {
  if(getdents_isblk(e))
    return TYPE_BLK;

  if(getdents_ischr(e))
    return TYPE_CHR;

  if(getdents_isdir(e))
    return TYPE_DIR;

  if(getdents_isfifo(e))
    return TYPE_FIFO;

  if(getdents_islnk(e))
    return TYPE_LNK;

  if(getdents_issock(e))
    return TYPE_SOCK;

  if(getdents_isreg(e))
    return TYPE_REG;

  return 0;
}

Directory*
getdents_new(void) {
  Directory* dir;

  if((dir = malloc(sizeof(Directory))))
    getdents_clear(dir);

  return dir;
}

#endif /* DIRECTORY_PORTABLE_BACKEND */

VISIBLE JSClassID js_directory_class_id = 0;
static JSValue directory_proto, directory_ctor;

enum {
  FLAG_NAME = 1,
  FLAG_TYPE = 2,
  FLAG_BOTH = FLAG_NAME | FLAG_TYPE,
  FLAG_BUFFER = 0x80,
};

enum {
  DIRECTORY_OPEN = 0,
  DIRECTORY_ADOPT,
  DIRECTORY_CLOSE,
  DIRECTORY_ITERATOR,
  DIRECTORY_VALUE_OF,
  DIRECTORY_NEXT,
  DIRECTORY_RETURN,
  DIRECTORY_THROW,
};

static JSValue
directory_namebuf(JSContext* ctx, DirEntry* entry) {
  size_t len = 0;
  return JS_NewArrayBufferCopy(ctx, getdents_namebuf(entry, &len), len);
}

static JSValue
directory_namestr(JSContext* ctx, DirEntry* entry) {
#if !(defined(_WIN32) && !defined(__MSYS__))
  return JS_NewString(ctx, getdents_cname(entry));
#else
  JSValue ret = JS_UNDEFINED;
  char* str;

  if((str = getdents_name(entry))) {
    ret = JS_NewString(ctx, str);
    free(str);
  }

  return ret;
#endif
}

static JSValue
js_directory_entry(JSContext* ctx, DirEntry* entry, int dflags) {
  JSValue name, ret;
  int type = -1;

  if(dflags & FLAG_NAME)
    name = (dflags & FLAG_BUFFER) ? directory_namebuf(ctx, entry) : directory_namestr(ctx, entry);

  if(dflags & FLAG_TYPE)
    type = getdents_type(entry);

  switch(dflags) {
    case FLAG_NAME: {
      ret = name;
      break;
    }

    case FLAG_TYPE: {
      ret = JS_NewInt32(ctx, type);
      break;
    }

    case FLAG_BOTH: {
      ret = JS_NewArray(ctx);

      JS_SetPropertyUint32(ctx, ret, 0, name);
      JS_SetPropertyUint32(ctx, ret, 1, JS_NewInt32(ctx, type));
      break;
    }
  }

  return ret;
}

static inline Directory*
js_directory_data(JSValueConst value) {
  return JS_GetOpaque(value, js_directory_class_id);
}

static inline Directory*
js_directory_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_directory_class_id);
}

static JSValue
js_directory_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Directory* directory;
  int32_t* opts;

  if(!(directory = js_malloc(ctx, getdents_size() + sizeof(int32_t) * 2)))
    return JS_EXCEPTION;

  getdents_clear(directory);

  opts = ((int32_t*)((char*)directory + getdents_size()));

  opts[0] = FLAG_BOTH;
  opts[1] = TYPE_MASK;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_directory_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  if(argc > 0) {
    if(JS_IsNumber(argv[0])) {
      int32_t fd = -1;

      JS_ToInt32(ctx, &fd, argv[0]);
      getdents_adopt(directory, fd);
    } else {
      const char* dir;
      dir = JS_ToCString(ctx, argv[0]);

      getdents_open(directory, dir);
      JS_FreeCString(ctx, dir);
    }
  }

  if(argc > 1)
    JS_ToInt32(ctx, &opts[0], argv[1]);

  if(argc > 2)
    JS_ToInt32(ctx, &opts[1], argv[2]);

  JS_SetOpaque(obj, directory);

  return obj;

fail:
  js_free(ctx, directory);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_directory_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Directory* directory;
  JSValue ret = JS_UNDEFINED;

  if(!(directory = js_directory_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case DIRECTORY_OPEN: {
      const char* dir;
      dir = JS_ToCString(ctx, argv[0]);

      if(getdents_open(directory, dir))
        ret = JS_ThrowInternalError(ctx, "getdents_open(%s) failed: %s", dir, strerror(errno));

      JS_FreeCString(ctx, dir);
      break;
    }

    case DIRECTORY_ADOPT: {
      int32_t fd = -1;

      JS_ToInt32(ctx, &fd, argv[0]);

      if(getdents_adopt(directory, fd))
        ret = JS_ThrowInternalError(ctx, "getdents_adopt(%d) failed: %s", fd, strerror(errno));

      break;
    }

    case DIRECTORY_ITERATOR: {
      ret = JS_DupValue(ctx, this_val);
      break;
    }

    case DIRECTORY_CLOSE: {
      getdents_close(directory);
      break;
    }

    case DIRECTORY_VALUE_OF: {
      ret = JS_NewInt64(ctx, getdents_handle(directory));
      break;
    }

    case DIRECTORY_NEXT: {
      DirEntry* entry;
      int32_t* opts = ((int32_t*)((char*)directory + getdents_size()));
      int32_t flags = opts[0], mask = opts[1];
      JSValue value = JS_UNDEFINED;
      BOOL done = FALSE, init = getdents_initialized(directory), empty = TRUE;

      if(argc > 0)
        JS_ToInt32(ctx, &flags, argv[0]);

      if(argc > 1)
        JS_ToInt32(ctx, &mask, argv[1]);

      for(;;) {
        if(!(entry = getdents_read(directory)))
          break;

        if((getdents_type(entry) & mask) == 0)
          continue;

        empty = FALSE;
        value = js_directory_entry(ctx, entry, flags);
        break;
      }

      if(!entry) {
        getdents_close(directory);
        done = TRUE;
      }

      if(init && empty) {
        ret = JS_ThrowInternalError(ctx, "empty directory");
        break;
      }

      ret = js_iterator_result(ctx, value, done);

      JS_FreeValue(ctx, value);
      break;
    }

    case DIRECTORY_RETURN: {
      ret = js_iterator_result(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, TRUE);
      break;
    }

    case DIRECTORY_THROW: {
      ret = JS_Throw(ctx, argv[0]);
      break;
    }
  }

  return ret;
}

static void
js_directory_finalizer(JSRuntime* rt, JSValue val) {
  Directory* directory;

  if((directory = js_directory_data(val))) {
    getdents_close(directory);
    js_free_rt(rt, directory);
  }
}

static JSClassDef js_directory_class = {
    .class_name = "Directory",
    .finalizer = js_directory_finalizer,
};

static const JSCFunctionListEntry js_directory_funcs[] = {
    JS_CFUNC_MAGIC_DEF("open", 1, js_directory_method, DIRECTORY_OPEN),
    JS_CFUNC_MAGIC_DEF("adopt", 1, js_directory_method, DIRECTORY_ADOPT),
    JS_CFUNC_MAGIC_DEF("close", 0, js_directory_method, DIRECTORY_CLOSE),
    JS_CFUNC_MAGIC_DEF("valueOf", 0, js_directory_method, DIRECTORY_VALUE_OF),
    JS_CFUNC_MAGIC_DEF("next", 0, js_directory_method, DIRECTORY_NEXT),
    JS_CFUNC_MAGIC_DEF("return", 0, js_directory_method, DIRECTORY_RETURN),
    JS_CFUNC_MAGIC_DEF("throw", 1, js_directory_method, DIRECTORY_THROW),
    JS_CFUNC_MAGIC_DEF("[Symbol.iterator]", 0, js_directory_method, DIRECTORY_ITERATOR),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Directory", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_directory_static[] = {
    JS_PROP_INT32_DEF("NAME", FLAG_NAME, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE", FLAG_TYPE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("BOTH", FLAG_BOTH, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_BLK", TYPE_BLK, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_CHR", TYPE_CHR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_DIR", TYPE_DIR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_FIFO", TYPE_FIFO, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_LNK", TYPE_LNK, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_REG", TYPE_REG, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_SOCK", TYPE_SOCK, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_MASK", TYPE_MASK, JS_PROP_ENUMERABLE),
};

int
js_directory_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_directory_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_directory_class_id, &js_directory_class);

  directory_ctor = JS_NewCFunction2(ctx, js_directory_constructor, "Directory", 1, JS_CFUNC_constructor, 0);
  JSValue generator_proto = js_generator_prototype(ctx);
  // directory_proto = JS_NewObjectProto(ctx, generator_proto);
  directory_proto = JS_NewObject(ctx);
  JS_FreeValue(ctx, generator_proto);

  JS_SetPropertyFunctionList(ctx, directory_proto, js_directory_funcs, countof(js_directory_funcs));
  JS_SetPropertyFunctionList(ctx, directory_proto, js_directory_static, countof(js_directory_static));
  JS_SetPropertyFunctionList(ctx, directory_ctor, js_directory_static, countof(js_directory_static));

  JS_SetClassProto(ctx, js_directory_class_id, directory_proto);
  JS_SetConstructor(ctx, directory_ctor, directory_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "Directory", directory_ctor);
    JS_SetModuleExport(ctx, m, "default", directory_ctor);
    JS_SetModuleExportList(ctx, m, js_directory_static, countof(js_directory_static));
  }

  return 0;
}

#ifdef JS_DIRECTORY_MODULE
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_directory
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_directory_init))) {
    JS_AddModuleExport(ctx, m, "Directory");
    JS_AddModuleExport(ctx, m, "default");
    JS_AddModuleExportList(ctx, m, js_directory_static, countof(js_directory_static));
  }

  return m;
}

/**
 * @}
 */
