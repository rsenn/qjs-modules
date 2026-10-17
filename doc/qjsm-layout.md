# qjsm: module layout, loader configuration, build info

Working document. It collects ideas and specs for one goal: a build and
installation whose module layout is **configurable, reported accurately,
consistent and inspectable**, and a `qjsm` binary that can say what it was built
from. Nothing here is implemented unless marked so.

Status tags: `[verified]` seen in the tree or by running it, `[proposed]` a spec
we have not agreed on yet, `[open]` a question, `[decided]` agreed with the user.

Related: [qjsm.md](qjsm.md) (loader pipeline, hooks), `cmake/qjsm.cmake` (builtins
configuration), `cmake/deps/FindQuickJS.cmake` (module path variables).

## 1. Goals

1. The module layout and the loader configuration are the same story for qjsm,
   Node, Bun, Deno and the browser: the same specifiers resolve to the same
   modules, or fail the same way. [decided]
2. The layout is a set of named variables, configurable at configure time, and
   every configure prints the resulting layout. [decided]
3. An installation in any directory is self-consistent and inspectable: from the
   installation alone one can tell which modules are builtin, which are installed
   as files, and which of the two wins. No silent shadowing. [decided]
4. `qjsm` carries build information that can be queried at run time: build date,
   git commit, the qjsc version the builtin bytecode was compiled with.
   [decided]
5. The data baked into qjsm (build info, builtin table, bytecode blobs) can be
   read from JS, through `lib/module.js` and an API in `qjsm.c`. [decided]

## 2. Where things stand today

### 2.1 Layout [verified]

`cmake --install` of a default configure puts:

| What | Where | Count |
|---|---|---|
| native modules, `.so` | `<prefix>/lib/<multiarch>/quickjs` (`QUICKJS_C_MODULE_DIR`) | 34 |
| JS modules, `.js` | `<prefix>/lib/quickjs` (`QUICKJS_JS_MODULE_DIR`) | the `lib/*.js` that are not builtin |
| scripts | `<prefix>/bin` | 16 |
| `qjsm` | `<prefix>/bin` | 1 |

`QUICKJS_MODULE_PATH` (default: header value, then the two directories above,
without duplicates) is compiled into qjsm and can be overridden by the
environment variable of the same name.

### 2.2 Builtin versus installed [verified]

- A builtin is found **before** any file search (`jsm_module_normalize`, step 1
  of the pipeline), so a builtin always shadows an installed module of the same
  name, whatever `QUICKJS_MODULE_PATH` says.
- Native modules: all 34 are installed as `.so`, **including the 13 that are also
  baked into qjsm**. So an installation holds both. Which one a script gets
  depends on how it imports: a bare name gives the builtin; reaching the file is
  [open], `import 'path.so'` on the installed qjsm failed with "could not load
  module filename 'path.so' as shared library" (checked 2026-10-14), so how to
  name the installed copy is itself unspecified. This is the conflict to remove.
- JS modules: the builtin ones are excluded from the install
  (`qjsm_resolve_builtins`, `EXCLUDE_REGEX`), so no duplicate there.
- Nothing in the installation records which modules are builtin in the qjsm that
  sits next to it. Only the running binary knows (`builtins` global,
  `moduleList`).

### 2.3 Loader configuration [verified]

