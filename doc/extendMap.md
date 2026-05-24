# extendMap

Source: `lib/extendMap.js` (pure JS) — default export: `extendMap`

Installs extra (non-enumerable) methods onto `Map.prototype`.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `extendMap(proto=Map.prototype)` | 0–1 | function | Installs the extensions. **(default export)** |
| `MapExtensions` | — | const | The bag of methods that get installed. |
| `MapPrototype` | — | const | Shortcut for `Map.prototype`. |

## Added `Map.prototype` methods

| Method | Description |
| --- | --- |
| `getOrInsert(key, value)` | Returns the value for `key`, inserting `value` if absent. |
| `getOrInsertComputed(key, fn)` | Like `getOrInsert` but computes the default via `fn(key)`. |
