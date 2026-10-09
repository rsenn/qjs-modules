# Node.js API compatibility gaps

Gaps between `lib/*.js` and the Node.js API documentation, found by comparing the docs
(`/tmp/node-doc/*.md`) with the code and by running differential batteries under node 22
and `qjsm` (`/tmp/claude-1000/cmp/b-*.mjs`). Moved here from `TODO.md` (formerly Tiers
14-22); see `api-compatibility.md` for the per-module classification and
`api-compatibility-plan.md` for the summary table and fix order.

## Sources

Every Node.js module has its API documentation as Markdown at
`https://github.com/nodejs/node/raw/refs/heads/main/doc/api/<name>.md`, for example
`.../doc/api/timers.md`, `.../doc/api/assert.md`, `.../doc/api/process.md`. Download it
to `/tmp/node-doc/<name>.md` and compare it with `lib/<name>.js` (section headings
list the documented members; the file also records the version each one was added in).

The list of all modules is the directory listing of that URL:

```sh
dlynx.sh https://github.com/nodejs/node/raw/refs/heads/main/doc/api/ | grep '\.md$' | uniq
```

## `lib/node/*`: Node-shaped layers (2026-10-09)

Modules whose Node API differs from the qjs-modules helpers live in `lib/node/<name>.js`
and are compiled as builtin `node_<name>` (CMake `BUILTINS_NODE`). `node:<name>` resolves
to them; a bare `<name>` does too when no builtin of that name exists (`buffer`).

| file | serves | notes |
| --- | --- | --- |
| `lib/node/buffer.js` | `node:buffer`, `buffer` | moved from `lib/buffer.js` |
| `lib/node/events.js` | `node:events` | `EventEmitter`, `once`, `on`, ...; `EventTarget` stays in `lib/events.js` |
| `lib/node/child_process.js` | `node:child_process` | layer over the native `child_process` |
| `lib/node/util.js` | `node:util` | Node's util; the bare `util` keeps the internal helpers |

Compile-time imports of these from other `lib/*.js` files need `-M node:events` in
`<module>_MODULES` (qjsc cannot resolve `node:` names otherwise).

### `lib/node/util.js` vs. `util.md`

Implemented: `inspect` (port of Node's formatter: depth, grouping, `<ref *1>`, classes, errors with
`cause`, typed arrays, `numericSeparator`, `sorted`, `getters`, `colors`/`styles`/`defaultOptions`,
`inspect.custom`), `format`/`formatWithOptions`, `types` (brand checks), `promisify` (+custom),
`callbackify`, `deprecate`, `debuglog`/`debug`, `inherits`, `isDeepStrictEqual` (+`skipPrototype`),
`isPartialDeepStrictEqual`, `parseArgs` (+tokens, `allowNegative`), `parseEnv`, `styleText`,
`stripVTControlCharacters`, `toUSVString`, `TextDecoder`/`TextEncoder`, `MIMEType`/`MIMEParams`,
`diff`, `debounce`, `throttle`, `getCallSites`, `getSystemError{Name,Map,Message}`,
`convertProcessSignalToExitCode`, `aborted`, `transferableAbort*`, `markPromiseAsHandled`,
legacy `is*`/`_extend`/`log`/`_errnoException`.

Gaps (engine limits): `inspect` cannot show Proxy target/handler, `Map`/`Set` iterator contents or
`WeakSet`/`WeakMap` entries; `types.isProxy/isExternal/isKeyObject/isCryptoKey` are `false`;
`ref()`/`unref()` of `debounce`/`throttle` are no-ops; `TextDecoder` supports utf-8, utf-16le and
windows-1252 only; `getCallSites().scriptId` is `'0'` and `sourceMap` is ignored.

## `lib/process.js` gaps vs. Node.js `process` (updated 2026-10-09)

Compared against the Node.js `process` docs (`/tmp/process.md`) and the export list
of `node:process` (`/tmp/process.describe.txt`). Everything in that export list is
implemented now (including named exports); events `exit beforeExit
uncaughtException uncaughtExceptionMonitor unhandledRejection rejectionHandled
warning SIG*` are wired through `__qjsm_hooks__` (`src/qjsm.c`). What differs:

- **stubs:** `getActiveResourcesInfo()` returns `[]`; `binding()` always throws;
  `allowedNodeEnvironmentFlags` is an empty `Set`; `features` reports no
  inspector/tls/uv; `report` has `getReport()/writeReport()` with a small payload.
- **not implemented:** `channel connected send disconnect` (no IPC),
  `permission`, `noDeprecation throwDeprecation traceDeprecation
  traceProcessWarnings` (+ the `DeprecationWarning` handling), `'message'`,
  `'disconnect'`, `'worker'`, `'workerMessage'` events.
- **approximations:** CPU times come from `/proc/*/stat` ticks (10 ms resolution);
  `memoryUsage()` has only a real `rss`; `resourceUsage()` fills fields it cannot
  read with 0; `uptime()` uses `/proc` start time; `getBuiltinModule()` loads
  through `requireModule()`; `dlopen()` ignores `flags`; `setSourceMapsEnabled()`
  only records the flag; `getgroups()` reads `/proc/self/status`.
- **event limits:** exceptions inside timer/IO callbacks never reach
  `uncaughtException` (printed by `js_std_loop()`); a rejection listener must exist
  before the rejection; `std.exit(n)` reports code 1 to `exit` listeners;
  `beforeExit` re-entry is decided by the loop taking >10 µs.
- **behaviour:** `nextTick` runs as a promise job (order vs. `Promise.then`
  differs from Node); `stdin/stdout/stderr` are `std` FILE objects with Node's
  `fd isTTY columns rows getWindowSize getColorDepth hasColors` added, not
  streams (per CLAUDE.md, no Node streams); `emitWarning()` ignores `ctor`;
  `uid gid euid egid` accessors remain as non-Node extras.

## `lib/timers.js` / `lib/timersPromises.js` gaps vs. Node.js `timers` (found 2026-10-09)

**Status (2026-10-09):** `lib/timers.js` rewritten: `Timeout`/`Immediate` classes (`ref/unref/hasRef/refresh/close/[Symbol.toPrimitive]/[Symbol.dispose]`), extra callback arguments, delay coercion, `ERR_INVALID_ARG_TYPE`, `this` = `Timeout`, `clearTimeout` accepts object/number/string, intervals clearable by either clear function, immediates queued during a pass run in the next one, globals replaced by these. Still open: `unref()` only records the flag (engine cannot exit with pending timers), sections C and D (`timers/promises` `ref`, `AbortController` global, `timers.promises`, uncaught timer exceptions).

