# extendArray

Source: `lib/extendArray.js` (pure JS) — default export: `extendArray`

Installs extra (non-enumerable) methods onto `Array.prototype`.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `extendArray(proto=Array.prototype)` | 0–1 | function | Defines the extension methods on `proto`. **(default export)** |
| `ArrayExtensions` | — | const | The bag of methods that get installed. |
| `ArrayPrototype` | — | const | Shortcut for `Array.prototype`. |

## Added `Array.prototype` methods

`at`, `clear`, `findLast`, `findLastIndex`, `unique`, `add`, `search`, `pushIf`,
`pushUnique`, `unshiftUnique`, `insert`, `inserter`, `delete`, `remove`,
`removeIf`, `rotateRight`, `rotateLeft`, `match`, `group`, `groupToMap`, `equal`.
