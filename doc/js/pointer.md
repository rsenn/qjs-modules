# pointer

Source: `quickjs-pointer.c` — module exports **`Pointer`** and **`DereferenceError`**

A JSON-Pointer-like path object: an ordered list of atoms (keys/indices) used to
address into nested data, with array-style manipulation.

## Pointer

```js
new Pointer(path)   // length 1
```

`path` may be a pointer string, an array of keys, or another `Pointer`.

### Methods

| Method | Args | Description |
| --- | --- | --- |
| `deref(root)` | 1 | Resolves the pointer against `root`, returning the addressed value (throws `DereferenceError` if missing). |
| `toString()` | 0 | Serializes to a pointer string. |
| `toArray()` | 0 | Returns the atoms as an array. |
| `shift()` | 0 | Removes and returns the first atom. |
| `unshift(atom)` | 1 | Prepends an atom. |
| `pop()` | 0 | Removes and returns the last atom. |
| `push(atom)` | 1 | Appends an atom. |
| `values()` | 0 | Iterates the atoms (also `[Symbol.iterator]`). |
| `hier()` | 0 | Returns the hierarchy of prefixes. |
| `at(index)` | 1 | Returns the atom at `index`. |
| `concat(other)` | 1 | Returns a new pointer with `other` appended. |
| `slice(start, end)` | 0 | Returns a sub-range pointer. |
| `splice(...)` | 0 | Array-style splice of atoms. |
| `up(n)` | 1 | Returns the pointer truncated by `n` levels (alias `truncate`). |
| `down(...)` | 0 | Extends the pointer downward. |
| `equal(other)` | 1 | Equality test. |
| `compare(other)` | 1 | Ordering comparison. |
| `common(other)` | 1 | Longest common prefix with `other`. |
| `relativeTo(other)` | 1 | Path relative to `other`. |
| `startsWith(other)` / `endsWith(other)` | 1 | Prefix/suffix tests. |

### Properties

| Property | Kind | Description |
| --- | --- | --- |
| `length` | getter | Number of atoms. |
| `path` | getter/setter | The path as an array. |
| `atoms` | getter | The raw atom list. |

### Static functions

| Function | Args | Description |
| --- | --- | --- |
| `from(value)` | 1 | Builds a pointer from a string/array. |
| `fromAtoms(atoms)` | 1 | Builds from an atom list. |
| `of(...keys)` | 0 | Builds from argument keys. |
| `ofAtoms(...atoms)` | 0 | Builds from argument atoms. |
| `isPointer(value)` | 1 | Whether `value` is a `Pointer`. |

## DereferenceError

`Error` subclass thrown when `deref` fails. Read-only properties: `message`,
`pointer`, `root`, `pos`.
