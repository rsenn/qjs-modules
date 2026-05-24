# deep

Source: `quickjs-deep.c` — module exports a function list, **`DeepIterator`**, and a `default` object.

Deep (recursive) traversal and manipulation of nested objects/arrays, addressing
nodes by a path (array of keys or a `Pointer`).

## Module functions

| Function | Args | Description |
| --- | --- | --- |
| `find(root, predicate)` | 2 | Returns the first value (deeply) matching `predicate`. |
| `select(root, predicate)` | 2 | Returns all values matching `predicate`. |
| `get(root, path)` | 2 | Reads the value at `path`. |
| `set(root, path, value)` | 3 | Writes `value` at `path`, creating intermediate nodes. |
| `unset(root, path)` | 2 | Deletes the value at `path`. |
| `flatten(root)` | 1 | Produces a flat map of path → value. |
| `pathOf(root, value)` | 2 | Returns the path at which `value` occurs. |
| `equals(a, b)` | 2 | Deep structural equality. |
| `iterate(root)` | 1 | Returns a `DeepIterator` over all nodes. |
| `forEach(root, fn)` | 2 | Invokes `fn(value, path, root)` for every node. |
| `clone(root)` | 1 | Deep copy. |

## DeepIterator

```js
new DeepIterator(root[, ...])   // length 1
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `next()` | 0 | method | Iterator step; yields `[value, path]`. |
| `return()` | 0 | method | Ends iteration. |
| `leave()` | 0 | method | Ascends out of the current subtree. |
| `skip()` | 0 | method | Skips descending into the current node. |
| `path` | — | getter | Path of the current node. |
| `[Symbol.iterator]()` | 0 | method | Returns itself. |
