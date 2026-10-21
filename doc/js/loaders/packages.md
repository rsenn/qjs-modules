# loaders/packages

Source: `lib/loaders/packages.js` (JS over `registerHooks()`, imports only `std` and `os`) — exports: `resolve`,
`packageResolve`, `packageImportsResolve`, `packageExportsResolve`, `legacyMainResolve`, `readPackageJson`; qjsm only

Node's ESM package resolution ([nodejs.org/api/esm.html](https://nodejs.org/api/esm.html#resolution-algorithm-specification))
as a resolve hook. qjsm preloads it like `loaders/node` (off with `-N`), so `import 'pkg'` finds
`node_modules/pkg` and `import.meta.resolve()` reports what it finds.

| Specifier | Resolves through |
| --- | --- |
| `pkg`, `pkg/sub`, `@scope/pkg/sub` | the nearest `node_modules/pkg` up from the importer: its `"exports"`, else `"main"` |
| `#name` | `"imports"` of the `package.json` nearest to the importer |
| `self/sub` | the importer's own package, by its `"name"`, when it has `"exports"` |
| anything else | the next resolver (relative paths, builtins, `QUICKJS_MODULE_PATH`) |

A bare name that no `node_modules` holds goes on to the next resolver, so a module on qjsm's search path
still loads.

## Conditions

`"exports"` and `"imports"` objects are read key by key in the package's order, the first key that is an active
condition wins. Active: `node`, `import`, `default`, plus every `-C name` / `--conditions=name` given to qjsm
(Node's flag). Node 23 also has `module-sync`, Bun has `bun` instead of `module-sync`.

```json
"exports": {
  ".": {
    "import": "./dist/codec.mjs",
    "default": "./dist/codec.umd.js"
  },
  "./sub/*": "./dist/sub/*.js",
  "./internal/*": null
}
```

## Behaviour

| Case | Result |
| --- | --- |
| `"exports"` string, array or condition object | the main export (`.`) |
| pattern key `./sub/*` | the longest matching base, `*` replaced in the target |
| target `null` | `ERR_PACKAGE_PATH_NOT_EXPORTED` |
| array target | the first that is not an invalid target, a missing file is not skipped (as on Node) |
| no `"exports"` | `main`, `main.js`, `main.json`, `main/index.js`, `main/index.json`, then `index.js`, `index.json` (no `.node` addons) |
| the file | through symlinks (`realpath`); a missing one is returned as is, the load then fails |

Errors carry Node's codes and messages: `ERR_MODULE_NOT_FOUND`, `ERR_PACKAGE_PATH_NOT_EXPORTED`,
`ERR_PACKAGE_IMPORT_NOT_DEFINED`, `ERR_INVALID_MODULE_SPECIFIER`, `ERR_INVALID_PACKAGE_TARGET`,
`ERR_INVALID_PACKAGE_CONFIG`.

## The functions behind the hook

They work on paths, not URLs; `utilities/resolve-imports.js` uses them for directory imports.

```js
import { packageResolve, packageExportsResolve, readPackageJson } from 'loaders/packages';

packageResolve('@sc/pkg/sub', '/app/src/main.js');           // '/app/node_modules/@sc/pkg/s.js'
packageResolve('cond', '/app/src/main.js', ['import']);       // conditions other than node, import
packageResolve('nope', '/app/src/main.js');                   // undefined: no such package
packageExportsResolve('/app/pkg', '.', readPackageJson('/app/pkg').exports);
```

| Function | Returns |
| --- | --- |
| `resolve(specifier, context, nextResolve)` | the hook: `{ url, shortCircuit: true }` or what `nextResolve` returns |
| `packageResolve(specifier, parent, conditions?)` | path, or `undefined` when no package of that name is found |
| `packageImportsResolve(specifier, parent, conditions?)` | path of a `#name` import |
| `packageExportsResolve(pkgDir, subpath, exports, conditions?)` | path of `'.'` or `'./sub'` |
| `legacyMainResolve(pkgDir, pjson)` | path of the `main` file |
| `readPackageJson(dir)` | the parsed `package.json`, `null` when there is none; cached |

Tests: `tests/unittests/test-loaders-packages.js`, expected values recorded from Node 23.11's
`import.meta.resolve()`, which Bun's `resolveSync()` matches on every package case.
