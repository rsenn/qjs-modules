#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <quickjs.h>
#include <archive.h>
#include <threads.h>
#include "quickjs-archive.h"
#include "utils.h"

#define max(a, b) ((a) > (b) ? (a) : (b))

thread_local VISIBLE JSClassID js_archive_class_id = 0;
thread_local JSValue archive_proto = {.tag = JS_TAG_UNDEFINED}, archive_ctor = {.tag = JS_TAG_UNDEFINED};

thread_local VISIBLE JSClassID js_archiveentry_class_id = 0;
thread_local JSValue archiveentry_proto = {.tag = JS_TAG_UNDEFINED}, archiveentry_ctor = {.tag = JS_TAG_UNDEFINED};

enum { ARCHIVE_METHOD_READ, ARCHIVE_METHOD_WRITE, ARCHIVE_METHOD_READFILE, ARCHIVE_METHOD_WRITEFILE };
enum { ARCHIVE_PROP_FORMAT, ARCHIVE_PROP_COMPRESSION };

enum {
  ARCHIVEENTRY_METHOD_READ,
  ARCHIVEENTRY_METHOD_WRITE,
  ARCHIVEENTRY_METHOD_READFILE,
  ARCHIVEENTRY_METHOD_WRITEFILE
};
enum {
  ENTRY_ATIME,
  ENTRY_BIRTHTIME,
  ENTRY_CTIME,
  ENTRY_DEV,
  ENTRY_DEVMAJOR,
  ENTRY_DEVMINOR,
  ENTRY_FILETYPE,
  ENTRY_FFLAGS,
  ENTRY_GID,
  ENTRY_GNAME,
  ENTRY_HARDLINK,
  ENTRY_INO,
  ENTRY_INO64,
  ENTRY_LINK,
  ENTRY_MODE,
  ENTRY_MTIME,
  ENTRY_NLINK,
  ENTRY_PATHNAME,
  ENTRY_PERM,
  ENTRY_RDEV,
  ENTRY_RDEVMAJOR,
  ENTRY_RDEVMINOR,
  ENTRY_SIZE,
  ENTRY_SYMLINK,
  ENTRY_UID,
  ENTRY_UNAME
};

static JSValue js_archiveentry_wrap_proto(JSContext* ctx, JSValueConst proto, struct archive_entry* ent);
static JSValue js_archiveentry_wrap(JSContext* ctx, struct archive_entry* ent);

struct archive*
js_archive_data(JSContext* ctx, JSValueConst value) {
  struct archive* ar;
  ar = JS_GetOpaque(value, js_archive_class_id);
  return ar;
}

static JSValue
js_archive_wrap_proto(JSContext* ctx, JSValueConst proto, struct archive* ar) {
  JSValue obj;

  if(js_archive_class_id == 0)
    js_archive_init(ctx, 0);
  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = JS_DupValue(ctx, archive_proto);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_archive_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ar);

  return obj;
fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_archive_wrap(JSContext* ctx, struct archive* ar) {
  return js_archive_wrap_proto(ctx, archive_proto, ar);
}

static JSValue
js_archive_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  struct archive* ar;
  JSValue proto = JS_GetPropertyStr(ctx, this_val, "prototype");
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case ARCHIVE_METHOD_READ: {
      uint32_t block_size = 10240;

      if(!(ar = archive_read_new()))
        return JS_ThrowOutOfMemory(ctx);

      archive_read_support_compression_all(ar);
      archive_read_support_filter_all(ar);
      archive_read_support_format_all(ar);

      if(argc > 1 && JS_IsNumber(argv[1])) {
        JS_ToUint32(ctx, &block_size, argv[1]);
      }
      if(argc > 0 && JS_IsString(argv[0])) {
        wchar_t* filename = js_towstring(ctx, argv[0]);
        int r = archive_read_open_filename_w(ar, filename, block_size);
        js_free(ctx, filename);

        if(r == ARCHIVE_OK) {
          ret = js_archive_wrap_proto(ctx, proto, ar);
        } else {
          ret = JS_ThrowInternalError(ctx, archive_error_string(ar));
          archive_read_free(ar);
        }
      }
      break;
    }
    case ARCHIVE_METHOD_WRITE: {
      if(!(ar = archive_write_new()))
        return JS_ThrowOutOfMemory(ctx);
      break;
    }
  }

  return ret;
}