Compared against the Node.js `timers` docs (`/tmp/node-doc/timers.md`) by reading both
files and probing under `qjsm`. Importing `timers`, `node:timers`, `timers/promises`
and `node:timers/promises` all resolve. Working: `setTimeout/clearTimeout`,
`setInterval/clearInterval`, `setImmediate/clearImmediate` (with args),
`timers/promises` `setTimeout/setImmediate/setInterval/scheduler` incl. `signal`.

The globals are `os.setTimeout`/`os.clearTimeout` (assigned in `src/qjsm.c`, and
`timers.js` only fills holes with `??=`), so the gaps in section A hit the globals too.

### A. Missing arguments and semantics

- `setTimeout(cb, delay, ...args)` — extra args are dropped (`os.setTimeout` takes two
  arguments); `setInterval(cb, delay, ...args)` likewise.
- `delay` coercion — Node: `> 2147483647`, `< 1` or `NaN` become `1`, fractions are
  truncated. Here `2**31+5` is kept (timer fires after ~24 days and keeps the loop
  alive), `NaN`/fractions are passed through to `os.setTimeout`.
- `setInterval(cb)` without delay — `t` is `undefined` (`setTimeout(…, undefined)`).
- callback validation — a bad callback throws `TypeError` without `code:
  'ERR_INVALID_ARG_TYPE'` (Node >= 18).
- `this` inside the callback is not the `Timeout` object.
- ordering — `setImmediate` is a 0 ms timer, so immediates queued from an immediate run
  in the same pass (`i1,i2,i3,c` seen), where Node defers `i3` to the next iteration.

### B. Missing `Timeout` / `Immediate` classes

All of `setTimeout/setInterval/setImmediate` return plain numbers (or the custom
counter id for intervals), so none of these exist:

- `Timeout`: `ref()`, `unref()`, `hasRef()`, `refresh()`, `close()`,
  `[Symbol.toPrimitive]()`, `[Symbol.dispose]()`.
- `Immediate`: `ref()`, `unref()`, `hasRef()`, `[Symbol.dispose]()`.
- `unref()` needs engine support: `os.setTimeout` timers always keep the loop alive.
- `clearTimeout/clearInterval` accept the number or string form of the primitive in
  Node; only numbers are tested here. `clearImmediate(undefined)`/`null` is safe.
- `setInterval` ids come from a separate counter and cannot be cleared by
  `clearTimeout` (Node allows both on any `Timeout`).

### C. `timers/promises`

- `options.ref` is ignored in `setTimeout/setImmediate/setInterval` and
  `scheduler.wait`.
- `setInterval()` does not validate `delay`/`options`; the abort check happens only per
  tick; `value` yields every tick correctly.
- `AbortController`/`AbortSignal` are not global in `qjsm` (`typeof AbortController`
  is `undefined`), so `{ signal }` is unusable without importing `abort` first.
- errors: `AbortError` has `code: 'ABORT_ERR'` and `cause`, matching Node; invalid
  argument errors lack `ERR_INVALID_ARG_TYPE` codes.

### D. Other

- `require('timers').promises` is not exposed on `timers` (Node's module has a
  `promises` getter); `timers` has no default export.
- exceptions thrown from timer callbacks are printed and the loop continues
  (including `setInterval`, which keeps firing); Node raises `uncaughtException`.

## `lib/assert.js` gaps vs. Node.js `assert` (found 2026-10-09)

**Status (2026-10-09):** `lib/assert.js` rewritten: real `AssertionError` (`code`, `generatedMessage`, `actual`, `expected`, `operator`, options-object constructor), Node message texts and `+ actual - expected` diff, Error-instance messages thrown as is, own deep-equality (Map/Set/Date/RegExp/typed arrays/boxed/prototypes/symbols/cycles), `throws/rejects` validation (RegExp, object, class, function, `ERR_AMBIGUOUS_ARGUMENT`), `assert.strict`, `assert/strict` (`lib/assertStrict.js`), `assert.Assert`. Still open: `CallTracker`, `Assert` `diff: 'full'` output (option accepted only), source-text in `assert.ok` messages.

Compared against `/tmp/node-doc/assert.md` and by running one 60-case battery under
node 22 and `qjsm` and diffing the results (`/tmp/claude-1000/cmp/b-assert.mjs`).
Every documented function exists except the ones in section C, but most of them
differ from Node in what they throw and in what they accept.

### A. The error object

- Functions throw a plain `Error` (message `assert.<fn>(): <text>`), not an
  `AssertionError`: `e.name` is `Error`, `e.code` is unset (Node: `'ERR_ASSERTION'`),
  and `generatedMessage`, `actual`, `expected`, `operator` are never set, so code that
  catches and inspects them breaks.
- `new AssertionError(options)` takes `(fn, message)` instead of Node's
  `{ message, actual, expected, operator, stackStartFn, diff }`;
  `new AssertionError({ message: 'hi' }).message` is `'assert.[object Object]()'`.
- `assert(0)` throws message `assertundefined()` (the `fn` argument is `undefined`).
- `assert.ok()` with no argument should say ``No value argument passed to `assert.ok()` ``.
- a custom `message` that is an `Error` instance must be thrown as is (`strictEqual(1,
  2, new RangeError('re'))`, `fail(new TypeError(..))`); here it is wrapped.
- generated messages differ: Node prints `Expected values to be strictly equal:` plus a
  `+ actual - expected` diff, `The expression evaluated to a falsy value:` plus the
  source text, `Failed` etc.; here `1 === '1'` style one-liners and the wrong function
  name (`deepStrictEqual` reports `assert.deepEqual()`, `doesNotThrow` reports
  `assert.doesNotReject()`). The ANSI reset `\x1b[0m` is embedded in the message text.
- `assert.match(1, /x/)` must fail with the `"string" argument must be of type string`
  message; here it reports a non-match.

### B. Comparison semantics (`lib/assert.js` delegates to `deep.equals`)

`deepStrictEqual` wrongly fails for equal `Map`, `Set`, `Date`, `RegExp`, and wrongly
passes for `Uint8Array` vs `Uint16Array`. Not checked: own symbol keys, `-0` vs `0`,
`Error` name/message, boxed primitives, prototype equality (`Object.create(null)` vs
`{}`). `deepEqual` (loose) fails `{a:1}` vs `{a:'1'}`; `strictEqual(NaN, NaN)` must
pass (`Object.is`) and fails.
- `throws(fn, expected)`: the `RegExp` form does not test the message (always passes),
  the object form does not compare properties, a string second argument is treated as
  valid instead of raising `ERR_AMBIGUOUS_ARGUMENT`; `rejects(asyncFn, { message })`
  calls the object (`TypeError: not a function`).
- `doesNotThrow/doesNotReject` messages must be `Got unwanted exception.` /
  `Got unwanted rejection.`.

### C. Missing exports

- `assert.strict` / named `strict` / the `assert/strict` entry point
  (`import 'assert/strict'` fails to resolve); strict mode where `equal`,
  `deepEqual`, `notEqual`, `notDeepEqual` alias their strict versions.
