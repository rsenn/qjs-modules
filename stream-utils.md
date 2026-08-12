# Stream Utilities: I/O Behavior Assessment

## Executive Summary

**Current implementation**: All JS function wrappers are now **non-greedy** — they call the JS function once and return immediately.

**Status**: ✓ Fixed — JS wrappers now match fd/buffer backend behavior and support async I/O patterns.

---

## QuickJS Async I/O Model

From `doc/quickjs.md`:

```javascript
os.setReadHandler(fd, func)
```
Add a read handler to the file handle `fd`. `func` is called each time there is data pending for `fd`. A single read handler per file handle is supported.

```javascript
os.setWriteHandler(fd, func)
```
Add a write handler to the file handle `fd`. `func` is called each time data can be written to `fd`.

**Key insight**: These are **event-driven, non-blocking** handlers. The function is called when the fd is ready, and must return immediately with whatever bytes are available/transferable. Greedy loops would:
- Block the event loop
- Prevent other handlers from running
- Break the non-blocking contract

---

## JS Function Wrapper Implementation

### write_jsinvoke / write_jsfunction (src/stream-utils.c:481-523)

**Implementation**: Non-greedy (single call)

```c
static ssize_t
write_jsinvoke(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  JSFunc* fw = (JSFunc*)fd;
  JSValue ret = jsfunc_invoke(fw, (void*)buf, len, TRUE);

  if(JS_IsException(ret)) {
    JS_FreeValue(fw->ctx, JS_GetException(fw->ctx));
    return -1;
  }

  int32_t written;
  if(JS_ToInt32(fw->ctx, &written, ret)) {
    JS_FreeValue(fw->ctx, ret);
    return -1;
  }
  JS_FreeValue(fw->ctx, ret);

  return written;  // Return whatever JS returned
}
```

**Behavior**:
- Calls JS function once with (buf, len) or (buf, offset, len)
- Returns immediately with bytes written
- Caller decides whether to retry on partial writes
- **Compatible with `os.setWriteHandler()`**

### read_jsinvoke / read_jsfunction (src/stream-utils.c:993-1025)

**Implementation**: Non-greedy (single call)

```c
static ssize_t
read_jsinvoke(intptr_t fd, void* buf, size_t len, Reader* rd) {
  JSFunc* fr = (JSFunc*)fd;
  JSValue ret = jsfunc_invoke(fr, buf, len, FALSE);

  if(JS_IsException(ret)) {
    JS_FreeValue(fr->ctx, JS_GetException(fr->ctx));
    return -1;
  }

  int32_t n = js_toint32_free(fr->ctx, ret);

  return n;  // Return whatever JS returned
}
```

**Behavior**:
- Calls JS function once with (buf, len) or (buf, offset, len)
- Returns immediately with bytes read
- Caller decides whether to retry on partial reads
- **Compatible with `os.setReadHandler()`**

---

## Client Assessment

### 1. XMLSerializer / JsonSerializer

**Location**: `quickjs-xml.c:1020-1100`, `quickjs-json.c:1120-1200`

**I/O Pattern**: Pull-based streaming with explicit partial-write handling

**Implementation**:
```c
// quickjs-xml.c:1048-1062
ssize_t w = writer_write(&xs->skip_writer, buf, len);
if(w < 0) {
  xs->error = TRUE;
  return FALSE;
}
if(w == 0) {
  xs->blocked = TRUE;  // Explicit blocked state handling
  return FALSE;
}
xs->delivered += (size_t)w;  // Track partial progress
```

**Status**: ✓ **Already handles partial writes correctly**

**Rationale**:
- Serializers are designed for pull-based streaming (caller pulls via `.read()`)
- Must handle backpressure from slow consumers
- State machine tracks position for resumption after blocked writes
- Checks for `w < 0` (error), `w == 0` (blocked), `w > 0` (success)

---

### 2. ReadableStream

**Location**: `quickjs-stream.c:1020-1080`

**I/O Pattern**: Pull-based chunked reading for Streams API

