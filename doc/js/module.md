# module

Source: `lib/module.js` (pure JS)

Node's `node:module` (https://nodejs.org/api/module.html). `import ... from
'node:module'` resolves here too (`lib/nodeHooks.js` maps a leading `node:` to the bare name). The
default export is the `Module` class, with every named export as a static.

## Exports

| Export | Description |
| --- | --- |
| `Module` | the CommonJS module class: `new Module(id, parent)`, `load()`, `require()`, `_compile()`, `parent`, `isPreloading` |
| `builtinModules` | sorted builtin names, with Node's subpaths (`fs/promises`, `timers/promises`, `readline/promises`) |
| `isBuiltin(name)` | with or without `node:` |
| `createRequire(filename)` | a `require()` for a path, `file://` URL string or URL; backed by the `require` builtin |
| `registerHooks(hooks)` | the qjsm global of the same name (`undefined` on other engines), see `doc/qjsm.md` |
| `findPackageJSON(specifier, base)` | closest `package.json` of a relative/absolute specifier, or the package root's for a bare one; `undefined` if none |
| `runMain(main)` | loads `main` as the entry module |
| `constants` | `{ compileCacheStatus }` |
| `globalPaths` | `NODE_PATH`, `~/.node_modules`, `~/.node_libraries`, `<prefix>/lib/node` |
| `SourceMap` | a Source Map v3: `payload`, `lineLengths`, `findEntry()`, `findOrigin()` |
| `findSourceMap(path)` | the map named by a file's `//# sourceMappingURL=` (data: URL or path), once `setSourceMapsSupport(true)` |
| `getSourceMapsSupport()`, `setSourceMapsSupport(enabled, options)` | the flags are recorded; no stack trace uses them |
| `stripTypeScriptTypes(code, options)` | `mode: 'strip'` only; runs the external `swc` CLI, throws `ERR_FEATURE_UNAVAILABLE` without it |

The internals Node exposes on `Module` are there too: `_cache`, `_pathCache`,
`_extensions` (`.js`, `.json`, `.node`), `_load`, `_resolveFilename`,
`_resolveLookupPaths`, `_findPath`, `_nodeModulePaths`, `_initPaths`,
`_preloadModules`, `_debug` (`NODE_DEBUG=module`), `_stat`, `_readPackage`,
`wrap`, `wrapper`.

## Differences from Node

| Export | Here |
| --- | --- |
| `register()` | throws `ERR_FEATURE_UNAVAILABLE`; use `registerHooks()` |
| `syncBuiltinESMExports()` | no-op; builtin namespaces are the module exports themselves |
| `enableCompileCache()` | returns `{ status: FAILED, message }`; `getCompileCacheDir()` is `undefined`, `flushCompileCache()` a no-op |
| `.node` extension | throws `ERR_DLOPEN_FAILED` |
| `findPackageJSON()` | returns `undefined` for a missing package (Node 22 throws) |
| `createRequire()` | resolves with `lib/require.js`, not `Module._load()` |
