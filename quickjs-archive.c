#include "include/defines.h"
#include <archive.h>
#include <archive_entry.h>
#include "quickjs-archive.h"
#include "include/utils.h"
#include "include/debug.h"

/**
 * \addtogroup quickjs-archive
 * @{
 */

#define max(a, b) ((a) > (b) ? (a) : (b))

thread_local VISIBLE JSClassID js_archive_class_id = 0;
thread_local JSValue archive_proto = {{JS_TAG_UNDEFINED}}, archive_ctor = {{JS_TAG_UNDEFINED}};

thread_local VISIBLE JSClassID js_archiveentry_class_id = 0;
thread_local JSValue archiveentry_proto = {{JS_TAG_UNDEFINED}}, archiveentry_ctor = {{JS_TAG_UNDEFINED}};

enum {
  ARCHIVE_METHOD_READ,
  ARCHIVE_METHOD_WRITE,
  ARCHIVE_METHOD_READFILE,
  ARCHIVE_METHOD_WRITEFILE,
};
enum {
  ARCHIVE_PROP_FORMAT,
  ARCHIVE_PROP_COMPRESSION,
  ARCHIVE_PROP_FILTERS,
  ARCHIVE_PROP_FILECOUNT,
};

enum {
  ARCHIVEENTRY_METHOD_READ,
  ARCHIVEENTRY_METHOD_WRITE,
  ARCHIVEENTRY_METHOD_READFILE,
  ARCHIVEENTRY_METHOD_WRITEFILE,
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

struct ArchiveInstance {
  JSValue archive;
};
struct ArchiveEntryRef {
  JSContext* ctx;
  JSValueConst callback, args[2];
};

static void
js_archive_free_buffer(JSRuntime* rt, void* opaque, void* ptr) {
  struct ArchiveInstance* ainst = opaque;
  JS_FreeValueRT(rt, ainst->archive);
  js_free_rt(rt, ainst);
}

static void
js_archive_progress_callback(void* opaque) {
  struct ArchiveEntryRef* aeref = opaque;

  JSValue ret = JS_Call(aeref->ctx, aeref->callback, JS_UNDEFINED, 2, aeref->args);
  JS_FreeValue(aeref->ctx, ret);
}

struct archive*
js_archive_data(JSContext* ctx, JSValueConst value) {
  struct archive* ar;
  ar = JS_GetOpaque2(ctx, value, js_archive_class_id);
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
  struct archive* ar = 0;
  JSValue proto = JS_GetPropertyStr(ctx, this_val, "prototype");
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case ARCHIVE_METHOD_READ: {
      uint32_t block_size = 10240;

      if(!(ar = archive_read_new()))
        return JS_ThrowOutOfMemory(ctx);

      // archive_read_support_compression_all(ar);
      archive_read_support_format_all(ar);
      archive_read_support_filter_all(ar);

      if(argc > 1 && JS_IsNumber(argv[1])) {
        JS_ToUint32(ctx, &block_size, argv[1]);
      }
      if(JS_IsString(argv[0])) {
        wchar_t* filename = js_towstring(ctx, argv[0]);
        int r = archive_read_open_filename_w(ar, filename, block_size);
        js_free(ctx, filename);

        if(r != ARCHIVE_OK) {
          ret = JS_ThrowInternalError(ctx, "libarchive error: %s", archive_error_string(ar));
          archive_read_free(ar);
          return ret;
        }
      }

      ret = js_archive_wrap_proto(ctx, proto, ar);
      break;
    }
    case ARCHIVE_METHOD_WRITE: {
      if(!(ar = archive_write_new()))
        return JS_ThrowOutOfMemory(ctx);

      if(JS_IsString(argv[0])) {
        wchar_t* filename = js_towstring(ctx, argv[0]);
        int r = archive_write_open_filename_w(ar, filename);
        js_free(ctx, filename);

        if(r != ARCHIVE_OK) {
          ret = JS_ThrowInternalError(ctx, "libarchive error: %s", archive_error_string(ar));
          archive_read_free(ar);
          return ret;
        }
      }
      ret = js_archive_wrap_proto(ctx, proto, ar);
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
      ret = JS_NewString(ctx, archive_filter_name(ar, 0));
      break;
    }
    case ARCHIVE_PROP_FILTERS: {
      int i, num_filters = archive_filter_count(ar);
      ret = JS_NewArray(ctx);
      for(i = 0; i < num_filters; i++) { JS_SetPropertyUint32(ctx, ret, i, JS_NewString(ctx, archive_filter_name(ar, i))); }
      break;
    }
    case ARCHIVE_PROP_FILECOUNT: {
      ret = JS_NewUint32(ctx, archive_file_count(ar));
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
    case ARCHIVE_FATAL: *pdone = TRUE; return JS_ThrowInternalError(ctx, "libarchive error: %s", archive_error_string(ar));
  }

  if(result == ARCHIVE_WARN) {
    fprintf(stderr, "WARNING: %s\n", archive_error_string(ar));
    archive_clear_error(ar);
  }

  *pdone = FALSE;

  return js_archiveentry_wrap(ctx, ent);
}

static JSValue
js_archive_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  struct archive* ar;
  uint8_t* ptr;
  size_t len;
  ssize_t r;
  size_t offset = 0, length = 0;