**Implementation**:
```c
// quickjs-stream.c:1059-1071
n = reader_read(&rs->reader, buf, rs->chunk_size);
if(n < 0) {
  return JS_ThrowInternalError(ctx, "ReadableStream.fromReader(): read error");
}
if(n == 0) {
  // EOF - close stream
  fn = JS_GetPropertyStr(ctx, controller, "close");
  ret = JS_Call(ctx, fn, controller, 0, 0);
  return ret;
}
// n > 0: enqueue partial chunk, don't loop
buffer = JS_NewArrayBuffer(ctx, buf, n, ...);
```

**Status**: ✓ **Already handles partial reads correctly**

**Rationale**:
- Streams API is designed for async, non-blocking I/O
- Consumer wants whatever is available now, not to block until buffer is full
- Checks for `n < 0` (error), `n == 0` (EOF), `n > 0` (partial data)

---

### 3. Inspect

**Location**: `quickjs-inspect.c:480-920`

**I/O Pattern**: All-or-nothing writes to memory buffer

**Implementation**:
```c
// quickjs-inspect.c:833
writer_write(wr, str, len);  // No return value check
```

**Status**: ✓ **Acceptable for memory buffers**

**Rationale**:
- Writing to memory buffer (DynBuf) always succeeds or fails completely (OOM)
- No partial write handling needed
- DynBuf grows as needed, so "blocking" doesn't apply
- Current implementation assumes success (reasonable for memory)

---

### 4. TextEncoder/TextDecoder

**Location**: `quickjs-textcode.c:320-370`

**I/O Pattern**: Batch encoding/decoding with output to Writer

**Implementation**:
```c
// quickjs-textcode.c:335
if(writer_write(&wr, ptr, next - ptr) < 0)
  // error handling
```

**Status**: ✓ **Handles errors correctly**

**Rationale**:
- Output goes to memory buffer (DynBuf)
- Checks for `w < 0` (error)
- Encoding/decoding is batch operation

---

### 5. Pointer Utilities

**Location**: `src/pointer.c:50-60`

**I/O Pattern**: Formatting to memory buffer

**Implementation**:
```c
// src/pointer.c:53
writer_write(wr, (const uint8_t*)buf, fmt_ulong(buf, idx));  // No check
```

**Status**: ✓ **Acceptable for memory buffers**

**Rationale**:
- Writing to memory buffer (DynBuf)
- Small writes (numbers, strings)
- Assumes success (reasonable for memory)

---

### 6. Stream Wrappers (src/stream-utils.c)

#### write_dynbuf
- **Status**: ✓ Always succeeds (DynBuf) or returns -1 on OOM
- No partial writes possible

#### write_tee
- **Status**: ✓ Handles errors correctly
- Uses `RESULT` macro which returns early on error
- Returns `MIN_NUM(written[0], written[1])`

#### write_escaped
- **Status**: ✓ Handles partial writes correctly
- Checks `writer_putc()` return value for each character
- Returns bytes written or error code

#### write_urlencoded
- **Status**: ✓ Handles partial writes correctly
- Checks return values for each byte written
- Returns bytes written or error code

#### write_counted
- **Status**: ✓ Propagates parent's return value
- Counts bytes/characters on success
- Returns parent's result (may be partial)

#### write_stubborn (for fd writes)
- **Status**: ✓ Greedy with EINTR handling
- Loops on partial writes and EINTR
- Appropriate for blocking file I/O

#### empty_buffered
- **Status**: ✓ Handles partial writes correctly
- Returns error/EOF or bytes written
- Adjusts buffer position on partial write

#### write_buffered
- **Status**: ✓ Handles partial writes correctly
- Calls `empty_buffered()` which checks return values
- Returns bytes consumed or error code

#### write_linebuffered
- **Status**: ✓ Handles partial writes correctly
- Calls `empty_buffered()` on newline or buffer full
- Returns bytes consumed or error code

#### write_location
- **Status**: ✓ Handles partial writes correctly
- Tracks UTF-8 location for bytes actually written
- Propagates parent's return value

#### read_counted
- **Status**: ✓ Propagates parent's return value
- Counts bytes/characters on success
- Returns parent's result (may be partial)

#### read_buffered
- **Status**: ✓ Handles partial reads correctly
- Checks for `bytes < 0` (error), `bytes == 0` (EOF)
- Returns bytes read or error code

#### read_linebuffered
- **Status**: ✓ Handles partial reads correctly
- Checks for `r < 0` (error), `r == 0` (EOF)
- Returns line length or error code