- `assert.Assert` class (`new assert.Assert({ diff: 'full' })`, v24+) and
  `assert.CallTracker` (deprecated).
- extra non-Node exports: `assert` (named), `assert_default`, `noop`.
- `assert.partialDeepStrictEqual` exists; its comparison shares the `deep.equals`
  limitations above.

## `lib/buffer.js` gaps vs. Node.js `buffer` (found 2026-10-09)

Compared against `/tmp/node-doc/buffer.md`: every documented `Buffer.*` and `buf.*`
member exists (checked by heading, 93 present) and a 90-case battery (alloc, from,
encodings, ranges, indexOf, fill, write, copy, swap, read/write of every integer, bigint,
float width, error codes, inspect, iteration) gave identical results under node 22 and
`qjsm` except for the items below (`/tmp/claude-1000/cmp/b-buffer.mjs`).

### A. Missing exports
- `File` (`buffer.File`, `new File([..], name, { lastModified, type })`, `file.name`,
  `file.lastModified`).
- `transcode(source, fromEnc, toEnc)` (throws `TypeError: not a function` today).
- `resolveObjectURL(id)` (needs `URL.createObjectURL`, see TODO.md Tier 9.6).
- `isLatin1(input)` (documented for current Node; absent in node 22 too).
- `Buffer.stringLength` and `blob.textStream()` are documented but absent.

### B. Globals
Importing `buffer` does not define `globalThis.Buffer`, `Blob`, `File`, `atob`, `btoa`
(`typeof` is `undefined` after `import 'buffer'`); in Node they are always global.
Whatever installs them for `qjsm` scripts is outside `lib/buffer.js` (see the `lib/globals.js` section).

### C. Small deviations
- `Buffer.from('abc').buffer === Buffer.from('abc').buffer` is `true` in Node
  (shared 8 KiB pool, `Buffer.poolSize`); here every buffer owns its `ArrayBuffer`.
  Harmless except for code that inspects `byteOffset` of small buffers.
- error text for `writeBigInt64LE(1)` / `writeBigUInt64LE(-1n)`: Node's type check
  message comes from the bigint arithmetic (`Cannot mix BigInt and other types`);
  here `ERR_INVALID_ARG_TYPE`; the out-of-range message prints the bound as a
  decimal, Node prints `2n ** 64n`.
- `btoa`/`atob` error messages differ (`Invalid character` in Node); the
  `InvalidCharacterError` name and code 5 match.
- `--zero-fill-buffers` and `buffer.constants` values are not wired to any option.

## `lib/console.js` gaps vs. Node.js `console` (found 2026-10-09)

Compared against `/tmp/node-doc/console.md` and a 55-case battery run under node 22 and
`qjsm` (`/tmp/claude-1000/cmp/b-console.mjs`). The global `console` of `qjsm` is
`new Console(out, { inspectOptions: { customInspect: true } })` created in `src/qjsm.c`;
`lib/console.js` implements `log info error warn debug time timeLog timeEnd` only.

### A. Missing methods (calling them throws `TypeError: not a function`)

`assert`, `clear`, `count`, `countReset`, `dir`, `dirxml`, `group`, `groupCollapsed`,
`groupEnd`, `table`, `trace`, `profile`, `profileEnd`, `timeStamp`, and the static
`console.Console` (and `console.context`, `console.createTask` in Node 22).
`import console from 'console'` has no own-property methods either: `console.group`
is `undefined` on both the default export and `globalThis.console`.

### B. Format string handling

`console.log('%s %d %i %f %j %o %O %% %c', ...)` prints the format string literally
and appends the arguments (`err %s x`); `%s %d %i %f %j %o %O %c %%` substitution,
`%d` with `0x10`/bigint, and `%s` of objects (`toString`, `-0`) are missing.
`console.log('100%')`, `'%'`, `'%x'` need the same escaping rules.

### C. Object formatting (`inspect` module, used by `log`)

Output differs from Node's `util.inspect` in nearly every non-trivial case, because
the Console is created with `compact: false` style (one entry per line, 1-space
indent): arrays and objects are always expanded, arrays do not wrap in columns
and truncate at 30 (`... 90 more items`) instead of 100; `depth` is unlimited (no
`[Object]`/`[Array]` at depth 2); `Map`/`Set` print as `Map {  1 => 2 }`/`Set [  1 ]`;
class instances lose their name (`{ x: 1 }` vs `A { x: 1 }`) and classes print as
`[Function: A]`; circular refs print `[loop]` not `<ref *1> … [Circular *1]`;
getters/setters are evaluated (`a: 1`) not shown as `[Getter]`/`[Setter]`;
`Date`/boxed primitives print as `Invalid Date`/`'s'`/`1`; sparse arrays print
`, ,` instead of `<1 empty item>`; `-0` prints `0`; `Promise`, `WeakMap`,
`ArrayBuffer`, typed arrays, `Object.create(null)` and function properties
differ; strings with `'` use `\'` instead of switching to double quotes;
`Symbol.for('nodejs.util.inspect.custom')` is not checked. `Buffer` is not a
global (see `lib/globals.js` below), so `console.log(Buffer.from(..))` throws a `ReferenceError`.

### D. `new Console(...)`

- the options form `new Console({ stdout, stderr, ignoreErrors, colorMode,
  inspectOptions, groupIndentation })` is not supported: the object is taken as
  `opts` and output still goes to the real stdout/stderr (captured string stays `''`).
- streams must be `std` FILE-like (`fileno()`); a plain `{ write(s) }` object or a
  Node-style `Writable` does not work (`new Console(w).error('e')` throws).
- `ignoreErrors`, `colorMode`, `groupIndentation` options are ignored.

### E. Timers

- `console.time`/`timeLog`/`timeEnd` print `label: 0.001267ms`; Node prints
  `label: 0.002ms` (3 decimals, `s`/`min`/`h` units above 1 s) and supports extra
  `timeLog(label, ...data)` arguments.
- unknown labels throw (`Error: Timer 'x' does not exist`); Node emits a process
  warning (`No such label 'x' for console.timeEnd()`) and continues; a duplicate
  `console.time('x')` should warn `Label 'x' already exists`.

## `lib/fs.js` / `lib/fsPromises.js` gaps vs. Node.js `fs` (found 2026-10-09)

Compared against `/tmp/node-doc/fs.md` (94 documented `fs.*` functions, 32 `fsPromises.*`,
22 `FileHandle` members) and a 103-case battery run under node 22 and `qjsm`
(`/tmp/claude-1000/cmp/b-fs2.mjs`, import `fs`/`fsPromises` as module *namespaces*): 38
cases give identical results, 65 differ. `lib/fs.js` is a thin layer over `std`/`os`
(`fopen`, `open`, `stat`, ...); the callback API is `cbify(xxxSync)` and `lib/fsPromises.js`
wraps the same sync functions.

