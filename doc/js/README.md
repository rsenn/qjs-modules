# JavaScript Modules

Pure JavaScript modules (`lib/*.js`) providing polyfills for standard APIs, wrappers around native modules, and prototype extensions.

## Complete List

### Core & Language Extensions
- [util](util.md) — Central utility library
- [reflect](reflect.md) — Reflection and value serialization
- [iterator](iterator.md) — Base iterator class
- [asyncIterator](asyncIterator.md) — Async iterator base class
- [arrayLike](arrayLike.md) — Array-like base class

### Prototype Extensions
- [extendArray](extendArray.md) — Array prototype extensions
- [extendArrayBuffer](extendArrayBuffer.md) — ArrayBuffer prototype extensions
- [extendObject](extendObject.md) — Object prototype extensions
- [extendMap](extendMap.md) — Map prototype extensions
- [extendSet](extendSet.md) — Set prototype extensions
- [extendMath](extendMath.md) — Math object extensions
- [extendFunction](extendFunction.md) — Function prototype extensions
- [extendAsyncFunction](extendAsyncFunction.md) — AsyncFunction prototype extensions
- [extendGenerator](extendGenerator.md) — Generator prototype extensions
- [extendAsyncGenerator](extendAsyncGenerator.md) — AsyncGenerator prototype extensions

### Runtime / Node.js Compatible
- [assert](assert.md) — Node.js assert module
- [console](console.md) — WHATWG Console API
- [process](process.md) — Node.js process object
- [events](events.md) — Node.js EventEmitter
- [abort](abort.md) — AbortController/AbortSignal
- [timers](timers.md) — HTML5 Timers API (setTimeout, setInterval, etc.)
- [perf_hooks](perf_hooks.md) — Performance hooks
- [module](module.md) — Module utilities
- [require](require.md) — CommonJS require
- [stack](stack.md) — Stack utilities

### I/O & Filesystem
- [fs](fs.md) — Node.js fs module (wraps native modules)
- [fsPromises](fsPromises.md) — Promise-based fs API
- [io](io.md) — I/O utilities
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
- [grammar](grammar.md) — Grammar utilities
- [parsel](parsel.md) — CSS selector parser
- [css-selectors](css-selectors.md) — CSS selector engine
- [css3-selectors](css3-selectors.md) — CSS3 selector engine
- [url](url.md) — WHATWG URL API (polyfill)

### Databases
- [db](db.md) — Database utilities
- [database](database.md) — Database abstraction

### Tooling
- [repl](repl.md) — REPL implementation
- [testharness](testharness.md) — W3C test harness
- [testharnessreport](testharnessreport.md) — Test harness reporting

### Utilities
- [buffer](buffer.md) — Buffer utilities
- [readline](readline.md) — Readline utilities
- [describe-class](describe-class.md) — Class description utilities
- [dbi](dbi.md) — Database interface

## Module Classification

### Polyfills (Standalone JS)
Pure JavaScript implementations of standard APIs with no native dependencies:
- **WHATWG/W3C/HTML5:** events, url, timers, abort, console
- **Node.js:** assert, fs (partial), process (partial)
- **Utilities:** iterator, asyncIterator, arrayLike

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
- extendArray, extendArrayBuffer, extendObject, extendMap, extendSet, extendMath
- extendFunction, extendAsyncFunction, extendGenerator, extendAsyncGenerator

## Statistics

- **Total JS modules:** 60+
- **Polyfills:** 25 (standalone JS implementations)
- **Wrappers:** 32 (wrap native modules)
- **Extensions:** 10 (prototype extensions)

## Standards Compliance

- **WHATWG/W3C/HTML5:** events, url, timers, abort, console, dom
- **Node.js:** assert, fs, fsPromises, process, events, util
- **Custom:** Many utility modules (see api-compatibility.md)
