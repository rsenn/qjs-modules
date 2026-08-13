# qjs-modules — API Reference

This directory contains comprehensive API documentation for qjs-modules.

## Structure

The documentation is organized into three categories:

- **`native/`** — Native C modules (`quickjs-*.c`). These are the primary implementations for modules that have native bindings.
- **`js/`** — Pure JavaScript modules (`lib/*.js`). These are either polyfills for standard APIs or wrappers around native modules.
- **Root files** — General documentation (README, grammar, buffer, readline, api-compatibility)

### Module Classification

1. **Native modules** (in `native/`): Direct C implementations exposed to JavaScript
   - Examples: blob, stream, dom, fs, process, child-process, sockets

2. **JavaScript polyfills** (in `js/`): Standalone JS implementations of standard APIs
   - Examples: events, url, timers, console (WHATWG/W3C/HTML5 standards)
   - Some polyfills also have native counterparts (deep, misc, pointer, predicate, xml) — documented in `native/`

3. **JavaScript wrappers** (in `js/`): JS code that wraps native modules to provide higher-level APIs
   - Examples: assert, fsPromises, util, dom (wraps native modules with JS convenience)

4. **Prototype extensions** (in `js/`): Extend built-in JavaScript prototypes
   - Examples: extendArray, extendString, extendObject, etc.

## Documentation Index

### Native Modules (`native/`)

See [native/README.md](native/README.md) for the complete list of native C modules.

Key modules:
- [blob](native/blob.md) — WHATWG Blob API
- [stream](native/stream.md) — WHATWG Streams API
- [dom](native/dom.md) — W3C DOM API
- [child-process](native/child-process.md) — Node.js child_process
- [sockets](native/sockets.md) — BSD sockets
- [deep](native/deep.md) — Deep object comparison and cloning
- [inspect](native/inspect.md) — Object inspection and formatting

### JavaScript Modules (`js/`)

See [js/README.md](js/README.md) for the complete list of JavaScript modules.

Key modules:
- [events](js/events.md) — Node.js EventEmitter
- [url](js/url.md) — WHATWG URL API
- [timers](js/timers.md) — HTML5 Timers API
- [console](js/console.md) — WHATWG Console API
- [assert](js/assert.md) — Node.js assert module
- [fs](js/fs.md) — Node.js fs module
- [fsPromises](js/fsPromises.md) — Promise-based fs API

### API Compatibility

See [api-compatibility.md](api-compatibility.md) for:
- Classification of all modules by standard compliance
- Standards compatibility tracking (WHATWG, W3C, Node.js, etc.)
- Roadmap for improving standards compliance

### General Documentation

- [grammar.md](grammar.md) — JavaScript grammar specification
- [buffer.md](buffer.md) — Buffer handling
- [readline.md](readline.md) — Readline utilities