  if(!(ar = js_archive_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(argc < 1 || !js_is_arraybuffer(ctx, argv[0])) {
    void* data;
    size_t size;
    __LA_INT64_T offset;
    switch(archive_read_data_block(ar, (const void**)&data, &size, &offset)) {
      case ARCHIVE_OK: {
        struct ArchiveInstance* abuf = js_malloc(ctx, sizeof(struct ArchiveInstance));
        abuf->archive = JS_DupValue(ctx, this_val);
        ret = JS_NewArrayBuffer(ctx, data, size, js_archive_free_buffer, abuf, FALSE);
        break;
      }
      case ARCHIVE_EOF: {
        ret = JS_NULL;
        break;
      }
      case ARCHIVE_FATAL: {
        ret = JS_ThrowInternalError(ctx, "libarchive error: %s", archive_error_string(ar));
        break;
      }
    }

    return ret;
  }

  if(!(ptr = JS_GetArrayBuffer(ctx, &len, argv[0])))
    return JS_ThrowInternalError(ctx, "Failed getting ArrayBuffer data");

  if(argc >= 2 && JS_IsNumber(argv[1])) {
    JS_ToIndex(ctx, &offset, argv[1]);
    if(offset > len)
      offset = len;
  }

  length = len - offset;

  if(argc >= 3 && JS_IsNumber(argv[2])) {
    JS_ToIndex(ctx, &length, argv[2]);
    if(length > (len - offset))
      length = (len - offset);
  }

  if((r = archive_read_data(ar, ptr + offset, length)) >= 0)
    ret = JS_NewInt64(ctx, r);
  else
    ret = JS_ThrowInternalError(ctx, "libarchive error: %s", archive_error_string(ar));
  return ret;
}

static JSValue
js_archive_seek(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  struct archive* ar;
  int64_t offset = 0;
  int32_t whence = 0;

  if(!(ar = js_archive_data(ctx, this_val)))
    return JS_EXCEPTION;

  JS_ToInt64(ctx, &offset, argv[0]);
  JS_ToInt32(ctx, &whence, argv[1]);

  ret = JS_NewInt64(ctx, archive_seek_data(ar, offset, whence));
  return ret;
}

static JSValue
js_archive_extract(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  struct archive* ar;
  struct archive_entry* ent;
  int32_t flags;
  struct ArchiveEntryRef* aeref = 0;

  if(!(ar = js_archive_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(ent = js_archiveentry_data(ctx, argv[0])))
    return JS_EXCEPTION;

  if(argc >= 2)
    JS_ToInt32(ctx, &flags, argv[1]);

  if(argc >= 3) {
    if(!(aeref = js_malloc(ctx, sizeof(struct ArchiveEntryRef))))
      return JS_ThrowOutOfMemory(ctx);
    aeref->ctx = ctx;
    aeref->callback = argv[2];
    aeref->args[0] = this_val;
    aeref->args[1] = argv[0];

    archive_read_extract_set_progress_callback(ar, js_archive_progress_callback, aeref);
  }

  ret = JS_NewInt32(ctx, archive_read_extract(ar, ent, flags));

  if(aeref) {
    archive_read_extract_set_progress_callback(ar, 0, 0);
    js_free(ctx, aeref);
  }

  return ret;
}

static JSValue
js_archive_filterbytes(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  struct archive* ar;
  int32_t index = -1;

  if(!(ar = js_archive_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(argc >= 1)
    JS_ToInt32(ctx, &index, argv[0]);

  ret = JS_NewInt64(ctx, archive_filter_bytes(ar, index));

  return ret;
}

static JSValue
js_archive_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  struct archive* ar;

  if(!(ar = js_archive_data(ctx, this_val)))
    return JS_EXCEPTION;

  ret = JS_NewInt32(ctx, archive_read_close(ar));

  return ret;
}

static JSValue
js_archive_version(JSContext* ctx, JSValueConst this_val) {
  return JS_NewString(ctx, archive_version_details());
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
    JS_CGETSET_MAGIC_DEF("filters", js_archive_getter, 0, ARCHIVE_PROP_FILTERS),
    JS_CGETSET_MAGIC_DEF("fileCount", js_archive_getter, 0, ARCHIVE_PROP_FILECOUNT),
    JS_CFUNC_DEF("read", 1, js_archive_read),
    JS_CFUNC_DEF("seek", 2, js_archive_seek),
    JS_CFUNC_DEF("extract", 1, js_archive_extract),
    JS_CFUNC_DEF("filterBytes", 1, js_archive_filterbytes),
    JS_CFUNC_DEF("close", 0, js_archive_close),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_archive_iterator),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Archive", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_archive_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("read", 1, js_archive_functions, ARCHIVE_METHOD_READ),
    JS_CFUNC_MAGIC_DEF("write", 1, js_archive_functions, ARCHIVE_METHOD_WRITE),
    JS_PROP_INT32_DEF("SEEK_SET", SEEK_SET, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SEEK_CUR", SEEK_CUR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SEEK_END", SEEK_END, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ARCHIVE_EOF", ARCHIVE_EOF, 0),
    JS_PROP_INT32_DEF("ARCHIVE_OK", ARCHIVE_OK, 0),
    JS_PROP_INT32_DEF("ARCHIVE_RETRY", ARCHIVE_RETRY, 0),
    JS_PROP_INT32_DEF("ARCHIVE_WARN", ARCHIVE_WARN, 0),
    JS_PROP_INT32_DEF("ARCHIVE_FAILED", ARCHIVE_FAILED, 0),
    JS_PROP_INT32_DEF("ARCHIVE_FATAL", ARCHIVE_FATAL, 0),
#ifdef ARCHIVE_FILTER_NONE
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_NONE", ARCHIVE_FILTER_NONE, 0),
#endif
#ifdef ARCHIVE_FILTER_GZIP
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_GZIP", ARCHIVE_FILTER_GZIP, 0),
#endif
#ifdef ARCHIVE_FILTER_BZIP2
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_BZIP2", ARCHIVE_FILTER_BZIP2, 0),
#endif
#ifdef ARCHIVE_FILTER_COMPRESS
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_COMPRESS", ARCHIVE_FILTER_COMPRESS, 0),
#endif
#ifdef ARCHIVE_FILTER_PROGRAM
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_PROGRAM", ARCHIVE_FILTER_PROGRAM, 0),
#endif
#ifdef ARCHIVE_FILTER_LZMA
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_LZMA", ARCHIVE_FILTER_LZMA, 0),
#endif
#ifdef ARCHIVE_FILTER_XZ
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_XZ", ARCHIVE_FILTER_XZ, 0),
#endif
#ifdef ARCHIVE_FILTER_UU
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_UU", ARCHIVE_FILTER_UU, 0),
#endif
#ifdef ARCHIVE_FILTER_RPM
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_RPM", ARCHIVE_FILTER_RPM, 0),
#endif
#ifdef ARCHIVE_FILTER_LZIP
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_LZIP", ARCHIVE_FILTER_LZIP, 0),
#endif
#ifdef ARCHIVE_FILTER_LRZIP
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_LRZIP", ARCHIVE_FILTER_LRZIP, 0),
#endif
#ifdef ARCHIVE_FILTER_LZOP
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_LZOP", ARCHIVE_FILTER_LZOP, 0),
#endif
#ifdef ARCHIVE_FILTER_GRZIP
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_GRZIP", ARCHIVE_FILTER_GRZIP, 0),
#endif
#ifdef ARCHIVE_FILTER_LZ4
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_LZ4", ARCHIVE_FILTER_LZ4, 0),
#endif
#ifdef ARCHIVE_FILTER_ZSTD
    JS_PROP_INT32_DEF("ARCHIVE_FILTER_ZSTD", ARCHIVE_FILTER_ZSTD, 0),
#endif

#ifdef ARCHIVE_FORMAT_BASE_MASK
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_BASE_MASK", ARCHIVE_FORMAT_BASE_MASK, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_CPIO", ARCHIVE_FORMAT_CPIO, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO_POSIX
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_CPIO_POSIX", ARCHIVE_FORMAT_CPIO_POSIX, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_CPIO", ARCHIVE_FORMAT_CPIO, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO_BIN_LE
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_CPIO_BIN_LE", ARCHIVE_FORMAT_CPIO_BIN_LE, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_CPIO", ARCHIVE_FORMAT_CPIO, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO_BIN_BE
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_CPIO_BIN_BE", ARCHIVE_FORMAT_CPIO_BIN_BE, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_CPIO", ARCHIVE_FORMAT_CPIO, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO_SVR4_NOCRC
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_CPIO_SVR4_NOCRC", ARCHIVE_FORMAT_CPIO_SVR4_NOCRC, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_CPIO", ARCHIVE_FORMAT_CPIO, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO_SVR4_CRC
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_CPIO_SVR4_CRC", ARCHIVE_FORMAT_CPIO_SVR4_CRC, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_CPIO", ARCHIVE_FORMAT_CPIO, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO_AFIO_LARGE
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_CPIO_AFIO_LARGE", ARCHIVE_FORMAT_CPIO_AFIO_LARGE, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_CPIO", ARCHIVE_FORMAT_CPIO, 0),
#endif
#ifdef ARCHIVE_FORMAT_SHAR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_SHAR", ARCHIVE_FORMAT_SHAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_SHAR_BASE
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_SHAR_BASE", ARCHIVE_FORMAT_SHAR_BASE, 0),
#endif
#ifdef ARCHIVE_FORMAT_SHAR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_SHAR", ARCHIVE_FORMAT_SHAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_SHAR_DUMP
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_SHAR_DUMP", ARCHIVE_FORMAT_SHAR_DUMP, 0),
#endif
#ifdef ARCHIVE_FORMAT_SHAR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_SHAR", ARCHIVE_FORMAT_SHAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_TAR", ARCHIVE_FORMAT_TAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR_USTAR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_TAR_USTAR", ARCHIVE_FORMAT_TAR_USTAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_TAR", ARCHIVE_FORMAT_TAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE", ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_TAR", ARCHIVE_FORMAT_TAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR_PAX_RESTRICTED
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_TAR_PAX_RESTRICTED", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_TAR", ARCHIVE_FORMAT_TAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR_GNUTAR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_TAR_GNUTAR", ARCHIVE_FORMAT_TAR_GNUTAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_TAR", ARCHIVE_FORMAT_TAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_ISO9660
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_ISO9660", ARCHIVE_FORMAT_ISO9660, 0),
#endif
#ifdef ARCHIVE_FORMAT_ISO9660_ROCKRIDGE
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_ISO9660_ROCKRIDGE", ARCHIVE_FORMAT_ISO9660_ROCKRIDGE, 0),
#endif
#ifdef ARCHIVE_FORMAT_ISO9660
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_ISO9660", ARCHIVE_FORMAT_ISO9660, 0),
#endif
#ifdef ARCHIVE_FORMAT_ZIP
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_ZIP", ARCHIVE_FORMAT_ZIP, 0),
#endif
#ifdef ARCHIVE_FORMAT_EMPTY
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_EMPTY", ARCHIVE_FORMAT_EMPTY, 0),
#endif
#ifdef ARCHIVE_FORMAT_AR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_AR", ARCHIVE_FORMAT_AR, 0),
#endif
#ifdef ARCHIVE_FORMAT_AR_GNU
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_AR_GNU", ARCHIVE_FORMAT_AR_GNU, 0),
#endif
#ifdef ARCHIVE_FORMAT_AR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_AR", ARCHIVE_FORMAT_AR, 0),
#endif
#ifdef ARCHIVE_FORMAT_AR_BSD
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_AR_BSD", ARCHIVE_FORMAT_AR_BSD, 0),
#endif
#ifdef ARCHIVE_FORMAT_AR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_AR", ARCHIVE_FORMAT_AR, 0),
#endif
#ifdef ARCHIVE_FORMAT_MTREE
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_MTREE", ARCHIVE_FORMAT_MTREE, 0),
#endif
#ifdef ARCHIVE_FORMAT_RAW
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_RAW", ARCHIVE_FORMAT_RAW, 0),
#endif
#ifdef ARCHIVE_FORMAT_XAR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_XAR", ARCHIVE_FORMAT_XAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_LHA
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_LHA", ARCHIVE_FORMAT_LHA, 0),
#endif
#ifdef ARCHIVE_FORMAT_CAB
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_CAB", ARCHIVE_FORMAT_CAB, 0),
#endif
#ifdef ARCHIVE_FORMAT_RAR
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_RAR", ARCHIVE_FORMAT_RAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_7ZIP
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_7ZIP", ARCHIVE_FORMAT_7ZIP, 0),
#endif
#ifdef ARCHIVE_FORMAT_WARC
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_WARC", ARCHIVE_FORMAT_WARC, 0),
#endif
#ifdef ARCHIVE_FORMAT_RAR_V5
    JS_PROP_INT32_DEF("ARCHIVE_FORMAT_RAR_V5", ARCHIVE_FORMAT_RAR_V5, 0),
#endif
#ifdef ARCHIVE_READ_FORMAT_CAPS_NONE
    JS_PROP_INT32_DEF("ARCHIVE_READ_FORMAT_CAPS_NONE", ARCHIVE_READ_FORMAT_CAPS_NONE, 0),
#endif
#ifdef ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_DATA
    JS_PROP_INT32_DEF("ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_DATA", ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_DATA, 0),
#endif
#ifdef ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_METADATA
    JS_PROP_INT32_DEF("ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_METADATA", ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_METADATA, 0),
#endif
#ifdef ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED
    JS_PROP_INT32_DEF("ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED", ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED, 0),
#endif
#ifdef ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW
    JS_PROP_INT32_DEF("ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW", ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW, 0),
#endif
#ifdef ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED
    JS_PROP_INT32_DEF("ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED", ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED, 0),
#endif
#ifdef ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW
    JS_PROP_INT32_DEF("ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW", ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW, 0),
#endif
#ifdef ARCHIVE_EXTRACT_OWNER
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_OWNER", ARCHIVE_EXTRACT_OWNER, 0),
#endif
#ifdef ARCHIVE_EXTRACT_PERM
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_PERM", ARCHIVE_EXTRACT_PERM, 0),
#endif
#ifdef ARCHIVE_EXTRACT_TIME
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_TIME", ARCHIVE_EXTRACT_TIME, 0),
#endif
#ifdef ARCHIVE_EXTRACT_NO_OVERWRITE
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_NO_OVERWRITE", ARCHIVE_EXTRACT_NO_OVERWRITE, 0),
#endif
#ifdef ARCHIVE_EXTRACT_UNLINK
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_UNLINK", ARCHIVE_EXTRACT_UNLINK, 0),
#endif
#ifdef ARCHIVE_EXTRACT_ACL
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_ACL", ARCHIVE_EXTRACT_ACL, 0),
#endif
#ifdef ARCHIVE_EXTRACT_FFLAGS
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_FFLAGS", ARCHIVE_EXTRACT_FFLAGS, 0),
#endif
#ifdef ARCHIVE_EXTRACT_XATTR
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_XATTR", ARCHIVE_EXTRACT_XATTR, 0),
#endif
#ifdef ARCHIVE_EXTRACT_UNLINK
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_UNLINK", ARCHIVE_EXTRACT_UNLINK, 0),
#endif
#ifdef ARCHIVE_EXTRACT_SECURE_SYMLINKS
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_SECURE_SYMLINKS", ARCHIVE_EXTRACT_SECURE_SYMLINKS, 0),
#endif
#ifdef ARCHIVE_EXTRACT_SECURE_NODOTDOT
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_SECURE_NODOTDOT", ARCHIVE_EXTRACT_SECURE_NODOTDOT, 0),
#endif
#ifdef ARCHIVE_EXTRACT_NO_AUTODIR
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_NO_AUTODIR", ARCHIVE_EXTRACT_NO_AUTODIR, 0),
#endif
#ifdef ARCHIVE_EXTRACT_NO_OVERWRITE_NEWER
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_NO_OVERWRITE_NEWER", ARCHIVE_EXTRACT_NO_OVERWRITE_NEWER, 0),
#endif
#ifdef ARCHIVE_EXTRACT_SPARSE
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_SPARSE", ARCHIVE_EXTRACT_SPARSE, 0),
#endif
#ifdef ARCHIVE_EXTRACT_MAC_METADATA
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_MAC_METADATA", ARCHIVE_EXTRACT_MAC_METADATA, 0),
#endif
#ifdef ARCHIVE_EXTRACT_NO_HFS_COMPRESSION
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_NO_HFS_COMPRESSION", ARCHIVE_EXTRACT_NO_HFS_COMPRESSION, 0),
#endif
#ifdef ARCHIVE_EXTRACT_HFS_COMPRESSION_FORCED
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_HFS_COMPRESSION_FORCED", ARCHIVE_EXTRACT_HFS_COMPRESSION_FORCED, 0),
#endif
#ifdef ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS", ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS, 0),
#endif
#ifdef ARCHIVE_EXTRACT_CLEAR_NOCHANGE_FFLAGS
    JS_PROP_INT32_DEF("ARCHIVE_EXTRACT_CLEAR_NOCHANGE_FFLAGS", ARCHIVE_EXTRACT_CLEAR_NOCHANGE_FFLAGS, 0),
#endif

    JS_CGETSET_DEF("version", js_archive_version, 0),
};

struct archive_entry*
js_archiveentry_data(JSContext* ctx, JSValueConst value) {
  struct archive_entry* ent;
  ent = JS_GetOpaque2(ctx, value, js_archiveentry_class_id);
  return ent;
}

static JSValue
js_archiveentry_wrap_proto(JSContext* ctx, JSValueConst proto, struct archive_entry* ent) {
  JSValue obj;

  if(js_archive_class_id == 0)
    js_archive_init(ctx, 0);

  if(js_is_nullish(ctx, proto))
    proto = archiveentry_proto;

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
      const char* str;
      if((str = archive_entry_fflags_text(ent)))
        ret = JS_NewString(ctx, str);
      break;
    }
    case ENTRY_GID: {
      ret = JS_NewInt64(ctx, archive_entry_gid(ent));
      break;
    }
    case ENTRY_GNAME: {
      const char* str;
      if((str = archive_entry_gname_utf8(ent)))
        ret = JS_NewString(ctx, str);
      break;
    }
    case ENTRY_HARDLINK: {
      const char* str;
      if((str = archive_entry_hardlink_utf8(ent)))
        ret = JS_NewString(ctx, str);
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
      const char* str;
      if((str = archive_entry_pathname_utf8(ent)))
        ret = JS_NewString(ctx, str);
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
      const char* str;
      if((str = archive_entry_symlink_utf8(ent)))
        ret = JS_NewString(ctx, str);
      break;
    }
    case ENTRY_UID: {
      ret = JS_NewInt64(ctx, archive_entry_uid(ent));
      break;
    }
    case ENTRY_UNAME: {
      const char* str;
      if((str = archive_entry_uname_utf8(ent)))
        ret = JS_NewString(ctx, str);
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
      } else {
        archive_entry_copy_fflags_text(ent, 0);
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
      if(js_is_nullish(ctx, value)) {
        archive_entry_set_gname_utf8(ent, 0);
      } else {
        const char* str = JS_ToCString(ctx, value);
        archive_entry_set_gname_utf8(ent, str);
        JS_FreeCString(ctx, str);
      }
      break;
    }
    case ENTRY_HARDLINK: {
      if(js_is_nullish(ctx, value)) {
        archive_entry_set_hardlink_utf8(ent, 0);
      } else {
        const char* str = JS_ToCString(ctx, value);
        archive_entry_set_hardlink_utf8(ent, str);
        JS_FreeCString(ctx, str);
      }
      break;
    }
    case ENTRY_INO: {
      int64_t n;
      if(!JS_ToInt64(ctx, &n, value))
        archive_entry_set_ino(ent, n);
      break;
    }
    case ENTRY_LINK: {
      if(js_is_nullish(ctx, value)) {
        archive_entry_set_link_utf8(ent, 0);
      } else {
        const char* str = JS_ToCString(ctx, value);
        archive_entry_set_link_utf8(ent, str);
        JS_FreeCString(ctx, str);
      }
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
      if(js_is_nullish(ctx, value)) {
        archive_entry_set_pathname_utf8(ent, 0);
      } else {
        const char* str = JS_ToCString(ctx, value);
        archive_entry_set_pathname_utf8(ent, str);
        JS_FreeCString(ctx, str);
      }
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
      if(js_is_nullish(ctx, value)) {
        archive_entry_set_symlink_utf8(ent, 0);
      } else {
        const char* str = JS_ToCString(ctx, value);
        archive_entry_set_symlink_utf8(ent, str);
        JS_FreeCString(ctx, str);
      }
      break;
    }
    case ENTRY_UID: {
      int64_t n;
      if(!JS_ToInt64(ctx, &n, value))
        archive_entry_set_uid(ent, n);
      break;
    }
    case ENTRY_UNAME: {
      if(js_is_nullish(ctx, value)) {
        archive_entry_set_uname_utf8(ent, 0);
      } else {
        const char* str = JS_ToCString(ctx, value);
        archive_entry_set_uname_utf8(ent, str);
        JS_FreeCString(ctx, str);
      }
      break;
    }
  }
  return ret;
}

static JSValue
js_archiveentry_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
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
    // JS_ALIAS_DEF("ino64", "ino"),
    JS_CGETSET_MAGIC_DEF("nlink", js_archiveentry_getter, js_archiveentry_setter, ENTRY_NLINK),
    JS_CGETSET_ENUMERABLE_DEF("pathname", js_archiveentry_getter, js_archiveentry_setter, ENTRY_PATHNAME),
    JS_CGETSET_MAGIC_DEF("uname", js_archiveentry_getter, js_archiveentry_setter, ENTRY_UNAME),
    JS_CGETSET_MAGIC_DEF("gname", js_archiveentry_getter, js_archiveentry_setter, ENTRY_GNAME),
    JS_CGETSET_MAGIC_DEF("mode", js_archiveentry_getter, js_archiveentry_setter, ENTRY_MODE),
    JS_CGETSET_MAGIC_DEF("perm", js_archiveentry_getter, js_archiveentry_setter, ENTRY_PERM),
    JS_CGETSET_ENUMERABLE_DEF("size", js_archiveentry_getter, js_archiveentry_setter, ENTRY_SIZE),
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

/**
 * @}
 */
