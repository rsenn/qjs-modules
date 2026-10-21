# require

Source: `lib/require.js` (pure JS) — default export: `require`

A CommonJS-style `require()` for loading modules synchronously.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `require(m)` | 1 | function | Resolves and loads module `m`, returning its `exports` (or `exports.default`). **(default export)** |

## Resolution

| `m` | Result |
| --- | --- |
| `./x`, `../x`, `/x` | the file, relative to the caller |
| `node:fs`, `fs`, `util`, `fs/promises`, ... (Node's names) | Node's module object, the same as `import('node:fs')`'s default |
| another bare name | the nearest `node_modules/m` above the caller (`package.json` `main`, then `index.js`), else a qjsm builtin or `QUICKJS_MODULE_PATH` |

An error thrown while loading `m` reaches the caller unchanged; only a
`SyntaxError` makes it retry `m` as an ES module.
