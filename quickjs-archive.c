#include "defines.h"
#include <archive.h>
#include <archive_entry.h>
#include "quickjs-archive.h"
#include "utils.h"
#include "buffer-utils.h"
#include "debug.h"

/**
 * \addtogroup quickjs-archive
 * @{
 */

VISIBLE JSClassID js_archive_class_id = 0, js_archive_iterator_class_id = 0, js_archiveentry_class_id = 0,
                  js_archivematch_class_id = 0;
static JSValue archive_proto, archive_ctor, iterator_proto, entry_proto, entry_ctor, match_proto, match_ctor;

typedef enum { READ = 0, WRITE = 1 } archive_mode;

static JSValue js_archive_wrap(JSContext* ctx, JSValueConst proto, struct archive* ar);
static JSValue js_archiveentry_wrap(JSContext* ctx, JSValueConst proto, struct archive_entry* ent);

typedef struct {
  JSValue archive;
} ArchiveInstance;

typedef struct {
  JSContext* ctx;
  JSValueConst callback, args[2];
} ArchiveEntryRef;

typedef struct {
  int fd;
  JSContext* ctx;
  JSValue this_obj, open, write, close;
} ArchiveVirtual;

/* Returns size actually written, zero on EOF, -1 on error. */
static la_ssize_t
archive_write(struct archive* ar, void* client_data, const void* buffer, size_t length) {
  ArchiveVirtual* cb = client_data;
  JSContext* ctx = cb->ctx;
  int64_t r = -1;
  JSValue args[] = {
      JS_NewArrayBufferCopy(ctx, buffer, length),
      JS_NewInt64(ctx, length),
      js_archive_wrap(ctx, archive_proto, ar),
  };
  JSValue ret = JS_Call(ctx, cb->write, cb->this_obj, countof(args), args);

  JS_ToInt64(ctx, &r, ret);
  JS_FreeValue(ctx, ret);
  return r;
}

static int
archive_open(struct archive* ar, void* client_data) {
  ArchiveVirtual* cb = client_data;
  JSContext* ctx = cb->ctx;
  JSValue ret = JS_Call(ctx, cb->open, cb->this_obj, 0, 0);

  JS_FreeValue(ctx, ret);
  return ARCHIVE_OK;
}

static int
archive_close(struct archive* ar, void* client_data) {
  ArchiveVirtual* cb = client_data;
  JSContext* ctx = cb->ctx;
  JSValue ret = JS_Call(ctx, cb->close, cb->this_obj, 0, 0);

  JS_FreeValue(ctx, ret);
  return ARCHIVE_OK;
}

static int
archive_destroy(struct archive* ar, void* client_data) {
  ArchiveVirtual* cb = client_data;
  JSContext* ctx = cb->ctx;

  JS_FreeValue(ctx, cb->open);
  JS_FreeValue(ctx, cb->write);
  JS_FreeValue(ctx, cb->close);
  JS_FreeValue(ctx, cb->this_obj);

  js_free(ctx, cb);
  return 0;
}

static inline struct archive*
js_archive_data(JSValueConst value) {
  return JS_GetOpaque(value, js_archive_class_id);
}

static inline struct archive*
js_archive_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_archive_class_id);
}

static inline struct archive_entry*
js_archiveentry_data(JSValueConst value) {
  return JS_GetOpaque(value, js_archiveentry_class_id);
}

static inline struct archive_entry*
js_archiveentry_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_archiveentry_class_id);
}

static inline struct archive*
js_archivematch_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_archivematch_class_id);
}

static inline archive_mode
js_archive_mode(JSContext* ctx, JSValueConst this_val) {
  return js_get_propertystr_int32(ctx, this_val, "mode");
}

static void
js_archive_set_mode(JSContext* ctx, JSValueConst this_val, int mode) {
  JS_DefinePropertyValueStr(ctx, this_val, "mode", JS_NewInt32(ctx, mode), JS_PROP_CONFIGURABLE);
}

static inline JSValue
js_archive_return(JSContext* ctx, JSValueConst this_val, int result) {
  JSValue ret = JS_NewInt32(ctx, result);
  struct archive* ar = js_archive_data2(ctx, this_val);

  switch(result) {
    case ARCHIVE_EOF: {
      JS_DefinePropertyValueStr(ctx, this_val, "eof", JS_TRUE, JS_PROP_CONFIGURABLE);
      break;
    }

    case ARCHIVE_FATAL: {
      ret = JS_ThrowInternalError(ctx, "libarchive error: %s", archive_error_string(ar));
      break;
    }
  }

  return ret;
}

static void
js_archive_free_buffer(JSRuntime* rt, void* opaque, void* ptr) {
  ArchiveInstance* ainst = opaque;

  JS_FreeValueRT(rt, ainst->archive);
  js_free_rt(rt, ainst);
}

static void
js_archive_progress_callback(void* opaque) {
  ArchiveEntryRef* aeref = opaque;

  JSValue ret = JS_Call(aeref->ctx, aeref->callback, JS_UNDEFINED, 2, aeref->args);
  JS_FreeValue(aeref->ctx, ret);
}

static JSValue
js_archive_wrap(JSContext* ctx, JSValueConst proto, struct archive* ar) {
  if(js_archive_class_id == 0)
    js_archive_init(ctx, 0);

  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = JS_DupValue(ctx, archive_proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_archive_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ar);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_READ,
  METHOD_WRITE,
  METHOD_READFILE,
  METHOD_WRITEFILE,
};