### A. Missing functions

- `fs` (24): `fchmod(Sync)`, `fchown(Sync)`, `futimes(Sync)`, `glob`/`globSync`,
  `lchmod(Sync)`, `lchown` (only `lchownSync`), `lutimes` (only `lutimesSync`),
  `mkdtempDisposableSync`, `openAsBlob`, `opendir` (callback form; `opendirSync` exists),
  `readv(Sync)`, `writev(Sync)`, `statfs(Sync)`, `watchFile`, `unwatchFile`. `fs.watch`
  exists, `fs.promises.watch` too, but `FSWatcher`, `StatWatcher`, `ReadStream`,
  `WriteStream` classes do not (`fs.Dir`, `fs.Dirent`, `fs.Stats` do).
- `fsPromises` (3): `glob`, `lchmod`, `statfs`.
- `FileHandle` (9 of 22): `createReadStream`, `createWriteStream`, `readLines`, `readv`,
  `writev`, `readableWebStream`, `pull`, `pullSync`, `writer`.
- `realpathSync.native` / `realpath.native` are `undefined`.
- `fs.constants` lacks `UV_DIRENT_*` (8), `UV_FS_COPYFILE_EXCL/FICLONE/FICLONE_FORCE`,
  `UV_FS_SYMLINK_DIR/JUNCTION`, `UV_FS_O_FILEMAP`.
- Not Node API, extra exports of `fs`: `InvalidBuffer`, `buffer*` helpers, `chdir`,
  `F_*`/`O_*` fcntl numbers as top-level names, `FileHandle`; of `fsPromises`:
  `mkdtempDisposable`, `read`, `write`, `reader`, `readAll`.

### B. The default export is a different, smaller object

`import fs from 'fs'` (and therefore `node:fs` consumers using the default) lacks 14
documented sync functions that the namespace has: `appendFileSync`, `chmodSync`,
`chownSync`, `copyFileSync`, `cpSync`, `lchownSync`, `linkSync`, `lutimesSync`,
`mkdtempSync`, `opendirSync`, `rmSync`, `rmdirSync`, `truncateSync`, `utimesSync`
(`fs.default.rmSync` is `undefined`, `fs.default.copyFile` is a function).
`fs.promises` is an object but `fs.promises !== fsPromises` (Node: identical), and
`fsPromises.default` is an empty object.

### C. Data types

- `readFileSync`/`fsPromises.readFile` without encoding return an `ArrayBuffer`
  (Node: `Buffer`), so `.length`, `.toString('hex')`, `.join()` etc. fail; the same for
  `FileHandle#read`/`readFile` results (`[object ArrayBuffer]`) and `readdir` with
  `encoding: 'buffer'` (returns strings).
- `readFileSync(p, 'hex')` / `'latin1'` / `{ encoding: ... }` other than utf8 are ignored
  (returns the utf8 text); `writeFileSync(p, '6869', 'hex')` writes the literal text.
- `stat`/`lstat`: no `birthtime`/`birthtimeMs`/`blksize`, `{ bigint: true }` is
  ignored (numbers, no `mtimeNs`), `{ throwIfNoEntry: false }` still throws, and
  `stats.constructor` is `undefined` (`stats.constructor.name` throws) although
  `stats instanceof fs.Stats` is true.
- `Dirent` has only `name` (no `parentPath`/`path`); `readdirSync(p, { recursive: true })`
  works.
- `mkdirSync(p, { recursive: true })` returns the deepest path (Node: the first directory
  actually created, `undefined` when nothing was created); `fs.mkdir` with a callback
  and `fsPromises.mkdir` the same (`…/cbd/a/b` vs `…/cbd`).
- `writeFileSync(p, 123)` throws a non-`Error` value (message `undefined`); Node throws
  `TypeError [ERR_INVALID_ARG_TYPE]`.

### D. Errors

Node: `Error: ENOENT: no such file or directory, open '/x'` with own `errno` (-2), `code`,
`syscall` (`open`, `stat`, `scandir`, `lstat`, ...), `path`. Here the error is a
`SyscallError` whose `name` is the code (`e.name === 'ENOENT'`, Node: `'Error'`), the
message is `stat() = -1 (errno = 2): No such file or directory` (or
`fs.readFileSync('/x')() = -1 …`), `errno` is positive, `syscall` is the libc name
(`fopen` for open, `readdir` for scandir, or the whole `fs.readFileSync('/x')` string)
and `path` is never set. Other differences in the battery:
- `readFileSync(dir)`: `RangeError: invalid array index` (Node `EISDIR … read`);
- `rmSync(nonEmptyDir)` without `recursive`: `ENOTEMPTY rmdir` (Node
  `SystemError ERR_FS_EISDIR`); `rmSync(missing)` without `force` matches (`ENOENT lstat`);
- `openSync(p, 'zz')`: `TypeError: invalid file mode` (Node `ERR_INVALID_ARG_VALUE`);
- argument validation (`ERR_INVALID_ARG_TYPE` …) is missing; only `fs.readFile` without a
  callback throws `TypeError [ERR_INVALID_ARG_TYPE]` (message lacks `Received undefined`).

### E. Options and flags that are ignored or unsupported

- `openSync` flags `'wx'`, `'ax'`, `'rs'`, `'sx'`, … (exclusive/sync) throw
  `TypeError: invalid file mode`; only `r r+ w w+ a a+` (+`b`) work. Hence
  `copyFileSync(a, b, COPYFILE_EXCL)` throws `invalid file mode` instead of `EEXIST`.
- `writeFileSync(p, d, { mode: 0o600 })` ignores `mode` (file is `0664`); `flag` works.
- `readSync(fd, buf, { position, length })` (options form) ignores the object (reads
  4 bytes at 0 where Node reads 2 at position 1).
- `ftruncateSync(fd, len)` fails with `EINVAL ftruncate` for descriptors returned by
  `openSync` (these are `FILE` objects, `typeof fd` is `'object'`; Node: integer, as is
  the `fd` of a `FileHandle` here).
- `{ recursive: true }` for `rmSync`/`cpSync`/`readdirSync` is supported; `force` for
  `rmSync` is; `cpSync` options beyond `recursive/force/errorOnExist` (`filter`,
  `dereference`, `preserveTimestamps`, `verbatimSymlinks`) are ignored.

### F. Callback and promise API

- callbacks receive the sync return value as a second argument where Node passes only
  `err`: `fs.writeFile(p, d, cb)` → `cb(null, 1)`, `fs.rm(..., cb)` → `cb(null, 0)`,
  `fsPromises.writeFile` resolves `1`, `fsPromises.rm` resolves `0` (Node: `undefined`).
- `fs.opendir(path, cb)` throws `TypeError: not a function`.
- `fsPromises.opendir()` returns a `Dir` whose `read()`/`close()`/`entries()`/async
  iteration do not exist (only `readSync`/`closeSync`).
