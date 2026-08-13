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

This document is being actively researched. Agents are currently inventorying:
- All C native module exports (quickjs-*.c)
- All JS module exports (lib/*.js)

Results will be populated below as research completes.

---

## C Native Modules

### quickjs-archive.c
**Module:** `archive`  
**Status:** Researching...

### quickjs-arraybuffer-sink.c
**Module:** `arraybuffer-sink`  
**Status:** Researching...

### quickjs-bcrypt.c
**Module:** `bcrypt`  
**Status:** Researching...

### quickjs-bjson.c
**Module:** `bjson`  
**Status:** Researching...

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
**Status:** Researching...

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
**Status:** Researching...

### quickjs-gpio.c
**Module:** `gpio`  
**Status:** Researching...

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
**Status:** Researching...

### quickjs-lexer.c
**Module:** `lexer`  
**Status:** Researching...

### quickjs-list.c
**Module:** `list`  
**Status:** Researching...

### quickjs-location.c
**Module:** `location`  
**Status:** Researching...

### quickjs-magic.c
**Module:** `magic`  
**Status:** Researching...

### quickjs-misc.c
**Module:** `misc`  
**Status:** Researching...

### quickjs-mmap.c
**Module:** `mmap`  
**Status:** Researching...

### quickjs-mysql.c
**Module:** `mysql`  
**Status:** Researching...

### quickjs-path.c
**Module:** `path`  
**Status:** Researching...

### quickjs-pgsql.c
**Module:** `pgsql`  
**Status:** Researching...

### quickjs-pointer.c
**Module:** `pointer`  
**Status:** Researching...

### quickjs-predicate.c
**Module:** `predicate`  
**Status:** Researching...

### quickjs-queue.c
**Module:** `queue`  
**Status:** Researching...

### quickjs-repeater.c
**Module:** `repeater`  
**Status:** Researching...

### quickjs-serial.c
**Module:** `serial`  
**Status:** Researching...

### quickjs-sockets.c
**Module:** `sockets`  
**Status:** Researching...

### quickjs-sqlite.c
**Module:** `sqlite`  
**Status:** Researching...

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
**Status:** Researching...

### quickjs-textcode.c
**Module:** `textcode`  
**Status:** Researching...

### quickjs-tree-walker.c
**Module:** `tree-walker`  
**Status:** Researching...

### quickjs-virtual.c
**Module:** `virtual`  
**Status:** Researching...

### quickjs-xml.c
**Module:** `xml`  
**Status:** Researching...

---

## JavaScript Modules

### lib/abort.js
**Module:** `abort`  
**Status:** Researching...

### lib/arrayLike.js
**Module:** `arrayLike`  
**Status:** Researching...

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
**Status:** Researching...

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
**Status:** Researching...

### lib/css3-selectors.js
**Module:** `css3-selectors`  
**Status:** Researching...

### lib/database.js
**Module:** `database`  
**Status:** Researching...

### lib/db.js
**Module:** `db`  
**Status:** Researching...

### lib/dbi.js
**Module:** `dbi`  
**Status:** Researching...

### lib/deep.js
**Module:** `deep`  
**Status:** Researching...

### lib/describe-class.js
**Module:** `describe-class`  
**Status:** Researching...

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
**Notes:** Comprehensive DOM implementation covering 90%+ of browser DOM APIs. See TODO Tier 9 for remaining gaps: Fetch API, FormData, CSSOM, IntersectionObserver, ResizeObserver, WebSocket, Canvas, Web Workers.

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
**Status:** Researching...

### lib/extendArrayBuffer.js
**Module:** `extendArrayBuffer`  
**Status:** Researching...

### lib/extendAsyncFunction.js
**Module:** `extendAsyncFunction`  
**Status:** Researching...

### lib/extendAsyncGenerator.js
**Module:** `extendAsyncGenerator`  
**Status:** Researching...

### lib/extendFunction.js
**Module:** `extendFunction`  
**Status:** Researching...

### lib/extendGenerator.js
**Module:** `extendGenerator`  
**Status:** Researching...

### lib/extendMap.js
**Module:** `extendMap`  
**Status:** Researching...

### lib/extendMath.js
**Module:** `extendMath`  
**Status:** Researching...

### lib/extendObject.js
**Module:** `extendObject`  
**Status:** Researching...

### lib/extendSet.js
**Module:** `extendSet`  
**Status:** Researching...

### lib/file.js
**Module:** `file`  
**Status:** Researching...

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
**Notes:** Comprehensive Node.js fs API implementation. Uses quickjs-fs.c native bindings. Some advanced features (watch, streams) may have limitations vs Node.js.

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
**Notes:** Promise-based fs API. Wraps lib/fs.js callback API with Promise interface.

### lib/html.js
**Module:** `html`  
**Status:** Researching...

### lib/inotify.js
**Module:** `inotify`  
**Status:** Researching...

### lib/io.js
**Module:** `io`  
**Status:** Researching...

### lib/iterator.js
**Module:** `iterator`  
**Status:** Researching...

### lib/misc.js
**Module:** `misc`  
**Status:** Researching...

### lib/module.js
**Module:** `module`  
**Status:** Researching...

### lib/parsel.js
**Module:** `parsel`  
**Status:** Researching...

### lib/parser.js
**Module:** `parser`  
**Status:** Researching...

### lib/perf_hooks.js
**Module:** `perf_hooks`  
**Status:** Researching...

### lib/pointer.js
**Module:** `pointer`  
**Status:** Researching...

### lib/predicate.js
**Module:** `predicate`  
**Classification:** Internal  
**Exports:**
- `Predicate` class - Type predicate for operator overloading
- `Predicate.prototype.call(value)` - Test value against predicate
- `Predicate.prototype.and(other)` - Logical AND
- `Predicate.prototype.or(other)` - Logical OR
- `Predicate.prototype.not()` - Logical NOT
- `Predicate.type(typeCode)` - Create type predicate
- `Predicate.instance(class)` - Create instanceof predicate
- `Predicate.value(val)` - Create equality predicate

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

### lib/reflect.js
**Module:** `reflect`  
**Status:** Researching...

### lib/repl.js
**Module:** `repl`  
**Status:** Researching...

### lib/require.js
**Module:** `require`  
**Status:** Researching...

### lib/socklen_t.js
**Module:** `socklen_t`  
**Status:** Researching...

### lib/stack.js
**Module:** `stack`  
**Status:** Researching...

### lib/stream.js
**Module:** `stream`  
**Status:** Researching...

### lib/streams.js
**Module:** `streams`  
**Status:** Researching...

### lib/terminal.js
**Module:** `terminal`  
**Status:** Researching...

### lib/testharness.js
**Module:** `testharness`  
**Status:** Researching...

### lib/testharnessreport.js
**Module:** `testharnessreport`  
**Status:** Researching...

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
**Status:** Researching...

### lib/tty.js
**Module:** `tty`  
**Status:** Researching...

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
**Status:** Researching...

### lib/vfs.js
**Module:** `vfs`  
**Status:** Researching...

### lib/xml.js
**Module:** `xml`  
**Status:** Researching...

### lib/xpath.js
**Module:** `xpath`  
**Status:** Researching...

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
