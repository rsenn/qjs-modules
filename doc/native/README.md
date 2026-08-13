# Native Modules

Native C modules (`quickjs-*.c`) providing direct JavaScript bindings to system functionality.

## Complete List

### Data & Text Processing
- [archive](archive.md) — libarchive reading/writing (tar, zip, cpio, etc.)
- [arraybuffer-sink](arraybuffer-sink.md) — Incremental byte accumulator
- [bcrypt](bcrypt.md) — bcrypt password hashing
- [bjson](bjson.md) — Native binary object (de)serialization
- [blob](blob.md) — WHATWG Blob API
- [json](json.md) — JSON read/write and streaming parser
- [lexer](lexer.md) — Rule-based lexer/tokenizer
- [list](list.md) — Doubly-linked list
- [textcode](textcode.md) — TextEncoder/TextDecoder (WHATWG Encoding API)
- [xml](xml.md) — XML/HTML parser and writer

### System & I/O
- [child-process](child-process.md) — Node.js child_process module
- [directory](directory.md) — Directory iteration
- [gpio](gpio.md) — GPIO pin control
- [inotify](inotify.md) — Linux inotify file system events
- [location](location.md) — Source location tracking
- [magic](magic.md) — libmagic content detection
- [misc](misc.md) — System/process/buffer utilities
- [mmap](mmap.md) — Memory-mapped files
- [path](path.md) — Path manipulation
- [serial](serial.md) — Serial port access
- [sockets](sockets.md) — BSD sockets
- [syscallerror](syscallerror.md) — System call error codes

### Object Manipulation
- [deep](deep.md) — Deep object comparison and cloning
- [inspect](inspect.md) — Object inspection and formatting (Node.js util.inspect)
- [pointer](pointer.md) — JSON Pointer operations
- [predicate](predicate.md) — Type predicates and operator overloading
- [tree-walker](tree-walker.md) — AST/tree traversal

### Streams & Async
- [queue](queue.md) — FIFO queue
- [repeater](repeater.md) — Async iteration utilities
- [stream](stream.md) — WHATWG Streams API (ReadableStream, WritableStream, TransformStream)

### Virtual & Proxy
- [virtual](virtual.md) — Virtual property system

### Databases
- [mysql](mysql.md) — MySQL client
- [pgsql](pgsql.md) — PostgreSQL client
- [sqlite](sqlite.md) — SQLite3 database

### Internal
- [internal](internal.md) — Internal module introspection (qjsm only)

## Module Naming Convention

Native modules follow the pattern:
- C source: `quickjs-<name>.c`
- JavaScript import: `import { ... } from '<name>'`
- Documentation: `doc/native/<name>.md`

## Statistics

- **Total native modules:** 33
- **Standard compliance:** 4 modules (WHATWG/W3C/HTML5 specs)
- **Node.js compatible:** 7 modules
- **Internal/utilities:** 8 modules
- **Custom APIs:** 14 modules
