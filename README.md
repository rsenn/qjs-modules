# qjs-modules

qjs-modules builds `qjsm`, a [QuickJS](https://bellard.org/quickjs/)-based
interpreter with both native C modules and JavaScript modules statically
linked in. The modules themselves provide the runtime APIs plain QuickJS
doesn't: `console`, `fs`, `process`, streams, sockets, XML/DOM, and more,
modeled on their Node.js/Bun/Deno/browser equivalents so scripts written
for those runtimes need few changes to run here. Some native modules wrap
existing C libraries (libarchive, libmagic, the MariaDB/MySQL client,
libpq, SQLite, libserialport, a bundled libbcrypt); the rest, and most of
the JavaScript modules, are original implementations or ports of existing
JavaScript libraries.

Every module can also be built standalone as a shared library and
`import`ed at runtime by any QuickJS build, independent of `qjsm`. Each
`quickjs-*.c` file provides one native module (except
`quickjs-internal.c`, which contains shared support code compiled into
other modules).

## Overview

- **`qjsm`** — the interpreter binary this project builds (see
  [qjsm](#qjsm) below): QuickJS plus this project's modules statically
  linked in, so scripts get `console`, `fs`, `process`, and the rest
  without any setup.
- **Native C modules** (`quickjs-*.c`, [Module index](#module-index)
  below) — one QuickJS module per file, documented in
  [`doc/native/`](doc/native/). Some wrap third-party C libraries
  (libarchive, libmagic, MariaDB/MySQL, libpq, SQLite, libserialport);
  others are original C implementations.
- **JavaScript modules** (`lib/*.js`) — polyfills of standard APIs,
  higher-level wrappers over the native modules, and prototype
  extensions (`Array`, `Map`, …), documented in [`doc/js/`](doc/js/).
- **Vendored C libraries** (git submodules and bundled sources:
  libarchive, libbcrypt, libserialport, libutf, tutf8e) that some
  native modules build against or statically link.
- **Sibling projects** (`../qjs-lws`, `../qjs-glfw`, `../qjs-nanovg`) —
  HTTP/WebSocket, windowing/GL, and 2D canvas support. Kept out of this
  repo so it stays installable on plain QuickJS without pulling in a
  network stack or a GPU-backed renderer; see [Sibling
  Projects](CLAUDE.md#sibling-projects) for why.

## Building

```sh
cmake -B build .
make -C build
```

The core modules are always built. Some modules depend on external libraries
and are gated by CMake options:

| Option | Module | Dependency |
|--------|--------|------------|
| `MODULE_ARCHIVE` | [archive](doc/native/archive.md) | libarchive (`BUILD_LIBARCHIVE` builds the bundled one) |
| `MODULE_BCRYPT` | [bcrypt](doc/native/bcrypt.md) | bundled libbcrypt |
| `MODULE_MAGIC` | [magic](doc/native/magic.md) | libmagic |
| `MODULE_MYSQL` | [mysql](doc/native/mysql.md) | MariaDB/MySQL client library |
| `MODULE_PGSQL` | [pgsql](doc/native/pgsql.md) | libpq |
| `MODULE_SQLITE` | [sqlite](doc/native/sqlite.md) | sqlite3 |
| `MODULE_SERIAL` | [serial](doc/native/serial.md) | libserialport (`BUILD_LIBSERIALPORT` builds the bundled one) |

In addition to the native modules listed below, the `lib/` directory
contains pure-JavaScript modules (`console`, `fs`, `process`, `repl`,
`require`, `util`, parsers, …) built on top of them; see
[`doc/js/`](doc/js/).

## Module index

| Module | Exports | Description |
|--------|---------|-------------|
| [archive](doc/native/archive.md) | `Archive`, `ArchiveEntry`, `ArchiveMatch` | Read/write archives via libarchive |
| [arraybuffer_sink](doc/native/arraybuffer-sink.md) | `ArrayBufferSink` | Collect streamed writes into an ArrayBuffer |
| [bcrypt](doc/native/bcrypt.md) | `genSalt`, `hash`, `compare` | bcrypt password hashing |
| [bjson](doc/native/bjson.md) | `read`, `write` | Binary JSON (QuickJS object serialization) |
| [blob](doc/native/blob.md) | `Blob` | W3C-style binary blob |
| [child_process](doc/native/child-process.md) | `exec`, `spawn`, `ChildProcess`, … | Spawn and control subprocesses |
| [deep](doc/native/deep.md) | `find`, `get`, `set`, `iterate`, … | Deep object-tree traversal and manipulation |
| [directory](doc/native/directory.md) | `Directory` | Low-level directory reader (getdents) |
| [gpio](doc/native/gpio.md) | `GPIO` | Memory-mapped GPIO (Raspberry Pi) |
| [inspect](doc/native/inspect.md) | `inspect` | Pretty-print JS values (like Node's `util.inspect`) |
| [json](doc/native/json.md) | `read`, `write`, `JsonParser` | JSON parser/serializer with location info |
| [lexer](doc/native/lexer.md) | `Lexer`, `Token`, `Location` | Regex-rule based tokenizer |
| [list](doc/native/list.md) | `List`, `ListIterator`, `ListNode` | Doubly-linked list with Array-like API |
| [location](doc/native/location.md) | `Location` | Source position (file:line:column) |
| [magic](doc/native/magic.md) | `Magic` | File type detection via libmagic |
| [misc](doc/native/misc.md) | many functions | Grab-bag of OS, buffer, type and engine utilities |
| [mmap](doc/native/mmap.md) | `mmap`, `munmap`, … | Memory-mapped files as ArrayBuffers |
| [mysql](doc/native/mysql.md) | `MySQL`, `MySQLResult`, `MySQLError` | Non-blocking (promise-based) MySQL/MariaDB client |
| [path](doc/native/path.md) | `join`, `basename`, `resolve`, … | Path manipulation and filesystem tests |
| [pgsql](doc/native/pgsql.md) | `PGconn`, `PGresult`, `PGerror` | Non-blocking PostgreSQL client |
| [pointer](doc/native/pointer.md) | `Pointer`, `DereferenceError` | Object-graph paths (JSON-pointer-like) |
| [predicate](doc/native/predicate.md) | `Predicate` | Composable, callable predicate functions |
| [queue](doc/native/queue.md) | `Queue` | Chunked byte FIFO |
| [repeater](doc/native/repeater.md) | `Repeater` | Push-to-async-iterator bridge |
| [serial](doc/native/serial.md) | `Serial`, `SerialPort`, `SerialError` | Serial ports via libserialport |
| [sockets](doc/native/sockets.md) | `Socket`, `AsyncSocket`, `SockAddr`, … | BSD sockets, sync and async |
| [sqlite](doc/native/sqlite.md) | `SQLite3`, `SQLite3Result`, `SQLite3Error` | SQLite3 client |
| [stream](doc/native/stream.md) | `ReadableStream`, `WritableStream`, … | WHATWG-style streams |
| [syscallerror](doc/native/syscallerror.md) | `SyscallError` | Error class carrying syscall name + errno |
| [textcode](doc/native/textcode.md) | `TextDecoder`, `TextEncoder` | UTF-8/UTF-16/UTF-32 transcoding |
| [tree_walker](doc/native/tree-walker.md) | `TreeWalker`, `TreeIterator` | DOM-TreeWalker-style object traversal |
| [virtual](doc/native/virtual.md) | `VirtualProperties` | Uniform property access over Map/Array/Object |
| [xml](doc/native/xml.md) | `read`, `write` | XML parser and serializer |

---

## qjsm

`qjsm` is the interpreter binary this project builds: [QuickJS](https://bellard.org/quickjs/)
with a curated set of this project's modules statically linked in and
registered as builtins, so scripts can `import` them without any
`dlopen()`/module-path setup. Bare `qjs` (upstream QuickJS's own shell)
has none of this — only `std`/`os`, and only when passed `--std`. `qjsm`
also supports `--std`, but neither `std` nor `os` is loaded unless asked
for; nothing here auto-injects globals a script didn't request.

Which modules are builtins is controlled by CMake variables, set at
configure time:

| Variable | Meaning | Default |
|---|---|---|
| `BUILTINS_COMPILED` | Precompiled JS modules (bytecode) | `assert console events fs module perf_hooks process repl timers tty url util` |
| `BUILTINS_NATIVE` | Native C modules linked in directly | `path stream yaml` |
| `BUILTINS_NATIVE_EXCLUDE` | Native modules built but not registered as builtins (still importable via a `.so`, if `BUILD_SHARED_MODULES` built one) | `arraybuffer-sink child-process lexer location repeater tree-walker virtual` |
| `EXTERNAL_MODULES` | Externally-built modules (e.g. from `../qjs-glfw`) to link in, via `<name>_MODULE` | *(empty)* |
| `BUILTIN_MODULES` | Convenience list: mixes module names and external-module build directories, auto-routed into the three variables above | *(empty)* |

`BUILTINS_NATIVE`'s short default list isn't the full picture — any
builtin's own transitive module dependencies (e.g. `stream` needs
`queue`) are pulled in automatically by a dependency closure at
configure time, regardless of `BUILTINS_NATIVE_EXCLUDE`. CommonJS
`require()` (`lib/require.js`) is not a default builtin; add `require`
to `BUILTINS_COMPILED` to get it.

Every builtin module can still be built as a standalone `.so` and
`import`ed at runtime instead of using the static build —
`BUILD_SHARED_MODULES`/`BUILD_STATIC_MODULES` (both `ON` by default)
build each module both ways independently. Static linking into `qjsm`
is the convenient default, not the only way to consume a module.
