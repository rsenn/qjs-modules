# list

Source: `quickjs-list.c` — module exports **`List`**, **`ListIterator`**, **`ListNode`**

A doubly-linked list with an `Array`-like API plus node-level access.

## List

```js
new List([iterable])   // length 1
```

### Structural methods

| Method | Args | Description |
| --- | --- | --- |
| `clear()` | 0 | Removes all nodes. |
| `begin()` / `end()` | 0 | Iterators to the first / past-the-last node. |
| `rbegin()` / `rend()` | 0 | Reverse iterators. |
| `erase(it)` | 1 | Removes the node at an iterator. |
| `insert(value)` | 1 | Inserts after the current position. |
| `insertBefore(value)` | 1 | Inserts before the current position. |
| `unique()` | 0 | Removes consecutive duplicates. |
| `merge(other)` | 1 | Merges another (sorted) list in. |

### Array-style methods

| Method | Args | Description |
| --- | --- | --- |
| `push` / `pop` / `unshift` / `shift` | 0–1 | Add/remove at the ends. |
| `includes(v)` / `indexOf(v)` / `lastIndexOf(v)` | 1 | Membership / search. |
| `find` / `findLast` / `findIndex` / `findLastIndex` | 1 | Predicate search. |
| `concat(other)` | 1 | Concatenation. |
| `slice(start, end)` | 0 | Sub-range copy. |
| `reverse()` / `toReversed()` | 0 | In-place / copying reversal. |
| `splice(...)` | 0 | Remove/insert range. |
| `fill(value, start, end)` | 1 | Fill range with a value. |
| `rotate(n)` | 1 | Rotate elements by `n`. |
| `sort([cmp])` | 0 | Sort in place. |

### Functional methods

`every`, `some`, `filter`, `forEach`, `map`, `reduce`, `reduceRight` — same
semantics as the `Array` equivalents.

### Iteration

`values()`, `keys()`, `entries()` (and `[Symbol.iterator]` = `values`).

### Properties

| Property | Kind | Description |
| --- | --- | --- |
| `length` | getter | Number of elements (enumerable). |
| `address` | getter | Native address of the list. |

### Static functions

| Function | Args | Description |
| --- | --- | --- |
| `from(iterable)` | 1 | Builds a list from an iterable. |
| `of(...items)` | 0 | Builds a list from arguments. |
| `isList(value)` | 1 | Type predicate. |

## ListIterator

```js
new ListIterator(...)   // length 1
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `next()` | 0 | method | Iterator step. |
| `equals(other)` | 1 | method | Compares two iterators. |
| `copy()` | 0 | method | Clones the iterator. |
| `isAccessible()` | 0 | method | Whether the iterator points at a live node. |
| `container` | — | getter | The owning list. |
| `type` | — | getter | Iteration kind. |

## ListNode

```js
new ListNode(value)   // length 1
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `equals(other)` | 1 | method | Node identity comparison. |
| `valueOf()` | 0 | method | Returns the node value. |
| `prev` / `next` | — | getter | Neighboring nodes. |
| `linked` | — | getter | Whether the node is in a list. |
| `sentinel` | — | getter | Whether it is the list sentinel. |
| `value` | — | getter/setter | The stored value. |
| `address` | — | getter | Native address. |
