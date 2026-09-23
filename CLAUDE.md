# CLAUDE.md

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

---

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Philosophy

### Core Mission
**Be the standard library QuickJS deserves** by providing WHATWG-spec'd web APIs and Deno/Bun-like runtime APIs with a coherent, documented JS surface over native bindings.

### API Design Principles

1. **Prefer Standards Over Custom APIs**
   - Priority: WHATWG > Browser > Bun > Node > Deno
   - Avoid "qjs-modules-isms" (custom APIs that lock users in)
   - Scripts from browser/Node/Deno/Bun should run with minimal changes
   - **Never implement Node.js Streams** (`stream.Readable`/`Writable`/`Duplex`/
     `Transform`, `net.Socket` extending `Duplex`, etc.) - they're a separate,
     incompatible stream model from WHATWG/browser Streams (`ReadableStream`/
     `WritableStream`/`TransformStream`), which this project already implements
     (`lib/streams.js`) and which the priority order above says wins. Any Node-API
     wrapper (`net`, future `http`, etc.) that would otherwise need to extend
     `Duplex` should expose a WHATWG-stream-shaped surface instead, even where that
     means it isn't a drop-in `instanceof net.Socket` match for Node code.

2. **Prefer JS-Idiomatic APIs Over C++ API Parity**
   - When binding C++ containers, prefer plain JS arrays with GC over strict API reproduction
   - Use JS-native patterns: arrays, iterators, `for...of`, spread, indexing, length
   - Avoid verbose container APIs (`.get()`, `.delete()`, `.size()`) when plain arrays are cleaner
   - If C++-style containers are needed, enhance them with JS-idiomatic extensions (Symbol.iterator, indexing)

3. **Internal vs Public APIs**
   - Modules like `deep`, `predicate`, `pointer`, `misc` are internal implementation details
   - Don't add new custom public APIs unless absolutely necessary
   - Document rationale for any custom API added
   - **`quickjs-virtual.c` (`VirtualProperties`) is a deliberate exception** - it has zero
     usage anywhere and no spec target (checked 2026-09, alongside removing several other
     zero-usage/no-target modules: `reflect.js`, `arrayLike.js`, `extendMath.js`,
     `extendObject.js`), but it's being kept anyway - the author's call, not a scoring
     decision. Don't flag it again in a future cleanup pass without being asked.

## What this is

A collection of **native C modules for QuickJS** (`quickjs-*.c`/`.h` bindings, e.g. `stream`,
`xml`, `deep`, `dom`, `json`), plus JS-side wrappers and helpers in `lib/*.js`. Built via CMake;
tests live in `tests/test_*.js` and run under `qjs`/`qjsm`, wired up as CTest cases
(`add_test` in `CMakeLists.txt`).

## Recent Work (August 2026)

### Documentation Restructuring (Completed)
- **Reorganized `doc/` folder** into logical subdirectories:
  - `doc/native/` - C native modules (32 modules + README)
  - `doc/js/` - JavaScript modules (46 modules + README)
  - `doc/` - General documentation (README, grammar, buffer, readline, api-compatibility)
- **Created comprehensive READMEs** for both subdirectories explaining structure and classification
- **Updated all references** throughout codebase to new paths

### Module Classification System (Completed)
All modules classified into four categories:

1. **Native Modules** (32 C bindings in `doc/native/`):
   - Direct C implementations exposed to JavaScript
   - Examples: blob, stream, dom, fs, process, child-process, sockets
   - Document the JS API exposed by C bindings
   - **NO references to `lib/*.js` files**

2. **JavaScript Polyfills** (15 in `doc/js/`):
   - Standalone JS implementations of standard APIs
   - No native imports, work in other runtimes
   - Examples: deep.js, pointer.js, predicate.js, xml.js, misc.js, stream.js, events.js, abort.js

3. **JavaScript Wrappers** (18 in `doc/js/`):
   - Wrap native modules to provide higher-level APIs
   - Examples: fs.js, process.js, console.js, assert.js, streams.js