static JSValue
js_archive_getter(JSContext* ctx, JSValueConst this_val, int magic) {
  struct archive* ar;
  JSValue ret = JS_UNDEFINED;

  if(!(ar = js_archive_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case ARCHIVE_PROP_FORMAT: {
      ret = JS_NewString(ctx, archive_format_name(ar));
      break;
    }
    case ARCHIVE_PROP_COMPRESSION: {
      ret = JS_NewString(ctx, archive_compression_name(ar));
      break;
    }
  }
  return ret;
}

static JSValue
js_archive_setter(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  struct archive* ar;
  JSValue ret = JS_UNDEFINED;

  if(!(ar = js_archive_data(ctx, this_val)))
    return ret;

  switch(magic) {}
  return ret;
}

static JSValue
js_archive_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  return js_archive_wrap_proto(ctx, proto, 0);

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_archive_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  JSValue ret = JS_UNDEFINED;
  struct archive* ar;
  struct archive_entry* ent;
  int result;

  if(!(ar = js_archive_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(ent = archive_entry_new2(ar)))
    return JS_ThrowOutOfMemory(ctx);

  result = archive_read_next_header2(ar, ent);

  switch(result) {
    case ARCHIVE_EOF: *pdone = TRUE; return JS_UNDEFINED;
    case ARCHIVE_FATAL: *pdone = TRUE; return JS_ThrowInternalError(ctx, archive_error_string(ar));
  }

  if(result == ARCHIVE_WARN) {
    fprintf(stderr, "WARNING: %s\n", archive_error_string(ar));
    archive_clear_error(ar);
  }

  return js_archiveentry_wrap(ctx, ent);
}

static JSValue
js_archive_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

static void
js_archive_finalizer(JSRuntime* rt, JSValue val) {
  struct archive* ar = JS_GetOpaque(val, js_archive_class_id);
  if(ar) {
    archive_free(ar);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_archive_class = {
    .class_name = "Archive",
    .finalizer = js_archive_finalizer,
};

static const JSCFunctionListEntry js_archive_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_archive_next, 0),
    JS_CGETSET_MAGIC_DEF("format", js_archive_getter, 0, ARCHIVE_PROP_FORMAT),
    JS_CGETSET_MAGIC_DEF("compression", js_archive_getter, 0, ARCHIVE_PROP_COMPRESSION),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_archive_iterator),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Archive", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_archive_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("read", 1, js_archive_functions, ARCHIVE_METHOD_READ),
    JS_CFUNC_MAGIC_DEF("write", 1, js_archive_functions, ARCHIVE_METHOD_WRITE),
};

struct archive_entry*
js_archiveentry_data(JSContext* ctx, JSValueConst value) {
  struct archive_entry* ent;
  ent = JS_GetOpaque(value, js_archiveentry_class_id);
  return ent;
}

static JSValue
js_archiveentry_wrap_proto(JSContext* ctx, JSValueConst proto, struct archive_entry* ent) {
  JSValue obj;

  if(js_archive_class_id == 0)
    js_archive_init(ctx, 0);

  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = JS_DupValue(ctx, archiveentry_proto);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_archiveentry_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ent);

  return obj;
fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_archiveentry_wrap(JSContext* ctx, struct archive_entry* ent) {
  return js_archiveentry_wrap_proto(ctx, archiveentry_proto, ent);
}

