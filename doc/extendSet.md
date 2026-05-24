# extendSet

Source: `lib/extendSet.js` (pure JS) — default export: `extendSet`

Installs the set-algebra methods (TC39 Set-methods proposal) onto
`Set.prototype`.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `extendSet(proto=Set.prototype, ctor=Set)` | 0–2 | function | Installs the extensions. **(default export)** |
| `SetExtensions` | — | const | The prototype method bag. |
| `SetStatic` | — | const | The static helper bag. |

## Added `Set.prototype` methods

| Method | Description |
| --- | --- |
| `union(other)` | Elements in either set. |
| `intersection(other)` | Elements in both sets. |
| `difference(other)` | Elements in this set but not `other`. |
| `symmetricDifference(other)` | Elements in exactly one set. |
| `isSubsetOf(other)` | Whether this set is contained in `other`. |
| `isSupersetOf(other)` | Whether this set contains `other`. |
| `isDisjointFrom(other)` | Whether the sets share no elements. |