static JSValue
js_archive_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  struct archive* ar = 0;
  JSValue ret = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, this_val, "prototype");

  switch(magic) {
    case METHOD_READ: {
      uint32_t block_size = 10240;

      if(!(ar = archive_read_new()))
        return JS_ThrowOutOfMemory(ctx);

      archive_read_support_filter_all(ar);
      // archive_read_support_compression_all(ar);
      archive_read_support_format_all(ar);
      archive_read_support_filter_all(ar);

      if(argc > 1 && JS_IsNumber(argv[1]))
        JS_ToUint32(ctx, &block_size, argv[1]);

      wchar_t* filename = js_towstring(ctx, argv[0]);
      int r = archive_read_open_filename_w(ar, filename, block_size);

      js_free(ctx, filename);

      if(r != ARCHIVE_OK) {
        ret = JS_ThrowInternalError(ctx, "libarchive error: %s", archive_error_string(ar));
        archive_read_free(ar);

        return ret;
      }

      break;
    }

    case METHOD_WRITE: {
      int r;
      const char* f;

      if(!(ar = archive_write_new()))
        return JS_ThrowOutOfMemory(ctx);

      archive_write_set_bytes_per_block(ar, 1024 * 1024);

      if(JS_IsString(argv[0])) {
        f = JS_ToCString(ctx, argv[0]);
        int r2 = archive_write_set_format_filter_by_ext(ar, f);
        JS_FreeCString(ctx, f);

        wchar_t* filename = js_towstring(ctx, argv[0]);
        r = archive_write_open_filename_w(ar, filename);

        js_free(ctx, filename);
      } else if(JS_IsObject(argv[0])) {
        ArchiveVirtual* cb;

        if(!(cb = js_malloc(ctx, sizeof(ArchiveVirtual))))
          return JS_ThrowOutOfMemory(ctx);

        if(js_has_propertystr(ctx, argv[0], "blockSize")) {
          uint64_t bs = js_get_propertystr_uint64(ctx, argv[0], "blockSize");
          archive_write_set_bytes_per_block(ar, bs);
        }

        if((f = js_get_propertystr_cstring(ctx, argv[0], "format"))) {
          archive_write_set_format_by_name(ar, f);
          JS_FreeCString(ctx, f);
        }

        cb->fd = -1;
        cb->ctx = ctx;
        cb->this_obj = JS_DupValue(ctx, argv[0]);
        cb->open = JS_GetPropertyStr(ctx, argv[0], "open");
        cb->write = JS_GetPropertyStr(ctx, argv[0], "write");
        cb->close = JS_GetPropertyStr(ctx, argv[0], "close");

        archive_open_callback* open = JS_IsFunction(ctx, cb->open) ? &archive_open : NULL;
        archive_write_callback* write = JS_IsFunction(ctx, cb->write) ? &archive_write : NULL;
        archive_close_callback* close = JS_IsFunction(ctx, cb->close) ? &archive_close : NULL;

        r = archive_write_open2(ar, cb, open, write, close, &archive_destroy);
      }

      if(r == ARCHIVE_FATAL || r == ARCHIVE_FAILED) {
        ret = JS_ThrowInternalError(ctx, "libarchive error: %s", archive_error_string(ar));
        archive_write_free(ar);

        return ret;
      }

      break;
    }
  }

  ret = js_archive_wrap(ctx, proto, ar);
  JS_DefinePropertyValueStr(ctx, ret, "file", JS_DupValue(ctx, argv[0]), JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
  js_archive_set_mode(ctx, ret, magic == METHOD_READ ? READ : WRITE);

  return ret;
}

enum {
  PROP_ERRNO,
  PROP_ERROR_STRING,
  PROP_FORMAT,
  PROP_COMPRESSION,
  PROP_FILTERS,
  PROP_FILECOUNT,
  PROP_POSITION,
  PROP_READ_HEADER_POSITION,
  PROP_HAS_ENCRYPTED_ENTRIES,
  PROP_BLOCKSIZE,
};

static JSValue
js_archive_get(JSContext* ctx, JSValueConst this_val, int magic) {
  struct archive* ar;
  JSValue ret = JS_UNDEFINED;

  if(!(ar = js_archive_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_ERRNO: {
      ret = JS_NewInt32(ctx, archive_errno(ar));
      break;
    }

    case PROP_ERROR_STRING: {
      const char* err = archive_error_string(ar);

      ret = err ? JS_NewString(ctx, err) : JS_NULL;
      break;
    }

    case PROP_FORMAT: {
      const char* fmt = archive_format_name(ar);

      ret = fmt ? JS_NewString(ctx, fmt) : JS_NULL;
      break;
    }

    case PROP_COMPRESSION: {
      ret = JS_NewString(ctx, archive_filter_name(ar, 0));
      break;
    }

    case PROP_FILTERS: {
      int num_filters = archive_filter_count(ar);
      ret = JS_NewArray(ctx);

      for(int i = 0; i < num_filters; i++) {
        const char* s = archive_filter_name(ar, i);
        JS_SetPropertyUint32(ctx, ret, i, s ? JS_NewString(ctx, s) : JS_NULL);
      }

      break;
    }

    case PROP_FILECOUNT: {
      ret = JS_NewUint32(ctx, archive_file_count(ar));
      break;
    }

    case PROP_POSITION: {
      ret = JS_NewInt64(ctx, archive_filter_bytes(ar, -1));
      break;
    }

    case PROP_READ_HEADER_POSITION: {
      if(js_archive_mode(ctx, this_val) == READ) {

        int64_t r = archive_read_header_position(ar);

        if(r >= 0)
          ret = JS_NewInt64(ctx, r);
      }

      break;
    }

    case PROP_HAS_ENCRYPTED_ENTRIES: {
      if(js_archive_mode(ctx, this_val) == READ)
        ret = JS_NewInt32(ctx, archive_read_has_encrypted_entries(ar));

      break;
    }

    case PROP_BLOCKSIZE: {
      if(js_archive_mode(ctx, this_val) == WRITE)
        ret = JS_NewInt32(ctx, archive_write_get_bytes_per_block(ar));

      break;
    }
  }

  return ret;
}

static JSValue
js_archive_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  struct archive* ar;
  JSValue ret = JS_UNDEFINED;

  if(!(ar = js_archive_data2(ctx, this_val)))
    return ret;

  switch(magic) {
    case PROP_FORMAT: {
      const char* fmt;

      if(js_archive_mode(ctx, this_val) == WRITE) {
        if((fmt = JS_ToCString(ctx, value))) {
          ret = JS_NewInt32(ctx, archive_write_set_format_by_name(ar, fmt));
          JS_FreeCString(ctx, fmt);
        }
      }

      break;
    }

    case PROP_BLOCKSIZE: {
      if(js_archive_mode(ctx, this_val) == WRITE) {
        int32_t bs = -1;

        JS_ToInt32(ctx, &bs, value);
        ret = JS_NewInt32(ctx, archive_write_set_bytes_per_block(ar, bs));
      }

      break;
    }
  }

  return ret;
}