4. **Prototype Extensions** (8 in `doc/js/`):
   - Extend built-in prototypes (Array, Map, etc.)
   - Examples: extendArray.js, extendMap.js, extendSet.js

### Overlap Resolution (Completed)
**Decision:** Modules with both C and JS implementations (deep, pointer, predicate, stream, xml, misc, path):
- Documented **ONLY in `doc/native/`** (C is primary/authoritative)
- JS polyfills are alternative implementations for other runtimes
- Avoids duplication and confusion

### API Compatibility Research (Completed)
- **Inventoried all 33 native C modules** with detailed export listings
- **Inventoried all 46+ JS modules** with classification and usage
- **Created `doc/api-compatibility.md`** with standards compliance tracking
- **Created `doc/api-compatibility-plan.md`** with roadmap and statistics
- **Verified no native docs reference `.js` files**

### Bug Fixes (Completed)
- Fixed `process.js` scriptArgs crash under `-e` mode
- Fixed `property_enumeration_setpos` assertion with empty objects
- Fixed `Blob.prototype.stream()` to return proper ReadableStream
- Fixed XML test assertion for digit-starting tag names
- Fixed stream tests to use lib/stream.js instead of native module
- Added missing path module exports (isin, equal, toArray)
- **Replaced lib/stream.js with qjs-lws version** for better WHATWG Streams compliance
- **Added BYOB (Bring Your Own Buffer) support** - Fixed missing `isDataViewConstructor` helper and `pendingPullIntos` property access
- **Added compatibility exports to lib/assert.js** - `noop` and `assert_default` for qjs-lws compatibility
- **Fixed BYOB timeout issues** - Fixed `ReadableByteStreamControllerCallPullIfNeeded` to pull when there are pending read requests, even if desiredSize <= 0. All 5 BYOB tests now passing.

## Current State

### Test Results
- **Overall pass rate**: 85% (41/48 tests passing)
- **Stream tests**: 41/41 passing (100%) - all tests passing!
- **BYOB tests**: 5/5 passing (100%) - all timeout issues fixed
- **Other test failures**: 7 tests failing in other modules (documented in TODO.md)
- All recent bug fixes verified with tests

### Module Statistics
- **Native modules:** 32 (C bindings)
- **JavaScript modules:** 46 (polyfills, wrappers, extensions)
- **Total modules:** 78
- **Documentation files:** 85 (33 native + 46 JS + 6 general)

### Standards Compliance
**Implemented:**
- WHATWG: Streams, Blob, URL, Console, AbortController
- W3C: DOM (comprehensive, 90%+ coverage), TreeWalker, XPath
- HTML5: Timers
- Node.js: fs, process, events, assert, child-process, path, util, tty

**Missing (tracked in TODO.md):**
- Web Workers (Tier 9.9)
- URL.createObjectURL/revokeObjectURL (Tier 9.6)
- Streams BYOB safety checks (Tier 3)

**Out of scope (implemented by sibling projects, see "Sibling Projects" below):**
- Fetch API, FormData, WebSocket - `../qjs-lws/`
- Canvas API - `../qjs-nanovg/` (`lib/canvas2d.js`) + `../qjs-glfw/`

## Sibling Projects

qjs-modules is meant to be the "standard library" QuickJS deserves - it should not
grow heavyweight, opinionated dependencies like an HTTP/WebSocket stack or a GPU-backed
2D renderer. Those live in sibling projects instead, each runnable on a **plain QuickJS
install with no dependency on qjs-modules**:

- **`../qjs-lws/`** - HTTP client/server and WebSocket bindings on top of libwebsockets.
  Provides `lib/fetch.js` (Fetch API), `lib/websocket.js`/`lib/websocketstream.js`
  (WebSocket), and `lib/lws/formdata.js` (FormData).
- **`../qjs-nanovg/`** - 2D vector graphics on top of nanovg. Provides `lib/canvas2d.js`,
  a Canvas API implementation.
- **`../qjs-glfw/`** - Window/GL context creation and input via GLFW, used by
  `qjs-nanovg` to get a drawable surface.