static JSValue
js_archiveentry_getter(JSContext* ctx, JSValueConst this_val, int magic) {
  struct archive_entry* ent;
  JSValue ret = JS_UNDEFINED;

  if(!(ent = js_archiveentry_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case ENTRY_ATIME: {
      if(archive_entry_atime_is_set(ent))
        ret = js_date_from_time_ns(ctx, archive_entry_atime(ent), archive_entry_atime_nsec(ent));
      break;
    }
    case ENTRY_CTIME: {
      if(archive_entry_ctime_is_set(ent))
        ret = js_date_from_time_ns(ctx, archive_entry_ctime(ent), archive_entry_ctime_nsec(ent));
      break;
    }
    case ENTRY_MTIME: {
      if(archive_entry_mtime_is_set(ent))
        ret = js_date_from_time_ns(ctx, archive_entry_mtime(ent), archive_entry_mtime_nsec(ent));
      break;
    }
    case ENTRY_BIRTHTIME: {
      if(archive_entry_birthtime_is_set(ent))
        ret = js_date_from_time_ns(ctx, archive_entry_birthtime(ent), archive_entry_birthtime_nsec(ent));
      break;
    }
    case ENTRY_DEV: {
      if(archive_entry_dev_is_set(ent))
        ret = JS_NewInt64(ctx, archive_entry_dev(ent));
      break;
    }
    case ENTRY_DEVMAJOR: {
      if(archive_entry_dev_is_set(ent))
        ret = JS_NewInt64(ctx, archive_entry_devmajor(ent));
      break;
    }
    case ENTRY_DEVMINOR: {
      if(archive_entry_dev_is_set(ent))
        ret = JS_NewInt64(ctx, archive_entry_devminor(ent));
      break;
    }
    case ENTRY_FILETYPE: {
      ret = JS_NewInt64(ctx, archive_entry_filetype(ent));
      break;
    }
    case ENTRY_FFLAGS: {
      ret = JS_NewString(ctx, archive_entry_fflags_text(ent));
      break;
    }
    case ENTRY_GID: {
      ret = JS_NewInt64(ctx, archive_entry_gid(ent));
      break;
    }
    case ENTRY_GNAME: {
      ret = JS_NewString(ctx, archive_entry_gname_utf8(ent));
      break;
    }
    case ENTRY_HARDLINK: {
      ret = JS_NewString(ctx, archive_entry_hardlink_utf8(ent));
      break;
    }
    case ENTRY_INO: {
      if(archive_entry_ino_is_set(ent))
        ret = JS_NewInt64(ctx, archive_entry_ino(ent));
      break;
    }
    case ENTRY_LINK: {
      // ret = JS_NewString(ctx, archive_entry_link_utf8(ent));
      break;
    }
    case ENTRY_MODE: {
      ret = JS_NewUint32(ctx, archive_entry_mode(ent));
      break;
    }
    case ENTRY_NLINK: {
      ret = JS_NewUint32(ctx, archive_entry_nlink(ent));
      break;
    }
    case ENTRY_PATHNAME: {
      ret = JS_NewString(ctx, archive_entry_pathname_utf8(ent));
      break;
    }
    case ENTRY_PERM: {
      ret = JS_NewUint32(ctx, archive_entry_perm(ent));
      break;
    }
    case ENTRY_RDEV: {
      ret = JS_NewInt64(ctx, archive_entry_rdev(ent));
      break;
    }
    case ENTRY_RDEVMAJOR: {
      ret = JS_NewInt64(ctx, archive_entry_rdevmajor(ent));
      break;
    }
    case ENTRY_RDEVMINOR: {
      ret = JS_NewInt64(ctx, archive_entry_rdevminor(ent));
      break;
    }
    case ENTRY_SIZE: {
      if(archive_entry_size_is_set(ent))
        ret = JS_NewInt64(ctx, archive_entry_size(ent));
      break;
    }
    case ENTRY_SYMLINK: {
      ret = JS_NewString(ctx, archive_entry_symlink_utf8(ent));
      break;
    }
    case ENTRY_UID: {
      ret = JS_NewInt64(ctx, archive_entry_uid(ent));
      break;
    }
    case ENTRY_UNAME: {
      ret = JS_NewString(ctx, archive_entry_uname_utf8(ent));
      break;
    }
  }
  return ret;
}

static JSValue
js_archiveentry_setter(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  struct archive_entry* ent;
  JSValue ret = JS_UNDEFINED;

  if(!(ent = js_archiveentry_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case ENTRY_ATIME: {
      if(js_is_nullish(ctx, value)) {
        archive_entry_unset_atime(ent);
      } else {
        struct timespec ts = js_date_timespec(ctx, value);
        archive_entry_set_atime(ent, ts.tv_sec, ts.tv_nsec);
      }
      break;
    }
    case ENTRY_CTIME: {
      if(js_is_nullish(ctx, value)) {
        archive_entry_unset_ctime(ent);
      } else {
        struct timespec ts = js_date_timespec(ctx, value);
        archive_entry_set_ctime(ent, ts.tv_sec, ts.tv_nsec);
      }
      break;
    }
    case ENTRY_MTIME: {
      if(js_is_nullish(ctx, value)) {
        archive_entry_unset_mtime(ent);
      } else {
        struct timespec ts = js_date_timespec(ctx, value);
        archive_entry_set_mtime(ent, ts.tv_sec, ts.tv_nsec);
      }
      break;
    }
    case ENTRY_BIRTHTIME: {
      if(js_is_nullish(ctx, value)) {
        archive_entry_unset_birthtime(ent);
      } else {
        struct timespec ts = js_date_timespec(ctx, value);
        archive_entry_set_birthtime(ent, ts.tv_sec, ts.tv_nsec);
      }
      break;
    }
    case ENTRY_DEV: {
      int64_t n;
      if(!JS_ToInt64(ctx, &n, value))
        archive_entry_set_dev(ent, n);
      break;
    }
    case ENTRY_DEVMAJOR: {
      int64_t n;
      if(!JS_ToInt64(ctx, &n, value))
        archive_entry_set_devmajor(ent, n);
      break;
    }
    case ENTRY_DEVMINOR: {
      int64_t n;
      if(!JS_ToInt64(ctx, &n, value))
        archive_entry_set_devminor(ent, n);
      break;
    }
    case ENTRY_FILETYPE: {
      uint32_t n;
      if(!JS_ToUint32(ctx, &n, value))
        archive_entry_set_filetype(ent, n);
      break;
    }
    case ENTRY_FFLAGS: {
      if(JS_IsString(value)) {
        const char* str = JS_ToCString(ctx, value);
        archive_entry_copy_fflags_text(ent, str);
        JS_FreeCString(ctx, str);
      } else if(JS_IsArray(ctx, value)) {
        JSValue arr[2] = {JS_GetPropertyUint32(ctx, value, 0), JS_GetPropertyUint32(ctx, value, 1)};

        uint32_t set = 0, clr = 0;
        if(!JS_ToUint32(ctx, &set, arr[0]) && !JS_ToUint32(ctx, &clr, arr[1]))
          archive_entry_set_fflags(ent, set, clr);
        JS_FreeValue(ctx, arr[0]);
        JS_FreeValue(ctx, arr[1]);
      }
      break;
    }
    case ENTRY_GID: {
      int64_t n;
      if(!JS_ToInt64(ctx, &n, value))
        archive_entry_set_gid(ent, n);
      break;
    }
    case ENTRY_GNAME: {
      const char* str = JS_ToCString(ctx, value);
      archive_entry_set_gname_utf8(ent, str);
      JS_FreeCString(ctx, str);
      break;
    }
    case ENTRY_HARDLINK: {
      const char* str = JS_ToCString(ctx, value);
      archive_entry_set_hardlink_utf8(ent, str);
      JS_FreeCString(ctx, str);
      break;
    }
    case ENTRY_INO: {
      int64_t n;
      if(!JS_ToInt64(ctx, &n, value))
        archive_entry_set_ino(ent, n);
      break;
    }
    case ENTRY_LINK: {
      const char* str = JS_ToCString(ctx, value);
      archive_entry_set_link_utf8(ent, str);
      JS_FreeCString(ctx, str);
      break;
    }
    case ENTRY_MODE: {
      uint32_t n;
      if(!JS_ToUint32(ctx, &n, value))
        archive_entry_set_mode(ent, n);
      break;
    }
    case ENTRY_NLINK: {
      uint32_t n;
      if(!JS_ToUint32(ctx, &n, value))
        archive_entry_set_nlink(ent, n);
      break;
    }
    case ENTRY_PATHNAME: {
      const char* str = JS_ToCString(ctx, value);
      archive_entry_set_pathname_utf8(ent, str);
      JS_FreeCString(ctx, str);
      break;
    }
    case ENTRY_PERM: {
      uint32_t n;
      if(!JS_ToUint32(ctx, &n, value))
        archive_entry_set_perm(ent, n);
      break;
    }
    case ENTRY_RDEV: {
      int64_t n;
      if(!JS_ToInt64(ctx, &n, value))
        archive_entry_set_rdev(ent, n);
      break;
    }
    case ENTRY_RDEVMAJOR: {
      int64_t n;
      if(!JS_ToInt64(ctx, &n, value))
        archive_entry_set_rdevmajor(ent, n);
      break;
    }
    case ENTRY_RDEVMINOR: {
      int64_t n;
      if(!JS_ToInt64(ctx, &n, value))
        archive_entry_set_rdevminor(ent, n);
      break;
    }
    case ENTRY_SIZE: {
      if(js_is_nullish(ctx, value)) {
        archive_entry_unset_size(ent);
      } else {
        int64_t n;
        if(!JS_ToInt64(ctx, &n, value))
          archive_entry_set_size(ent, n);
      }
      break;
    }
    case ENTRY_SYMLINK: {
      const char* str = JS_ToCString(ctx, value);
      archive_entry_set_symlink_utf8(ent, str);
      JS_FreeCString(ctx, str);
      break;
    }
    case ENTRY_UID: {
      int64_t n;
      if(!JS_ToInt64(ctx, &n, value))
        archive_entry_set_uid(ent, n);
      break;
    }
    case ENTRY_UNAME: {
      const char* str = JS_ToCString(ctx, value);
      archive_entry_set_uname_utf8(ent, str);
      JS_FreeCString(ctx, str);
      break;
    }
  }
  return ret;
}

static JSValue
js_archiveentry_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  return js_archiveentry_wrap_proto(ctx, proto, 0);

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_archiveentry_finalizer(JSRuntime* rt, JSValue val) {
  struct archive_entry* ent = JS_GetOpaque(val, js_archiveentry_class_id);
  if(ent) {
    archive_entry_free(ent);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_archiveentry_class = {
    .class_name = "ArchiveEntry",
    .finalizer = js_archiveentry_finalizer,
};

static const JSCFunctionListEntry js_archiveentry_funcs[] = {
    JS_CGETSET_MAGIC_DEF("atime", js_archiveentry_getter, js_archiveentry_setter, ENTRY_ATIME),
    JS_CGETSET_MAGIC_DEF("ctime", js_archiveentry_getter, js_archiveentry_setter, ENTRY_CTIME),
    JS_CGETSET_MAGIC_DEF("mtime", js_archiveentry_getter, js_archiveentry_setter, ENTRY_MTIME),
    JS_CGETSET_MAGIC_DEF("birthtime", js_archiveentry_getter, js_archiveentry_setter, ENTRY_BIRTHTIME),
    JS_CGETSET_MAGIC_DEF("dev", js_archiveentry_getter, js_archiveentry_setter, ENTRY_DEV),
    JS_CGETSET_MAGIC_DEF("devmajor", js_archiveentry_getter, js_archiveentry_setter, ENTRY_DEVMAJOR),
    JS_CGETSET_MAGIC_DEF("devminor", js_archiveentry_getter, js_archiveentry_setter, ENTRY_DEVMINOR),
    JS_CGETSET_MAGIC_DEF("rdev", js_archiveentry_getter, js_archiveentry_setter, ENTRY_RDEV),
    JS_CGETSET_MAGIC_DEF("rdevmajor", js_archiveentry_getter, js_archiveentry_setter, ENTRY_RDEVMAJOR),
    JS_CGETSET_MAGIC_DEF("rdevminor", js_archiveentry_getter, js_archiveentry_setter, ENTRY_RDEVMINOR),
    JS_CGETSET_MAGIC_DEF("filetype", js_archiveentry_getter, js_archiveentry_setter, ENTRY_FILETYPE),
    JS_CGETSET_MAGIC_DEF("fflags", js_archiveentry_getter, js_archiveentry_setter, ENTRY_FFLAGS),
    JS_CGETSET_MAGIC_DEF("uid", js_archiveentry_getter, js_archiveentry_setter, ENTRY_UID),
    JS_CGETSET_MAGIC_DEF("gid", js_archiveentry_getter, js_archiveentry_setter, ENTRY_GID),
    JS_CGETSET_MAGIC_DEF("ino", js_archiveentry_getter, js_archiveentry_setter, ENTRY_INO),
    JS_ALIAS_DEF("ino64", "ino"),
    JS_CGETSET_MAGIC_DEF("nlink", js_archiveentry_getter, js_archiveentry_setter, ENTRY_NLINK),
    JS_CGETSET_MAGIC_DEF("pathname", js_archiveentry_getter, js_archiveentry_setter, ENTRY_PATHNAME),
    JS_CGETSET_MAGIC_DEF("uname", js_archiveentry_getter, js_archiveentry_setter, ENTRY_UNAME),
    JS_CGETSET_MAGIC_DEF("gname", js_archiveentry_getter, js_archiveentry_setter, ENTRY_GNAME),
    JS_CGETSET_MAGIC_DEF("mode", js_archiveentry_getter, js_archiveentry_setter, ENTRY_MODE),
    JS_CGETSET_MAGIC_DEF("perm", js_archiveentry_getter, js_archiveentry_setter, ENTRY_PERM),
    JS_CGETSET_MAGIC_DEF("size", js_archiveentry_getter, js_archiveentry_setter, ENTRY_SIZE),
    JS_CGETSET_MAGIC_DEF("symlink", js_archiveentry_getter, js_archiveentry_setter, ENTRY_SYMLINK),
    JS_CGETSET_MAGIC_DEF("hardlink", js_archiveentry_getter, js_archiveentry_setter, ENTRY_HARDLINK),
    JS_CGETSET_MAGIC_DEF("link", js_archiveentry_getter, js_archiveentry_setter, ENTRY_LINK),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ArchiveEntry", JS_PROP_CONFIGURABLE),
};

int
js_archive_init(JSContext* ctx, JSModuleDef* m) {

  if(js_archive_class_id == 0) {
    JS_NewClassID(&js_archive_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_archive_class_id, &js_archive_class);

    archive_ctor = JS_NewCFunction2(ctx, js_archive_constructor, "Archive", 1, JS_CFUNC_constructor, 0);
    archive_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, archive_proto, js_archive_funcs, countof(js_archive_funcs));
    JS_SetPropertyFunctionList(ctx, archive_ctor, js_archive_static_funcs, countof(js_archive_static_funcs));
    JS_SetClassProto(ctx, js_archive_class_id, archive_proto);

    JS_NewClassID(&js_archiveentry_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_archiveentry_class_id, &js_archiveentry_class);

    archiveentry_ctor = JS_NewCFunction2(ctx, js_archiveentry_constructor, "ArchiveEntry", 1, JS_CFUNC_constructor, 0);
    archiveentry_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, archiveentry_proto, js_archiveentry_funcs, countof(js_archiveentry_funcs));
    JS_SetClassProto(ctx, js_archiveentry_class_id, archiveentry_proto);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "Archive", archive_ctor);
    JS_SetModuleExport(ctx, m, "ArchiveEntry", archiveentry_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_archive
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  if(!(m = JS_NewCModule(ctx, module_name, &js_archive_init)))
    return m;
  JS_AddModuleExport(ctx, m, "Archive");
  return m;
}