- `FileHandle#close()` twice rejects (`TypeError: invalid file handle`; Node resolves),
  operations after close reject `TypeError: invalid file handle` (Node `EBADF … file
  closed`); the class of `await fsPromises.open()` is named `PromiseFileHandle`.
- the callback API is `cbify(xxxSync)` and `fsPromises` wraps the sync functions, so all
  I/O blocks the event loop.

### G. Streams and watchers

`fs.createReadStream()` returns a plain `Object` with no `on`/`pipe`/async iterator (not
a `Readable`), so `for await (const c of fs.createReadStream(p))` throws `not a function`;
`fs.createWriteStream()` is not callable (`not a function`). `fs.watchFile`/`unwatchFile`
and `FSWatcher#ref/unref` are absent. A full `node:stream` layer is needed first.

### H. Hang

`fs.copyFileSync(src, dest)` (and therefore `copyFile`/`fsPromises.copyFile`) **never
returns for an empty source file**: `len = 0` and the copy loop condition
`len + pos + rem <= size` with `pos = len * i` stays `0 <= 0` (`lib/fs.js`
`copyFileSync`).

## `lib/globals.js` gaps vs. Node.js globals (found 2026-10-09)

Compared against `/tmp/node-doc/globals.md` (78 documented global names probed with
`n in globalThis`) and a 50-case battery under node 22 and `qjsm`
(`/tmp/claude-1000/cmp/b-globals.mjs`).

### A. Opt-in

A plain `qjsm script.js` has **none** of the web/Node globals except `process`,
`console`, `setTimeout/clearTimeout` and `module`: 71 of the 78 names are missing until
the script does `import 'globals'`. Node defines them all at startup. Options: import
`globals` from `qjsm` before the main script (or behind a `--no-globals` flag), at
least for the cheap, already-implemented ones.

### B. Still missing after `import 'globals'` (and present in Node 22)

- Classes: `Buffer`, `File`, `Event`, `CustomEvent`, `DOMException`, `MessageChannel`,
  `MessagePort`, `MessageEvent`, `BroadcastChannel`, `Navigator` (+ `navigator`),
  `CompressionStream`, `DecompressionStream`, `Crypto`, `CryptoKey`, `SubtleCrypto`
  (`crypto.subtle` is `undefined`), `PerformanceResourceTiming`, `WebAssembly`.
- Stream internals: `ReadableStreamDefaultReader`, `ReadableStreamBYOBReader`,
  `ReadableStreamBYOBRequest`, `ReadableByteStreamController`,
  `ReadableStreamDefaultController`, `WritableStreamDefaultWriter`,
  `WritableStreamDefaultController`, `TransformStreamDefaultController`.
- `global` (alias of `globalThis`), `navigator`.
- Out of scope here (sibling projects, CLAUDE.md): `fetch`, `Request`, `Response`,
  `Headers`, `FormData`, `WebSocket` — `../qjs-lws/lib/fetch.js`, `lib/websocket.js`.
- Not in Node 22 either (flag-gated or newer): `CloseEvent`, `ErrorEvent`,
  `EventSource`, `QuotaExceededError`, `Storage`/`localStorage`/`sessionStorage`,
  `URLPattern`, `Worker`.

### C. Behaviour differences of what is installed

- `Event` is missing but `EventTarget` exists: `new EventTarget().dispatchEvent` has no
  event class to pass, and `addEventListener('x', { handleEvent })` throws
  `The "listener" argument must be of type function` (objects with `handleEvent`,
  `{ once }`, `{ signal }` options, `dispatchEvent` return value untested).
- `AbortController`: default reason is the string-ish `AbortError: This operation was
  aborted` rather than a `DOMException` (`name 'AbortError'`, `code 20`);
  `abort()` called twice fires the `abort` listeners again (Node: once);
  `AbortSignal.any([..])` throws `cannot read property 'reason' of undefined`;
  `AbortSignal.timeout(ms)` rejects with a plain `Error` (Node: `DOMException`
  `TimeoutError`, `code 23`).
- `performance`: `timeOrigin`, `mark`, `measure`, `getEntries*`, `toJSON` missing
  and its tag is `[object Object]` not `[object Performance]` (see `perf_hooks` below).
- `TextDecoder`/`TextEncoder`: `encoding` is `UTF-8`/`UTF-16` (spec: `utf-8`,
  `utf-16le`); `{ fatal: true }` does not throw `ERR_ENCODING_INVALID_ENCODED_DATA`.
- `structuredClone()`/`queueMicrotask(5)` throw `TypeError` without Node's
  `ERR_MISSING_ARGS`/`ERR_INVALID_ARG_TYPE` codes; `btoa`/`atob` and
  `crypto.getRandomValues` errors lack the DOMException `code` (17, 22) and use
  different texts.
- property attributes: `structuredClone`, `atob`, `btoa`, `queueMicrotask` should be
  enumerable (Node: `EWC`); here non-enumerable.