#### read_location
- **Status**: ✓ Handles partial reads correctly
- Tracks UTF-8 location for bytes actually read
- Propagates parent's return value

---

## Summary Table

| Client | Backend | Status | Behavior | Notes |
|--------|---------|--------|----------|-------|
| XMLSerializer | Any | ✓ | Non-greedy | Handles blocked state explicitly |
| JsonSerializer | Any | ✓ | Non-greedy | Handles blocked state explicitly |
| ReadableStream | Any | ✓ | Non-greedy | Pulls available chunks, doesn't block |
| XMLParser | Any | ✓ | Non-greedy | Streaming parser with state |
| JSONParser | Any | ✓ | Non-greedy | Streaming parser with state |
| Inspect | DynBuf | ✓ | All-or-nothing | Memory buffer, no blocking |
| TextEncoder/Decoder | DynBuf | ✓ | All-or-nothing | Memory buffer, checks errors |
| Pointer utils | DynBuf | ✓ | All-or-nothing | Memory buffer, no blocking |
| **write_jsfunction** | **JS** | **✓ Fixed** | **Non-greedy** | Single call, immediate return |
| **write_jsinvoke** | **JS** | **✓ Fixed** | **Non-greedy** | Single call, immediate return |
| **read_jsfunction** | **JS** | **✓ Fixed** | **Non-greedy** | Single call, immediate return |
| **read_jsinvoke** | **JS** | **✓ Fixed** | **Non-greedy** | Single call, immediate return |

---

## Return Value Contract

All `ReadFunction`/`WriteFunction` implementations must follow this contract:

### Success (n > 0)
- `n` bytes read/written
- May be less than requested (partial transfer)
- Caller decides whether to retry

### EOF (n == 0)
- No more data available (read) or broken pipe (write)
- Caller should stop

### Error (n < 0)
- Operation failed
- Caller should handle error

**Key principle**: Functions return immediately with whatever is available/transferable. The caller is responsible for retrying on partial transfers if needed.

---

## Testing Strategy

1. **Non-greedy JS functions**: Test with functions that return partial results
2. **Backpressure**: Test serializers with slow consumers (JS functions returning < requested)
3. **Streaming**: Test parsers with partial input (JS functions returning < requested)
4. **Error handling**: Test all error paths (n < 0)
5. **EOF handling**: Test all EOF paths (n == 0)
6. **os.setReadHandler integration**: Test with event-driven I/O
7. **ReadableStream**: Test async pull-based reading

---

## Conclusion

**All stream-utils clients now have appropriate I/O behavior**:

1. **JS function wrappers are non-greedy** ✓
   - Call JS function once, return immediately
   - Match fd/buffer backend behavior
   - Compatible with `os.setReadHandler()`/`os.setWriteHandler()`
   - Enable proper streaming and backpressure

2. **All other clients handle partial I/O correctly** ✓
   - XMLSerializer/JsonSerializer: Explicit blocked state handling
   - ReadableStream: Pulls available chunks
   - Memory buffer clients: Acceptable all-or-nothing behavior
   - Stream wrappers: Proper error propagation

3. **Consistent return value contract** ✓
   - `n > 0`: bytes transferred (may be partial)
   - `n == 0`: EOF/broken pipe
   - `n < 0`: error
   - Caller decides whether to retry

**The stream-utils library now fully supports both blocking and non-blocking I/O patterns.**

---

## C Data Structures with Read/Write Contracts

Several C data structures in `src/*.c` have natural read/write APIs that could easily be adapted to Reader/Writer. These are not currently wrapped because they're not needed, but demonstrate how easily the stream-utils abstraction could be extended.

### Queue (src/queue.c)

**API**:
```c
ssize_t queue_write(Queue* q, const void* x, size_t n);
ssize_t queue_read(Queue* q, void* x, size_t n);
```

**Characteristics**:
- Non-blocking: returns immediately with bytes transferred
- Returns 0 when queue is empty (read) or full (write)
- Returns -1 on allocation failure (write)
- Partial transfers allowed (returns actual bytes)

