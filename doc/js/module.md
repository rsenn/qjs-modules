# module

Source: `lib/module.js` (pure JS)

Implements the subset of Node's `node:module` API
(https://nodejs.org/api/module.html) that's implementable on top of this
engine's own builtin-module registry (`globalThis.builtins`, native — see
`jsm_builtins()` in `src/qjsm.c`). `import ... from 'node:module'` resolves
here too — `src/qjsm.c` strips a leading `node:` before builtin-name lookup.

Not the same file as `lib/nodeModulesLoader.js` (formerly `lib/module.js`),
which is an unrelated custom `moduleLoader()` hook demo — see
`doc/js/nodeModulesLoader.md`.

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `builtinModules` | string[] | Sorted names from `globalThis.builtins`. |
| `isBuiltin(name)` | function | Whether `name` (with or without a `node:` prefix) is a builtin module. |
| `createRequire(filename)` | function | Returns a synchronous CJS `require()` resolving relative/absolute specifiers against `filename`. No `node_modules`/`package.json` resolution — use `require` (`lib/require.js`) directly for that. |
| *(default)* | object | `{ builtinModules, isBuiltin, createRequire }` |

## Not implemented

Node's `Module` class, `register()` (loader hooks), `syncBuiltinESMExports()`,
and `SourceMap` aren't implemented — see `TODO.md`.
