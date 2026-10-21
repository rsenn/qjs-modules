# JavaScript Modules

Pure JavaScript modules (`lib/*.js`) providing polyfills for standard APIs, wrappers around native modules, and prototype extensions.

## Complete List

### Core & Language Extensions
- [util](util.md) — Central utility library
- [iterator](iterator.md) — Base iterator class
- [asyncIterator](asyncIterator.md) — Async iterator base class

### Prototype Extensions
- [extendArray](extendArray.md) — Array prototype extensions
- [extendArrayBuffer](extendArrayBuffer.md) — ArrayBuffer prototype extensions
- [extendMap](extendMap.md) — Map prototype extensions
- [extendSet](extendSet.md) — Set prototype extensions
- [extendFunction](extendFunction.md) — Function prototype extensions
- [extendAsyncFunction](extendAsyncFunction.md) — AsyncFunction prototype extensions
- [extendGenerator](extendGenerator.md) — Generator prototype extensions
- [extendAsyncGenerator](extendAsyncGenerator.md) — AsyncGenerator prototype extensions

### Runtime / Node.js Compatible
- [assert](assert.md) — Node.js assert module
- [console](console.md) — WHATWG Console API
- [process](process.md) — Node.js process object
- [readline](readline.md) — Node's `readline`
- [readlinePromises](readlinePromises.md) — `readline/promises`
- [events](events.md) — Node.js EventEmitter
- [abort](abort.md) — AbortController/AbortSignal
- [timers](timers.md) — HTML5 Timers API (setTimeout, setInterval, etc.)
- [perf_hooks](perf_hooks.md) — Performance hooks
- [module](module.md) — Module utilities, `registerHooks()`
- [bun](bun.md) — the `Bun` global: `Bun.plugin()` runtime plugins, `Bun.file()`, `Bun.write()`
- [node](node.md) — Node compatibility qjsm loads at startup: the `node:`/`node_modules`/CommonJS loaders and Node's globals
- [loaders/commonjs](loaders/commonjs.md) — `commonjs` module format loader (`registerHooks()`)
- [loaders/file](loaders/file.md) — `with { type: 'file' }` imports, as in Bun (`registerHooks()`)
- [loaders/typescript](loaders/typescript.md) — `.ts` loader, types stripped by node's `module.stripTypeScriptTypes` (`registerHooks()`)
- [loaders/packages](loaders/packages.md) — `node_modules` and `package.json` `exports`/`imports`/`main`, Node's ESM resolution (`registerHooks()`, preloaded)
- [globals](globals.md) — web/Node globals (`URL`, `structuredClone`, `crypto`, `Intl`, ...), installed by [node](node.md)
- [intl](intl.md) — `Intl` (ECMA-402) with English data: `NumberFormat`, `DateTimeFormat`, `PluralRules`, `Collator`, `ListFormat`, `RelativeTimeFormat`, `Locale`
- [jsonStreams](jsonStreams.md) — Deno's `JsonParseStream`, `ConcatenatedJsonParseStream`, `JsonStringifyStream`
- [string_decoder](string_decoder.md) — Node's `string_decoder` (`StringDecoder`)
- [textLineStream](textLineStream.md) — Deno's `TextLineStream`
- [timersPromises](timersPromises.md) — `timers/promises`
- [wasi](wasi.md) — `WASI` (Node's `node:wasi`, preview1), portable: qjsm, node, bun, deno
- [webassembly](webassembly.md) — the `WebAssembly` global over the `wasm` module (opt-in)
- [loaders/wasm](loaders/wasm.md) — `.wasm` ES module loader (`registerHooks()`, opt-in)
- [require](require.md) — CommonJS require
- [stack](stack.md) — Stack utilities

### I/O & Filesystem
- [fs](fs.md) — Node.js fs module (wraps native modules)
- [fsPromises](fsPromises.md) — Promise-based fs API
- [io](io.md) — I/O utilities
- [stream](stream.md) — WHATWG Streams API (ReadableStream, WritableStream, TransformStream)
- [streams](streams.md) — Stream utilities
- [vfs](vfs.md) — Virtual filesystem
- [inotify](inotify.md) — inotify wrapper
- [tty](tty.md) — TTY utilities
- [terminal](terminal.md) — Terminal utilities
- [socklen_t](socklen_t.md) — Socket length type

### Parsing, DOM & Selectors
- [dom](dom.md) — W3C DOM API (wraps native modules)
- [xpath](xpath.md) — XPath implementation
- [parser](parser.md) — Parser combinator framework
- [grammar](../grammar.md) — Grammar utilities
- [lex](lex.md) — lex/flex `.l` files: lexer, parser (AST), generated lexers
- [grammar-parsers](grammar-parsers.md) — BNF, EBNF, ANTLR `.g4` and yacc/jison files to an AST
- [parse-ast](parse-ast.md) — parse sources with a grammar file, print the tree as JSON (`utilities/parse-ast.js`)
- [module-lexer](module-lexer.md) — imports and exports of a JS source as records (ES module and CommonJS), `print()`, `merge()`
- [css3-selectors](css3-selectors.md) — CSS3 selector engine
- [url](url.md) — WHATWG URL API (polyfill)

### Databases
- [sql](sql.md) — Bun `bun:sql`-flavored tagged-template client unifying sqlite/pgsql/mysql
- [db](db.md) — Database utilities
- database (`lib/database.js`) — Database abstraction (undocumented)

### Tooling
- [repl](repl.md) — REPL implementation
- [lexers](lexers.md) — Lexer for a file or language name

### Utilities
- describe (`lib/describe.js`) — describeClass()/describeObject(), also a command-line tool (undocumented)
- dbi (`lib/dbi.js`) — Database interface (undocumented)

## Module Classification

### Polyfills (Standalone JS)
Pure JavaScript implementations of standard APIs with no native dependencies:
- **WHATWG/W3C/HTML5:** events, url, timers, abort, console
- **ECMA-402:** intl (English only)
- **Node.js:** assert, fs (partial), process (partial)
- **Utilities:** iterator, asyncIterator

### Wrappers (Wrap Native Modules)
JavaScript code that wraps native modules to provide higher-level APIs:
- **fs** — Wraps misc, mmap, syscallerror
- **fsPromises** — Wraps fs
- **dom** — Wraps fs, util, timers, css3-selectors, deep, pointer, tree-walker, xml, url
- **console** — Wraps os, util, inspect, std
- **process** — Wraps os, path, util, misc, std
- **assert** — Wraps util
- **util** — Wraps misc, timers

### Prototype Extensions
Extend built-in JavaScript prototypes (Array, Object, Map, etc.):
- extendArray, extendArrayBuffer, extendMap, extendSet
- extendFunction, extendAsyncFunction, extendGenerator, extendAsyncGenerator

## Statistics

- **Total JS modules:** 60+
- **Polyfills:** 26 (standalone JS implementations)
- **Wrappers:** 32 (wrap native modules)
- **Extensions:** 10 (prototype extensions)

## Standards Compliance

- **WHATWG/W3C/HTML5:** events, url, timers, abort, console, dom
- **Node.js:** assert, fs, fsPromises, process, events, util
- **Custom:** Many utility modules (see api-compatibility.md)