**Reader/Writer adapter**:
```c
// queue_read/queue_write signatures are compatible after casting:
//   ssize_t queue_read (Queue*, void*, size_t)
//   ssize_t queue_write(Queue*, const void*, size_t)
//
// ReadFunction:  ssize_t(intptr_t fd, void* buf, size_t len, Reader* rd)
// WriteFunction: ssize_t(intptr_t fd, const void* buf, size_t len, Writer* wr)
//
// The 4th arg (Reader*/Writer*) is ignored; fd carries the Queue*.

static inline Reader
reader_from_queue(Queue* q) {
  return (Reader){(ReadFunction*)queue_read, q, NULL, NULL};
}

static inline Writer
writer_from_queue(Queue* q) {
  return (Writer){(WriteFunction*)queue_write, q, NULL};
}
```

**Status**: ✓ Already used in `src/stream-utils.c` for ReadableStream (see `read_jsstream`/`write_jsstream`)

---

### RingBuffer (src/ringbuffer.c)

**API**:
```c
ssize_t ringbuffer_write(RingBuffer* r, const void* x, size_t len);
ssize_t ringbuffer_read(RingBuffer* r, void* x, size_t len);
```

**Characteristics**:
- Circular buffer with fixed size
- Returns -1 when full (write) or empty (read)
- Returns bytes transferred (may be partial if buffer wraps)
- All-or-nothing: either full transfer or -1

**Reader/Writer adapter**:
```c
// ringbuffer_read/ringbuffer_write signatures are compatible after casting:
//   ssize_t ringbuffer_read (RingBuffer*, void*, size_t)
//   ssize_t ringbuffer_write(RingBuffer*, const void*, size_t)
//
// The 4th arg (Reader*/Writer*) is ignored; fd carries the RingBuffer*.

static inline Reader
reader_from_ringbuffer(RingBuffer* rb) {
  return (Reader){(ReadFunction*)ringbuffer_read, rb, NULL, NULL};
}

static inline Writer
writer_from_ringbuffer(RingBuffer* rb) {
  return (Writer){(WriteFunction*)ringbuffer_write, rb, NULL};
}
```

**Status**: Not currently used, but could be useful for fixed-size circular buffers

---

### InputBuffer / OutputBuffer (src/buffer-utils.c)

**API**:
```c
ssize_t inputbuffer_read(InputBuffer* in, void* buf, size_t len);
ssize_t outputbuffer_write(OutputBuffer* out, const void* ptr, size_t len);
```

**Characteristics**:
- InputBuffer: read-only view over memory region
  - Returns bytes available (may be less than requested)
  - Returns 0 at end of buffer
  - Never fails
- OutputBuffer: write-only view over memory region
  - Returns -1 if not enough space
  - Returns bytes written on success
  - All-or-nothing

**Reader/Writer adapter**:
```c
// inputbuffer_read/outputbuffer_write signatures are compatible after casting:
//   ssize_t inputbuffer_read (InputBuffer*, void*, size_t)
//   ssize_t outputbuffer_write(OutputBuffer*, const void*, size_t)
//
// The 4th arg (Reader*/Writer*) is ignored; fd carries the buffer pointer.

static inline Reader
reader_from_inputbuffer(InputBuffer* in) {
  return (Reader){(ReadFunction*)inputbuffer_read, in, NULL, NULL};
}

static inline Writer
writer_from_outputbuffer(OutputBuffer* out) {
  return (Writer){(WriteFunction*)outputbuffer_write, out, NULL};
}
```

**Status**: Not currently used, but InputBuffer/OutputBuffer already have similar semantics to Reader/Writer

---

### Summary of C Data Structure Adapters

| Data Structure | Read API | Write API | Partial Transfer | Blocking | Currently Used |
|----------------|----------|-----------|------------------|----------|----------------|
| Queue | `queue_read` | `queue_write` | ✓ Yes | ✗ No | ✓ Yes (ReadableStream) |
| RingBuffer | `ringbuffer_read` | `ringbuffer_write` | ✗ No (all-or-nothing) | ✗ No | ✗ No |
| InputBuffer | `inputbuffer_read` | N/A | ✓ Yes | ✗ No | ✗ No |
| OutputBuffer | N/A | `outputbuffer_write` | ✗ No (all-or-nothing) | ✗ No | ✗ No |

**Key insight**: All these data structures have non-blocking semantics and return immediately with whatever bytes are available/transferable, making them natural fits for the Reader/Writer abstraction.