Resolution order: builtin registry, relative path, `.so` with search, existing
file; load: `data:`, `node:`, hooks, `package.json` `_moduleAliases`, builtin
again, `.json`, default loader. The module path comes only from
`QUICKJS_MODULE_PATH`. Details in [qjsm.md](qjsm.md#module-resolution-pipeline).

### 2.4 What the build knows and discards [verified]

- `qjsc` prints `QuickJS Compiler version 2026-06-04`; the generated
  `modules/<name>.c` has no version, date or source hash in it.
- `qjsm` only has `CONFIG_VERSION` (the QuickJS version); `qjsm --version` is
  an unknown option.
- The bytecode of a builtin is `const uint8_t qjsc_<name>[N]` plus
  `const uint32_t qjsc_<name>_size`, referenced from `quickjs-builtins.h` via
  `BUILTIN_COMPILED(name)`.
- Bytecode is only valid for the exact QuickJS bytecode version it was compiled
  with; the version of the *runtime* the blob is loaded by must match.

## 3. Layout as configuration

### 3.1 Variables [implemented]

One table, all cache variables, each with a default derived from the previous
one, so `-DCMAKE_INSTALL_PREFIX` alone gives a consistent layout:

| Variable | Default | Meaning |
|---|---|---|
| `QJSM_BINDIR` | `bin` | qjsm and the scripts |
| `QJSM_C_MODULE_DIR` | `lib/<multiarch>/quickjs` | native `.so` modules |
| `QJSM_JS_MODULE_DIR` | `lib/quickjs` | JS modules |
| `QJSM_DATA_DIR` | `share/qjsm` | manifest, import maps, docs |
| `QJSM_MODULE_PATH` | both module dirs | what is compiled in as the search path |
| `QJSM_RELOCATABLE` | `OFF` | paths relative to the executable instead of absolute |

[open] Reuse `QUICKJS_C_MODULE_DIR`/`QUICKJS_JS_MODULE_DIR` (what the build takes
from FindQuickJS today) or introduce `QJSM_*` and map from the old names? The
quickjs fork installs by the old names, so the two must not disagree.

### 3.2 Relocatable installs [proposed]

`QJSM_RELOCATABLE=ON`: module path entries are stored relative to the directory
of the executable (`$ORIGIN/../lib/quickjs`), resolved at start. Needed for an
installation "somewhere in a folder" (tarball, portable app). RUNPATH likewise
(`$ORIGIN/../lib/<multiarch>`). [open] Windows and macOS equivalents
(`@executable_path`, DLL search).

### 3.3 Report at configure time [implemented]

The configure summary prints the table above with the resolved values, for each
row where it came from (default, cache, command line), the resulting search path
in order, and a verdict line:

```
-- layout
--   prefix            /opt/qjs
--   native modules    /opt/qjs/lib/x86_64-linux-gnu/quickjs   (34 files, 21 not builtin)
--   JS modules        /opt/qjs/lib/quickjs                    (61 files)
--   module path       /opt/qjs/lib/quickjs : /opt/qjs/lib/x86_64-linux-gnu/quickjs
--   builtin and installed at the same time: none
```

The last line is the check from section 4.

## 4. Builtin versus installed: one rule [implemented]

Make an installation unambiguous:

1. **Do not install a native module that is builtin.** Same rule JS modules
   already follow. Option `QJSM_INSTALL_BUILTIN_NATIVE` (default `OFF`) to install
   them anyway, for programs that load `fs.so` by path.
2. If both exist (option on, or a stale file from an earlier install), the
   configure summary and the manifest (section 5) list it as *shadowed*.
3. Precedence is documented and queryable: builtin, then module path in order.
   [open] a documented spelling that reaches the installed file of a module that
   is also builtin (see 2.2); today `import 'path.so'` does not.
4. A manifest installed with the modules, `<QJSM_DATA_DIR>/manifest.json`,
   records for this installation:

```json
{
  "qjsm": { "version": "0.9", "commit": "d59a45e4", "date": "2026-10-14T09:30:00Z" },
  "quickjs": { "version": "2026-06-04", "bytecode": 24 },
  "paths": { "native": "lib/x86_64-linux-gnu/quickjs", "js": "lib/quickjs" },
  "builtin": { "native": ["bjson", "..."], "js": ["assert", "..."] },
  "installed": { "native": ["archive", "..."], "js": ["abort", "..."] },
  "shadowed": []
}
```

`qjsm --layout` (or `Module.layout()`) prints the same information from the
binary and the files it finds, and reports differences between the two (a
module in the manifest that is not on disk, a file on the search path that is
not in the manifest).

[open] JSON or a `.cmake`/`.ini` file? JSON is readable from JS without
bootstrap and by every other runtime's tooling.

### 4.1 Builtins are closed [implemented]

**Hard rule: a compiled-in JS builtin never loads a `.js` from disk.** Every
module it imports statically is itself a builtin, built from the same `lib/`
the importer was built against.

*cmake.* `qjsm_resolve_builtins` scans each builtin's literal `import`/`export
from` specifiers (`qjsm_scan_imports`; computed specifiers are reported, not followed) and
adds what they name to `BUILTINS_NEEDED` (`_JS`, `_NATIVE` for the split).

- Weak: derived on every configure, never cached. Remove the dependants from
  `BUILTIN_MODULES` and the dependencies leave the binary with them.
- Compiled in is `${BUILTIN_MODULES} ${BUILTINS_NEEDED}`. Needed JS modules are
  not installed as `.js`; needed natives join the link closure (their
  libraries and extra sources follow).
- The configure summary prints "Pulled in as dependencies of the builtins" as
  `name (js|C) <- importers`. `vfs` loads `archive` on first use (`requireModule`), so it is not a build
  dependency: the default qjsm no longer links libarchive and `archive.so` is found on the
  module path when an archive is opened.
- Names containing `-` are emitted with `BUILTIN_COMPILED_AS(symbol, "name")`
  (`simple_queue` / "simple-queue").
- A builtin does not import `node:x`: C knows nothing of that prefix. The
  lib files use `node/x` (`lib/readline.js`, `lib/node/child_process.js`).

*qjsm.c.* While the engine resolves a builtin's static imports
(`jsm_builtin_init` around `JS_ResolveModule`, `jsm_builtin_depth > 0`):

- only the builtin table is consulted: no disk, no `QUICKJS_MODULE_PATH`, no
  `package.json`, no hooks. A miss throws a ReferenceError naming the importer,
  the specifier and the reason (not built in, or `--disable-builtin`).
- the window is per thread and covers the builtin and its dependencies, so a
  C->JS->C chain stays inside it.
- run-time dynamic `import()` called by a builtin (e.g. `require.js`) is *not*
  inside the window; it is the user's request and resolves normally. [decided
  2026-10-14] This is not restricted: `require()`, the REPL and `describe` exist to load
  what the user names. A literal `import('x')` is followed at build time like a static
  import (so `process` pulls in `node/events`); a computed one is listed in the configure
  summary ("Builtins with a computed import()", `qjsm_scan_computed_imports`).

*Hooks.* `registerHooks()` hooks do not see imports of a builtin or its
dependencies, so they cannot redirect them. `--hooks-on-builtins` /
`QJSM_HOOKS_ON_BUILTINS=1` lifts this for the *bare name* lookup of user code
(a hook then runs before the builtin is chosen). Default off.

*Aliases.* `package.json` `_moduleAliases` is no longer in the C loader. The
builtin `loaders_package-aliases` (preloaded, only when present) registers a resolve
hook when `./package.json` has the key, so aliases apply after builtin
resolution and never to a builtin's own imports.

*Disabling.* `--disable-builtin a,b` or `QJSM_DISABLE_BUILTINS=a,b` mark
builtins unfindable (`-l` shows "(disabled)", the `builtins` global skips
them). A builtin importing a disabled one fails with the error above. From JS,
`disableBuiltin(...names)` does the same at run time (returns the names switched off;
modules already loaded stay loaded).

*Compat.* Node (`loaders_node`, `node/*`), module-alias (`loaders_package-aliases`), Bun
and Web platform layers are builtins like any other and so opt-out: leave them
out of `BUILTIN_MODULES`/`BUILTINS_COMPILED` at build time or disable them on
the command line. The configure summary reports each one present (specifiers
it provides, what it rewrites, globals); with no builtins and no compat it
prints nothing.

*Tracing.* `DEBUG=modules` prints the load tree (tags `import`/`load`/`FAIL`,
`| ` per nesting level, `(builtin)`/`(builtin, dependency)`/`(native)`/`(file)`
marks, colors on a terminal, `NO_COLOR` honored) and a count line at exit;
`DEBUG=modules,modules,...` adds the C-level `DEBUG_MODULE()` detail. It stays
in C because it also sees what happens before any hook exists and does not
perturb hook chains. The JS counterpart is `lib/loaders/trace.js` (`doc/js/loaders/trace.md`).

## 5. Build info in qjsm

### 5.1 Fields [implemented]

| Field | Source | Note |
|---|---|---|
| `version` | `project(VERSION)` | qjs-modules |
| `commit` | `git rev-parse --short=12 HEAD` | plus `-dirty` if the tree has changes |
| `date` | configure or build time? | [open] see below |
| `quickjs.version` | `CONFIG_VERSION`, libquickjs | the runtime linked |
| `qjsc.version` | `qjsc -h` first line, parsed | the compiler of the builtins |
| `qjsc.path` | `QJSC` | |
| `bytecode` | bytecode version number of the engine | a blob is unusable on a different one |
| `compiler` | `CMAKE_C_COMPILER_ID` + version | |
| `target` | `CMAKE_SYSTEM_NAME-ARCH`, multiarch triple | |
| `build_type` | `CMAKE_BUILD_TYPE` | |
| `layout` | the table of 3.1 | the compiled-in view |
| `builtins` | counts and the names | same as the manifest |

[open] Date: a build date changes the object file on every build (no reproducible
builds, always relinks). Options: `SOURCE_DATE_EPOCH` when set, otherwise the
configure time; a separate tiny object (`qjsm-buildinfo.c`) regenerated on every
build so only that object and the link change.

### 5.2 Carrying it [implemented]

- Generated file `qjsm-buildinfo.c` in the build directory, written by CMake at
  configure time (static part) plus a build-time step for the volatile part
  (date, dirty flag), compiled into `qjsm`.
- Contents: one JSON document as `const char qjsm_buildinfo[]` in its own
  section (`.qjsm.info`) so the blob can also be found from outside with
  `objcopy -O binary --only-section=.qjsm.info qjsm` or `readelf -p`.
  [open] COFF/Mach-O equivalents of the section.
- Prefix with a magic string (`QJSM-BUILDINFO\0`) so `strings`/`file` style tools
  and a future `qjsm --inspect <binary>` can locate it in a binary they did not
  start.
- The same data is exposed by:
  - `qjsm --version` (one line) and `qjsm --build-info` (JSON);
  - the global `qjsm.info` / `import { buildInfo } from 'module'`.

### 5.3 Compatibility check [implemented]

At start qjsm compares `quickjs.version`/`bytecode` of the info blob with the
linked libquickjs. A mismatch (builtin bytecode compiled by another qjsc than the
engine that runs it) is a hard error naming both versions, not a crash in
`JS_ReadObject`.

## 6. Reading the baked-in data from JS

### 6.1 The C side [implemented]

`qjsm.c` owns four things that are today only reachable through the loader:

1. the build info blob (5.2),
2. the builtin table (`jsm_builtins`: name, native or compiled, init function,
   bytecode pointer and size),
3. the qjsc bytecode blobs `qjsc_<name>`,
4. the layout (compiled-in module path, prefix).

API options, from most conservative:

- **A. Plain accessors, ArrayBuffer out.** A native module `qjsm` (or properties
  on the existing `*Module` globals) with
  `info() -> object`, `builtins() -> [{name, kind}]`,
  `bytecode(name) -> ArrayBuffer` (a **copy**, or a non-owning view; see open
  question), `layout() -> object`. No pointers leave C.
- **B. Addresses via `dlsym()`.** `dlsym(RTLD_DEFAULT, "qjsc_assert")` needs the
  symbols exported from the executable (`-rdynamic`/`--export-dynamic`), which
  bloats the dynamic symbol table and does not exist on static, Windows or wasm
  builds. [open] probably not; one sentence in the doc so nobody tries it again
  unless the numbers say it is worth it.
- **C. One table, one pass.** `qjsm.c` exports a single array
  `{name, kind, data, size}` and the JS side gets it as ArrayBuffers sliced from
  one view over `.rodata` (zero-copy) when the platform allows
  `JS_NewArrayBuffer` with a no-op free callback.

[open] Copy or view? A view over `.rodata` is free and read-only in practice but
JS can write to an ArrayBuffer; a write faults. Either freeze (a typed array on a
copy-on-first-write wrapper) or copy: 34 modules are about 640 KiB, so a copy per
`bytecode(name)` call is acceptable and safe. Start with copies.

### 6.2 `lib/module.js` [implemented]

Names follow Node where Node has one (`builtinModules`, `isBuiltin`,
`findPackageJSON` already exist in `lib/module.js`):

| Function | Returns |
|---|---|
| `Module.builtinModules` | list of builtin names (exists) |
| `Module.isBuiltin(name)` | boolean (exists) |
| `Module.buildInfo` | the parsed info blob |
| `Module.layout()` | compiled-in paths plus the effective search path (env override applied) |
| `Module.builtins()` | `[{name, kind: 'native'\|'js', size?}]` |
| `Module.bytecode(name)` | `ArrayBuffer` of the qjsc blob of a compiled builtin |
| `Module.installed()` | scan of the search path: `[{name, kind, path, shadowed}]` |
| `Module.manifest()` | the installed `manifest.json`, or `null` |
| `Module.verify()` | compare binary, manifest and disk; returns the list of differences |

On Node/Bun/Deno the same functions exist with the portable parts only
(`builtinModules`, `isBuiltin`, `layout()` from `process.execPath`/import maps),
so a script can call `Module.layout()` everywhere. [open] where that shim lives
(`lib/` polyfill versus a separate package). Today the functions exist in qjsm only;
`Module.buildInfo` is `undefined` elsewhere.

## 7. Cross-runtime layout

The point of a clear layout is that the **same import specifiers work** in qjsm,
Node, Bun, Deno and a browser. What each needs:

| Runtime | How it finds a module | What the build can provide |
|---|---|---|
| qjsm | builtin table, `QUICKJS_MODULE_PATH`, hooks | the table and path of sections 3 to 6 |
| Node | `node_modules`, `package.json` `exports`/`imports` | a generated `package.json` with `exports` for the installed JS modules |
| Bun | like Node, plus `bunfig.toml` | the same `package.json` |
| Deno | import map (`tests/deno-import-map.json` exists) | an import map generated from the layout instead of hand written |
| Browser | import map in the page, ES module URLs | the same import map, with URLs relative to the install root |

[proposed] The build generates, from the one layout description, into
`<QJSM_DATA_DIR>`:

- `package.json` (`exports`, `imports`) for Node and Bun,
- `import-map.json` for Deno and browsers,
- `manifest.json` (section 4).

so the tests (`@portable` tests run on node, bun and deno) and users share one
source of truth and the hand written `tests/deno-import-map.json` goes away.
[open] Native modules have no meaning outside qjsm: the maps list only the
JS modules, and a native-only module is marked as unavailable (or points at the
portable JS fallback where one exists, e.g. `lib/*.js` polyfills of builtins).

Consistency rule: a specifier that resolves to a builtin in qjsm must not
resolve to a *different* implementation in another runtime without being listed
as such in the manifest (`"portable": false`, `"fallback": "<path>"`).

## 8. Order of work

1. Layout variables and the configure report (3.1, 3.3). Smallest, no runtime
   change. Includes the "builtin and installed at the same time" line.
2. Stop installing builtin natives (4.1) behind an option; the manifest file
   (4.4) written at install time.
3. Build info: generated object, `qjsm --version`/`--build-info` (5).
4. C accessors and `Module.*` in `lib/module.js` (6).
5. Relocatable installs (3.2).
6. Generated `package.json`/import maps (7).

Each step: a snapshot comparison of the configure artifacts for the unchanged
parts, and a test for the new behaviour (`tests/unittests/test-module.js`).

## 9. Open questions

1. `QJSM_*` variables versus the existing `QUICKJS_*` names (3.1).
2. Build date and reproducibility (5.1).
3. Copy or view for bytecode blobs (6.1).
4. Where the cross-runtime `Module.*` shim lives (6.2).
5. Native modules in the generated maps (7).
6. ~~JS API to disable a builtin at run time~~ `disableBuiltin()` global, done 2026-10-14.
7. Do installed natives that are also builtin ever matter? `qjs` (not qjsm) can
   load `fs.so` from the module path; if the answer is yes, option
   `QJSM_INSTALL_BUILTIN_NATIVE` stays, defaulting to `OFF`.

## 10. Decision log

| Date | Decision |
|---|---|
| 2026-10-14 | Goals 1 to 5 as listed in section 1 (user). |
| 2026-10-14 | Document started; sections 3 to 7 are proposals. |
| 2026-10-14 | Builtins are closed: dependencies of builtins are builtins (`BUILTINS_NEEDED`), no disk or hook access below a builtin; `_moduleAliases` moved to a hook (user). |
