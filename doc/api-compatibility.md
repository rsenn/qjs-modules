# API Compatibility & Standards Compliance

This document catalogs every API in qjs-modules and its relationship to established standards.

## Goals

1. **Be the standard library QuickJS deserves** - WHATWG-spec'd web APIs and Deno/Bun-like runtime APIs
2. **Compatibility layer** - scripts from browser, Node, Deno, Bun should execute with minimal changes
3. **No qjs-modules-isms** - avoid custom APIs that lock users into this implementation
4. **Standards first** - prefer WHATWG > Browser > Bun > Node > Deno when implementing similar functionality

## Classification

- **Standard** - Matches a published spec (WHATWG, W3C, ECMA-262, Node.js, Bun.js)
- **Compatible** - Similar to a standard but with minor differences
- **Custom** - qjs-modules specific API with no standard equivalent
- **Internal** - Private implementation detail, not part of public API surface
- **Deprecated** - Should be removed or replaced with standard alternative

## Research Status

Complete: all C native module exports (quickjs-*.c) and JS module exports (lib/*.js) are classified below with spec/doc URLs.

For live spec-conformance testing, the WHATWG/W3C test suite is checked out out-of-tree at `~/Projects/wpt` (https://github.com/web-platform-tests/wpt, shallow clone) - relevant for cross-checking `stream`, `blob`, `url`, `abort`, `dom`, `textcode`, and the `testharness`/`testharnessreport` ports against upstream.

---

## C Native Modules

### quickjs-archive.c
**Module:** `archive`  
**Classification:** Custom (libarchive wrapper)  
**Exports:**
- `Archive` class - Read/write compressed archives (tar, zip, cpio, etc.)
- `ArchiveEntry` class - Entry metadata
- `ArchiveMatch` class - Pattern matching for entries

**Notes:** Wraps libarchive for reading/writing compressed archive formats. Not a standard API, but follows established archive format specifications. Useful for file management and distribution.

### quickjs-arraybuffer-sink.c
**Module:** `arraybuffer-sink`  
**Classification:** Compatible (Bun.js)  
**Spec:** https://bun.com/reference/bun/ArrayBufferSink  
**Exports:**
- `ArrayBufferSink` class - Writable buffer sink backed by resizable ArrayBuffer

**Runtime Compatibility:** Bun (`Bun.ArrayBufferSink`)  
**Notes:** Matches the shape of Bun's `ArrayBufferSink`; used internally by other modules for buffer accumulation.

### quickjs-bcrypt.c
**Module:** `bcrypt`  
**Classification:** Custom (libbcrypt wrapper) - see `lib/password.js` for the `Bun.password`-compatible surface  
**Spec:** https://bun.com/docs/runtime/hashing#bcrypt-modular-crypt-format  
**Exports:**
- `genSalt(rounds)` - Generate a bcrypt salt
- `hash(password, salt)` - Hash a password
- `compare(password, hash)` - Verify a password against a hash
- `HASHSIZE`, `SALTSIZE` - Buffer size constants

**Notes:** Low-level bcrypt primitives; `lib/password.js` wraps these into `Bun.password`'s
`hash()`/`hashSync()`/`verify()`/`verifySync()` shape - use that module instead of this one
directly for Bun-compatible code.

**Notes:** Currently a thin, synchronous libbcrypt wrapper with its own `genSalt`/`hash`/`compare` shape. Bun's `Bun.password` API (async `hash`/`verify`, `{algorithm: "bcrypt", cost}` options, modular crypt format output) is the closest JS-runtime precedent; aligning with it (async, `verify()` naming, options object) would need C-level changes in `quickjs-bcrypt.c`, not just a JS wrapper.

### quickjs-bjson.c
**Module:** `bjson`  
**Classification:** Internal (QuickJS-specific serialization)  
**Exports:**
- `read(buffer, offset, length, flags)` - Deserialize a value from QuickJS's native binary object format
- `write(value, flags)` - Serialize a value to that format

**Notes:** Exposes QuickJS's own `JS_ReadObject`/`JS_WriteObject` bytecode/value serialization (not a standard like `structuredClone` or `v8.serialize`) - engine-specific by nature, not portable to other runtimes.

### quickjs-blob.c
**Module:** `blob`  
**Classification:** Standard (WHATWG)  
**Spec:** https://w3c.github.io/FileAPI/#blob-section  
**Exports:**
- `Blob` class - Binary large object container
- `Blob.prototype.size` - Size in bytes
- `Blob.prototype.type` - MIME type
- `Blob.prototype.slice(start, end, type)` - Returns a new Blob
- `Blob.prototype.text()` - Returns Promise<string>
- `Blob.prototype.arrayBuffer()` - Returns Promise<ArrayBuffer>
- `Blob.prototype.stream()` - Returns ReadableStream (implemented 2026-08-12)

**Browser Compatibility:** Chrome 5+, Firefox 4+, Safari 5.1+, Edge 12+  
**Notes:** Fully implements WHATWG File API spec. The `stream()` method was recently fixed to return a proper ReadableStream.

### quickjs-child-process.c
**Module:** `child-process`  
**Classification:** Compatible (Node.js)  
**Spec:** https://nodejs.org/api/child_process.html  
**Exports:**
- `ChildProcess` class - spawned process handle (`wait()`, `kill()`, `pid`, `exitcode`, `stdio`, ...)
- `exec(command, options)` / `execSync` - run via shell
- `spawn(file, args, options)` / `spawnSync` - run without shell
- `kill(pid, signal)` - signal a process

**Notes:** Node `child_process`-flavored API; see doc/native/child-process.md for full member list.

### quickjs-deep.c
**Module:** `deep`  
**Classification:** Internal  
**Exports:**
- `deep.equal(a, b)` - Deep equality comparison
- `deep.clone(value)` - Deep clone object
- `deep.merge(target, source)` - Deep merge objects
- `deep.diff(a, b)` - Compute differences
- `deep.patch(target, patch)` - Apply diff patch

**Notes:** Internal implementation detail used by lib/deep.js and test infrastructure. Provides low-level deep comparison and cloning. Not intended for direct use in user code - use lib/deep.js wrapper instead.

### quickjs-directory.c
**Module:** `directory`  
**Classification:** Custom (POSIX opendir/readdir wrapper)  
**Exports:**
- `Directory` class - iterable wrapper over `opendir`/`readdir` (`open`, `close`, `next`, `[Symbol.iterator]`)
- Entry-shape flags (`NAME`/`TYPE`/`BOTH`) and `TYPE_*` dirent type constants

**Notes:** Thin POSIX dirent binding, not a Node/WHATWG API; closest thing to a "standard" here is the POSIX `readdir()` semantics it mirrors.

### quickjs-gpio.c
**Module:** `gpio`  
**Classification:** Custom (Linux memory-mapped GPIO)  
**Exports:**
- `GPIO` class - `initPin`, `setPin`, `getPin`, `buffer`
- Constants: `INPUT`, `OUTPUT`, `LOW`, `HIGH`

**Notes:** Hardware-specific (Linux GPIO register mmap), no cross-runtime standard exists; kernel GPIO docs are the closest reference.

### quickjs-inspect.c
**Module:** `inspect`  
**Classification:** Compatible (Node.js util.inspect)  
**Spec:** https://nodejs.org/api/util.html#utilinspectobject-options  
**Exports:**
- `inspect(value, options)` - Format value for display
- `inspect(value, depth)` - Format with depth limit
- `inspect.compact` - Control whitespace (positive = entry limit, negative = leaf-relative compaction)
- `inspect.colors` - Enable/disable ANSI colors
- `inspect.maxArrayLength` - Limit array display
- `inspect.maxStringLength` - Limit string display
- `inspect.depth` - Recursion depth limit
- `inspect.getters` - Show getter values
- `inspect.showHidden` - Show non-enumerable properties
- `inspect.custom` - Symbol for custom inspect methods

**Runtime Compatibility:** Node.js util.inspect  
**Notes:** Enhanced Node.js util.inspect with negative compact values for leaf-relative object compaction (see doc/native/inspect.md). Used by console.dir() and deep.equal() for formatting.

### quickjs-internal.c
**Module:** `internal`  
**Classification:** Internal  
**Exports:**
- `Module` class - JavaScript module introspection
- `getBytecode(func)` - Get compiled bytecode
- `getOpcodes()` - List QuickJS opcodes

**Notes:** Internal implementation detail for module system introspection and bytecode inspection. Not part of public API surface. Used by qjsm (QuickJS Manager) for module management. Should not be exposed to user code in production.

### quickjs-json.c
**Module:** `json`  
**Classification:** Compatible (ECMA-262 JSON) + Custom (streaming extensions)  
**Spec:** https://tc39.es/ecma262/#sec-json-object  
**Exports:**
- `read(input, inputName?)` / `write(value, indent?)` - Whole-document parse/serialize, like `JSON.parse`/`JSON.stringify`
- `JsonParser` - Incremental pull tokenizer (no standard equivalent; closest analogue is a hand-rolled SAX-style JSON reader)
- `JsonPushParser` - Incremental push parser (callback- or builder-mode), akin in spirit to Node's `stream-json`/`clarinet` userland packages
- `JsonWriter` - Push-based incremental writer (event-driven, like a JSON equivalent of an XML `Writer`)
- `JsonSerializer` - Pull-based lazy serializer (chunked `.read()`)

**Notes:** `read`/`write` are drop-in for `JSON.parse`/`JSON.stringify`; the `Json*Parser`/`JsonWriter`/`JsonSerializer` classes are qjs-modules-specific streaming additions with no WHATWG/Node standard - closest prior art is third-party streaming JSON libraries, not a spec.

### quickjs-lexer.c
**Module:** `lexer`  
**Classification:** Custom  
**Exports:**
- `Lexer` class - Rule-based, regex-driven tokenizer with named states (`peek`/`next`/`lex`, `addRule`/`pushState`/`popState`)
- `Token` class - A scanned token
- `Location` - see [location](#quickjs-locationc)

**Notes:** No standard/browser/Node equivalent; closest prior art is hand-rolled lexer generators (e.g. `moo`, `lex.js`) in the npm ecosystem, not a spec-backed API.

### quickjs-list.c
**Module:** `list`  
**Classification:** Custom (Array-like doubly-linked list)  
**Exports:**
- `List` class - Doubly-linked list with an `Array`-like API (`push`/`pop`/`shift`/`unshift`/`splice`/`sort`/`reverse`/`clear`, `values()`/`keys()`/`entries()`)
- `ListIterator`, `ListNode` classes

**Notes:** Deliberately omits Array's O(n) convenience methods (`indexOf`, `find`, `map`, etc. - see doc/native/list.md) since a linked list can't provide them in O(1); no ECMA-262/WHATWG equivalent, as JS has no built-in linked-list type.

### quickjs-location.c
**Module:** `location`  
**Classification:** Internal (source-position tracking)  
**Exports:**
- `Location` class - line/column/char-offset/filename position in source text

**Notes:** Not the browser `window.location`/WHATWG URL `Location` object (that's `dom.js`'s `Location`) - this is an internal position type used by `lexer`/`json`/parsers to report where in a source text/stream they are.

### quickjs-magic.c
**Module:** `magic`  
**Classification:** Custom (libmagic wrapper)  
**Exports:**
- `Magic` class (also callable) - content-type detection via `libmagic`/`file(1)`
- `.descriptor(fd)`, `.file(path)`, `.buffer(data)` - identify content by fd, path, or in-memory buffer

**Notes:** Wraps the same `libmagic` used by the Unix `file` command; closest standard analogue is the browser's `Blob.type`/`File.type` (sniffed from extension/headers) or Node's `file-type` npm package, but there's no formal spec - MIME sniffing itself is standardized (https://mimesniff.spec.whatwg.org/) though this module doesn't implement that algorithm, it defers to libmagic's own signature database.

### quickjs-misc.c
**Module:** `misc`  
**Classification:** Custom (POSIX syscall grab-bag)  
**Exports:** Large flat list of raw POSIX bindings grouped by theme - filesystem (`fnmatch`, `glob`, `wordexp`, `chmod`, `access`, `fcntl`, …), process/user (`fork`, `exec`, `kill`, `setsid`, `getpid`, …), plus buffer/QuickJS-internals utilities (see doc/native/misc.md for the full list).

**Notes:** Direct 1:1 POSIX bindings, not wrapped in a Node-style `fs`/`process` API (those higher-level wrappers live in `lib/fs.js`/`lib/process.js`); no single spec covers the whole surface, each function maps to its own POSIX man page. Fixed 2026-09: the inotify-event `watch(buffer, offset, length)` parser computed each event's `name` length via `byte_chr(ev->name, '\0', ev->len)` - swapped arguments (every other `byte_chr` call site in this codebase uses `(ptr, len, char)`), so it always searched a zero-length region and `name` was silently dropped from every event.

### quickjs-mmap.c
**Module:** `mmap`  
**Classification:** Custom (POSIX mmap wrapper)  
**Exports:**
- `mmap(length, prot, flags, fd, offset)` - Map memory, returned as an `ArrayBuffer`
- `munmap(buffer)`, `msync(buffer, flags)`, `mprotect(buffer, prot)` - Unmap/flush/reprotect a mapping
- `filename(buffer)`, `toString(buffer)` - Introspect a mapping

**Notes:** Thin POSIX syscall binding; no browser/Node/WHATWG equivalent (Node has no public `mmap` binding in core, only via npm packages like `mmap-io`).

### quickjs-mysql.c
**Module:** `mysql`  
**Classification:** Custom (libmysqlclient wrapper)  
**Exports:**
- `MySQL` class - `connect`, `query`/`execute`, `close`, `escapeString`, static `insertQuery`/`valueString` helpers
- `MySQLResult` class - iterable result set (`fetchRow`, `fetchAssoc`, `fetchFields`)
- `MySQLError` - `Error` subclass

**Notes:** Async wrapper around the MySQL/MariaDB C client; no JS-runtime-standard MySQL API exists to align with this native module's own shape directly, but see `lib/sql.js` (doc/js/sql.md) for a Bun `bun:sql`-flavored JS-facing layer unifying this with `pgsql`/`sqlite`.

### quickjs-path.c
**Module:** `path`  
**Classification:** Compatible (Node.js, extended with POSIX helpers)  
**Spec:** https://nodejs.org/api/path.html  
**Exports:**
- Node-style: `basename`, `dirname`, `extname`, `join`, `parse`, `format`, `resolve`, `relative`, `normalize`, `sep`, `delimiter`
- Extra POSIX-flavored helpers: `components`, `at`, `right`, `skip`, `fnmatch`, `realpath`, `canonical`, `getcwd`, `gethome`

**Notes:** Superset of Node's `path` module; the extra component/query helpers have no Node equivalent (Custom within an otherwise Compatible module).

### quickjs-pgsql.c
**Module:** `pgsql`  
**Classification:** Custom (libpq wrapper)  
**Exports:**
- `PGconn` class - `connect`, `query`/`execute`, `close`, `escapeLiteral`/`escapeIdentifier`/`escapeBytea`, static `insertQuery` helpers
- `PGresult` class - iterable result set (`fetchRow`, `fetchAssoc`, `fetchFields`)
- `PGerror` - `Error` subclass

**Notes:** Async wrapper around libpq; mirrors libpq's own escaping/identifier vocabulary rather than any JS-ecosystem pg client. See `lib/sql.js` (doc/js/sql.md) for a Bun `bun:sql`-flavored JS-facing layer unifying this with `mysql`/`sqlite`.

### quickjs-pointer.c
**Module:** `pointer`  
**Classification:** Compatible (RFC 6901 JSON Pointer, via `toRFC6901()`)  
**Spec:** https://www.rfc-editor.org/rfc/rfc6901  
**Exports:**
- `Pointer` class - a deep-property path for JS Object/Array trees, backed by an array of `JSAtom` (not a JS `Array`) so each path segment resolves via a fast `JS_GetProperty(ctx, obj, atom)` C-level lookup instead of a JS property-name string comparison; `deref`, `toString`, `toRFC6901`, `push`/`pop`/`shift`/`unshift`, `up`/`down`, static `from`/`of`
- `DereferenceError` - `Error` subclass for failed `deref`

**Notes:** The default string format (`toString()`/`new Pointer(str)`/console inspection) is this module's own dot/bracket path syntax (`foo.bar[1]`), not RFC 6901 - but `new Pointer(str)`'s parser also accepts RFC 6901 slash syntax (`/foo/bar/1`, with `~0`/`~1` escaping) as an input format, chosen automatically when a `/` precedes any `.`/`[`, and `toRFC6901()` serializes back to spec-conformant slash syntax; round-trips exactly against every example in RFC 6901 §5's table.

### quickjs-predicate.c
**Module:** `predicate`  
**Classification:** Internal  
**Exports:**
- `Predicate` class - a callable, combinable boolean-test object: takes any number of arguments of any type and returns true/false (`.call()`/`.eval()`)
- Factory functions for leaf predicates: `type`, `charset`, `string`, `regexp`, `instanceOf`, `prototypeIs`, `equal`, `property`, `has`, `member`, `index`, `function`, `some`, `every`
- Combinators: `and`, `or`, `xor`, `not`, `notnot`, plus arithmetic-style combinators (`add`, `sub`, `mul`, `div`, `mod`, `pow`, `atan2`, `bor`, `band`, `bnot`, `sqrt`) for numeric predicates
- Optional operator sugar via `Symbol.operatorSet` (`+ - * / % | & **`) so predicates can be combined with operators instead of combinator calls - a QuickJS-only extension, not used elsewhere in this project

**Notes:** Distinct from (and lower-level than) `lib/predicate.js`, a separate, simpler type-predicate wrapper used by `lib/parser.js` for `>>` operator dispatch. The module's purpose is generating composable boolean predicates, not "operator overloading" - the operator-overload sugar is an optional QuickJS-specific convenience on top of the (already callable/combinable) `Predicate` objects, not the module's reason for existing.

### quickjs-queue.c
**Module:** `queue`  
**Classification:** Custom  
**Exports:**
- `Queue` class - chunked byte FIFO (`write`, `read`, `peek`, `skip`, `clear`, iterable over chunks)
- `QueueIterator` - iterator over buffered chunks

**Notes:** No standard equivalent; closest analog is Node's internal `BufferList`/`readable-stream` internals, but this is a public, directly-iterable byte queue.

### quickjs-repeater.c
**Module:** `repeater`  
**Classification:** Compatible (`@repeaterjs/repeater`)  
**Spec:** https://repeater.js.org/  
**Exports:**
- `Repeater` class - async-iterable push stream (`new Repeater(executor)`, `next()`, `[Symbol.asyncIterator]`)
- Static combinators: `race`, `merge`, `zip`, `latest`

**Notes:** Explicitly modeled on the `@repeaterjs/repeater` npm package's API and combinator set, not a WHATWG/Node standard.

### quickjs-serial.c
**Module:** `serial`  
**Classification:** Compatible (Web Serial API)  
**Spec:** https://wicg.github.io/serial/  
**Exports:**
- `Serial` static namespace - `getPorts()`, `requestPort(filter)`
- `SerialPort` class - `open()`, `close()`, `getInfo()`, `getSignals()`, `setSignals()`, `read()`, `write()`, `drain()`, `flush()`, plus `fd`/`name`/`transport`/`inputWaiting`/`outputWaiting` getters
- `SerialError` - `Error` subclass for serial I/O failures

**Notes:** Web Serial-flavored API layered over POSIX termios; not the browser's exact spec (extra low-level termios controls like signals/drain/flush), hence Compatible not Standard.

### quickjs-sockets.c
**Module:** `sockets`  
**Classification:** Custom (POSIX sockets(2) wrapper)  
**Exports:**
- `Socket` / `AsyncSocket` classes - `bind()`, `connect()`, `listen()`, `accept()`, `send()`/`recv()`, `sendto()`/`recvfrom()`, `sendmsg`/`recvmsg`, `sendmmsg`/`recvmmsg`, `shutdown()`, `getsockopt`/`setsockopt`, `close()`
- `SockAddr` class - address family/addr/port/path wrapper
- Module functions: `socketpair()`, `select()`, `poll()`
- `SOCK_*`/`AF_*`/`SOL_*`/`SO_*`/`IPPROTO_*`/`MSG_*`/`SHUT_*`/`POLL*` constants

**Notes:** Direct 1:1 BSD-socket binding, much lower-level than Node's `net`/`dgram` or WHATWG WebSocket; no standard JS equivalent at this level of granularity.

### quickjs-sqlite.c
**Module:** `sqlite`  
**Classification:** Custom (SQLite3 wrapper)  
**Spec:** https://bun.sh/docs/api/sqlite (see also https://nodejs.org/api/sqlite.html)  
**Exports:**
- `SQLite` connection class - `open()`, `query()`, `exec()`, `close()`, value/query string helpers (`escapeString`, `quoteString`, `insertQuery`, etc.)
- `SQLiteResult` - prepared-statement result wrapper
- `SQLiteError` - `Error` subclass

**Notes:** Custom binding around libsqlite3; API shape (`query`/`exec`/`close`) is closer to Bun's `bun:sqlite` and Node's newer `node:sqlite` than to any WHATWG spec — cite those as the closest runtime precedent. See also `lib/sql.js` (doc/js/sql.md), which layers Bun's newer unified `bun:sql` tagged-template shape on top of this driver alongside `mysql`/`pgsql`.

### quickjs-stream.c
**Module:** `stream`  
**Classification:** Standard (WHATWG)  
**Spec:** https://streams.spec.whatwg.org/  
**Exports:**
- `ReadableStream` class - Asynchronous data source
- `WritableStream` class - Asynchronous data sink
- `TransformStream` class - Readable + Writable pair
- `ByteLengthQueuingStrategy` - Queue based on byte length
- `CountQueuingStrategy` - Queue based on chunk count

**Browser Compatibility:** Chrome 52+, Firefox 65+, Safari 10.1+, Edge 16+  
**Notes:** Implements core WHATWG Streams spec. Missing: `respondWithNewView()` safety checks for BYOB (Bring Your Own Buffer) readers - this is a known spec compliance gap tracked in TODO Tier 3.

### quickjs-syscallerror.c
**Module:** `syscallerror`  
**Classification:** Compatible (Node.js SystemError-style)  
**Spec:** https://nodejs.org/api/errors.html#class-systemerror  
**Exports:**
- `SyscallError` class (`Error` subclass) - `.syscall`, `.errno`, `.message`
- `errno` constants (`EAGAIN`, `ENOENT`, etc.)

**Notes:** Thrown by the POSIX-style bindings (sockets, misc, mmap, …); shape mirrors Node's internal `SystemError` (`syscall`/`errno`/`code`) though it isn't a public Node class users import directly.

### quickjs-textcode.c
**Module:** `textcode`  
**Classification:** Compatible (WHATWG Encoding, with extensions)  
**Spec:** https://encoding.spec.whatwg.org/  
**Exports:**
- `TextDecoder` class - `.decode(input)`, `.encoding`, plus non-standard `.endian` and `.buffered` (incremental decode)
- `TextEncoder` class - `.encode(input)`, `.encodeInto(source, dest)`

**Browser Compatibility:** Chrome 38+, Firefox 19+, Safari 10.1+, Edge 79+  
**Notes:** Matches the WHATWG Encoding Standard's core `TextDecoder`/`TextEncoder` surface; adds endianness control and buffered/incremental decoding beyond the spec.

### quickjs-tree-walker.c
**Module:** `tree-walker`  
**Classification:** Compatible (DOM TreeWalker shape, generalized)  
**Spec:** https://dom.spec.whatwg.org/#treewalker  
**Exports:**
- `TreeWalker` class - `firstChild()`, `lastChild()`, `nextNode()`, `nextSibling()`, `parentNode()`, `previousNode()`, `previousSibling()`, plus `root`/`currentNode`/`currentPath`/`depth`/`tagMask`/`filter`/`flags`
- `TreeIterator` class - iterable wrapper (`next()`, `Symbol.iterator`)

**Notes:** Mirrors the DOM `TreeWalker` method names/semantics but walks arbitrary nested JS values (objects/arrays), not actual DOM nodes — Compatible rather than Standard.

### quickjs-virtual.c
**Module:** `virtual`  
**Classification:** Custom  
**Exports:**
- `VirtualProperties` class - wraps an arbitrary backing store (array, map, plain object) behind a uniform get/set/has/delete property interface
- Static `array(target)`, `map(target)`, `object(target)`, `from(target)` factories

**Notes:** Not a virtual filesystem despite the name — see doc/native/virtual.md. No standard equivalent; closest prior art is JS `Proxy` traps, but this is a concrete class rather than a trap-based proxy.

### quickjs-xml.c
**Module:** `xml`  
**Classification:** Custom (XML/HTML parser+serializer, DOMParser-adjacent)  
**Spec:** https://developer.mozilla.org/en-US/docs/Web/API/DOMParser (closest precedent; no exact WHATWG spec for this API shape)  
**Exports:**
- `read(input, inputName?, options?)` / `write(value, maxDepth?)` module functions
- `XMLParser` - incremental pull parser (event-based, builds `.root` tree)
- `XMLNodeParser` - pull parser yielding flat node-per-call
- `XMLPushParser` - push-style parser fed via `.write()`
- `XMLSerializer` - lazy pull serializer (name coincides with WHATWG `XMLSerializer` but different API: pull-based `.read(n)`, not `.serializeToString()`)
- `XMLWriter` - event-driven writer with manual `elementStart`/`attribute`/`text`/`elementEnd` calls

**Notes:** Produces/consumes plain `{tagName, attributes, children}` objects rather than DOM nodes; `XMLSerializer` name collides with the WHATWG `XMLSerializer` interface but the API is unrelated (pull-based, not `serializeToString`) — worth flagging as a naming trap for portability, not a real compatibility win.

---

## JavaScript Modules

### lib/abort.js
**Module:** `abort`  
**Classification:** Standard (WHATWG)  
**Spec:** https://dom.spec.whatwg.org/#interface-abortcontroller  
**Exports:**
- `AbortController` class - `signal`, `abort(reason)`
- `AbortSignal` class - `aborted`, `reason`, `onabort`, `throwIfAborted()`
- `AbortSignal.timeout(ms)` - static, aborts after timeout
- `AbortSignal.any(iterable)` - static, composite signal
- `AbortSignal.abort(reason)` - static, pre-aborted signal

**Browser Compatibility:** Chrome 66+, Firefox 57+, Safari 12.1+, Edge 16+  
**Notes:** Pure-JS WHATWG AbortController/AbortSignal polyfill built on lib/events.js's EventTarget; includes the newer static helpers (`timeout`, `any`, `abort`).

### lib/arrayLike.js
**Module:** `arrayLike`  
**Classification:** Custom  
**Exports:**
- `ArrayLike` class - mixin providing `at`, `push`, `pop`, `splice`, `indexOf`, `lastIndexOf`, `forEach`, `map`, `find(Index)`, `findLast(Index)`, `filter`, `reduce`, `reduceRight`

**Notes:** Internal-style mixin giving Array.prototype-like methods to any object with numeric-indexed properties and a `length`; no standard equivalent since real arrays already have these.

### lib/assert.js
**Module:** `assert`  
**Classification:** Compatible (Node.js)  
**Spec:** https://nodejs.org/api/assert.html  
**Exports:**
- `assert(value, message)` - Truthiness assertion
- `assert.ok(value, message)` - Truthiness assertion
- `assert.equal(actual, expected, message)` - Loose equality
- `assert.notEqual(actual, expected, message)` - Loose inequality
- `assert.strictEqual(actual, expected, message)` - Strict equality
- `assert.notStrictEqual(actual, expected, message)` - Strict inequality
- `assert.deepEqual(actual, expected, message)` - Deep loose equality
- `assert.notDeepEqual(actual, expected, message)` - Deep loose inequality
- `assert.deepStrictEqual(actual, expected, message)` - Deep strict equality
- `assert.notDeepStrictEqual(actual, expected, message)` - Deep strict inequality
- `assert.throws(fn, error, message)` - Expect exception
- `assert.doesNotThrow(fn, error, message)` - Expect no exception
- `assert.rejects(asyncFn, error, message)` - Expect promise rejection
- `assert.doesNotReject(asyncFn, error, message)` - Expect no rejection
- `assert.ifError(value)` - Throw if truthy
- `assert.fail(message)` - Always fail
- `assert.match(string, regexp, message)` - RegExp match
- `assert.doesNotMatch(string, regexp, message)` - RegExp non-match

**Runtime Compatibility:** Node.js, Bun, Deno  
**Notes:** Comprehensive Node.js assert API. Used extensively in test suite.

### lib/asyncIterator.js
**Module:** `asyncIterator`  
**Classification:** Compatible (TC39 Iterator Helpers)  
**Spec:** https://tc39.es/proposal-iterator-helpers/  
**Exports:**
- `AsyncIterator` class - `drop`, `takeWhile`, `dropWhile`, `every`, `filter`, `find`, `forEach`, `map`, `reduce`, `some`, `take`, `flatMap`, `toArray`, `AsyncIterator.from(iter)`

**Notes:** Polyfills the built-in `AsyncIterator` helper methods from the TC39 Iterator Helpers proposal (now Baseline in modern engines); `from()` and `reduce()` reference undeclared variables in a couple of code paths — a latent bug, not fixed here (out of scope for this research task, flagging for follow-up).

### lib/console.js
**Module:** `console`  
**Classification:** Standard (WHATWG)  
**Spec:** https://console.spec.whatwg.org/  
**Exports:**
- `console.log(...args)` - Log message
- `console.error(...args)` - Log error
- `console.warn(...args)` - Log warning
- `console.info(...args)` - Log info
- `console.debug(...args)` - Log debug
- `console.dir(obj, options)` - Inspect object
- `console.time(label)` - Start timer
- `console.timeEnd(label)` - End timer
- `console.trace(...args)` - Stack trace
- `console.assert(condition, ...args)` - Assertion
- `console.table(data)` - Table format
- `console.group(label)` - Group logs
- `console.groupEnd()` - End group

**Browser Compatibility:** Universal  
**Notes:** Implements core WHATWG Console API. Uses `inspect` module for `console.dir()` formatting.

### lib/css-selectors.js
**Module:** `css-selectors`  
**Classification:** Standard (W3C)  
**Spec:** https://www.w3.org/TR/selectors-3/  
**Exports:**
- `parseSelectors(selector)` - parse a CSS selector string (via lib/parsel.js)
- `emitPredicates(node)` - compile parsed selector AST into predicate functions for matching DOM-like nodes

**Notes:** Selector-matching engine backing `querySelector`/`querySelectorAll` in lib/dom.js; works against wrapped dom.js elements or plain object trees.

### lib/css3-selectors.js
**Module:** `css3-selectors`  
**Classification:** Standard (W3C)  
**Spec:** https://www.w3.org/TR/selectors-4/  
**Exports:**
- `TypeSelector`, `ClassSelector`, `IdSelector`, `AttributeSelector`, `PseudoClassSelector` - selector-node constructors
- `LogicPredicate(pred, ...args)` - combine selectors with Predicate and/or
- `parseSelectors(s)` - generator yielding parsed selector groups

**Notes:** A second, apparently newer/alternate CSS Selectors implementation (Level 4, pseudo-classes) alongside css-selectors.js — the two appear to overlap; worth flagging for possible consolidation.

### lib/database.js
**Module:** `database`  
**Classification:** Compatible (Bun `bun:sqlite`)  
**Spec:** https://bun.sh/docs/api/sqlite  
**Exports:**
- `Database` class - `constructor(filename, opts)`, `run(sql, ...bindings)`, `prepare(sql)`, `query(sql)`, `close()`
- `Statement` class - `all(...params)`, `get(...params)`, `iterate(...params)`, `run(...params)`, `toString()`

**Notes:** Thin `?`-placeholder query builder over the native `sqlite` module (`SQLite3`), matching Bun's `Database`/`Statement` shape (`query()` caches, `prepare()` doesn't).

### lib/db.js
**Module:** `db`  
**Classification:** Compatible (node-postgres `pg`)  
**Spec:** https://node-postgres.com/apis/pool  
**Exports:**
- `Pool` class - `constructor(construct, size, lazy)`, `connect()`, `end()`, `list()`, `available`, `size`
- `PoolClient` class - `query(...args)`, `release()`

**Notes:** Generic connection-pool matching the shape of node-postgres's `Pool`/`PoolClient` (`pool.connect()` → client with `.release()`), but driver-agnostic via an injected `construct` factory rather than being pg-specific.

### lib/dbi.js
**Module:** `dbi`  
**Classification:** Custom  
**Exports:**
- `Database` class - `Database.register(name, AdapterClass)`, `Database.connect(driver, options)`, `query(sql)`, `exec(sql)`, `close()`, `insertId`, `affectedRows`, `quote(value)`, `insertQuery(table, fields, values)`
- `Result` class - `numFields`, `fetchFields()`, `all()`, async-iterable rows

**Notes:** Explicitly modeled on Perl DBI / libdbi (per file's own header comment) as a driver-agnostic layer over the native `sqlite`/`mysql`/`pgsql` bindings; intentionally custom, no JS-ecosystem standard for this. `Result`'s row iteration now always goes through `fetchAssoc()` (fixed 2026-09) rather than each driver's own default `Symbol.iterator`/`Symbol.asyncIterator`, since the latter's returned iterator is a distinct object from the result it came from and never saw a `resultType` flag set on the result itself — `sqlite`/`pgsql` silently gave positional arrays instead of row objects through plain iteration, unlike `mysql`. See `lib/sql.js`.

### lib/deep.js
**Module:** `deep`  
**Classification:** Custom  
**Exports:**
- Type/flag constants (`TYPE_*`, `FILTER_*`, `RETURN_*`, etc.)
- `clone`, `equals`, `extend`, `select`, `find`, `forEach`, `get`, `set`, `delegate`, `transform`, `unset`, `unflatten`

**Notes:** Pure-JS reimplementation of the native `deep` module's recursive traversal/comparison API for use in runtimes without the C binding; per CLAUDE.md's overlap-resolution decision it's documented only under `doc/native/deep.md` (C is authoritative) to avoid duplication.

### lib/describe-class.js
**Module:** `describe-class`  
**Classification:** Custom  
**Exports:**
- `describeClass(Ctor, opts)` - introspects a constructor's static/prototype chains (methods, getters, setters, fields) into a plain-object description

**Notes:** Reflection/introspection helper with no standard JS equivalent; used for tooling/debugging (e.g. generating docs or REPL help from a class definition).

### lib/dom.js
**Module:** `dom`  
**Classification:** Standard (W3C)  
**Spec:** https://dom.spec.whatwg.org/  
**Exports:**
- `Document` class - HTML/XML document
- `Element` class - DOM element
- `Node` class - Base DOM node
- `NodeList` class - Collection of nodes
- `HTMLCollection` class - Live collection of elements
- `Event` class - DOM event
- `EventTarget` class - Event dispatch base
- `HTMLElement` + 50+ subclasses (Input, Button, Form, etc.)
- `MutationObserver` class - DOM mutation tracking
- `Range` class - Document range selection
- `Selection` class - User selection
- `DOMRect` class - Element geometry
- `History` class - Browser history API
- `Navigator` class - Browser environment info
- `Location` class - URL/location API
- `Storage` class - localStorage/sessionStorage
- `Window` class - Global window object
- `File` class - File API (in lib/file.js)
- `DOMStringMap` class - dataset API
- `CSSStyleDeclaration` class - Inline styles

**Browser Compatibility:** Universal (DOM Level 4)  
**Notes:** Comprehensive DOM implementation covering 90%+ of browser DOM APIs. See TODO Tier 9 for remaining gaps: CSSOM, IntersectionObserver, ResizeObserver, Web Workers. (Fetch, FormData, and WebSocket are intentionally out of scope here - implemented separately in `../qjs-lws/`; Canvas likewise in `../qjs-nanovg/` + `../qjs-glfw/`.)

### lib/events.js
**Module:** `events`  
**Classification:** Compatible (Node.js)  
**Spec:** https://nodejs.org/api/events.html  
**Exports:**
- `EventEmitter` class - Event emitter base class
- `EventEmitter.prototype.on(event, listener)` - Add listener
- `EventEmitter.prototype.once(event, listener)` - Add one-time listener
- `EventEmitter.prototype.off(event, listener)` - Remove listener
- `EventEmitter.prototype.emit(event, ...args)` - Trigger event
- `EventEmitter.prototype.listenerCount(event)` - Count listeners

**Runtime Compatibility:** Node.js, Bun, Deno (with --unstable)  
**Notes:** Compatible with Node.js EventEmitter API. Different from browser EventTarget/Event API (which is in lib/dom.js). For browser-style events, use the DOM module instead.

### lib/extendArray.js
**Module:** `extendArray`  
**Classification:** Custom (mostly qjs-modules-specific; a few methods overlap partially with ECMA-262/TC39)  
**Spec:** https://tc39.es/ecma262/#sec-array.prototype.at (partial overlap only)  
**Exports:**
- `front`/`back`/`head`/`tail` getters+setters, `at(i)`, `clear()`, `findLastIndex`/`findLast` (native since ES2023, reimplemented here), `unique()`, `add()`, `search()`, `pushIf()`, `pushUnique()`, `unshiftUnique()`, `insert()`, `inserter()`, `delete()`, `remove()`, `removeIf()`, `rotateRight()`/`rotateLeft()`, `match()`, `group()`/`groupToMap()` (cf. TC39 `Object.groupBy`/`Map.groupBy` but on Array), `equal()`, `partition()`

**Notes:** `at`/`findLast`/`findLastIndex` duplicate now-native ES2023 methods; the rest (rotate, unique, group, partition, pushUnique, etc.) are qjs-modules-isms with no standard equivalent — candidates for reduction per project goal of minimizing custom APIs.

### lib/extendArrayBuffer.js
**Module:** `extendArrayBuffer`  
**Classification:** Custom  
**Exports:**
- `search()`/`searchAll()` (byte-pattern search), `view(offset, length)` (buffer-view slice via native `dupArrayBuffer`), `address` getter (pointer value via native `toPointer`), static `fromString()`/`fromAddress()` (via native `toArrayBuffer`)

**Notes:** No WHATWG/ECMA-262 equivalent — native-pointer-aware extension (`address`, `fromAddress`) specific to embedding ArrayBuffer in native memory; inherently non-portable/custom by design.

### lib/extendAsyncFunction.js
**Module:** `extendAsyncFunction`  
**Classification:** Custom  
**Exports:**
- `catch()`, `then()`, `finally()` (Promise-like combinators applied to the function itself, not its return value), `indirect()`, `bindArguments()`, `bindArray()`, `bindThis()`

**Notes:** No standard equivalent — treats an async function as a thenable/catchable object, a qjs-modules-ism. `bindThis`/`bindArguments` overlap conceptually with `Function.prototype.bind` but with different semantics (returns a metadata-carrying wrapper, not a native bound function).

### lib/extendAsyncGenerator.js
**Module:** `extendAsyncGenerator`  
**Classification:** Compatible (TC39 Iterator Helpers, adapted for async)  
**Spec:** https://tc39.es/proposal-iterator-helpers/  
**Exports:**
- `includes()` (via `some`), `enumerate()` (via `map`), `chain()`, `chainAll()`, `range()`; sets `[[Prototype]]` to `AsyncIterator.prototype` from lib/asyncIterator.js

**Notes:** Builds on the project's own `AsyncIterator` — `includes`/`map` are native async-iterator-helper names; `enumerate`/`chain`/`chainAll`/`range` are additional qjs-modules-isms not in the TC39 proposal.

### lib/extendFunction.js
**Module:** `extendFunction`  
**Classification:** Custom  
**Exports:**
- `catch()`, `then()`, `finally()`, `indirect()`, `bindArguments()`, `bindArray()`, `bindThis()`

**Notes:** Same combinator pattern as extendAsyncFunction.js applied to plain `Function.prototype`; no standard equivalent.

### lib/extendGenerator.js
**Module:** `extendGenerator`  
**Classification:** Compatible (TC39 Iterator Helpers)  
**Spec:** https://tc39.es/proposal-iterator-helpers/  
**Exports:**
- `includes()` (via `IteratorPrototype.some`), `enumerate()`, `chain()`, `chainAll()`, `range()`; sets `[[Prototype]]` to `Iterator.prototype` from lib/iterator.js

**Notes:** Sync counterpart of extendAsyncGenerator.js; same mix of native-helper-backed (`includes`) and custom (`enumerate`/`chain`/`chainAll`/`range`) methods.

### lib/extendMap.js
**Module:** `extendMap`  
**Classification:** Standard (TC39 "Map upsert" proposal, shipped ES2026)  
**Spec:** https://tc39.es/proposal-upsert/  
**Exports:**
- `getOrInsert(k, v)`, `getOrInsertComputed(k, fn)`

**Notes:** Names and semantics match the TC39 Map upsert proposal (`Map.prototype.getOrInsert`/`getOrInsertComputed`) exactly — a polyfill for a real, now-standard API.

### lib/extendMath.js
**Module:** `extendMath`  
**Classification:** Custom  
**Exports:**
- `exp10(n)`, `mantissa(n)`, `exponent(n)`, `fsign(n)`, `float64(mantissa, exponent, sign)` - IEEE-754 double bit-field extraction/construction via `BigUint64Array`/`Float64Array` reinterpretation

**Notes:** No standard equivalent; low-level float-bits introspection, qjs-modules-specific.

### lib/extendObject.js
**Module:** `extendObject`  
**Classification:** Custom  
**Exports:**
- static `getMemberNames`/`getMemberSymbols`/`getMethodNames`/`getMethodSymbols`/`getPropertyNames`/`getPropertySymbols`/`getPropertyDescriptor`/`getPropertyDescriptors` - prototype-chain-walking introspection helpers

**Notes:** No direct standard equivalent (native `Object.getOwnPropertyNames`/`getOwnPropertySymbols` only look at own properties, not the full prototype chain like these do); qjs-modules-specific reflection utilities.

### lib/extendSet.js
**Module:** `extendSet`  
**Classification:** Standard (TC39 Set methods proposal, shipped ES2025)  
**Spec:** https://tc39.es/proposal-set-methods/  
**Exports:**
- `difference()`, `symmetricDifference()`, `isDisjointFrom()`, `union()`, `intersection()`, `isSubsetOf()`, `isSupersetOf()`; static `from()`, `of()`

**Notes:** Instance methods are a correct-shaped polyfill for the now-native ES2025 Set methods proposal. Static `from`/`of` have no Set-specific standard counterpart (Array has `Array.from`/`Array.of`, Set does not) — those two are custom.

### lib/file.js
**Module:** `file`  
**Classification:** Standard (WHATWG/W3C)  
**Spec:** https://w3c.github.io/FileAPI/#file-section  
**Exports:**
- `File` class (extends `Blob`) - `name`, `lastModified`, `webkitRelativePath`

**Browser Compatibility:** Universal  
**Notes:** Thin File API extension over the `blob` module's `Blob` class.

### lib/fs.js
**Module:** `fs`  
**Classification:** Compatible (Node.js)  
**Spec:** https://nodejs.org/api/fs.html  
**Exports:**
- `readFile(path, options, callback)` - Read file async
- `readFileSync(path, options)` - Read file sync
- `writeFile(path, data, options, callback)` - Write file async
- `writeFileSync(path, data, options)` - Write file sync
- `mkdir(path, options, callback)` - Create directory
- `mkdirSync(path, options)` - Create directory sync
- `readdir(path, options, callback)` - List directory
- `readdirSync(path, options)` - List directory sync
- `stat(path, callback)` - File stats async
- `statSync(path)` - File stats sync
- `unlink(path, callback)` - Delete file
- `unlinkSync(path)` - Delete file sync
- `exists(path, callback)` - Check existence
- `existsSync(path)` - Check existence sync
- `chmod(path, mode, callback)` - Change permissions
- `chmodSync(path, mode)` - Change permissions sync
- `chown(path, uid, gid, callback)` - Change ownership
- `chownSync(path, uid, gid)` - Change ownership sync
- `copyFile(src, dest, callback)` - Copy file
- `copyFileSync(src, dest)` - Copy file sync
- `rename(oldPath, newPath, callback)` - Rename file
- `renameSync(oldPath, newPath)` - Rename file sync
- `link(existingPath, newPath, callback)` - Create hard link
- `linkSync(existingPath, newPath)` - Create hard link sync
- `symlink(target, path, callback)` - Create symlink
- `symlinkSync(target, path)` - Create symlink sync
- `readlink(path, callback)` - Read symlink
- `readlinkSync(path)` - Read symlink sync
- `realpath(path, callback)` - Resolve path
- `realpathSync(path)` - Resolve path sync
- `truncate(path, len, callback)` - Truncate file
- `truncateSync(path, len)` - Truncate file sync
- `open(path, flags, mode, callback)` - Open file
- `openSync(path, flags, mode)` - Open file sync
- `close(fd, callback)` - Close file
- `closeSync(fd)` - Close file sync
- `read(fd, buffer, offset, length, position, callback)` - Read from fd
- `readSync(fd, buffer, offset, length, position)` - Read from fd sync
- `write(fd, buffer, offset, length, position, callback)` - Write to fd
- `writeSync(fd, buffer, offset, length, position)` - Write to fd sync
- `fstat(fd, callback)` - File stats by fd
- `fstatSync(fd)` - File stats by fd sync
- `ftruncate(fd, len, callback)` - Truncate by fd
- `ftruncateSync(fd, len)` - Truncate by fd sync
- `watch(filename, options, listener)` - Watch file
- `watchFile(filename, options, listener)` - Watch file (polling)
- `unwatchFile(filename, listener)` - Stop watching
- `createReadStream(path, options)` - Create read stream
- `createWriteStream(path, options)` - Create write stream
- `promises` - Promise-based API (see fsPromises.js)

**Runtime Compatibility:** Node.js, Bun, Deno (with --unstable)  
**Notes:** Comprehensive Node.js fs API implementation; pure JS (`lib/fs.js`), built on QuickJS's `std`/`os` plus the native `misc` module's inotify bindings, not a dedicated native `fs` binding. `watch()` (fixed 2026-09) now correctly emits Node's `'change'`/`'rename'` events with the affected filename, supports `options.signal`, and throws synchronously for an invalid path - see doc/js/fs.md.

### lib/fsPromises.js
**Module:** `fsPromises`  
**Classification:** Compatible (Node.js)  
**Spec:** https://nodejs.org/api/fs.html#fs_promises_api  
**Exports:**
- `fsPromises.readFile(path, options)` - Read file (Promise)
- `fsPromises.writeFile(path, data, options)` - Write file (Promise)
- `fsPromises.appendFile(path, data, options)` - Append to file (Promise)
- `fsPromises.mkdir(path, options)` - Create directory (Promise)
- `fsPromises.readdir(path, options)` - List directory (Promise)
- `fsPromises.stat(path)` - File stats (Promise)
- `fsPromises.lstat(path)` - Symlink stats (Promise)
- `fsPromises.unlink(path)` - Delete file (Promise)
- `fsPromises.rmdir(path, options)` - Remove directory (Promise)
- `fsPromises.rm(path, options)` - Remove file/dir (Promise)
- `fsPromises.rename(oldPath, newPath)` - Rename (Promise)
- `fsPromises.copyFile(src, dest, mode)` - Copy file (Promise)
- `fsPromises.chmod(path, mode)` - Change permissions (Promise)
- `fsPromises.chown(path, uid, gid)` - Change ownership (Promise)
- `fsPromises.link(existingPath, newPath)` - Create hard link (Promise)
- `fsPromises.symlink(target, path, type)` - Create symlink (Promise)
- `fsPromises.readlink(path)` - Read symlink (Promise)
- `fsPromises.realpath(path, options)` - Resolve path (Promise)
- `fsPromises.truncate(path, len)` - Truncate file (Promise)
- `fsPromises.utimes(path, atime, mtime)` - Change timestamps (Promise)
- `fsPromises.access(path, mode)` - Check access (Promise)
- `fsPromises.open(path, flags, mode)` - Open file handle (Promise)
- `fsPromises.cp(src, dest, options)` - Copy file/dir (Promise)
- `fsPromises.glob(pattern, options)` - Glob pattern match (Promise)
- `fsPromises.watch(filename, options)` - Watch file (AsyncIterator)

**Runtime Compatibility:** Node.js 10+, Bun, Deno (with --unstable)  
**Notes:** Almost every export besides `open`/`read`/`write` and `watch()` is an empty stub (see BUGS: `fspromises-mostly-stubs`). `watch()` (added/fixed 2026-09) is now a real async generator over `fs.js`'s `watch()`, yielding `{eventType, filename}` and honoring `options.signal` - see doc/js/fsPromises.md.

### lib/html.js
**Module:** `html`  
**Classification:** Custom  
**Spec:** https://html.spec.whatwg.org/multipage/parsing.html (nearest precedent; not compliant)  
**Exports:**
- `HTMLParser` class - SAX-style pull parser built on the `xml` module's `XMLParser`
- `HTMLParser.prototype.parse()` - Advance parser, returns event object
- `HTMLParser.prototype.attributes` / `.tag` - Current element accessors
- `streamSrcHrefAndText(source, options)` - Generator yielding `src`/`href`/text events

**Notes:** Reuses the XML tokenizer rather than implementing the WHATWG HTML parsing algorithm (no tag-soup/implicit-close handling), so it's not standards-compliant HTML parsing — a lightweight custom SAX layer for well-formed markup.

### lib/inotify.js
**Module:** `inotify`  
**Classification:** Custom (Linux syscall wrapper)  
**Exports:**
- `inotify_init(flags)`, `inotify_add_watch(fd, pathname, mask)`, `inotify_rm_watch(fd, wd)`, `inotify_close(fd)` - Direct syscall-style wrappers
- `IN_*` flag constants (re-exported from native `misc` module)
- `inotify` class - Higher-level watcher with `add()`/`remove()`/`watch()`/`close()` and `onread`/`onclose`/`onerror` hooks

**Runtime Compatibility:** Linux-only; nearest cross-platform analogue is Node's `fs.watch`  
**Notes:** Direct binding to Linux inotify(7), not a portable/standard API by nature. `fs.watch()`/`fsPromises.watch()` (see `lib/fs.js`/`lib/fsPromises.js`) now provide the actual Node-`fs.watch`-shaped JS API on top of the same native inotify plumbing this module and `lib/fs.js` share. Fixed 2026-09: this class's read handler never advanced its byte counter (`bytes += r`) after `os.read()`, so `onread` never fired for any event; a stray, undefined-reference `EWOULDBLOCK;` expression statement in the same branch would also have thrown as soon as an event *did* arrive. Both are fixed.

### lib/io.js
**Module:** `io`  
**Classification:** Custom  
**Exports:**
- `Multiplexer` class - Per-fd read/write callback registry (`on`, `once`, `fds`, `close`)
- `HandlerEntry`, `DescriptorMap` classes - Internal storage for the multiplexer
- `setReadHandler(fd, cb)` / `setWriteHandler(fd, cb)` - Register on the default `Multiplexer.instance`
- `IOReadDecorator` / `IOWriteDecorator` - Mixin objects adding `readAsString`, `getline`, `puts`, `printf`, etc.

**Notes:** qjs-modules-specific fd event multiplexer; no standard/runtime equivalent (closest conceptually is libuv's poll handles used internally by Node, but there's no public Node API match).

### lib/iterator.js
**Module:** `iterator`  
**Classification:** Compatible (TC39 proposal)  
**Spec:** https://github.com/tc39/proposal-iterator-helpers  
**Exports:**
- `Iterator` class - `Iterator.from(iterable)`, `.drop()`, `.take()`, `.takeWhile()`, `.dropWhile()`, `.map()`, `.filter()`, `.flatMap()`, `.reduce()`, `.every()`, `.some()`, `.find()`, `.forEach()`, `.toArray()`
- `IteratorPrototype` - Shared prototype object

**Notes:** Polyfills the TC39 Iterator Helpers proposal (now Stage 4 / shipped in modern engines) for QuickJS, which lacks a native `Iterator.prototype` helper set.

### lib/misc.js
**Module:** `misc`  
**Classification:** Internal  
**Notes:** JS-side helper wrapper around the native `misc` module's POSIX grab-bag (see quickjs-misc.c above); not a public standard API surface, used internally by other lib/*.js wrappers.

### lib/module.js
**Module:** `module` (`node:module`)  
**Classification:** Compatible (Node.js `node:module`, partial)  
**Spec:** https://nodejs.org/api/module.html  
**Exports:**
- `builtinModules` - sorted names from `globalThis.builtins` (this engine's own builtin registry)
- `isBuiltin(name)` - accepts a `node:`-prefixed or bare name
- `createRequire(filename)` - synchronous CJS loader resolving relative/absolute specifiers only (no `node_modules`/`package.json` resolution)

**Notes:** Backed by this engine's own builtin-module registry rather than a hand-maintained list, matching how Bun/Deno alias `node:module` to their own module systems. Node's `Module` class, `register()` (loader hooks), `syncBuiltinESMExports()` and `SourceMap` are not implemented (see `TODO.md`).

### lib/nodeModulesLoader.js
**Module:** `nodeModulesLoader`  
**Classification:** Custom (Node.js-style module resolution demo)  
**Spec:** https://nodejs.org/api/modules.html#all-together  
**Exports:**
- default export: installs a custom `moduleLoader` (normalize/loader pair) implementing Node-style `node_modules` resolution, `package.json` `"exports"`/`"module"`/`"main"` field lookup, and an experimental `.ts` transpile-via-`swc` loader

**Notes:** Formerly named `module`/`lib/module.js`; renamed to free up the `module` specifier for Node's actual `node:module` API (see above). Approximates Node's CommonJS/ESM resolution algorithm for QuickJS's module loader hooks; the `.ts` loader path shells out to an external `swc` binary and is best-effort/dev-only.

### lib/parsel.js
**Module:** `parsel`  
**Classification:** Compatible (port of the `parsel-js` npm package)  
**Spec:** https://github.com/LeaVerou/parsel  
**Exports:**
- `tokenize(selector)`, `parse(selector, options)`, `nestTokens(tokens)`, `flatten(node)`, `walk(node, visit)`, `stringify(listOrNode)`

**Notes:** Line-for-line port of Lea Verou's `parsel-js` CSS selector tokenizer/AST parser; not a full CSS Selectors Level 4 grammar implementation but covers the common selector syntax.

### lib/parser.js
**Module:** `parser`  
**Classification:** Custom  
**Exports:**
- `Rule` base class and combinators: `Terminal`, `Literal`, `CharClass`, `Sequence`, `Alternative`, `Optional`, `OneOrMore`, `ZeroOrMore`, `Expect`, `Capture`
- Helper constructors: `_char`, `_lit`, `_charPred`, `_charRange`, `_charSet`, `_capture`
- `Parser` class - Top-level driver combining rules into a grammar
- `ExpectationError` - Parse failure exception

**Notes:** Custom parser-combinator toolkit (PEG-style), used together with `predicate` for building small recursive-descent grammars (e.g. `css-selectors`/`css3-selectors`); no standard equivalent.

### lib/password.js
**Module:** `password`  
**Classification:** Compatible (Bun `Bun.password`, bcrypt-only)  
**Spec:** https://bun.com/docs/api/hashing, https://bun.com/guides/util/hash-a-password  
**Exports:**
- `hash(password, options?)` / `hashSync(password, options?)` - `options: { algorithm: "bcrypt", cost }`
- `verify(password, hash)` / `verifySync(password, hash)`
- default export: `{ hash, hashSync, verify, verifySync }`

**Notes:** Matches `Bun.password`'s method names/shapes on top of the native `bcrypt` module.
Bun also supports Argon2 (its default algorithm) - this engine has no Argon2 binding, so
`algorithm` here only ever accepts `"bcrypt"` and throws for anything else, rather than
silently hashing with an unavailable algorithm.

### lib/perf_hooks.js
**Module:** `perf_hooks`  
**Classification:** Compatible (Node.js perf_hooks / W3C High Resolution Time)  
**Spec:** https://nodejs.org/api/perf_hooks.html  
**Exports:**
- `performance` object (default export) - `now()`, `timeOrigin`
- `now()` - High-resolution timestamp in ms since module load

**Notes:** Minimal subset of Node's `perf_hooks.performance` (just `now()`/`timeOrigin`, no `PerformanceObserver`/marks/measures).

### lib/pointer.js
**Module:** `pointer`  
**Classification:** Compatible (RFC 6901 JSON Pointer, via `toRFC6901()`)  
**Spec:** https://www.rfc-editor.org/rfc/rfc6901  
**Exports:**
- `Pointer` class - JSON-Pointer-like path object (JS polyfill counterpart of the native module)
- `Pointer.fromArray/fromString/from(...)` - constructors; `fromString`/the constructor accept both this module's dot/bracket syntax and RFC 6901 slash syntax (same auto-detection as the native module)
- `Pointer.prototype.toString()` (dot/bracket, e.g. `foo.bar[1]`), `toRFC6901()` (slash syntax), `hier()`, `deref(obj)`, `[Symbol.iterator]`
- Array-style methods borrowed from `Array.prototype` (`push`, `slice`, `shift`, `keys`, `values`, `map`, `reduce`, `forEach`, `concat`)

**Notes:** Per CLAUDE.md's overlap-resolution policy, `quickjs-pointer.c` (documented above) is authoritative; this is a thinner JS-only alternative for other runtimes, missing most of the native module's methods. `deref(obj)` is still an empty stub, unrelated to this RFC 6901 work (tracked in `BUGS` as `lib-pointer-deref-stub`).

### lib/predicate.js
**Module:** `predicate`  
**Classification:** Internal  
**Exports:**
- `Predicate` class - a callable, combinable boolean-test object: takes any number of arguments of any type and returns true/false
- `Predicate.prototype.call(value)` - Test value against predicate
- `Predicate.prototype.and(other)` - Logical AND combinator
- `Predicate.prototype.or(other)` - Logical OR combinator
- `Predicate.prototype.not()` - Logical NOT combinator
- `Predicate.type(typeCode)` - Create type predicate
- `Predicate.instance(class)` - Create instanceof predicate
- `Predicate.value(val)` - Create equality predicate

**Notes:** JS-only, simpler counterpart to `quickjs-predicate.c` (documented above), used by `lib/parser.js` for `>>` operator dispatch. The module's purpose is generating composable boolean predicates - not operator overloading.

**Notes:** Internal implementation detail used by lib/parser.js for cross-type operator dispatch. Enables `terminalA >> terminalB` syntax. Not intended for direct use in user code. See doc/js/predicate.md for implementation details.

### lib/process.js
**Module:** `process`  
**Classification:** Compatible (Node.js)  
**Spec:** https://nodejs.org/api/process.html  
**Exports:**
- `process.argv` - Command-line arguments
- `process.argv0` - Original argv[0]
- `process.env` - Environment variables
- `process.cwd()` - Current working directory
- `process.chdir(dir)` - Change working directory
- `process.exit(code)` - Exit process
- `process.exitCode` - Exit code
- `process.pid` - Process ID
- `process.ppid` - Parent process ID
- `process.platform` - Platform string
- `process.arch` - Architecture string
- `process.version` - Node.js version (compatibility shim)
- `process.versions` - Version info
- `process.release` - Release metadata
- `process.title` - Process title
- `process.stdout` - Standard output stream
- `process.stderr` - Standard error stream
- `process.stdin` - Standard input stream
- `process.nextTick(callback, ...args)` - Queue callback
- `process.hrtime()` - High-resolution time
- `process.hrtime.bigint()` - High-resolution time as BigInt
- `process.memoryUsage()` - Memory usage stats
- `process.cpuUsage()` - CPU usage stats
- `process.uptime()` - Process uptime
- `process.kill(pid, signal)` - Send signal to process
- `process.on(event, listener)` - Add event listener
- `process.once(event, listener)` - Add one-time listener
- `process.emit(event, ...args)` - Emit event
- `process.removeListener(event, listener)` - Remove listener
- `process.removeAllListeners(event)` - Remove all listeners

**Runtime Compatibility:** Node.js, Bun, Deno (with --unstable)  
**Notes:** Comprehensive Node.js process API. Some features may have limitations vs Node.js (e.g., process.fork(), cluster support). The `process.argv` issue with `-e` mode has been fixed (see commit 72c0364d).

### lib/repl.js
**Module:** `repl`  
**Classification:** Compatible (Node.js)  
**Spec:** https://nodejs.org/api/repl.html  
**Exports:**
- `REPL` class - prompt, line editing, history, completion, evaluation, output
- `REPLServer` class - Node `repl`-style session wrapper
- `loadModule(moduleName)` - dynamic import helper

**Notes:** Derived from the original QuickJS `qjscalc`/repl.js (Bellard/Gordon copyright header retained); mirrors Node's `repl` module shape (`REPLServer`) rather than reimplementing it exactly.

### lib/require.js
**Module:** `require`  
**Classification:** Compatible (Node.js)  
**Spec:** https://nodejs.org/api/modules.html  
**Exports:**
- `require(m)` (default export) - resolves and loads module `m` synchronously, returning `exports` (or `exports.default`)

**Notes:** CommonJS-style loader for use in an ESM-first runtime; mirrors Node's `require()` semantics closely enough for drop-in compatibility with simple CJS modules.

### lib/socklen_t.js
**Module:** `socklen_t`  
**Classification:** Custom (POSIX helper)  
**Exports:**
- `socklen_t` class (default export) - 4-byte `ArrayBuffer` subclass representing a C `socklen_t`, for in/out length params to `getsockopt`/`accept`-style socket APIs

**Notes:** No JS standard equivalent; mirrors the POSIX `socklen_t` type for FFI-style interop with the `sockets` native bindings.

### lib/sql.js
**Module:** `sql`  
**Classification:** Compatible (Bun `bun:sql`)  
**Spec:** https://bun.com/docs/runtime/sql  
**Exports:**
- `SQL` class - callable tagged-template query client (`` sql`...` ``, `.values()`, `.unsafe(text, params)`, `.begin(fn)`, `.close()`, `.driver`), unifying `sqlite`/`pgsql`/`mysql` via `dbi.js`'s `Database`
- `SQLError` - thrown for a bad connection string/options object

**Notes:** Single-connection (no pooling, unlike Bun's own `bun:sql`); interpolated values are escaped via the underlying driver's own `quote()`/`valueString()` and inlined into the query text, since none of the three native drivers expose real prepared-statement/placeholder binding — injection-safe, but not sent as separate wire-protocol parameters the way Bun's own Zig-native drivers do. See doc/js/sql.md.

### lib/stack.js
**Module:** `stack`  
**Classification:** Compatible (V8 stack trace convention)  
**Spec:** https://v8.dev/docs/stack-trace-api  
**Exports:**
- `Stack(st, pred)` - parses a raw stack string/array into `StackFrame`s, filtered by `pred`
- `StackFrame` class (default export) - one parsed frame (function name, file, line, column, etc.)
- `Location` - re-exported from `location` module

**Notes:** Modeled on the de facto V8 `Error.stack`/CallSite conventions (also resembles the popular `stackframe`/`error-stack-parser` npm packages) rather than a formal spec.

### lib/stream.js
**Module:** `stream`  
**Classification:** Standard (WHATWG)  
**Spec:** https://streams.spec.whatwg.org/  
**Exports:** JS-side polyfill with the same surface as the native `stream` module - `ReadableStream`, `WritableStream`, `TransformStream`, queuing strategies

**Notes:** Per project CLAUDE.md, this is "the qjs-lws version" — a ported streams polyfill for use on runtimes without the native module. Per the overlap-resolution policy, `quickjs-stream.c` (documented above) is authoritative.

### lib/streams.js
**Module:** `streams`  
**Classification:** Compatible (builds on WHATWG Streams)  
**Exports:**
- `export * from 'stream'` - re-exports all native/JS stream classes
- `FileSystemReadableStream`/`FileSystemReadableFileStream`/`FileSystemWritableFileStream` - file-backed streams
- `StreamReadIterator`/`LineStreamIterator` - async iteration helpers
- `ByLineStream` - line-splitting transform
- `TextEncoderStream`/`TextDecoderStream` - `TransformStream` subclasses (WHATWG Encoding spec: https://encoding.spec.whatwg.org/#interface-textencoderstream)

**Notes:** Higher-level convenience layer over the WHATWG `stream` module, closer in spirit to Node's fs-stream/readline helpers than to any single spec.

### lib/terminal.js
**Module:** `terminal`  
**Classification:** Custom (ANSI/VT escape sequences)  
**Exports:** cursor movement, screen/line editing, `Screen` backbuffer class, color, mouse/device/tabs, low-level escape-sequence builders - see doc/js/terminal.md for full list

**Notes:** Implements de facto ANSI/VT100-family terminal control sequences; not a WHATWG/Node/Bun API, closest analogue is Node's `readline`/`tty` escape helpers or the `ansi-escapes` npm package.

### lib/testharness.js
**Module:** `testharness`  
**Classification:** Standard (W3C/WHATWG)  
**Spec:** https://web-platform-tests.org/writing-tests/testharness-api.html  
**Exports:** `test`/`async_test`/`promise_test`, `assert_*` family, `Test`/`Tests`/`TestsStatus`, `WindowTestEnvironment`/`WorkerTestEnvironment`/etc., `EventWatcher`, `format_value` - see doc/js/testharness.md for full grouped list

**Notes:** A direct port of the WPT `testharness.js` framework, matching `~/Projects/wpt/resources/testharness.js` upstream (see cloned WPT checkout referenced at the top of this file) — worth diffing periodically to check for drift.

### lib/testharnessreport.js
**Module:** `testharnessreport`  
**Classification:** Standard (WHATWG/W3C test infra)  
**Spec:** https://github.com/web-platform-tests/wpt/blob/master/resources/testharnessreport.js  
**Exports:**
- `dump_test_results(tests, status)` - Vendor integration hook, reports results as JSON (browser) or console/global (non-browser)
- Re-exports everything from `testharness.js`

**Notes:** Near-verbatim port of WPT's vendor-integration reporter, adapted to also work when there's no `document`/`window` (dumps results via `console.log`/`globalThis` instead).

### lib/timers.js
**Module:** `timers`  
**Classification:** Standard (HTML5)  
**Spec:** https://html.spec.whatwg.org/multipage/timers-and-user-prompts.html  
**Exports:**
- `setTimeout(callback, delay, ...args)` - Delayed execution
- `clearTimeout(id)` - Cancel timeout
- `setInterval(callback, delay, ...args)` - Repeated execution
- `clearInterval(id)` - Cancel interval
- `setImmediate(callback, ...args)` - Immediate execution (Node.js)
- `clearImmediate(id)` - Cancel immediate (Node.js)
- `queueMicrotask(callback)` - Queue microtask

**Browser Compatibility:** Universal  
**Notes:** Implements HTML5 Timers API. Also includes Node.js-style `setImmediate` for compatibility.

### lib/tree_walker.js
**Module:** `tree_walker`  
**Classification:** Standard (WHATWG DOM)  
**Spec:** https://dom.spec.whatwg.org/#interface-treewalker  
**Exports:**
- `TreeWalker` class - DOM4 TreeWalker polyfill (parentNode/firstChild/nextSibling traversal with NodeFilter)
- `NodeFilter` constants - FILTER_ACCEPT/REJECT/SKIP, SHOW_* bitmask constants

**Notes:** Cross-browser polyfill for `TreeWalker`, distinct from the native `quickjs-tree-walker.c` binding (documented above, C is primary per project convention).

### lib/tty.js
**Module:** `tty`  
**Classification:** Compatible (Node.js)  
**Spec:** https://nodejs.org/api/tty.html  
**Exports:**
- `ReadStream(fd)` / `WriteStream(fd)` - TTY stream constructors
- `WriteStream.prototype.clearLine(dir)` - Clear current line
- `WriteStream.prototype.clearScreenDown()` - Clear screen from cursor down
- `WriteStream.prototype.cursorTo(x, y)` - Move cursor to position
- `WriteStream.prototype.moveCursor(dx, dy)` - Move cursor relative
- `WriteStream.prototype.getWindowSize()` - `[columns, rows]`
- `isatty(fd)` - Check if fd is a TTY

**Notes:** Node.js `tty` module shape (ANSI escape codes under the hood), built on `os`/`std` primitives; auto-updates `columns`/`rows` on SIGWINCH.

### lib/url.js
**Module:** `url`  
**Classification:** Standard (WHATWG)  
**Spec:** https://url.spec.whatwg.org/  
**Exports:**
- `URL` class - URL parser and serializer
- `URL.prototype.href` - Full URL string
- `URL.prototype.origin` - Origin (scheme + host + port)
- `URL.prototype.protocol` - URL scheme
- `URL.prototype.host` - Host + port
- `URL.prototype.hostname` - Host without port
- `URL.prototype.port` - Port number
- `URL.prototype.pathname` - Path component
- `URL.prototype.search` - Query string
- `URL.prototype.searchParams` - URLSearchParams object
- `URL.prototype.hash` - Fragment identifier
- `URLSearchParams` class - Query string parser
- `URL.createObjectURL(blob)` - Create object URL (not yet implemented)
- `URL.revokeObjectURL(url)` - Revoke object URL (not yet implemented)

**Browser Compatibility:** Chrome 32+, Firefox 19+, Safari 7+, Edge 12+  
**Notes:** Core URL parsing is implemented. Missing: `createObjectURL()` and `revokeObjectURL()` for Blob/File references (tracked in TODO Tier 9.6).

### lib/util.js
**Module:** `util`  
**Classification:** Compatible (Node.js util) + Custom (reflection extras)  
**Spec:** https://nodejs.org/api/util.html  
**Exports:** Large grab-bag re-exporting/wrapping ECMA-262 `Object`/`Reflect` statics (`getPrototypeOf`, `defineProperty`, `assign`, ...), Node-style helpers (`setImmediate`/`clearImmediate`, `queueMicrotask`, `inherits`), plus custom additions (`memoize`, `chain`, `TypeIds`, `types` type-check table, POSIX `errno` name table) - see file for full list (2500+ lines).

**Notes:** Mixes thin ECMA-262 `Object`/`Reflect` pass-throughs with Node.js `util`-style helpers and qjs-modules-specific utilities (memoize, type tables); no single spec covers it, closest is Node's `util` module for the parts that overlap.

### lib/vfs.js
**Module:** `vfs`  
**Classification:** Custom (union filesystem)  
**Exports:**
- `UnionFS` class - Overlays multiple search paths (each with its own fs-like backend, e.g. `Archive`) into one namespace
- `UnionFS.prototype.appendPath/prependPath/removePath/hasPath(path, impl)` - Manage search paths

**Notes:** Conceptually similar to the `unionfs`/`mount-fs` npm packages; not a standard API. Supports mounting `Archive` objects (tar/zip) as read-only filesystem layers alongside real `fs`. Distinct from `quickjs-virtual.c`'s `VirtualProperties`, which wraps a plain Object/Array/Map, not a filesystem.

### lib/xml.js
**Module:** `xml`  
**Classification:** Custom (low-level XML tokenizer)  
**Exports:**
- `parse(s)` - Tokenize/parse XML string
- `read(generator)` - Build tree from token generator
- `write(obj, level)` - Serialize object tree back to XML

**Notes:** Pure-JS XML lexer/parser polyfill; a different shape from DOM's `DOMParser`/`XMLSerializer` (no `Document`/`Element` objects). The native `quickjs-xml.c` binding (documented above) is authoritative per project convention.

### lib/xpath.js
**Module:** `xpath`  
**Classification:** Compatible (W3C DOM XPath)  
**Spec:** https://www.w3.org/TR/DOM-Level-3-XPath/xpath.html  
**Exports:**
- `XPath` class (aliased as `ImmutableXPath`, `MutableXPath`, `XPathExpression`) - Path-segment array/string handling
- `XPathEvaluator` class - Evaluates XPath expressions
- `XPathResult` class - Result wrapper
- `XPathException` class - Error type
- `parseXPath(str)` - Parse XPath string to segments
- `buildXPath(ptr, root)` / `getSiblings(ptr, root)` - Build/query paths from a `Pointer` (see `pointer` module)
- `DereferenceError` class - Custom error for failed dereferences

**Notes:** Mirrors DOM XPath class names (`XPathEvaluator`/`XPathResult`/`XPathException`) but is built on the project's own `Pointer`/`Predicate` addressing scheme rather than operating over `dom.js` `Node` trees directly — a compatible-shape, not a spec-literal implementation.

---

## Roadmap

Once research is complete, prioritize:

1. **Deprecate custom APIs** that have standard equivalents
2. **Fix incompatible APIs** to match standards more closely
3. **Document gaps** where standards exist but aren't implemented
4. **Keep internal APIs** that are implementation details (predicate, deep, etc.)
5. **Stop adding non-standard APIs** unless absolutely necessary

## Notes

- APIs like `deep` and `predicate` are internal implementation details used by other modules
- `inspect` is similar to Node.js `util.inspect` but with additional features (negative compact values)
- `stream` module implements WHATWG Streams spec
- `blob` module implements WHATWG Blob spec
- `dom` module implements W3C DOM spec
