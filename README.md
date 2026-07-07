# qjs-modules

A collection of native modules for [QuickJS](https://bellard.org/quickjs/).

Each module is built both as a shared library (importable at runtime by the
interpreter) and as a static library that can be linked into an embedding
application. Every `quickjs-*.c` file provides one module (except
`quickjs-internal.c`, which contains shared support code compiled into other
modules).

## Building

```sh
cmake -B build .
make -C build
```

The core modules are always built. Some modules depend on external libraries
and are gated by CMake options:

| Option | Module | Dependency |
|--------|--------|------------|
| `MODULE_ARCHIVE` | [archive](#archive) | libarchive (`BUILD_LIBARCHIVE` builds the bundled one) |
| `MODULE_BCRYPT` | [bcrypt](#bcrypt) | bundled libbcrypt |
| `MODULE_MAGIC` | [magic](#magic) | libmagic |
| `MODULE_MYSQL` | [mysql](#mysql) | MariaDB/MySQL client library |
| `MODULE_PGSQL` | [pgsql](#pgsql) | libpq |
| `MODULE_SQLITE` | [sqlite](#sqlite) | sqlite3 |
| `MODULE_SERIAL` | [serial](#serial) | libserialport (`BUILD_LIBSERIALPORT` builds the bundled one) |

In addition to the native modules documented below, the `lib/` directory
contains pure-JavaScript modules (`console`, `fs`, `process`, `repl`,
`require`, `util`, parsers, …) built on top of them.

## Module index

| Module | Exports | Description |
|--------|---------|-------------|
| [archive](#archive) | `Archive`, `ArchiveEntry`, `ArchiveMatch` | Read/write archives via libarchive |
| [arraybuffer_sink](#arraybuffer_sink) | `ArrayBufferSink` | Collect streamed writes into an ArrayBuffer |
| [bcrypt](#bcrypt) | `genSalt`, `hash`, `compare` | bcrypt password hashing |
| [bjson](#bjson) | `read`, `write` | Binary JSON (QuickJS object serialization) |
| [blob](#blob) | `Blob` | W3C-style binary blob |
| [child_process](#child_process) | `exec`, `spawn`, `ChildProcess`, … | Spawn and control subprocesses |
| [deep](#deep) | `find`, `get`, `set`, `iterate`, … | Deep object-tree traversal and manipulation |
| [directory](#directory) | `Directory` | Low-level directory reader (getdents) |
| [gpio](#gpio) | `GPIO` | Memory-mapped GPIO (Raspberry Pi) |
| [inspect](#inspect) | `inspect` | Pretty-print JS values (like Node's `util.inspect`) |
| [json](#json) | `read`, `write`, `JsonParser` | JSON parser/serializer with location info |
| [lexer](#lexer) | `Lexer`, `Token`, `Location` | Regex-rule based tokenizer |
| [list](#list) | `List`, `ListIterator`, `ListNode` | Doubly-linked list with Array-like API |
| [location](#location) | `Location` | Source position (file:line:column) |
| [magic](#magic) | `Magic` | File type detection via libmagic |
| [misc](#misc) | many functions | Grab-bag of OS, buffer, type and engine utilities |
| [mmap](#mmap) | `mmap`, `munmap`, … | Memory-mapped files as ArrayBuffers |
| [mysql](#mysql) | `MySQL`, `MySQLResult`, `MySQLError` | Non-blocking (promise-based) MySQL/MariaDB client |
| [path](#path) | `join`, `basename`, `resolve`, … | Path manipulation and filesystem tests |
| [pgsql](#pgsql) | `PGconn`, `PGresult`, `PGerror` | Non-blocking PostgreSQL client |
| [pointer](#pointer) | `Pointer`, `DereferenceError` | Object-graph paths (JSON-pointer-like) |
| [predicate](#predicate) | `Predicate` | Composable, callable predicate functions |
| [queue](#queue) | `Queue` | Chunked byte FIFO |
| [repeater](#repeater) | `Repeater` | Push-to-async-iterator bridge |
| [serial](#serial) | `Serial`, `SerialPort`, `SerialError` | Serial ports via libserialport |
| [sockets](#sockets) | `Socket`, `AsyncSocket`, `SockAddr`, … | BSD sockets, sync and async |
| [sqlite](#sqlite) | `SQLite3`, `SQLite3Result`, `SQLite3Error` | SQLite3 client |
| [stream](#stream) | `ReadableStream`, `WritableStream`, … | WHATWG-style streams (not built by default) |
| [syscallerror](#syscallerror) | `SyscallError` | Error class carrying syscall name + errno |
| [textcode](#textcode) | `TextDecoder`, `TextEncoder` | UTF-8/UTF-16/UTF-32 transcoding |
| [tree_walker](#tree_walker) | `TreeWalker`, `TreeIterator` | DOM-TreeWalker-style object traversal |
| [virtual](#virtual) | `VirtualProperties` | Uniform property access over Map/Array/Object |
| [xml](#xml) | `read`, `write` | XML parser and serializer |

---

## archive

Bindings to [libarchive](https://libarchive.org). Read and write many archive
formats (tar, zip, 7zip, iso9660, cpio, ar, …) with many compression filters
(gzip, bzip2, xz, zstd, lz4, …).

```js
import { Archive, ArchiveEntry } from 'archive';

// Iterate entries of an existing archive
const ar = Archive.read('dist.tar.gz');

for(const entry of ar) {
  console.log(entry.pathname, entry.size, entry.mtime);

  const buf = new ArrayBuffer(entry.size);
  ar.read(buf);       // read current entry's data
}

ar.close();
```

- **`Archive`** — `Archive.read(file)` / `Archive.write(file)` open an
  archive for reading/writing. Instance: `next()` (next `ArchiveEntry`),
  `read(buf)`, `write(buf)`, `skip()`, `seek(offset, whence)`,
  `extract(entry[, flags])`, `filterBytes(n)`, `close()`,
  `[Symbol.iterator]()`; getters `format`, `compression`, `filters`,
  `position`, `fileCount`, `blockSize`, `hasEncryptedEntries`, `errno`,
  `error`.
- **`ArchiveEntry`** — represents one archive member; read/write accessors
  for `pathname`, `size`, `mode`, `perm`, `filetype`/`type`, `uid`, `gid`,
  `uname`, `gname`, `atime`/`ctime`/`mtime`/`birthtime`, `symlink`,
  `hardlink`, `dev`/`rdev` (+ major/minor), `fflags`, `ino`, `nlink`;
  `isEncrypted`, `isDataEncrypted`, `isMetadataEncrypted`; `clone()`.
- **`ArchiveMatch`** — `include()` / `exclude()` pattern matching for
  selective extraction.
- Constants on `Archive`: `FORMAT_*`, `FILTER_*`, `EXTRACT_*` (extraction
  flags like `EXTRACT_PERM`, `EXTRACT_TIME`, `EXTRACT_SECURE_SYMLINKS`),
  result codes (`OK`, `EOF`, `RETRY`, `WARN`, `FAILED`, `FATAL`),
  `SEEK_SET`/`SEEK_CUR`/`SEEK_END`, and `version`.

## arraybuffer_sink

Collects a stream of writes into a single growing buffer.

```js
import { ArrayBufferSink } from 'arraybuffer_sink';

const sink = new ArrayBufferSink();
sink.write(new Uint8Array([1, 2, 3]));
sink.write('more data');            // strings and buffers accepted
console.log(sink.size);
const buf = sink.end();             // → ArrayBuffer with everything written
```

Methods: `write(data)`, `flush()`, `end()`; getter `size`.

## bcrypt

Password hashing using the bundled libbcrypt.

```js
import { genSalt, hash, compare, HASHSIZE, SALTSIZE } from 'bcrypt';

const salt = genSalt(12);            // work factor, default 12
const digest = hash('hunter2', salt); // or hash('hunter2', workFactor)
compare('hunter2', digest);          // → true
```

- `genSalt([workFactor][, buffer])` — generate a salt string (or fill a
  supplied buffer of at least `HASHSIZE` bytes).
- `hash(password, saltOrWorkFactor)` — returns the bcrypt digest string.
- `compare(password, digest)` — constant-time check, returns boolean.
- Constants: `HASHSIZE` (64), `SALTSIZE` (29).

## bjson

QuickJS's internal binary object serialization (`JS_ReadObject` /
`JS_WriteObject`) exposed to scripts. Handles cyclic references and can
serialize things JSON can't (typed arrays, Dates, …).

```js
import * as bjson from 'bjson';

const buf = bjson.write({ a: [1, 2, 3] });   // → ArrayBuffer
const obj = bjson.read(buf);                 // → deep copy of the value
```

- `write(value[, flagsOrRefBool])` → ArrayBuffer.
- `read(buffer[, , , flagsOrRefBool])` → value.
- Constants: `JS_READ_OBJ_BYTECODE`, `JS_READ_OBJ_REFERENCE`,
  `JS_READ_OBJ_SAB`, `JS_READ_OBJ_ROM_DATA`, `JS_WRITE_OBJ_BYTECODE`,
  `JS_WRITE_OBJ_REFERENCE`, `JS_WRITE_OBJ_SAB`, `JS_WRITE_OBJ_BSWAP`.
  Passing `true` instead of flags enables reference (cycle) support.

## blob

A [W3C Blob](https://developer.mozilla.org/en-US/docs/Web/API/Blob)-style
container for immutable binary data.

```js
import { Blob } from 'blob';

const blob = new Blob(['hello ', new Uint8Array([0x77, 0x6f]), 'rld'],
                      { type: 'text/plain' });

blob.size;                  // 11
await blob.text();          // 'hello world'
await blob.arrayBuffer();   // ArrayBuffer
await blob.bytes();         // Uint8Array
blob.slice(0, 5);           // → new Blob
```

Constructor takes an iterable of parts (`Blob | ArrayBuffer | TypedArray |
String`) and an options object with a `type` MIME string.

## child_process

Spawn and manage subprocesses.

```js
import { exec, execSync, spawn, spawnSync, kill, SIGINT } from 'child_process';

const cp = spawn('ls', ['-la'], {
  cwd: '/tmp',
  env: { PATH: '/usr/bin:/bin' },
  stdio: ['inherit', 'pipe', 'pipe'],
});

cp.pid;          // process id
cp.wait();       // wait for termination → exit status
cp.kill(SIGINT); // send a signal

cp.exitcode;     // exit code (or null)
cp.termsig;      // terminating signal (or null)
cp.exited; cp.signaled; cp.stopped; cp.continued;
```

- `spawn(file, args[, options])` / `spawnSync(...)` — start a process,
  returns a `ChildProcess`. Options: `cwd`, `env`, `stdio`
  (`'inherit'`/`'pipe'`/fd numbers, per stream), `usePath`.
- `exec(file, args)` / `execSync(file, args)` — simple execution.
- `kill(pid, signal)` — send a signal to any process.
- **`ChildProcess`** properties: `file`, `cwd`, `args`, `env`, `stdio`,
  `pid`, `exitcode`, `termsig`, `exited`, `signaled`, `stopped`,
  `continued`; methods `wait([flags])`, `kill([signal])`.
- Constants: `WNOHANG`, `WNOWAIT`, `WUNTRACED` and signal numbers
  (`SIGINT`, `SIGCHLD`, `SIGSTOP`, `SIGUSR1`, …).

## deep

Recursive traversal, search and manipulation of object trees. Paths are
arrays of keys, `Pointer`s or `/`-separated strings; behaviour is controlled
by a flags word.

```js
import * as deep from 'deep';

const obj = { a: { b: { c: 42 } } };

deep.get(obj, 'a.b.c');                    // 42
deep.set(obj, ['a', 'b', 'd'], 'hello');
deep.unset(obj, 'a.b.c');
deep.equals(x, y);                         // deep equality
deep.clone(obj);                           // deep copy

// find first value matching a predicate; select() finds all
deep.find(obj, (v, path) => v === 42, deep.RETURN_PATH);
deep.select(obj, v => typeof v == 'number');

// lazy traversal
for(const [value, path] of deep.iterate(obj, null, deep.RETURN_VALUE_PATH))
  console.log(path, value);

deep.flatten(obj);              // → map of path → leaf value
deep.pathOf(obj, needleValue);  // → path of a value
deep.forEach(obj, (v, path) => ...);
```

- **`DeepIterator`** — the iterator class behind `iterate()`; also
  constructible directly, with `next()`, `skip()`, `leave()`, `path`.
- Flags (combinable, exported as constants): what to yield
  (`RETURN_VALUE`, `RETURN_PATH`, `RETURN_VALUE_PATH`,
  `RETURN_PATH_VALUE`), path representation (`PATH_AS_ARRAY`,
  `PATH_AS_STRING`, `PATH_AS_POINTER`), traversal control (`RECURSE`,
  `NO_RECURSE`, `YIELD`, `YIELD_NO_RECURSE`), filtering (`FILTER_KEY_OF`,
  `FILTER_HAS_KEY`, `FILTER_NEGATE`, `NO_THROW`) and type masks
  (`TYPE_OBJECT`, `TYPE_STRING`, `TYPE_NUMBER`, `TYPE_ARRAY`,
  `TYPE_FUNCTION`, `TYPE_PRIMITIVE`, `TYPE_ALL`, …). The low 24 bits of the
  flags word are the maximum depth.

## directory

Low-level directory reader built directly on the `getdents(2)` syscall (no
libc `readdir`), usable as an iterator.

```js
import { Directory } from 'directory';

const dir = new Directory('.');           // or new Directory(fd)

for(const [name, type] of dir)
  if(type == Directory.TYPE_REG)
    console.log(name);
```

- Constructor: `new Directory(pathOrFd[, flags])`. Flags select what each
  iteration yields: `Directory.NAME`, `Directory.TYPE` or `Directory.BOTH`
  (default; yields `[name, type]`).
- Methods: `open(path)`, `adopt(fd)`, `close()`, `next()`, `valueOf()` (the
  fd), plus the iterator protocol.
- Entry type constants: `TYPE_REG`, `TYPE_DIR`, `TYPE_LNK`, `TYPE_BLK`,
  `TYPE_CHR`, `TYPE_FIFO`, `TYPE_SOCK`, `TYPE_MASK`.

## gpio

Memory-mapped GPIO register access (Raspberry Pi style, via `/dev/gpiomem`).

```js
import { GPIO } from 'gpio';

const gpio = new GPIO();
gpio.initPin(17, GPIO.OUTPUT);
gpio.setPin(17, GPIO.HIGH);
gpio.getPin(17);                // → 0 | 1
gpio.buffer;                    // raw register ArrayBuffer
```

Constants: `INPUT`, `OUTPUT`, `LOW`, `HIGH`.

## inspect

Pretty-printer for arbitrary JS values, modeled on Node's `util.inspect`.

```js
import inspect from 'inspect';

console.log(inspect(value, {
  depth: 4,
  colors: true,
  compact: 2,
  showHidden: false,
  customInspect: true,     // honors [Symbol.inspect] methods
  getters: false,
  reparseable: false,      // output re-evaluatable source
  maxArrayLength: 100,
  maxStringLength: Infinity,
  breakLength: 80,
  protoChain: 1,
}));
```

Objects can customize their output by defining a `[Symbol.inspect]` method.

## json

JSON reader/writer with source-location error reporting, plus an
incremental parser class.

```js
import { read, write, JsonParser } from 'json';

const value = read('{"a": [1,2,3]}', 'input.json');  // filename for errors
const text = write(value);

const parser = new JsonParser(...);
parser.parse(...);
parser.pos; parser.token; parser.state; parser.depth; parser.callback;
```

## lexer

A rule-based tokenizer: define token rules as regular expressions, get
`Token` objects with precise source locations. Used by the parser libraries
in `lib/`.

```js
import { Lexer, Token } from 'lexer';

const lex = new Lexer(source, Lexer.FIRST, 'file.js');

lex.define('name', /regexp/);   // named sub-expressions
lex.addRule('identifier', /[A-Za-z_][A-Za-z0-9_]*/);
lex.addRule('number', /[0-9]+/);
lex.addRule('ws', /[ \t\r\n]+/);

for(const tok of lex)
  console.log(tok.type, tok.lexeme, `${tok.loc}`);
```

- **`Lexer`** — constructor `new Lexer(input[, mode][, filename])`; mode is
  `Lexer.FIRST`, `Lexer.LONGEST` or `Lexer.LAST` (rule-matching strategy),
  optionally OR'ed with `YIELD_ID` / `YIELD_OBJ`. Rule management
  (`define`, `addRule`, `getRule`, `ruleNames`, `rules`), lexing
  (`next`/`nextToken`, `peek`/`peekToken`, `skipBytes`, `skipChars`,
  `skipToken`, `skipUntil`, `back`, `lex`, `tokens`, iterator protocol),
  start-condition states (`pushState`/`begin`, `popState`, `topState`,
  `states`, `stateDepth`, `stateStack`), position info (`loc`, `charPos`,
  `bytePos`, `currentLine`, `eof`, `fileName`, `input`, `lexeme`).
  Statics: `escape`, `unescape`, `toString`, `fromFile`.
- **`Token`** — `id`, `type` (rule name), `lexeme`, `loc` (a `Location`),
  `charRange`/`byteRange`, `charLength`/`byteLength`, `seq`, `rule`,
  `lexer`.
- Also re-exports **`Location`** (see [location](#location)).

## list

A doubly-linked list with the full `Array` method repertoire, plus C++-style
iterators that stay valid across mutation.

```js
import { List } from 'list';

const l = new List([1, 2, 3]);
l.push(4); l.unshift(0);
l.splice(2, 1);
l.sort((a, b) => a - b);

for(const v of l) console.log(v);

const it = l.begin();      // ListIterator; also end(), rbegin(), rend()
l.insert(it, 42);
l.erase(it);
```

- **`List`** — `push`, `pop`, `shift`, `unshift`, `at`, `includes`,
  `indexOf`, `lastIndexOf`, `find`, `findLast`, `findIndex`,
  `findLastIndex`, `concat`, `slice`, `splice`, `fill`, `reverse`,
  `toReversed`, `rotate`, `every`, `some`, `filter`, `map`, `forEach`,
  `reduce`, `reduceRight`, `sort`, `unique`, `merge`, `clear`, `keys`,
  `values`, `entries`, `length`; iterator accessors `begin`, `end`,
  `rbegin`, `rend`, `insert`, `insertBefore`, `erase`. Statics: `from`,
  `of`, `isList`.
- **`ListIterator`** — bidirectional iterator; `next()`, `equals()`,
  `copy()`, `isAccessible`, `container`, `type`.
- **`ListNode`** — a single node; `value`, `prev`, `next`, `linked`,
  `sentinel`, `equals()`, `valueOf()`.

## location

A source position — file, line, column — as used by [lexer](#lexer),
[json](#json) and [xml](#xml).

```js
import { Location } from 'location';

const loc = new Location('file.js', 10, 4);
loc.file; loc.line; loc.column;
loc.charOffset; loc.byteOffset;
`${loc}`;                       // 'file.js:10:4'
loc.clone(); loc.equal(other);
```

## magic

File type detection using
[libmagic](https://www.darwinsys.com/file/) (the library behind `file(1)`).

```js
import { Magic } from 'magic';

const m = new Magic(Magic.MIME_TYPE);   // flags, then optional db paths
m.file('picture.png');                  // 'image/png'
m.buffer(arrayBuffer);                  // detect from memory
m.descriptor(fd);                       // detect from open fd
```

Methods: `file(path)`, `buffer(buf)`, `descriptor(fd)`, `check(path)`,
`compile(path)`, `list(path)`, `load(path)`, `getflags()`/`setflags()`,
`getparam()`/`setparam()`, `version()`, `error`, `errno`.
Constants: `MIME_TYPE`, `MIME_ENCODING`, `MIME`, `SYMLINK`, `COMPRESS`,
`DEVICES`, `CONTINUE`, `RAW`, `EXTENSION`, `NO_CHECK_*`, `PARAM_*`,
`DEFAULT_DB`, `VERSION`, ….

## misc

A large grab-bag of utilities. Highlights, by area:

**Files & processes** —
`realpath`, `tempnam`, `mkstemp`, `fnmatch`, `glob`, `wordexp`, `watch`
(inotify), `daemon`, `fork`, `vfork`, `exec`, `kill`, `setsid`, `unlink`,
`link(at)`, `symlink(at)`, `chmod`/`fchmod`, `chown`/`fchown`/`lchown`,
`fsync`, `fdatasync`, `truncate`/`ftruncate`, `utime(s)`, `access`,
`fcntl`, `fstat`, `ioctl`, `ttySetRaw`.

**Process/system info** —
`getpid`, `getppid`, `gettid`, `getsid`, `getuid`/`geteuid`/`setuid`/…,
`getgid`/`getegid`/…, `hrtime`, `uname`, `getRelease`, `getExecutable`,
`getWorkingDirectory`, `getRootDirectory`, `getCommandLine`,
`getEnvironment`, `getProcStat`, `getProcMaps`, `getProcMounts`,
`getPerformanceCounter`, `getFileDescriptor`.

**Terminal** —
`getScreenSize`, `clearScreen`, `clearLine`, `setCursorPosition`,
`moveCursor`, `setTextAttribute`, `setTextColor`,
`getConsoleMode`/`setConsoleMode`.

**Buffers & strings** —
`toString`, `toArrayBuffer`, `toPointer`, `dupArrayBuffer`,
`sliceArrayBuffer`, `resizeArrayBuffer`, `concatArrayBuffer`,
`copyArrayBuffer`, `compareArrayBuffer`, `searchArrayBuffer`/`search`,
`fmemopen`, `strcmp`, `charCode`, `charLength`, `btoa`/`atob`,
`escape`/`unescape`, `quote`/`dequote`, bit helpers (`and`, `or`, `xor`,
`not`, `bits`, `bitfieldSet`, `bitfieldToArray`, `arrayToBitfield`).

**Engine introspection** —
`valueType`, `typeName`, `valueTag`, `valuePointer`, `objectClassId`,
`objectRefCount`, `objectOpaque`, `className`, `classId`, `classAtom`,
`stringPointer`, `stringLength`, `stringBuffer`, atom functions
(`findAtom`, `atomToString`, `valueToAtom`, …), `getPrototypeChain`,
`getOpCodes`, `getByteCode`, `writeObject`/`readObject`/`evalBinary`,
`promiseState`/`promiseResult`, `immutableClass`, `enqueueJob`,
`atexit`, `rand`/`randi`/`randb`/`srand`, and a full set of `is*`
type-testing functions (`isArray`, `isBigInt`, `isCFunction`,
`isConstructor`, `isError`, `isInstanceOf`, `isNull`, `isNumber`, …).

## mmap

Memory-map files (or anonymous memory) as ArrayBuffers.

```js
import { mmap, munmap, msync, PROT_READ, PROT_WRITE, MAP_SHARED } from 'mmap';

const buf = mmap(null, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
// ... use buf like any ArrayBuffer ...
msync(buf);
munmap(buf);      // detaches the ArrayBuffer
```

- `mmap(addr, length[, prot[, flags[, fd[, offset]]]])` — defaults:
  `PROT_READ|PROT_WRITE`, `MAP_ANONYMOUS`, `fd = -1`.
- `munmap(buf)`, `msync(buf[, length[, flags]])`,
  `mprotect(buf, length, prot)`, `filename(buf)`, `toString(buf)`.
- Constants: `PROT_*` (`READ`, `WRITE`, `EXEC`, `NONE`, …) and `MAP_*`
  (`SHARED`, `PRIVATE`, `ANONYMOUS`, `FIXED`, `LOCKED`, `POPULATE`, …).

## mysql

Non-blocking MySQL/MariaDB client. All I/O methods return promises
(using the MariaDB async API).

```js
import { MySQL } from 'mysql';

const db = new MySQL();
await db.connect({ host: 'localhost', user: 'me', password: 's3cret',
                   db: 'mydb', port: 3306 });
// or: connect(host, user, password, db, port, socket, flags)

db.resultType = MySQL.RESULT_OBJECT;   // rows as objects (or RESULT_STRING)

const res = await db.query(`SELECT * FROM users`);   // → MySQLResult

for await(const row of res)
  console.log(row);

db.close();
```

- **`MySQL`** — `connect(...)`, `query(sql)`/`execute(sql)`, `close()`,
  `escapeString(s)`, `getOption`/`setOption`; getters `errno`, `error`,
  `info`, `insertId`, `affectedRows`, `warningCount`, `fieldCount`,
  `moreResults`, `fd`, `charset`, `timeout`, `serverName`, `serverInfo`,
  `serverVersion`, `user`, `host`, `port`, `db`, `status`, `pending`.
  Statics: `escapeString`, `valueString`, `valuesString`, `insertQuery`,
  `clientInfo`, `clientVersion`, `threadSafe`.
- **`MySQLResult`** — `fetchRow()`, `fetchAssoc()`, `fetchField(i)`,
  `fetchFields()`, `next()`, `numRows`, `numFields`, `eof`; sync and async
  iterable.
- **`MySQLError`** — error class used for failures.
- Constants: `RESULT_OBJECT`/`RESULT_STRING`/`RESULT_TBLNAM`, `OPT_*`
  connection options, `STATUS_*`.

## path

Path manipulation and filesystem predicates.

```js
import * as path from 'path';

path.join('a', 'b', 'c.js');      // 'a/b/c.js'
path.basename('/foo/bar.txt');    // 'bar.txt'
path.dirname('/foo/bar.txt');     // '/foo'
path.extname('file.txt');         // '.txt'
path.resolve('rel/path');         // absolute path
path.normalize('a//b/../c');      // 'a/c'
path.relative('/from', '/to');
path.parse('/a/b.c');             // { root, dir, base, ext, name }
path.format({ dir: '/a', base: 'b.c' });
```

Also: `absolute`, `canonical`, `realpath`, `readlink`, `exists`,
`isAbsolute`, `isRelative`, `isDirectory`, `isFile`, `isSymlink`,
`isCharDev`, `isBlockDev`, `isFIFO`, `isSocket`, `isSeparator`,
`getcwd`, `gethome`, `getsep`, `components`, `at`, `length`, `right`,
`skip`, `skipSeparator`, `search`, `slice`, `fnmatch`, and the
constants `sep`, `delimiter`, `FNM_*`.

## pgsql

Non-blocking PostgreSQL client on libpq; I/O methods return promises.

```js
import { PGconn } from 'pgsql';

const pg = new PGconn();
await pg.connect('host=localhost dbname=mydb user=me'); // conninfo string

const res = await pg.query(`SELECT * FROM users`);      // → PGresult

for(const row of res)
  console.log(row);

pg.close();
```

- **`PGconn`** — `connect(conninfo)`, `query(sql)`/`execute(sql)`,
  `close()`, escaping helpers (`escapeString`, `escapeLiteral`,
  `escapeIdentifier`, `escapeBytea`, `unescapeBytea`), SQL builders
  (`valueString`, `valuesString`, `insertQuery`); getters `fd`,
  `errorMessage`, `cmdTuples`/`affectedRows`, `insertId`, `nonblocking`,
  `options`, `conninfo`, `charset`, `protocolVersion`, `serverVersion`,
  `user`, `password`, `host`, `port`, `db`. Statics: `escapeString`,
  `escapeBytea`, `unescapeBytea`.
- **`PGresult`** — `fetchRow()`, `fetchAssoc()`, `fetchField(i)`,
  `fetchFields()`, `next()`, `numRows`, `numFields`, `eof`; iterable.
- **`PGerror`** — error class.
- Constants: `RESULT_OBJECT`, `RESULT_STRING`, `RESULT_TBLNAM`.

## pointer

A path into an object graph (like a JSON Pointer), with path algebra.

```js
import { Pointer } from 'pointer';

const p = new Pointer('a.b.c');          // or ['a','b','c'] or '/a/b/c'
p.deref(obj);                            // walk obj along the path
p.toArray();                             // ['a','b','c']
`${p}`;                                  // string form

p.concat(other); p.slice(1); p.up(); p.down('d');
p.startsWith(q); p.endsWith(q); p.common(q); p.relativeTo(q);
p.equal(q); p.compare(q);
```

- **`Pointer`** — array-like ops (`push`, `pop`, `shift`, `unshift`, `at`,
  `slice`, `splice`, `concat`, `values`, `length`, iterator), navigation
  (`up`, `down`, `truncate`, `hier`), comparison (`equal`, `compare`,
  `common`, `relativeTo`, `startsWith`, `endsWith`), `deref(obj)`;
  getters `path`, `atoms`. Statics: `from`, `of`, `fromAtoms`, `ofAtoms`,
  `isPointer`.
- **`DereferenceError`** — thrown when `deref()` fails; carries `pointer`,
  `root` and `pos` of the failure.

## predicate

Composable, callable predicate objects. A `Predicate` instance is itself
callable, and predicates combine into expression trees that can be printed
(`toString`) or re-emitted as source (`toSource`).

```js
import { Predicate } from 'predicate';

const isFoo = Predicate.regexp(/^foo/);
const nonEmptyString = Predicate.and(Predicate.string(), Predicate.not(Predicate.equal('')));

isFoo('foobar');       // true — call directly
isFoo.eval('barbaz');  // false
```

Factories (each returns a `Predicate`): type tests (`type(mask)`,
`instanceOf(ctor)`, `prototypeIs(proto)`), values (`string(s)`,
`charset(chars)`, `regexp(re)`, `equal(v)`), logic (`and`, `or`, `xor`,
`not`, `notnot`), arithmetic (`add`, `sub`, `mul`, `div`, `mod`, `bor`,
`band`, `bnot`, `pow`, `sqrt`, `atan2`), structure (`has(prop)`,
`property(prop[, pred])`, `member(obj)`, `index(pos[, pred])`, `some(pred)`,
`every(pred)`), argument plumbing (`shift(n[, pred])`, `slice(start, end)`,
`function(fn[, this][, arity])`).

Instances: `eval(...args)` / `call(...args)`, `toString()`, `toSource()`,
`id` (type id, matching exported constants `Predicate.TYPE`,
`Predicate.REGEXP`, …), `length` (arity), `keys()`, `values()`. Arithmetic
operators (`+`, `-`, `*`, `/`, `%`, `|`, `&`, `**`) are overloaded via
`Symbol.operatorSet` where available.

## queue

A chunked byte FIFO: write buffers/strings in, read bytes out.

```js
import { Queue } from 'queue';

const q = new Queue();
q.write('hello');                    // string | ArrayBuffer | TypedArray
q.write(new Uint8Array([1, 2, 3]));

const out = new Uint8Array(4);
q.read(out);                         // dequeue into buffer → bytes read
q.peek(out);                         // read without consuming
q.skip(n);                           // discard n bytes

q.size;    // total bytes queued
q.empty;   // boolean
q.chunks;  // number of chunks
```

Also: `clear()`, `next()`, `chunk(i)`, `at(i)`, `head`, `tail`, and
iteration over chunks.

## repeater

An implementation of the
[Repeater](https://repeater.js.org/) pattern — turn push-based sources
(callbacks, events) into pull-based async iterators with backpressure.

```js
import { Repeater } from 'repeater';

const numbers = new Repeater(async (push, stop) => {
  push(1);
  push(2);
  await stop;
  cleanup();
});

for await(const n of numbers)
  console.log(n);
```

- Constructor: `new Repeater(executor)` where `executor(push, stop)`.
- Instance: `next()`, `state`, `[Symbol.asyncIterator]`.
- Statics: combinators `race`, `merge`, `zip`; state constants
  `INITIAL`, `STARTED`, `STOPPED`, `REJECTED`, `DONE`.

## serial

Serial-port I/O via libserialport, loosely modeled on the Web Serial API.

```js
import { Serial } from 'serial';

const ports = Serial.getPorts();          // enumerate SerialPort objects
const port = Serial.requestPort(...);

port.open({ baudRate: 115200, parity: ..., flowControl: ... });
port.write(data);
port.read(buffer);
port.drain(); port.flush();
port.getInfo(); port.getSignals(); port.setSignals(...);
port.close();

port.name; port.description; port.transport; port.fd;
port.inputWaiting; port.outputWaiting;
```

- **`Serial`** — `getPorts()`, `requestPort()`.
- **`SerialPort`** — constants `MODE_READ`, `MODE_WRITE`,
  `MODE_READ_WRITE`, `BUF_INPUT`, `BUF_OUTPUT`, `BUF_BOTH`, `ERR_*`.
- **`SerialError`** — error class.

## sockets

BSD sockets with both synchronous (`Socket`) and promise-based
(`AsyncSocket`) flavors. Failed syscalls throw `SyscallError`; non-fatal
non-blocking conditions (`EAGAIN`, `EINPROGRESS`, …) don't throw.

```js
import { Socket, AsyncSocket, SockAddr, AF_INET, SOCK_STREAM, IPPROTO_TCP } from 'sockets';

// Client
const sock = new Socket(AF_INET, SOCK_STREAM);
sock.connect(new SockAddr(AF_INET, '127.0.0.1', 8080));
sock.send('hello');
const buf = new ArrayBuffer(1024);
const n = sock.recv(buf);
sock.close();

// Server
const srv = new Socket(AF_INET, SOCK_STREAM);
srv.bind(new SockAddr(AF_INET, '0.0.0.0', 9000));
srv.listen(5);
const client = srv.accept();

// Async (await instead of blocking)
const as = new AsyncSocket(AF_INET, SOCK_STREAM);
await as.connect(addr);
await as.send('ping');
```

- **`Socket` / `AsyncSocket`** — `bind`, `connect`, `listen`, `accept`,
  `send`, `sendto`, `recv`, `recvfrom`, `sendmsg`/`recvmsg`,
  `sendmmsg`/`recvmmsg`, `shutdown`, `close`, `getsockopt`/`setsockopt`;
  properties `fd`, `open`, `eof`, `mode`, `nonblock`, `ndelay`, `af`,
  `local`, `remote` (SockAddrs), `errno`, `error`, `syscall`, `ret`.
  Static: `adopt(fd)`.
- **`SockAddr`** — `new SockAddr(family, address, port)` (or a path for
  `AF_UNIX`); `family`, `addr`, `port`, `path`, `buffer`, `byteLength`,
  `clone()`, `toString()`.
- Functions: `socketpair()`, `select()`, `poll()`, `getsockopt()`,
  `setsockopt()`.
- ~200 constants: `AF_*`, `SOCK_*`, `IPPROTO_*`, `SOL_*`, `SO_*`, `MSG_*`,
  `SHUT_RD`/`SHUT_WR`/`SHUT_RDWR`, `POLL*`, `O_NONBLOCK`, ….
- Re-exports **`SyscallError`** (see [syscallerror](#syscallerror)).

## sqlite

SQLite3 client following the same shape as the [mysql](#mysql) and
[pgsql](#pgsql) modules.

```js
import { SQLite3 } from 'sqlite';

const db = new SQLite3('test.sqlite3', SQLite3.OPEN_READWRITE | SQLite3.OPEN_CREATE);

db.exec(`CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)`);
const res = db.query(`SELECT * FROM t`);   // → SQLite3Result

for(const row of res)
  console.log(row);

db.insertId;       // lastInsertRowid
db.changes;        // affected rows
db.close();
```

- **`SQLite3`** — `open(filename[, flags])`, `query(sql)` (alias
  `execute`), `exec(sql)`, `close()`, `escapeString`, `quoteString`,
  `valueString`, `valuesString`, `insertQuery`; getters `errorMessage`,
  `errorCode`, `filename`, `changes`/`affectedRows`,
  `insertId`/`lastInsertRowid`, `totalChanges`.
- **`SQLite3Result`** — `fetchRow()`, `fetchAssoc()`, `fetchField(i)`,
  `fetchFields()`, `reset()`, `numRows`, `numFields`, `eof`; iterable.
- **`SQLite3Error`** — error class.
- Constants: `OPEN_READONLY`, `OPEN_READWRITE`, `OPEN_CREATE`, `OPEN_URI`,
  `OPEN_MEMORY`, `OPEN_*MUTEX`, `OPEN_*CACHE`; column types `INTEGER`,
  `FLOAT`, `TEXT`, `BLOB`, `NULL`; `RESULT_OBJECT`, `RESULT_STRING`.

## stream

WHATWG-Streams-style classes (currently disabled in the default build —
see the commented-out entry in `CMakeLists.txt`).

```js
import { ReadableStream, WritableStream, TransformStream } from 'stream';

const rs = new ReadableStream({
  start(controller) { ... },
  pull(controller) { controller.enqueue(chunk); },
  cancel(reason) { ... },
});

const reader = rs.getReader();
const { value, done } = await reader.read();
```

Exports: `ReadableStream` (+ `ReadableStreamDefaultReader`,
`ReadableStreamBYOBReader`, `ReadableStreamDefaultController`,
`ReadableByteStreamController`), `WritableStream`
(+ `WritableStreamDefaultWriter`, `WritableStreamDefaultController`) and
`TransformStream`.

## syscallerror

The error class used across these modules ([sockets](#sockets),
[misc](#misc), …) for failed system calls.

```js
import { SyscallError } from 'syscallerror';

try {
  sock.connect(addr);
} catch(e) {
  if(e instanceof SyscallError) {
    e.syscall;   // 'connect'
    e.errno;     // numeric errno
    e.message;   // 'connect: Connection refused'
    e.stack;
  }
}
```

Statics: `errno()` (current errno), `strerror(errno)`. The module also
exports the full set of `E*` errno constants (`EAGAIN`, `ENOENT`, …).

## textcode

Text encoding/decoding between strings and byte buffers. Unlike the
standard `TextEncoder`, encodings UTF-8, UTF-16 and UTF-32 (both endians)
are supported in both directions.

```js
import { TextEncoder, TextDecoder } from 'textcode';

const enc = new TextEncoder('utf-16le');
const bytes = enc.encode('hello');          // Uint8Array
enc.encodeInto('hello', targetU8);

const dec = new TextDecoder('utf-8');
dec.decode(bytes);                          // 'hello'
```

Both classes have `encoding`, `endian` and `buffered` getters; decoding is
streaming-capable (incomplete trailing sequences are buffered).

## tree_walker

Traverse object trees the way DOM `TreeWalker` traverses nodes.

```js
import { TreeWalker } from 'tree_walker';

const tw = new TreeWalker(root);

for(let node = tw.firstChild(); node; node = tw.nextNode())
  console.log(tw.currentPath, tw.currentNode);
```

- **`TreeWalker`** — navigation `firstChild()`, `lastChild()`,
  `nextNode()`, `previousNode()`, `nextSibling()`, `previousSibling()`,
  `parentNode()`; state `root`, `currentNode`, `currentKey`,
  `currentPath`, `depth`, `index`, `length`; filtering via `tagMask`
  (`TYPE_*` masks), `filter` and `flags` (`RETURN_VALUE`, `RETURN_PATH`,
  `RETURN_VALUE_PATH`, `FILTER_ACCEPT`/`REJECT`/`SKIP`).
- **`TreeIterator`** — same traversal as a JS iterator.

## virtual

`VirtualProperties` — a uniform `has`/`get`/`set`/`delete`/`keys` interface
over different container types (plain objects, `Map`s, arrays of
`[key, value]` entries), so code can manipulate any of them generically.

```js
import { VirtualProperties } from 'virtual';

const v = VirtualProperties.from(containerOrMapOrObject);
// or: VirtualProperties.map(m), .object(o), .array(a)

v.has('key');
v.get('key');
v.set('key', value);
v.delete('key');
v.keys();
```

## xml

Fast XML parser and serializer converting to/from a plain-object tree
(`{ tagName, attributes, children }`).

```js
import { read, write } from 'xml';

const doc = read('<root><item id="1">hello</item></root>');
// → [{ tagName: 'root', attributes: {}, children: [ ... ] }]

const str = write(doc);
```

- `read(input[, filename[, flags]])` — parse XML from a string or buffer.
- `write(obj[, indent])` — serialize an object tree back to XML text.