When a TODO/gap here looks like "implement Fetch" or "implement Canvas", check whether
it's already covered by one of these sibling projects before adding it to this repo's
roadmap - elaborate network clients/servers and graphics stacks don't belong in a stdlib.

## Documentation Structure

```
doc/
├── README.md                      # Main documentation index
├── grammar.md                     # Grammar framework
├── buffer.md                      # Buffer handling
├── readline.md                    # Readline utilities
├── api-compatibility.md          # Standards compliance research
├── api-compatibility-plan.md     # Classification and roadmap
│
├── native/                       # C native modules (32)
│   ├── README.md
│   ├── archive.md, arraybuffer-sink.md, bcrypt.md, bjson.md
│   ├── blob.md, child-process.md, deep.md, directory.md
│   ├── gpio.md, inspect.md, json.md, lexer.md, list.md
│   ├── location.md, magic.md, misc.md, mmap.md
│   ├── mysql.md, path.md, pgsql.md, pointer.md, predicate.md
│   ├── queue.md, repeater.md, serial.md, sockets.md, sqlite.md
│   ├── stream.md, syscallerror.md, textcode.md
│   ├── tree-walker.md, virtual.md, xml.md
│
└── js/                           # JavaScript modules (46)
    ├── README.md
    ├── Polyfills: abort.md, asyncIterator.md, events.md
    │              iterator.md, testharness.md, testharnessreport.md
    │              parsel.md, describe-class.md
    ├── Wrappers: assert.md, console.md, fs.md, fsPromises.md, process.md
    │             streams.md, io.md, tty.md, repl.md, require.md
    │             module.md, stack.md, inotify.md, terminal.md
    │             perf_hooks.md, url.md, xpath.md, vfs.md
    ├── Extensions: extendArray.md, extendArrayBuffer.md,
    │               extendMap.md, extendSet.md,
    │               extendFunction.md, extendAsyncFunction.md,
    │               extendGenerator.md, extendAsyncGenerator.md
    └── Other: css-selectors.md, css3-selectors.md, parser.md
               db.md, dbi.md, database.md, dom.md, file.md
               socklen_t.md, timers.md, util.md, html.md
```

## Tracking Work

Outstanding work lives in three files:

- **`TODO.md`** — Authoritative, tiered tracker. Items verified against code (file:line, concrete failure, repro). Tiered by leverage (highest-impact/cheapest first).
- **`BUGS`** — Bugs found incidentally while doing other work, deliberately left unfixed. Format: kebab-case name, all lowercase, 78-column wrap, with repro snippet.
- **`TODO`** (no extension) — Legacy list, superseded by `TODO.md`. Don't add here.

**When to update:**
- Found a bug but task isn't "fix bugs" → append to `BUGS`
- Verified actionable fix → add to `TODO.md`
- Fixed something → remove/mark done in `TODO.md`

## Build / Test

```sh
# Configure
cmake -B build/$(cc -dumpmachine) -S .

# Build
cmake --build build/$(cc -dumpmachine)

# Test all
ctest --test-dir build/$(cc -dumpmachine)

# Test specific
qjs tests/test_stream.js
```

## Important Files

- **`CMakeLists.txt`** - Build configuration, module registration, test setup
- **`lib/`** - JavaScript modules (polyfills, wrappers, extensions)
- **`quickjs-*.c`** - Native C module implementations
- **`include/`** - C headers for native modules
- **`tests/`** - Test suite (test_*.js)
- **`doc/`** - Documentation (see structure above)
- **`TODO.md`** - Work tracker
- **`BUGS`** - Known bugs
- **`CLAUDE.md`** - This file (AI assistant context)

## Development Conventions

### Code Style
- Match existing style in files you edit
- No speculative features or abstractions
- Surgical changes only - don't "improve" adjacent code
- Remove only what YOUR changes made unused

### Documentation
- Native modules: Document in `doc/native/<name>.md`
- JS modules: Document in `doc/js/<name>.md`
- General docs: Keep in `doc/` root
- Update docs when changing APIs

### Testing
- Write tests for new features
- Verify bug fixes with tests
- Run `ctest` before committing
- Individual tests: `qjs tests/test_<name>.js`

