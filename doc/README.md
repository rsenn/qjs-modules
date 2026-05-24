# qjs-modules — JS API reference

Per-module documentation of the JavaScript functions, classes and properties
exposed by each `quickjs-*.c` binding. Each page is named after the `*` in its
source file (e.g. `quickjs-blob.c` → [blob](blob.md)).

## Data & text

- [blob](blob.md) — WHATWG `Blob`
- [textcode](textcode.md) — `TextEncoder` / `TextDecoder`
- [json](json.md) — JSON read/write and streaming `JsonParser`
- [xml](xml.md) — XML/HTML read/write
- [bjson](bjson.md) — native binary object (de)serialization
- [inspect](inspect.md) — `util.inspect`-style pretty printer
- [lexer](lexer.md) — rule-based `Lexer` / `Token`
- [location](location.md) — source `Location`
- [predicate](predicate.md) — composable matcher/predicate objects

## Collections & traversal

- [list](list.md) — doubly-linked `List` / `ListIterator` / `ListNode`
- [queue](queue.md) — chunked byte `Queue`
- [pointer](pointer.md) — JSON-Pointer-like path object
- [deep](deep.md) — deep get/set/traverse of nested values
- [tree-walker](tree-walker.md) — DOM-style `TreeWalker` / `TreeIterator`
- [virtual](virtual.md) — `VirtualProperties` proxy over a backing store

## Streams & async

- [stream](stream.md) — WHATWG Streams (`ReadableStream`, `WritableStream`, `TransformStream`)
- [repeater](repeater.md) — async-iterable push stream
- [arraybuffer-sink](arraybuffer-sink.md) — incremental byte accumulator

## I/O, system & process

- [misc](misc.md) — system / process / buffer / internals utilities
- [sockets](sockets.md) — BSD sockets (`Socket`, `AsyncSocket`, `SockAddr`)
- [serial](serial.md) — serial-port access
- [child-process](child-process.md) — process spawning
- [directory](directory.md) — directory iteration
- [mmap](mmap.md) — memory mapping
- [path](path.md) — path manipulation
- [gpio](gpio.md) — GPIO pins
- [syscallerror](syscallerror.md) — `SyscallError` and `errno` constants

## Databases

- [mysql](mysql.md) — async MySQL/MariaDB client
- [pgsql](pgsql.md) — async PostgreSQL client

## Other

- [archive](archive.md) — libarchive reading/writing
- [magic](magic.md) — libmagic content detection
- [bcrypt](bcrypt.md) — bcrypt password hashing
- [internal](internal.md) — C support helpers (no standalone JS module)