- `module` exists as a global object in `qjsm` scripts (set by `jsm_stack_load`);
  `__dirname`, `__filename`, `require`, `exports` are not defined in ES modules
  (same as Node's ESM), but `require`/`exports` are also undefined for scripts run
  as CommonJS-style files.

## `lib/perf_hooks.js` gaps vs. Node.js `perf_hooks` (found 2026-10-09)

Compared against `/tmp/node-doc/perf_hooks.md` (headings) and a 30-case battery under
node 22 and `qjsm` (`/tmp/claude-1000/cmp/b-perf.mjs`). The user timing core matches
Node: `performance.now()`, `mark()` (incl. `detail`, `startTime`), `measure()` (names and
options form), `getEntries*`, `clearMarks/clearMeasures`, `PerformanceMark/Measure/Entry`
classes and `instanceof` relations.

### A. Missing exports
- `createHistogram([options])` + the `RecordableHistogram`/`Histogram` API (`record`,
  `recordDelta`, `add`, `reset`, `count`, `min`, `max`, `mean`, `stddev`, `percentile`,
  `percentiles`, `exceeds`, their `*BigInt` forms); the newer `createSlidingWindowHistogram`,
  `importHistogram` and statistics methods in the doc (`cdf`, `ccdf`, `ksTest`,
  `welchTest`, …) exist in no released Node and can wait.
- `monitorEventLoopDelay([options])` (`ELDHistogram`: `enable()/disable()`); needs the
  histogram above and a loop-delay sampler (a repeating `os.setTimeout` probe).
- `eventLoopUtilization()` (export and `performance.eventLoopUtilization`),
  `timerify(fn, options)` (export and method; wraps `fn` as `timerified <name>` and
  emits `function` entries).
- `constants` (`NODE_PERFORMANCE_GC_*`), `PerformanceResourceTiming` class.
- `now` is exported by `lib/perf_hooks.js` but not by Node (harmless).

### B. Missing members of `performance`
`nodeTiming` (`PerformanceNodeTiming`: `nodeStart`, `v8Start`, `environment`, `loopStart`,
`loopExit`, `bootstrapComplete`, `idleTime`, `uvMetricsInfo`), `clearResourceTimings`,
`markResourceTiming`, `setResourceTimingBufferSize`, `addEventListener` /
`EventTarget` behaviour (`performance` is an `EventTarget` in Node), and `toJSON()`
returns only `{ timeOrigin }` (Node: `nodeTiming`, `timeOrigin`,
`eventLoopUtilization`).

### C. `PerformanceObserver`
`PerformanceObserver.supportedEntryTypes` is `['mark', 'measure']`; Node adds `dns`,
`function`, `gc`, `http`, `http2`, `net`, `resource`. Entry types `function` (from
`timerify`) and `resource` need the pieces in A; `gc` has no QuickJS equivalent
(could be fed from `JS_SetGCThreshold`/memory-usage sampling, low value).

### D. Not applicable
`PerformanceNodeEntry` / `gc` flags, `http`/`http2`/`net`/`dns` entries,
`uvMetricsInfo` — there is no libuv; return zeros or omit and document it.

## `lib/tty.js` gaps vs. Node.js `tty` (found 2026-10-09)

**Status (2026-10-09):** `lib/tty.js` rewritten: loads; `isatty` validation; `ReadStream` (`isTTY`, `isRaw`, `setRawMode`, `rawMode`); `WriteStream` (`isTTY`, `fd`, `columns/rows`, `'resize'` event + `on/once/off/emit`, `getColorDepth`, `hasColors`, `write`, callbacks, 0-based `cursorTo`, fixed `moveCursor`); `ERR_INVALID_ARG_TYPE`/`ERR_OUT_OF_RANGE`/`ERR_TTY_INIT_FAILED`. Still open: no real streams (`pipe`, `'data'`).

Compared against `/tmp/node-doc/tty.md` and `/tmp/claude-1000/cmp/b-tty.mjs` (run under a
pty with node 22 and `qjsm`).

### A. The module does not load

`import 'tty'` fails under `qjsm` with `SyntaxError: Could not find export 'fdopenSync' in
module 'std'`: `std` exports `fdopen` (there is no `fdopenSync`). It is also compiled into
`qjsm` as a builtin, and `SIGWINCH` is imported from `util`, which does not export it (nor
does `os`; the constant is 28 on Linux — `misc.signum('SIGWINCH')` can supply it, see
`quickjs-misc.c`). Nothing else in `lib/` or `src/` imports `tty` today, so nobody noticed.
Everything below is therefore unverified at runtime; it is a reading of the code.

### B. API gaps (from the source and the doc)

- `tty.isatty(fd)` re-exports `os.isatty`: Node returns `false` for a non-integer or
  negative `fd` and for an invalid one; `os.isatty` may throw on non-numbers.
- `tty.ReadStream(fd)` / `tty.WriteStream(fd)` return `std` FILEs re-prototyped, not
  streams: no `.on('data'|'end'|'resize')`, `pipe()`, `write(chunk, enc, cb)`,
  `end()`, `readable`/`writable` flags. Per CLAUDE.md the replacement should be a
  WHATWG-stream-shaped surface, not `Duplex`.
- `ReadStream`: `isTTY`, `isRaw`, `setRawMode(mode)` (accepts `false` to restore the
  terminal), `rawMode` are not defined (`os.ttySetRaw` exists but is one-way).
- `WriteStream`: `isTTY` is not set on the instance; `'resize'` event is replaced by an
  ad-hoc `onresize` callback; `getColorDepth([env])` and `hasColors([count][, env])`
  are missing (the versions added to `process.stdout` in `lib/process.js` could be
  shared); `moveCursor(dx, dy, cb)` emits `ESC[n;A` with a stray `;` and tests `dy`
  where it should test `dx`/`dy` per axis; `cursorTo(x, y, cb)` ignores the callback and
  uses 1-based columns (Node: 0-based, `cursorTo(x)` → `ESC[(x+1)G`);
  `clearLine(dir, cb)` maps `dir` through `[1, 2, 0][dir + 1]` (Node: `-1` left =
  `ESC[1K`, `0` whole = `ESC[2K`... — verify) and `clearScreenDown` ignores the callback.
- `new WriteStream(fd)` for a non-tty fd returns a plain FILE without the tty methods
  (Node throws `ERR_TTY_INIT_FAILED`); invalid `fd` should throw `ERR_INVALID_FD`.
- no `tty.ReadStream`/`WriteStream` `instanceof` relation to `Readable`/`Writable`
  (not planned, see CLAUDE.md).

## `lib/url.js` gaps vs. Node.js `url` (found 2026-10-09)

Compared against `/tmp/node-doc/url.md` and a 45-case battery under node 22 and `qjsm`
(`/tmp/claude-1000/cmp/b-url.mjs`). `lib/url.js` is a 339-line hand-written parser, not a
WHATWG URL implementation; only plain `http(s)://host/path?query#hash` strings with a
well-formed authority come out right. Exports today: `URL`, `URLSearchParams`,
`fileURLToPath`, `pathToFileURL`.

### A. Crashes
- `URLSearchParams.toString()` throws `ReferenceError: 'replace' is not defined`
  (`lib/url.js:69`: the encode table is called `replace` but never declared), which also
  breaks `URL.search`/`href` after any `searchParams` change, `new URL('http://a/a b')`,
  percent-encoding in paths, and every setter (`pathname`, `search`, `hash`, …).
- `URL.prototype.toJSON` is missing (`JSON.stringify(url)` throws `not a function`).
- iterating `URLSearchParams` (`[...p]`, `for…of`) throws `value is not iterable`
  (`[Symbol.iterator]` missing); `new URLSearchParams(otherParams)` copies a `size`
  entry instead of the pairs.

### B. URL parsing (WHATWG URL Standard)
- no dot-segment removal (`/a/./b/../c` stays), no relative resolution against a base
  (`new URL('../x?y#z', 'http://a.com/b/c/d')` gives `http://a.com/..`), no `//host`
  scheme-relative or `/abs` handling to speak of.
- default ports are kept (`http://a:80/` → `:80`; spec: stripped); host is not
  lowercased fully / IDNA-encoded (`münchen.de` should be `xn--mnchen-3ya.de`),
  IPv4 shorthand (`0x7f.1`, trailing dot) and IPv6 (`[::1]:3000` → host split at the
  first `:`) are not normalised.
- non-special schemes: `origin` is `scheme://` instead of `'null'`; `blob:` origin
  is not derived from the inner URL; `file:` paths are not percent-encoded
  (`/tmp/a b`).
- no percent-encode sets (path, query, fragment, userinfo differ per component).
- invalid input throws `TypeError` without `code: 'ERR_INVALID_URL'` and the message
  includes the input (`Invalid URL: not a url`; Node: `Invalid URL`, with `input`
  property); assigning an invalid `href` is accepted silently.
- setters (`protocol`, `host`, `hostname`, `port`, `pathname`, `search`, `hash`,
  `username`, `password`, `href`) need the state-machine behaviour of the spec
  (e.g. `port = '80'` on `http:` → `''`); `searchParams` is not live-linked to
  `search` (both directions).
- missing statics: `URL.canParse` returns wrong results for relative input
  (`canParse('x','http://a')`), `URL.parse` is present, `URL.createObjectURL`,
  `URL.revokeObjectURL` (also blocks `buffer.resolveObjectURL`, `lib/buffer.js` below).
- `Object.prototype.toString.call(url)` is `[object Object]`; Node: `[object URL]`
  (`Symbol.toStringTag` on `URL`/`URLSearchParams`); `Object.keys(url)` has 6 own keys,
  spec: none (accessors on the prototype).

### C. URLSearchParams
After fixing A: `has(name, value)`, `delete(name, value)`, `size`, `sort()` (stable,
UTF-16 order), `forEach(cb, thisArg)`, application/x-www-form-urlencoded encoding
(`+` for space, `*-._` kept, others percent-encoded), tuple validation
(`new URLSearchParams([['a']])` → `ERR_INVALID_TUPLE`; here silently accepted), init
from `Map`/record/iterable/leading `?`.

### D. Missing legacy and helper API
`url.parse()` (+ `Url` class, `slashes`, `auth`, `query` with `parseQueryString`),
`url.format()` (string, legacy object and `URL` with `{ auth, fragment, search,
unicode }` options), `url.resolve()`, `url.resolveObject()`, `url.domainToASCII()`,
`url.domainToUnicode()`, `url.urlToHttpOptions()`, `url.fileURLToPathBuffer()`.
`fileURLToPath`/`pathToFileURL` exist but do not percent-decode/encode (`a%20b` stays,
`#`/`?` in paths are not escaped) and ignore the `{ windows }` option;
`fileURLToPath('http://a/b')` must throw `ERR_INVALID_URL_SCHEME`, and a `file://host/x`
URL `ERR_INVALID_FILE_URL_HOST`.

### E. Missing class
`URLPattern` (`new URLPattern(input[, baseURL][, options])`, `exec()`, `test()`,
component getters); not in Node 22 without a flag, low priority.

### Suggested approach
Port the WHATWG parsing state machine (or wrap a small C implementation such as
`ada`-style parsing in `quickjs-url.c`) behind the existing class names instead of
patching the string-splitting parser; a `tests/unittests/test-url.js` driven by the
web-platform-tests `urltestdata.json` would pin it down.

## `quickjs-path.c` gaps vs. Node.js `path` (found 2026-10-09)

Compared against `/tmp/node-doc/path.md` and a 190-case battery (every function with edge
inputs, POSIX and win32, plus argument validation) run under node 22 and `qjsm`
(`/tmp/claude-1000/cmp/b-path.mjs`). The `path` module of `qjsm` is the native
`quickjs-path.c`, designed around its own API (`absolute`, `at`, `baselen`, `canonical`,
`components`, `dirlen`, `equal`, `exists`, `extlen`, `fnmatch`, `getcwd`, `isin`,
`skip`, `slice`, …); the Node names exist but are string-slicing helpers, not Node's
normalising implementations. Code written for `node:path` produces wrong results.

### A. Missing API
- `path.win32` (the whole Windows implementation: drive letters, UNC, `\` separators,
  `C:` per-drive cwd), and the `path/win32` subpath.
- `path.matchesGlob(path, pattern)` (could wrap `fnmatch` with `FNM_PATHNAME`; Node
  semantics: `**`, `{a,b}`, `[!x]`, leading-dot files not matched by `*`).
- `path.toNamespacedPath(path)` (identity on POSIX), `path._makeLong`.
- `path.posix.win32`/`path.win32.posix` cross links; `path.posix === path.posix.posix`.
- `import path from 'path'` yields an object with only `posix`, so `path.join` is
  `undefined` on the default export; Node's default export is the module itself.
  Named imports (`import { join } from 'path'`) work.
- specifiers: `path/posix`, `path/win32` and `node:path/posix` fail to resolve;
  `node:path` resolves (checked with `qjsm -e "import('node:path')"`).

### B. Results that differ from Node (POSIX)

**Fixed (2026-10-09):** every row below now matches Node (`src/path.c`: `path_normalize*`,
`path_absolute3`, `path_dirlen2`, `path_basename3`, `path_extname1`, `path_relative5`;
`quickjs-path.c`: `join`, `resolve`, `parse`, `format`). The table records the old results.

| call | here | Node |
| --- | --- | --- |
| `join('/foo','bar','baz/asdf','quux','..')` | `/foo/bar/baz/asdf/quux/..` | `/foo/bar/baz/asdf` |
| `join()`, `join('')`, `normalize('')` | `''` | `'.'` |
| `join('/a/','/b/')` | `/b/` (absolute wins) | `/a/b/` |
| `join('a//b','c/')` | `a//b/c/` | `a/b/c/` |
| `normalize('/..')`, `normalize('/a/b/../../..')` | `/..` | `/` |
| `normalize('a/./b/.')`, `normalize('./a/./')` | `a/b/`, `./a/` | `a/b`, `a/` |
| `resolve('/a','','b')` | `/a/bb` (corrupt) | `/a/b` |
| `resolve('/a/b','../../../..')` | `/../..` | `/` |
| `resolve('/')` | `<cwd>/\0` (garbage) | `/` |
| `dirname('/foo')`, `dirname('/')`, `dirname('//a')` | `.` | `/`, `/`, `//` |
| `dirname('a//b')` | `a` | `a/` |
| `basename('')`, `parse('').base` | `"\0"` (reads past the end) | `''` |
| `basename('a.html','a.html')`, `basename('aaa','aaa')` | the name | `''` |
| `extname('..')`, `parse('..').ext` | `.` | `''` |
| `extname('a.b/')` | `''` | `.b` |
| `relative('/a/b','/a/b')` | `.` | `''` |
| `parse('/')` | `dir: ''` | `dir: '/'` |
| `format({root:'/ignored',dir:'/home/user/dir',base:'file.txt'})` | `/ignored/home/user/dir/file.txt` | `/home/user/dir/file.txt` (`root` ignored when `dir` is set) |
| `format({root:'/',base:'file.txt',ext:'ignored'})`, `…name:'file',ext:'.txt'` | `//file.txt` | `/file.txt` |
| `format({root:'/',name:'file',ext:'txt'})` | `//filetxt` | `/file.txt` (dot added, v19+) |
| `format({root:'/'})`, `format({ext:'.e'})` | `//`, `''` | `/`, `.e` |

`join`/`normalize`/`resolve` do not resolve `.`/`..` or collapse repeated separators,
which is the core of `node:path`; `\0` in results is an out-of-bounds read of the
terminating byte for empty input (a real out-of-bounds read).

### C. Argument validation
Node throws `TypeError [ERR_INVALID_ARG_TYPE]: The "path" argument must be of type
string. Received …` for every non-string argument. Here `basename(5)` returns `'5'`,
`dirname(null)` `'.'`, `extname({})` `''`, `join('a',5)` `a/5`, `relative('a',1)`
`../1`, `parse(5)` an object with `base: '5'`, `format('x')` `''`, `isAbsolute(1)`
`false`, `basename('a', 5)` `a`; only `normalize(undefined)` and `resolve('a', null)`
throw (a plain `TypeError`, `argument 1 must be a string`, without `code`).

### D. Suggested approach
Keep `quickjs-path.c` as the native primitive layer and add a thin `lib/path.js`
(or a second export table in C) that implements `node:path` exactly (port of Node's
`lib/path.js` posix and win32 functions, ~1500 lines of plain JS, no dependencies),
exporting `posix`, `win32`, the default export and `path/posix`, `path/win32`; reuse
`fnmatch` for `matchesGlob`. Run the 190-case battery against it.

## `quickjs-child-process.c` gaps vs. Node.js `child_process` (found 2026-10-09)

**Status (2026-10-09):** the bare `child_process` stays the native module; `node:child_process`
now loads `lib/node/child_process.js` (builtin `node_child_process`, mapped in
`jsm_module_normalize_core()`; CMake: `BUILTINS_NODE`), a layer over the native primitives
that adds: `spawn/spawnSync/exec/execSync/execFile/execFileSync/fork` argument handling,
`ChildProcess` as an `EventEmitter` (`spawn exit close error`), `stdout/stderr` readers and
`stdin` writer objects (`data/end`, `setEncoding`, async iterator, `write/end`), `shell`,
`input`, `encoding`, `maxBuffer`, `timeout`, `killSignal`, `signal`, ENOENT/EACCES errors,
`Command failed:` errors from the sync calls, Buffer results, `kill()` by name,
`util.promisify` for `exec/execFile`. Not done: IPC for `fork()` (`send`, `'message'`),
`argv0 uid gid detached`, real streams (CLAUDE.md), non-blocking stdin writes, `ref/unref`.
Native findings: the SIGCHLD handler reaches `os.signal` through the global `os` (the layer
defines it when missing), and `spawn(file, optionsObject)` still crashes natively.

Compared against `/tmp/node-doc/child_process.md` and the source (`quickjs-child-process.c`,
696 lines) with a short probe under node 22 and `qjsm`
(`/tmp/claude-1000/cmp/c1.mjs`). Exports: `ChildProcess`, `exec`, `execSync`, `spawn`,
`spawnSync`, `kill` and the `SIG*`/`WNOHANG`/`WNOWAIT`/`WUNTRACED` constants. The API
follows `spawn(2)`/`waitpid(2)` more than Node's: events and streams do not exist.

### A. Missing functions and classes
`execFile`, `execFileSync`, `fork` (and the IPC channel: `send`, `disconnect`,
`connected`, `channel`, `'message'`, `'disconnect'` events), `ChildProcess` as an
`EventEmitter` (no `on/once/off/emit`, no `'spawn'`, `'exit'`, `'close'`, `'error'`
events; only the non-standard `onExit` option/property and a blocking `wait()`),
`subprocess.ref()/unref()`, `subprocess[Symbol.dispose]`.

### B. `execSync` / `spawnSync` / `exec`
- the options object is only honoured when it is the *second* argument:
  `execSync('cmd', opts)` works, but `spawn(file, opts)` / `spawnSync('cat', { input })`
  treats `opts` as the argument array (`js_array_length` of an object), which ends in
  `execvp(): Bad address` and a core dump. `spawn(file, args, opts)` is the only working
  form.
- without options, `stdio` defaults to inheriting the parent's fds: `execSync('echo hi')`
  prints `hi` to the terminal and returns `{ pid, status, signal }`; Node returns the
  stdout `Buffer` (string with `encoding`), pipes stderr to the parent's stderr.
- `execSync` never throws on a non-zero exit (Node: `Error: Command failed: cmd` with
  `status`, `signal`, `stdout`, `stderr`, `pid`, `output`); `spawnSync` result lacks
  `error` (e.g. `ENOENT`), `stdout`/`stderr` are strings, not `Buffer`s, and are
  `null`/missing when `stdio` is not `pipe`; `output` is `[null, stdout, stderr]` only
  for pipes.
- `exec(cmd, [options], callback)` ignores the callback (`(err, stdout, stderr)`) and
  returns a `ChildProcess`; `execFile`-style `maxBuffer`, `timeout`, `killSignal`,
  `encoding`, `shell`, `windowsHide`, `uid`, `gid`, `argv0`, `detached`, `signal`
  (AbortSignal) options are not read: only `env`, `cwd`, `stdio`, `usePath`, `onExit`.
- shell: uses `$SHELL` (Node: `/bin/sh`), always with `sh -c`; `shell: true|string`
  option for `spawn`/`spawnSync` is missing.
- `input` option (stdin data), `encoding`/`maxBuffer`/`timeout` for the sync calls,
  `stdio` entries other than `'pipe' | 'ignore' | 'inherit' | number` (`'overlapped'`,
  `'ipc'`, a stream or file descriptor object) are missing.

### C. `ChildProcess` object
Present: `pid`, `exitCode`, `signalCode`, `killed`, `spawnfile`, `spawnargs`, `stdin`,
`stdout`, `stderr`, `stdio` (the parent fds as numbers, not streams), `kill(signal)`,
`wait()`. `stdin/stdout/stderr` are raw file descriptors (numbers), so there is no
`.write()`, `.on('data')` or `for await`; per CLAUDE.md the replacement should be
WHATWG streams (`ReadableStream` / `WritableStream`) rather than Node `Duplex`.
`kill()` takes a number (names such as `'SIGTERM'` are not parsed; reuse
`misc.signum()`); it sets `killed` correctly only for a successful `kill(2)`.

### D. Suggested approach
Layer a small `lib/child_process.js` on top of the native spawn/wait primitives that
adds Node's shapes: `execFile`, `execSync` (throw on failure, return `Buffer`/string),
`spawn(file, args?, options?)` argument normalisation (fixes the crash), the options
above, an `EventEmitter` base for `ChildProcess` fed by a `SIGCHLD`/`waitpid` poll in
the event loop, `shell`, `input`, `timeout`/`killSignal`/`signal`, and WHATWG stream
wrappers for the pipes; `fork` and IPC can wait.
