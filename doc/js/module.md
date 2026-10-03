# module

Source: `lib/module.js` (pure JS)

Implements the subset of Node's `node:module` API
(https://nodejs.org/api/module.html) that's implementable on top of this
engine's own builtin-module registry (`globalThis.builtins`, native — see
`jsm_builtins()` in `src/qjsm.c`). `import ... from 'node:module'` resolves
here too — `src/qjsm.c` strips a leading `node:` before builtin-name lookup.

Not the same file as `lib/nodeModulesLoader.js` (formerly `lib/module.js`),
which is an unrelated `registerHooks()` demo — see
`doc/js/nodeModulesLoader.md`.

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `builtinModules` | string[] | Sorted names from `globalThis.builtins`. |
| `isBuiltin(name)` | function | Whether `name` (with or without a `node:` prefix) is a builtin module. |
| `createRequire(filename)` | function | Returns a `require()` (the compiled-in `require` builtin) resolving specifiers against `filename`, a path or `file://` URL; includes `node_modules`/`package.json` lookup. |
| `registerHooks(hooks)` | function | Node's synchronous `module.registerHooks()` — the qjsm global of the same name (`undefined` on other engines). See `doc/qjsm.md`. |
| *(default)* | object | `{ builtinModules, isBuiltin, createRequire, registerHooks }` |

## Not implemented

Node's `Module` class, async `register()` hooks, `syncBuiltinESMExports()`,
and `SourceMap` aren't implemented — see `TODO.md`.
