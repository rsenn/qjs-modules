# Negative compact values for leaf-relative compaction

## Problem

With `compact: 1`, only top-level sections arrays compact to one line, but deeper nested objects (symbols, relocs) don't, because they're at a different absolute depth. We want to compact the **N deepest leaf objects** regardless of their absolute depth.

## Current behavior

- `compact` is `int32_t` in `InspectOptions` (quickjs-inspect.c:63)
- `IS_COMPACT(d)` = `(d > opts->compact)` — compacts when depth exceeds threshold
- `inspect_compact_object()` tries one-line rendering, gated by `count <= opts->compact` (entry count limit) and `break_length`
- `Writer` is a vtable (`stream-utils.h:34-38`) wrapping DynBuf (random-access `buf`/`size`) or other backends
- DynBuf provides random-access: `dbuf.buf` is a `uint8_t*` we can read/write directly
- Depth tracks from 0 at root, incremented on descent, decremented on closing

## Proposed design

### Semantics

`compact = -N` means: compact the N most-deeply-nested leaf objects.
- Leaf object = object/array whose children are all primitives (no further nested objects/arrays)
- Depth counted from leaves: leaf = depth -1, parent of leaf = depth -2, etc.
- `compact = -1` compacts only leaf objects
- `compact = -2` compacts leaf objects and their direct parents

### Challenge

Leaf depth is unknown until the object is fully serialized. We can't know at serialization time whether an object is a "leaf" until we've recursed into it.

### Solution: post-hoc buffer compaction

Since Writer is backed by DynBuf (always in practice), we can:

1. Save buffer position (`buf_offset`) before serializing a child object
2. Serialize normally (expanded with newlines/indentation)
3. After serialization, check if object qualifies as "leaf" at target negative depth
4. If yes: compact bytes in-place from `buf_offset` to current `buf.size`
   - Replace all `\n` + indentation runs with single spaces
   - `memmove()` to shrink buffer
   - Update `buf.size`

### Implementation plan

1. **Add Writer buffer introspection** — accessor for underlying `uint8_t*` and current size:
   ```c
   static inline DynBuf* writer_dynbuf(Writer* wr) {
       if(wr->write == write_dynbuf)
           return (DynBuf*)wr->opaque;
       return NULL;
   }
   ```
   Or add `uint8_t* buf` field to Writer struct (intrusive but simpler).

2. **Track leaf depth** — compute during recursion:
   - When object's children are all primitives → leaf (leaf_depth = -1)
   - When ascending: parent's leaf_depth = min(children's leaf_depth) - 1
   - Pass through `InspectOptions` or as local variable in `inspect_recursive()`

3. **Compaction function**:
   ```c
   static size_t compact_whitespace(uint8_t* buf, size_t start, size_t end) {
       size_t r = start, w = start;
       while(r < end) {
           if(buf[r] == '\n') {
               r++;
               while(r < end && buf[r] == ' ') r++;
               buf[w++] = ' ';
           } else {
               buf[w++] = buf[r++];
           }
       }
       return w;
   }
   ```

4. **Hook into inspect_recursive()** — after serializing child value:
   ```c
   DynBuf* db = writer_dynbuf(wr);
   if(opts->compact < 0 && leaf_depth <= opts->compact && db) {
       size_t new_end = compact_whitespace(db->buf, buf_offset, db->size);
       db->size = new_end;
   }
   ```

5. **Guard: only DynBuf-backed Writers**:
   - Check `wr->write == write_dynbuf` before attempting in-place compaction
   - If not DynBuf-backed, reject negative compact or clamp to 0

### Files to modify

- `quickjs-inspect.c` — `InspectOptions.compact` semantics, leaf tracking in `inspect_recursive()`, compaction hook
- `include/stream-utils.h` — Writer buffer access (add `uint8_t* buf` field or accessor)
- `src/stream-utils.c` — set `buf` field in `writer_from_dynbuf()`

### Edge cases

- Empty objects/arrays: leaf_depth = -1, always compactible
- Mixed children (some objects, some primitives): leaf_depth = min(child leaf_depths) - 1
- Circular references: already handled by recursion guard (`Inspector.hier`)
- Negative compact with non-DynBuf writer: reject with error, or clamp to 0