### Commits
- Clear, descriptive commit messages
- Reference issue/bug numbers when applicable
- Keep commits focused (one logical change per commit)

## Architecture Patterns

### Module Structure
```
quickjs-<name>.c          # C implementation
include/<name>.h          # C header
lib/<name>.js             # JS wrapper (if needed)
doc/native/<name>.md      # C API documentation
doc/js/<name>.md          # JS API documentation (if separate)
tests/test_<name>.js      # Test suite
```

### Wrapper Pattern
JS wrappers typically:
1. Import native module: `import { Foo } from '<name>'`
2. Add convenience methods or higher-level APIs
3. Re-export with enhancements
4. Document in `doc/js/<name>.md`

### Polyfill Pattern
JS polyfills typically:
1. Implement standard API (WHATWG/W3C/Node.js)
2. No native imports (pure JS)
3. Work in other runtimes
4. Document in `doc/js/<name>.md`

## Known Issues

See `TODO.md` for full list. Key items:

- **Streams BYOB safety checks** (Tier 3) - Missing validation in respondWithNewView()
- **URL.createObjectURL** (Tier 9.6) - Not implemented
- **11 test failures** - Pre-existing, documented in TODO.md

(Fetch, FormData, WebSocket, and Canvas are out of scope for this repo - see "Sibling Projects" above.)

## Future Roadmap

### Immediate (Tier 2-3)
- Fix Streams BYOB safety checks
- Fix remaining test failures
- Clean up dead code in C modules

### Medium-term (Tier 9)
- Implement Web Workers
- Add URL.createObjectURL/revokeObjectURL

### Long-term
- Achieve 95%+ standards compliance
- Reduce custom APIs to minimum
- Improve test coverage to 90%+
- Document all public APIs

## Context for AI Assistants

When working on this codebase:

1. **Check TODO.md first** - See what's planned/prioritized
2. **Read relevant docs** - `doc/native/` for C, `doc/js/` for JS
3. **Run tests** - Verify your changes don't break existing tests
4. **Update docs** - If you change APIs, update documentation
5. **Track work** - Add to TODO.md or BUGS as appropriate
6. **Prefer standards** - Align with WHATWG/W3C/Node.js when possible
7. **Prefer JS-idiomatic** - Use JS patterns over C++ API parity
8. **Be surgical** - Minimal changes, no scope creep

## Session History

**Recent sessions (August 2026):**
- Documentation reorganization and module classification
- API compatibility research and inventory
- Multiple bug fixes (process.js, Blob.stream(), XML tests, etc.)
- Test suite improvements (78% pass rate)
- Established standards compliance roadmap

**Next priorities:**
1. Fix Streams BYOB safety checks
2. Fix remaining 11 test failures
3. Reduce custom APIs, increase standards compliance

## GitHub Pages site

This project's GitHub Pages site (the `gh-pages` branch) is **generated, not
hand-maintained here**. Use the global `github-pages` skill and the shared site
build tool in the `rsenn/rsenn` repo, at `../../../rsenn/rsenn` (relative to this repo root;
i.e. `~/Projects/rsenn/rsenn`):

- site definition, landing page, theme, favicon: `../../../rsenn/rsenn/sites/qjs-modules/`
- generator and publisher: `../../../rsenn/rsenn/tools/site/` (see its `README.md`)
  - build: `qjsm ../../../rsenn/rsenn/tools/site/build.js qjs-modules` (`node` works too)
  - publish: `../../../rsenn/rsenn/tools/site/sync.sh qjs-modules` (commits locally; `--push` only after the user confirms)
- the markdown that becomes the site's pages is **this repo's own** `README.md`,
  `doc/` and `examples/`; a doc page appears on the site only once it is listed in
  `nav` in `../../../rsenn/rsenn/sites/qjs-modules/site.config.js`.

Do not add or extend a `tools/site/`, Pages workflow or `publish.sh` in this repo (any
existing ones are superseded and slated for removal), and do
not edit `gh-pages` by hand.