static JSValue
js_archive_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = js_archive_wrap(ctx, proto, 0);
  JS_FreeValue(ctx, proto);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_archive_open(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct archive* ar;
  int r;

  if((ar = js_archive_data(this_val)))
    return JS_ThrowInternalError(ctx, "Archive already open");

  ar = archive_read_new();

  archive_read_support_filter_all(ar);
  archive_read_support_format_all(ar);
  archive_read_support_filter_all(ar);
  JS_SetOpaque(this_val, ar);

  uint32_t block_size = 10240;

  if(argc > 1 && JS_IsNumber(argv[1]))
    JS_ToUint32(ctx, &block_size, argv[1]);

  JSValue arg = argc > 0 ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;
  const char* prop = "file";

  if(argc == 0) {
    if(js_has_propertystr(ctx, this_val, "file"))
      arg = JS_GetPropertyStr(ctx, this_val, "file");
    else if(js_has_propertystr(ctx, this_val, "buffer"))
      arg = JS_GetPropertyStr(ctx, this_val, "buffer");
    else if(js_has_propertystr(ctx, this_val, "fd"))
      arg = JS_GetPropertyStr(ctx, this_val, "fd");
  }

  js_delete_propertystr(ctx, this_val, "buffer");
  js_delete_propertystr(ctx, this_val, "file");
  js_delete_propertystr(ctx, this_val, "fd");

  if(JS_IsString(arg)) {
    wchar_t* filename = js_towstring(ctx, arg);
    r = archive_read_open_filename_w(ar, filename, block_size);
    js_free(ctx, filename);
  } else if(JS_IsNumber(arg)) {
    int32_t fd = -1;

    JS_ToInt32(ctx, &fd, arg);
    prop = "fd";
    r = archive_read_open_fd(ar, fd, block_size);
  } else if(!JS_IsUndefined(arg)) {
    InputBuffer input = js_input_chars(ctx, arg);

    prop = "buffer";
    r = archive_read_open_memory(ar, input_buffer_data(&input), input_buffer_length(&input));
    input_buffer_free(&input, ctx);
  }

  if(r != ARCHIVE_OK) {
    JS_FreeValue(ctx, arg);
    return JS_ThrowInternalError(ctx, "libarchive error: %s", archive_error_string(ar));
  }

  JS_DefinePropertyValueStr(ctx, this_val, prop, arg, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
  js_delete_propertystr(ctx, this_val, "eof");
  JS_DefinePropertyValueStr(ctx, this_val, "mode", JS_NewInt32(ctx, READ), JS_PROP_CONFIGURABLE);

  return JS_DupValue(ctx, this_val);
}

static JSValue
js_archive_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct archive* ar;
  uint8_t* ptr;
  size_t len, offset = 0, length = 0;

  if(js_archive_mode(ctx, this_val) != READ)
    return JS_ThrowInternalError(ctx, "archive not in read mode");

  if(!(ar = js_archive_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(js_get_propertystr_bool(ctx, this_val, "eof"))
    return JS_NULL;

  if(argc < 1 || !js_is_arraybuffer(ctx, argv[0])) {
    JSValue ret = JS_UNDEFINED;
    void* data;
    size_t size;
    __LA_INT64_T offs;

    switch(archive_read_data_block(ar, (const void**)&data, &size, &offs)) {
      case ARCHIVE_OK: {
        ArchiveInstance* abuf = js_malloc(ctx, sizeof(ArchiveInstance));
        abuf->archive = JS_DupValue(ctx, this_val);
        ret = JS_NewArrayBuffer(ctx, data, size, js_archive_free_buffer, abuf, FALSE);
        break;
      }

      case ARCHIVE_EOF: {
        JS_DefinePropertyValueStr(ctx, this_val, "eof", JS_TRUE, JS_PROP_CONFIGURABLE);
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

  ssize_t r;

  if(JS_IsNumber(argv[0])) {
    int32_t fd = -1;

    JS_ToInt32(ctx, &fd, argv[0]);
    r = archive_read_data_into_fd(ar, fd);
  } else {
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

    r = archive_read_data(ar, ptr + offset, length);
  }

  return JS_NewInt32(ctx, r);
}

static JSValue
js_archive_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct archive* ar;

  if(!(ar = js_archive_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(js_archive_mode(ctx, this_val) != WRITE)
    return JS_ThrowInternalError(ctx, "archive not in write mode");

  if(argc == 0 && js_has_propertystr(ctx, this_val, "entry")) {
    js_delete_propertystr(ctx, this_val, "entry");
    return archive_write_finish_entry(ar) == ARCHIVE_OK ? JS_TRUE : JS_FALSE;
  }

  JSValue ret = JS_UNDEFINED;
  BOOL in_entry = js_has_propertystr(ctx, this_val, "entry");

  while(argc > 0) {
    struct archive_entry* ent;

    if((ent = js_archiveentry_data(argv[0]))) {

      if(in_entry)
        archive_write_finish_entry(ar);

      ret = archive_write_header(ar, ent) == ARCHIVE_OK ? JS_TRUE : JS_FALSE;

      JS_DefinePropertyValueStr(ctx, this_val, "entry", JS_DupValue(ctx, argv[0]), JS_PROP_CONFIGURABLE);
      argv++;
      argc--;
      continue;
    }

    int n = 1;
    InputBuffer input = js_input_chars(ctx, argv[0]);

    if(argc > 1)
      n += js_offset_length(ctx, input.size, argc, argv, 1, &input.range);

    const uint8_t* buf = input_buffer_data(&input);
    size_t len = input_buffer_length(&input);
    size_t block_size = archive_write_get_bytes_per_block(ar);
    int64_t r, bytes = 0;

    while(len > 0) {
      size_t nb = MIN_NUM(block_size, len);

      r = archive_write_data(ar, buf, len);

      if(r > 0) {
        len -= r;
        buf += r;
        bytes += r;
        continue;
      }

      break;
    }

    ret = JS_NewInt64(ctx, bytes);

    input_buffer_free(&input, ctx);

    argv += n;
    argc -= n;
  }

  return ret;
}

static JSValue
js_archive_skip(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct archive* ar;

  if(!(ar = js_archive_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(js_archive_mode(ctx, this_val) == READ)
    return js_archive_return(ctx, this_val, archive_read_data_skip(ar));

  return JS_UNDEFINED;
}

static JSValue
js_archive_seek(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct archive* ar;
  int64_t r, offset = 0;
  int32_t whence = SEEK_SET;

  if(!(ar = js_archive_data2(ctx, this_val)))
    return JS_EXCEPTION;

  JS_ToInt64Ext(ctx, &offset, argv[0]);
  JS_ToInt32(ctx, &whence, argv[1]);

  r = archive_seek_data(ar, offset, whence);

  return js_archive_return(ctx, this_val, r);
}

static JSValue
js_archive_extract(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  struct archive* ar;
  struct archive_entry* ent;
  int32_t flags;
  ArchiveEntryRef* aeref = 0;

  if(js_archive_mode(ctx, this_val) != READ)
    return JS_ThrowInternalError(ctx, "archive not in read mode");

  if(!(ar = js_archive_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(ent = js_archiveentry_data2(ctx, argv[0])))
    return JS_EXCEPTION;

  if(argc >= 2)
    JS_ToInt32(ctx, &flags, argv[1]);

  if(argc >= 3) {
    if(!(aeref = js_malloc(ctx, sizeof(ArchiveEntryRef))))
      return JS_EXCEPTION;

    aeref->ctx = ctx;
    aeref->callback = argv[2];
    aeref->args[0] = this_val;
    aeref->args[1] = argv[0];

    archive_read_extract_set_progress_callback(ar, js_archive_progress_callback, aeref);
  }

  ret = js_archive_return(ctx, this_val, archive_read_extract(ar, ent, flags));

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

  if(!(ar = js_archive_data2(ctx, this_val)))
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

  if((ar = js_archive_data(this_val))) {

    if(js_archive_mode(ctx, this_val) == WRITE)
      if(js_has_propertystr(ctx, this_val, "entry"))
        archive_write_finish_entry(ar);

    int r = js_archive_mode(ctx, this_val) == WRITE ? archive_write_close(ar) : archive_read_close(ar);

    ret = r == ARCHIVE_OK ? JS_TRUE : JS_FALSE;

    js_delete_propertystr(ctx, this_val, "eof");
    js_delete_propertystr(ctx, this_val, "mode");
    js_delete_propertystr(ctx, this_val, "entry");
  }

  return ret;
}

static JSValue
js_archive_version(JSContext* ctx, JSValueConst this_val) {
  return JS_NewString(ctx, archive_version_details());
}

static JSValue
js_archive_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct archive* ar;
  struct archive_entry* ent = NULL;
  int result = ARCHIVE_EOF;

  if(js_archive_mode(ctx, this_val) != READ)
    return JS_ThrowInternalError(ctx, "archive not in read mode");

  if(!(ar = js_archive_data2(ctx, this_val)))
    return JS_EXCEPTION;

  js_delete_propertystr(ctx, this_val, "entry");

  BOOL eof = js_get_propertystr_bool(ctx, this_val, "eof");

  if(!eof) {
    if(!(ent = archive_entry_new2(ar)))
      return JS_ThrowOutOfMemory(ctx);

    result = archive_read_next_header2(ar, ent);
  } else {
    result = ARCHIVE_EOF;
  }

  JSValue ret;

  switch(result) {
    case ARCHIVE_EOF:
    case ARCHIVE_FATAL: {
      ret = eof ? JS_NULL : js_archive_return(ctx, this_val, result);
      break;
    }

    default: {
      ret = js_archiveentry_wrap(ctx, entry_proto, ent);
      JS_DefinePropertyValueStr(ctx, this_val, "entry", JS_DupValue(ctx, ret), JS_PROP_CONFIGURABLE);
      break;
    }
  }

  return ret;
}

static JSValue
js_archive_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  if(js_archive_mode(ctx, this_val) != READ)
    return JS_ThrowInternalError(ctx, "archive not in read mode");

  JSValue ret = JS_NewObjectProtoClass(ctx, iterator_proto, js_archive_iterator_class_id);
  struct archive* ar;

  if((ar = js_archive_data(this_val))) {
    JS_SetOpaque(ret, ar);
    JS_DefinePropertyValueStr(ctx, ret, "archive", JS_DupValue(ctx, this_val), JS_PROP_CONFIGURABLE);
  }

  return ret;
}

static JSValue
js_archive_iterator_next(JSContext* ctx, JSValueConst iter, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  struct archive* ar;
  struct archive_entry* ent = NULL;
  int result = ARCHIVE_EOF;

  if(!(ar = JS_GetOpaque2(ctx, iter, js_archive_iterator_class_id)))
    return JS_EXCEPTION;

  JSValue this_val = JS_GetPropertyStr(ctx, iter, "archive");

  BOOL eof = js_get_propertystr_bool(ctx, this_val, "eof");

  if(!eof) {
    if(!(ent = archive_entry_new2(ar)))
      return JS_ThrowOutOfMemory(ctx);

    result = archive_read_next_header2(ar, ent);
  }

  switch(result) {
    case ARCHIVE_EOF: {
      JS_DefinePropertyValueStr(ctx, this_val, "eof", JS_TRUE, JS_PROP_CONFIGURABLE);
      *pdone = TRUE;
      return JS_UNDEFINED;
    }

    case ARCHIVE_FATAL: {
      *pdone = TRUE;
      return JS_ThrowInternalError(ctx, "libarchive error: %s", archive_error_string(ar));
    }
  }

  if(result == ARCHIVE_WARN) {
    fprintf(stderr, "WARNING: %s\n", archive_error_string(ar));
    archive_clear_error(ar);
  }

  *pdone = FALSE;

  return js_archiveentry_wrap(ctx, entry_proto, ent);
}

static void
js_archive_finalizer(JSRuntime* rt, JSValue val) {
  struct archive* ar;

  if((ar = js_archive_data(val)))
    archive_free(ar);
}

static JSClassDef js_archive_class = {
    .class_name = "Archive",
    .finalizer = js_archive_finalizer,
};

static const JSCFunctionListEntry js_archive_funcs[] = {
    JS_CGETSET_MAGIC_DEF("errno", js_archive_get, 0, PROP_ERRNO),
    JS_CGETSET_MAGIC_DEF("error", js_archive_get, 0, PROP_ERROR_STRING),
    JS_CGETSET_MAGIC_DEF("format", js_archive_get, js_archive_set, PROP_FORMAT),
    JS_CGETSET_MAGIC_DEF("compression", js_archive_get, 0, PROP_COMPRESSION),
    JS_CGETSET_MAGIC_DEF("filters", js_archive_get, 0, PROP_FILTERS),
    JS_CGETSET_MAGIC_DEF("position", js_archive_get, 0, PROP_POSITION),
    JS_CGETSET_MAGIC_DEF("readHeaderPosition", js_archive_get, 0, PROP_READ_HEADER_POSITION),
    JS_CGETSET_MAGIC_DEF("hasEncryptedEntries", js_archive_get, 0, PROP_HAS_ENCRYPTED_ENTRIES),
    JS_CGETSET_MAGIC_DEF("blockSize", js_archive_get, js_archive_set, PROP_BLOCKSIZE),
    JS_CGETSET_MAGIC_DEF("fileCount", js_archive_get, 0, PROP_FILECOUNT),
    JS_CFUNC_DEF("next", 0, js_archive_next),
    JS_CFUNC_DEF("open", 1, js_archive_open),
    JS_CFUNC_DEF("read", 1, js_archive_read),
    JS_CFUNC_DEF("write", 1, js_archive_write),
    JS_CFUNC_DEF("skip", 0, js_archive_skip),
    JS_CFUNC_DEF("seek", 2, js_archive_seek),
    JS_CFUNC_DEF("extract", 1, js_archive_extract),
    JS_CFUNC_DEF("filterBytes", 1, js_archive_filterbytes),
    JS_CFUNC_DEF("close", 0, js_archive_close),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_archive_iterator),
    JS_PROP_INT32_DEF("READ", 0, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("WRITE", 1, JS_PROP_CONFIGURABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Archive", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_archive_iterator_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_archive_iterator_next, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ArchiveIterator", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_archive_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("read", 1, js_archive_functions, METHOD_READ),
    JS_CFUNC_MAGIC_DEF("write", 1, js_archive_functions, METHOD_WRITE),
    JS_PROP_INT32_DEF("READ", 0, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("WRITE", 1, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SEEK_SET", SEEK_SET, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SEEK_CUR", SEEK_CUR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SEEK_END", SEEK_END, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("EOF", ARCHIVE_EOF, 0),
    JS_PROP_INT32_DEF("OK", ARCHIVE_OK, 0),
    JS_PROP_INT32_DEF("RETRY", ARCHIVE_RETRY, 0),
    JS_PROP_INT32_DEF("WARN", ARCHIVE_WARN, 0),
    JS_PROP_INT32_DEF("FAILED", ARCHIVE_FAILED, 0),
    JS_PROP_INT32_DEF("FATAL", ARCHIVE_FATAL, 0),
#ifdef ARCHIVE_FILTER_NONE
    JS_PROP_INT32_DEF("FILTER_NONE", ARCHIVE_FILTER_NONE, 0),
#endif
#ifdef ARCHIVE_FILTER_GZIP
    JS_PROP_INT32_DEF("FILTER_GZIP", ARCHIVE_FILTER_GZIP, 0),
#endif
#ifdef ARCHIVE_FILTER_BZIP2
    JS_PROP_INT32_DEF("FILTER_BZIP2", ARCHIVE_FILTER_BZIP2, 0),
#endif
#ifdef ARCHIVE_FILTER_COMPRESS
    JS_PROP_INT32_DEF("FILTER_COMPRESS", ARCHIVE_FILTER_COMPRESS, 0),
#endif
#ifdef ARCHIVE_FILTER_PROGRAM
    JS_PROP_INT32_DEF("FILTER_PROGRAM", ARCHIVE_FILTER_PROGRAM, 0),
#endif
#ifdef ARCHIVE_FILTER_LZMA
    JS_PROP_INT32_DEF("FILTER_LZMA", ARCHIVE_FILTER_LZMA, 0),
#endif
#ifdef ARCHIVE_FILTER_XZ
    JS_PROP_INT32_DEF("FILTER_XZ", ARCHIVE_FILTER_XZ, 0),
#endif
#ifdef ARCHIVE_FILTER_UU
    JS_PROP_INT32_DEF("FILTER_UU", ARCHIVE_FILTER_UU, 0),
#endif
#ifdef ARCHIVE_FILTER_RPM
    JS_PROP_INT32_DEF("FILTER_RPM", ARCHIVE_FILTER_RPM, 0),
#endif
#ifdef ARCHIVE_FILTER_LZIP
    JS_PROP_INT32_DEF("FILTER_LZIP", ARCHIVE_FILTER_LZIP, 0),
#endif
#ifdef ARCHIVE_FILTER_LRZIP
    JS_PROP_INT32_DEF("FILTER_LRZIP", ARCHIVE_FILTER_LRZIP, 0),
#endif
#ifdef ARCHIVE_FILTER_LZOP
    JS_PROP_INT32_DEF("FILTER_LZOP", ARCHIVE_FILTER_LZOP, 0),
#endif
#ifdef ARCHIVE_FILTER_GRZIP
    JS_PROP_INT32_DEF("FILTER_GRZIP", ARCHIVE_FILTER_GRZIP, 0),
#endif
#ifdef ARCHIVE_FILTER_LZ4
    JS_PROP_INT32_DEF("FILTER_LZ4", ARCHIVE_FILTER_LZ4, 0),
#endif
#ifdef ARCHIVE_FILTER_ZSTD
    JS_PROP_INT32_DEF("FILTER_ZSTD", ARCHIVE_FILTER_ZSTD, 0),
#endif

#ifdef ARCHIVE_FORMAT_BASE_MASK
    JS_PROP_INT32_DEF("FORMAT_BASE_MASK", ARCHIVE_FORMAT_BASE_MASK, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO
    JS_PROP_INT32_DEF("FORMAT_CPIO", ARCHIVE_FORMAT_CPIO, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO_POSIX
    JS_PROP_INT32_DEF("FORMAT_CPIO_POSIX", ARCHIVE_FORMAT_CPIO_POSIX, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO
    JS_PROP_INT32_DEF("FORMAT_CPIO", ARCHIVE_FORMAT_CPIO, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO_BIN_LE
    JS_PROP_INT32_DEF("FORMAT_CPIO_BIN_LE", ARCHIVE_FORMAT_CPIO_BIN_LE, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO
    JS_PROP_INT32_DEF("FORMAT_CPIO", ARCHIVE_FORMAT_CPIO, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO_BIN_BE
    JS_PROP_INT32_DEF("FORMAT_CPIO_BIN_BE", ARCHIVE_FORMAT_CPIO_BIN_BE, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO
    JS_PROP_INT32_DEF("FORMAT_CPIO", ARCHIVE_FORMAT_CPIO, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO_SVR4_NOCRC
    JS_PROP_INT32_DEF("FORMAT_CPIO_SVR4_NOCRC", ARCHIVE_FORMAT_CPIO_SVR4_NOCRC, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO
    JS_PROP_INT32_DEF("FORMAT_CPIO", ARCHIVE_FORMAT_CPIO, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO_SVR4_CRC
    JS_PROP_INT32_DEF("FORMAT_CPIO_SVR4_CRC", ARCHIVE_FORMAT_CPIO_SVR4_CRC, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO
    JS_PROP_INT32_DEF("FORMAT_CPIO", ARCHIVE_FORMAT_CPIO, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO_AFIO_LARGE
    JS_PROP_INT32_DEF("FORMAT_CPIO_AFIO_LARGE", ARCHIVE_FORMAT_CPIO_AFIO_LARGE, 0),
#endif
#ifdef ARCHIVE_FORMAT_CPIO
    JS_PROP_INT32_DEF("FORMAT_CPIO", ARCHIVE_FORMAT_CPIO, 0),
#endif
#ifdef ARCHIVE_FORMAT_SHAR
    JS_PROP_INT32_DEF("FORMAT_SHAR", ARCHIVE_FORMAT_SHAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_SHAR_BASE
    JS_PROP_INT32_DEF("FORMAT_SHAR_BASE", ARCHIVE_FORMAT_SHAR_BASE, 0),
#endif
#ifdef ARCHIVE_FORMAT_SHAR
    JS_PROP_INT32_DEF("FORMAT_SHAR", ARCHIVE_FORMAT_SHAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_SHAR_DUMP
    JS_PROP_INT32_DEF("FORMAT_SHAR_DUMP", ARCHIVE_FORMAT_SHAR_DUMP, 0),
#endif
#ifdef ARCHIVE_FORMAT_SHAR
    JS_PROP_INT32_DEF("FORMAT_SHAR", ARCHIVE_FORMAT_SHAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR
    JS_PROP_INT32_DEF("FORMAT_TAR", ARCHIVE_FORMAT_TAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR_USTAR
    JS_PROP_INT32_DEF("FORMAT_TAR_USTAR", ARCHIVE_FORMAT_TAR_USTAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR
    JS_PROP_INT32_DEF("FORMAT_TAR", ARCHIVE_FORMAT_TAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE
    JS_PROP_INT32_DEF("FORMAT_TAR_PAX_INTERCHANGE", ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR
    JS_PROP_INT32_DEF("FORMAT_TAR", ARCHIVE_FORMAT_TAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR_PAX_RESTRICTED
    JS_PROP_INT32_DEF("FORMAT_TAR_PAX_RESTRICTED", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR
    JS_PROP_INT32_DEF("FORMAT_TAR", ARCHIVE_FORMAT_TAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR_GNUTAR
    JS_PROP_INT32_DEF("FORMAT_TAR_GNUTAR", ARCHIVE_FORMAT_TAR_GNUTAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_TAR
    JS_PROP_INT32_DEF("FORMAT_TAR", ARCHIVE_FORMAT_TAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_ISO9660
    JS_PROP_INT32_DEF("FORMAT_ISO9660", ARCHIVE_FORMAT_ISO9660, 0),
#endif
#ifdef ARCHIVE_FORMAT_ISO9660_ROCKRIDGE
    JS_PROP_INT32_DEF("FORMAT_ISO9660_ROCKRIDGE", ARCHIVE_FORMAT_ISO9660_ROCKRIDGE, 0),
#endif
#ifdef ARCHIVE_FORMAT_ISO9660
    JS_PROP_INT32_DEF("FORMAT_ISO9660", ARCHIVE_FORMAT_ISO9660, 0),
#endif
#ifdef ARCHIVE_FORMAT_ZIP
    JS_PROP_INT32_DEF("FORMAT_ZIP", ARCHIVE_FORMAT_ZIP, 0),
#endif
#ifdef ARCHIVE_FORMAT_EMPTY
    JS_PROP_INT32_DEF("FORMAT_EMPTY", ARCHIVE_FORMAT_EMPTY, 0),
#endif
#ifdef ARCHIVE_FORMAT_AR
    JS_PROP_INT32_DEF("FORMAT_AR", ARCHIVE_FORMAT_AR, 0),
#endif
#ifdef ARCHIVE_FORMAT_AR_GNU
    JS_PROP_INT32_DEF("FORMAT_AR_GNU", ARCHIVE_FORMAT_AR_GNU, 0),
#endif
#ifdef ARCHIVE_FORMAT_AR
    JS_PROP_INT32_DEF("FORMAT_AR", ARCHIVE_FORMAT_AR, 0),
#endif
#ifdef ARCHIVE_FORMAT_AR_BSD
    JS_PROP_INT32_DEF("FORMAT_AR_BSD", ARCHIVE_FORMAT_AR_BSD, 0),
#endif
#ifdef ARCHIVE_FORMAT_AR
    JS_PROP_INT32_DEF("FORMAT_AR", ARCHIVE_FORMAT_AR, 0),
#endif
#ifdef ARCHIVE_FORMAT_MTREE
    JS_PROP_INT32_DEF("FORMAT_MTREE", ARCHIVE_FORMAT_MTREE, 0),
#endif
#ifdef ARCHIVE_FORMAT_RAW
    JS_PROP_INT32_DEF("FORMAT_RAW", ARCHIVE_FORMAT_RAW, 0),
#endif
#ifdef ARCHIVE_FORMAT_XAR
    JS_PROP_INT32_DEF("FORMAT_XAR", ARCHIVE_FORMAT_XAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_LHA
    JS_PROP_INT32_DEF("FORMAT_LHA", ARCHIVE_FORMAT_LHA, 0),
#endif
#ifdef ARCHIVE_FORMAT_CAB
    JS_PROP_INT32_DEF("FORMAT_CAB", ARCHIVE_FORMAT_CAB, 0),
#endif
#ifdef ARCHIVE_FORMAT_RAR
    JS_PROP_INT32_DEF("FORMAT_RAR", ARCHIVE_FORMAT_RAR, 0),
#endif
#ifdef ARCHIVE_FORMAT_7ZIP
    JS_PROP_INT32_DEF("FORMAT_7ZIP", ARCHIVE_FORMAT_7ZIP, 0),
#endif
#ifdef ARCHIVE_FORMAT_WARC
    JS_PROP_INT32_DEF("FORMAT_WARC", ARCHIVE_FORMAT_WARC, 0),
#endif
#ifdef ARCHIVE_FORMAT_RAR_V5
    JS_PROP_INT32_DEF("FORMAT_RAR_V5", ARCHIVE_FORMAT_RAR_V5, 0),
#endif
#ifdef ARCHIVE_READ_FORMAT_CAPS_NONE
    JS_PROP_INT32_DEF("READ_FORMAT_CAPS_NONE", ARCHIVE_READ_FORMAT_CAPS_NONE, 0),
#endif
#ifdef ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_DATA
    JS_PROP_INT32_DEF("READ_FORMAT_CAPS_ENCRYPT_DATA", ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_DATA, 0),
#endif
#ifdef ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_METADATA
    JS_PROP_INT32_DEF("READ_FORMAT_CAPS_ENCRYPT_METADATA", ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_METADATA, 0),
#endif
#ifdef ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED
    JS_PROP_INT32_DEF("READ_FORMAT_ENCRYPTION_UNSUPPORTED", ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED, 0),
#endif
#ifdef ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW
    JS_PROP_INT32_DEF("READ_FORMAT_ENCRYPTION_DONT_KNOW", ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW, 0),
#endif
#ifdef ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED
    JS_PROP_INT32_DEF("READ_FORMAT_ENCRYPTION_UNSUPPORTED", ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED, 0),
#endif
#ifdef ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW
    JS_PROP_INT32_DEF("READ_FORMAT_ENCRYPTION_DONT_KNOW", ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW, 0),
#endif
#ifdef ARCHIVE_EXTRACT_OWNER
    JS_PROP_INT32_DEF("EXTRACT_OWNER", ARCHIVE_EXTRACT_OWNER, 0),
#endif
#ifdef ARCHIVE_EXTRACT_PERM
    JS_PROP_INT32_DEF("EXTRACT_PERM", ARCHIVE_EXTRACT_PERM, 0),
#endif
#ifdef ARCHIVE_EXTRACT_TIME
    JS_PROP_INT32_DEF("EXTRACT_TIME", ARCHIVE_EXTRACT_TIME, 0),
#endif
#ifdef ARCHIVE_EXTRACT_NO_OVERWRITE
    JS_PROP_INT32_DEF("EXTRACT_NO_OVERWRITE", ARCHIVE_EXTRACT_NO_OVERWRITE, 0),
#endif
#ifdef ARCHIVE_EXTRACT_UNLINK
    JS_PROP_INT32_DEF("EXTRACT_UNLINK", ARCHIVE_EXTRACT_UNLINK, 0),
#endif
#ifdef ARCHIVE_EXTRACT_ACL
    JS_PROP_INT32_DEF("EXTRACT_ACL", ARCHIVE_EXTRACT_ACL, 0),
#endif
#ifdef ARCHIVE_EXTRACT_FFLAGS
    JS_PROP_INT32_DEF("EXTRACT_FFLAGS", ARCHIVE_EXTRACT_FFLAGS, 0),
#endif
#ifdef ARCHIVE_EXTRACT_XATTR
    JS_PROP_INT32_DEF("EXTRACT_XATTR", ARCHIVE_EXTRACT_XATTR, 0),
#endif
#ifdef ARCHIVE_EXTRACT_UNLINK
    JS_PROP_INT32_DEF("EXTRACT_UNLINK", ARCHIVE_EXTRACT_UNLINK, 0),
#endif
#ifdef ARCHIVE_EXTRACT_SECURE_SYMLINKS
    JS_PROP_INT32_DEF("EXTRACT_SECURE_SYMLINKS", ARCHIVE_EXTRACT_SECURE_SYMLINKS, 0),
#endif
#ifdef ARCHIVE_EXTRACT_SECURE_NODOTDOT
    JS_PROP_INT32_DEF("EXTRACT_SECURE_NODOTDOT", ARCHIVE_EXTRACT_SECURE_NODOTDOT, 0),
#endif
#ifdef ARCHIVE_EXTRACT_NO_AUTODIR
    JS_PROP_INT32_DEF("EXTRACT_NO_AUTODIR", ARCHIVE_EXTRACT_NO_AUTODIR, 0),
#endif
#ifdef ARCHIVE_EXTRACT_NO_OVERWRITE_NEWER
    JS_PROP_INT32_DEF("EXTRACT_NO_OVERWRITE_NEWER", ARCHIVE_EXTRACT_NO_OVERWRITE_NEWER, 0),
#endif
#ifdef ARCHIVE_EXTRACT_SPARSE
    JS_PROP_INT32_DEF("EXTRACT_SPARSE", ARCHIVE_EXTRACT_SPARSE, 0),
#endif
#ifdef ARCHIVE_EXTRACT_MAC_METADATA
    JS_PROP_INT32_DEF("EXTRACT_MAC_METADATA", ARCHIVE_EXTRACT_MAC_METADATA, 0),
#endif
#ifdef ARCHIVE_EXTRACT_NO_HFS_COMPRESSION
    JS_PROP_INT32_DEF("EXTRACT_NO_HFS_COMPRESSION", ARCHIVE_EXTRACT_NO_HFS_COMPRESSION, 0),
#endif
#ifdef ARCHIVE_EXTRACT_HFS_COMPRESSION_FORCED
    JS_PROP_INT32_DEF("EXTRACT_HFS_COMPRESSION_FORCED", ARCHIVE_EXTRACT_HFS_COMPRESSION_FORCED, 0),
#endif
#ifdef ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS
    JS_PROP_INT32_DEF("EXTRACT_SECURE_NOABSOLUTEPATHS", ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS, 0),
#endif
#ifdef ARCHIVE_EXTRACT_CLEAR_NOCHANGE_FFLAGS
    JS_PROP_INT32_DEF("EXTRACT_CLEAR_NOCHANGE_FFLAGS", ARCHIVE_EXTRACT_CLEAR_NOCHANGE_FFLAGS, 0),
#endif

    JS_CGETSET_DEF("version", js_archive_version, 0),
};

static JSClassDef js_archive_iterator_class = {
    .class_name = "ArchiveIterator" /*,
     .finalizer = js_archiveiterator_finalizer,*/
};

static JSValue
js_archiveentry_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  struct archive_entry* ent;

  if(!(ent = archive_entry_new()))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get  the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_archiveentry_class_id);
  JS_FreeValue(ctx, proto);

  JS_SetOpaque(obj, ent);

  for(int i = 0; i < argc; i++) {
    if(JS_IsNumber(argv[i])) {
      int64_t size = -1;

      if(!JS_ToInt64Ext(ctx, &size, argv[i]))
        archive_entry_set_size(ent, size);
    } else if(JS_IsObject(argv[i])) {
      js_object_copy(ctx, obj, argv[i]);
    } else {
      const char* pathname;

      if((pathname = JS_ToCString(ctx, argv[i]))) {
        archive_entry_set_pathname_utf8(ent, pathname);
        JS_FreeCString(ctx, pathname);
      }
    }
  }

  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  ENTRY_CLONE,
};

static JSValue
js_archiveentry_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  struct archive_entry* ar = 0;
  JSValue ret = JS_UNDEFINED;

  if(!(ar = js_archiveentry_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case ENTRY_CLONE: {
      ret = js_archiveentry_wrap(ctx, JS_GetPrototype(ctx, this_val), archive_entry_clone(ar));
      break;
    }
  }

  return ret;
}

static JSValue
js_archiveentry_wrap(JSContext* ctx, JSValueConst proto, struct archive_entry* ent) {
  JSValue obj;

  if(js_archive_class_id == 0)
    js_archive_init(ctx, 0);

  if(js_is_nullish(ctx, proto))
    proto = entry_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_archiveentry_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ent);

  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  ENTRY_ATIME,
  ENTRY_BIRTHTIME,
  ENTRY_CTIME,
  ENTRY_DEV,
  ENTRY_DEVMAJOR,
  ENTRY_DEVMINOR,
  ENTRY_FILETYPE,
  ENTRY_TYPE,
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
  ENTRY_UNAME,
  ENTRY_ISENCRYPTED,
  ENTRY_ISMETADATAENCRYPTED,
  ENTRY_ISDATAENCRYPTED,
};

static JSValue
js_archiveentry_get(JSContext* ctx, JSValueConst this_val, int magic) {
  struct archive_entry* ent;
  JSValue ret = JS_UNDEFINED;

  if(!(ent = js_archiveentry_data2(ctx, this_val)))
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

    case ENTRY_TYPE: {
      int t = archive_entry_filetype(ent);
      const char* s = 0;

      switch(t & AE_IFMT) {
        case AE_IFLNK: s = "link"; break;
        case AE_IFSOCK: s = "sock"; break;
        case AE_IFCHR: s = "char"; break;
        case AE_IFBLK: s = "block"; break;
        case AE_IFDIR: s = "directory"; break;
        case AE_IFIFO: s = "pipe"; break;
        case AE_IFREG: s = "file"; break;
      }

      if(s)
        ret = JS_NewString(ctx, s);

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
        ret = JS_NewInt64(ctx, archive_entry_ino64(ent));

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

    case ENTRY_ISENCRYPTED: {
      ret = JS_NewBool(ctx, archive_entry_is_encrypted(ent));
      break;
    }

    case ENTRY_ISMETADATAENCRYPTED: {
      ret = JS_NewBool(ctx, archive_entry_is_metadata_encrypted(ent));
      break;
    }

    case ENTRY_ISDATAENCRYPTED: {
      ret = JS_NewBool(ctx, archive_entry_is_data_encrypted(ent));
      break;
    }
  }

  return ret;
}

static JSValue
js_archiveentry_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  struct archive_entry* ent;
  JSValue ret = JS_UNDEFINED;

  if(!(ent = js_archiveentry_data2(ctx, this_val)))
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

    case ENTRY_TYPE: {
      const char* str;

      if((str = JS_ToCString(ctx, value))) {
        int t = 0;

        switch(str[0]) {
          case 'l': t = AE_IFLNK; break;
          case 's': t = AE_IFSOCK; break;
          case 'c': t = AE_IFCHR; break;
          case 'b': t = AE_IFBLK; break;
          case 'd': t = AE_IFDIR; break;
          case 'p': t = AE_IFIFO; break;
          case 'f': t = AE_IFREG; break;
        }

        archive_entry_set_filetype(ent, t);
      }

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

static void
js_archiveentry_finalizer(JSRuntime* rt, JSValue val) {
  struct archive_entry* ent;

  if((ent = JS_GetOpaque(val, js_archiveentry_class_id)))
    archive_entry_free(ent);
}

static JSClassDef js_archiveentry_class = {
    .class_name = "ArchiveEntry",
    .finalizer = js_archiveentry_finalizer,
};

static const JSCFunctionListEntry js_archiveentry_funcs[] = {
    JS_CFUNC_MAGIC_DEF("clone", 0, js_archiveentry_functions, ENTRY_CLONE),
    JS_CGETSET_MAGIC_DEF("atime", js_archiveentry_get, js_archiveentry_set, ENTRY_ATIME),
    JS_CGETSET_MAGIC_DEF("ctime", js_archiveentry_get, js_archiveentry_set, ENTRY_CTIME),
    JS_CGETSET_MAGIC_DEF("mtime", js_archiveentry_get, js_archiveentry_set, ENTRY_MTIME),
    JS_CGETSET_MAGIC_DEF("birthtime", js_archiveentry_get, js_archiveentry_set, ENTRY_BIRTHTIME),
    JS_CGETSET_MAGIC_DEF("dev", js_archiveentry_get, js_archiveentry_set, ENTRY_DEV),
    JS_CGETSET_MAGIC_DEF("devmajor", js_archiveentry_get, js_archiveentry_set, ENTRY_DEVMAJOR),
    JS_CGETSET_MAGIC_DEF("devminor", js_archiveentry_get, js_archiveentry_set, ENTRY_DEVMINOR),
    JS_CGETSET_MAGIC_DEF("rdev", js_archiveentry_get, js_archiveentry_set, ENTRY_RDEV),
    JS_CGETSET_MAGIC_DEF("rdevmajor", js_archiveentry_get, js_archiveentry_set, ENTRY_RDEVMAJOR),
    JS_CGETSET_MAGIC_DEF("rdevminor", js_archiveentry_get, js_archiveentry_set, ENTRY_RDEVMINOR),
    JS_CGETSET_MAGIC_DEF("filetype", js_archiveentry_get, js_archiveentry_set, ENTRY_FILETYPE),
    JS_CGETSET_MAGIC_DEF("type", js_archiveentry_get, js_archiveentry_set, ENTRY_TYPE),
    JS_CGETSET_MAGIC_DEF("fflags", js_archiveentry_get, js_archiveentry_set, ENTRY_FFLAGS),
    JS_CGETSET_MAGIC_DEF("uid", js_archiveentry_get, js_archiveentry_set, ENTRY_UID),
    JS_CGETSET_MAGIC_DEF("gid", js_archiveentry_get, js_archiveentry_set, ENTRY_GID),
    JS_CGETSET_MAGIC_DEF("ino", js_archiveentry_get, js_archiveentry_set, ENTRY_INO),
    JS_CGETSET_MAGIC_DEF("nlink", js_archiveentry_get, js_archiveentry_set, ENTRY_NLINK),
    JS_CGETSET_ENUMERABLE_DEF("pathname", js_archiveentry_get, js_archiveentry_set, ENTRY_PATHNAME),
    JS_CGETSET_MAGIC_DEF("uname", js_archiveentry_get, js_archiveentry_set, ENTRY_UNAME),
    JS_CGETSET_MAGIC_DEF("gname", js_archiveentry_get, js_archiveentry_set, ENTRY_GNAME),
    JS_CGETSET_MAGIC_DEF("mode", js_archiveentry_get, js_archiveentry_set, ENTRY_MODE),
    JS_CGETSET_MAGIC_DEF("perm", js_archiveentry_get, js_archiveentry_set, ENTRY_PERM),
    JS_CGETSET_ENUMERABLE_DEF("size", js_archiveentry_get, js_archiveentry_set, ENTRY_SIZE),
    JS_CGETSET_MAGIC_DEF("symlink", js_archiveentry_get, js_archiveentry_set, ENTRY_SYMLINK),
    JS_CGETSET_MAGIC_DEF("hardlink", js_archiveentry_get, js_archiveentry_set, ENTRY_HARDLINK),
    JS_CGETSET_MAGIC_DEF("link", js_archiveentry_get, js_archiveentry_set, ENTRY_LINK),
    JS_CGETSET_MAGIC_DEF("isDataEncrypted", js_archiveentry_get, js_archiveentry_set, ENTRY_ISDATAENCRYPTED),
    JS_CGETSET_MAGIC_DEF("isMetadataEncrypted", js_archiveentry_get, js_archiveentry_set, ENTRY_ISMETADATAENCRYPTED),
    JS_CGETSET_MAGIC_DEF("isEncrypted", js_archiveentry_get, js_archiveentry_set, ENTRY_ISENCRYPTED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ArchiveEntry", JS_PROP_CONFIGURABLE),
};

static JSValue
js_archivematch_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_archivematch_class_id);
  JS_FreeValue(ctx, proto);

  JS_SetOpaque(obj, archive_match_new());
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  MATCH_INCLUDE,
  MATCH_EXCLUDE,
};

static JSValue
js_archivematch_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  struct archive* ar = 0;
  JSValue ret = JS_UNDEFINED;
  wchar_t* pattern = js_towstring(ctx, argv[0]);

  if(!(ar = js_archivematch_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case MATCH_INCLUDE: {
      ret = js_archive_return(ctx, this_val, archive_match_include_pattern_w(ar, pattern));
      break;
    }

    case MATCH_EXCLUDE: {
      ret = js_archive_return(ctx, this_val, archive_match_exclude_pattern_w(ar, pattern));
      break;
    }
  }

  return ret;
}

static void
js_archivematch_finalizer(JSRuntime* rt, JSValue val) {
  struct archive* m;

  if((m = JS_GetOpaque(val, js_archivematch_class_id)))
    archive_match_free(m);
}

static JSClassDef js_archivematch_class = {
    .class_name = "ArchiveMatch",
    .finalizer = js_archivematch_finalizer,
};

static const JSCFunctionListEntry js_archivematch_funcs[] = {
    JS_CFUNC_MAGIC_DEF("include", 1, js_archivematch_functions, MATCH_INCLUDE),
    JS_CFUNC_MAGIC_DEF("exclude", 1, js_archivematch_functions, MATCH_EXCLUDE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ArchiveMatch", JS_PROP_CONFIGURABLE),
};

int
js_archive_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_archive_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_archive_class_id, &js_archive_class);

  archive_ctor = JS_NewCFunction2(ctx, js_archive_constructor, "Archive", 1, JS_CFUNC_constructor, 0);
  archive_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, archive_proto, js_archive_funcs, countof(js_archive_funcs));
  JS_SetPropertyFunctionList(ctx, archive_ctor, js_archive_static_funcs, countof(js_archive_static_funcs));
  JS_SetClassProto(ctx, js_archive_class_id, archive_proto);

  JS_NewClassID(&js_archive_iterator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_archive_iterator_class_id, &js_archive_iterator_class);

  iterator_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, iterator_proto, js_archive_iterator_funcs, countof(js_archive_iterator_funcs));
  JS_SetClassProto(ctx, js_archive_iterator_class_id, iterator_proto);

  JS_NewClassID(&js_archiveentry_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_archiveentry_class_id, &js_archiveentry_class);

  entry_ctor = JS_NewCFunction2(ctx, js_archiveentry_constructor, "ArchiveEntry", 1, JS_CFUNC_constructor, 0);
  entry_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, entry_proto, js_archiveentry_funcs, countof(js_archiveentry_funcs));
  JS_SetClassProto(ctx, js_archiveentry_class_id, entry_proto);
  JS_SetConstructor(ctx, entry_ctor, entry_proto);

  JS_NewClassID(&js_archivematch_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_archivematch_class_id, &js_archivematch_class);

  match_ctor = JS_NewCFunction2(ctx, js_archivematch_constructor, "ArchiveMatch", 1, JS_CFUNC_constructor, 0);
  match_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, match_proto, js_archivematch_funcs, countof(js_archivematch_funcs));
  JS_SetClassProto(ctx, js_archivematch_class_id, match_proto);
  JS_SetConstructor(ctx, match_ctor, match_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "Archive", archive_ctor);
    JS_SetModuleExport(ctx, m, "ArchiveEntry", entry_ctor);
    JS_SetModuleExport(ctx, m, "ArchiveMatch", match_ctor);
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

  if((m = JS_NewCModule(ctx, module_name, js_archive_init))) {
    JS_AddModuleExport(ctx, m, "Archive");
    JS_AddModuleExport(ctx, m, "ArchiveEntry");
    JS_AddModuleExport(ctx, m, "ArchiveMatch");
  }

  return m;
}

/**
 * @}
 */
