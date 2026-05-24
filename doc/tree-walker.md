# tree-walker

Source: `quickjs-tree-walker.c` — module exports **`TreeWalker`** and **`TreeIterator`**

DOM-style traversal over an arbitrary nested JS value (objects/arrays treated as
a tree of nodes).

## TreeWalker

```js
new TreeWalker(root[, filter, flags])   // length 1
```

### Navigation methods (each returns the new current node)

| Method | Args | Description |
| --- | --- | --- |
| `firstChild()` | 0 | Descend to the first child of the current node. |
| `lastChild()` | 0 | Descend to the last child. |
| `nextNode()` | 0 | Advance to the next node in document order. |
| `nextSibling()` | 0 | Move to the next sibling. |
| `parentNode()` | 0 | Move up to the parent. |
| `previousNode()` | 0 | Move to the previous node in document order. |
| `previousSibling()` | 0 | Move to the previous sibling. |
| `toString()` | 0 | String representation of the walker. |

### Properties

| Property | Kind | Description |
| --- | --- | --- |
| `root` | getter | The traversal root. |
| `currentNode` | getter | The node currently pointed at. |
| `currentKey` | getter | Key/index of the current node within its parent. |
| `currentPath` | getter | Path from the root to the current node. |
| `depth` | getter | Current nesting depth. |
| `index` | getter/setter | Index of the current node among its siblings. |
| `length` | getter | Number of children of the current node. |
| `tagMask` | getter/setter | Bitmask of node types to visit. |
| `filter` | getter | The node filter (enumerable). |
| `flags` | getter/setter | Traversal flags. |

## TreeIterator

```js
new TreeIterator(root[, ...])   // length 1
```

Iterable form of the walker: exposes `next()`, `[Symbol.iterator]()` and tags
itself `"TreeIterator"`.
