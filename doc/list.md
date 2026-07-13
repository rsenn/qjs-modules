# list

Source: `quickjs-list.c` — module exports **`List`**, **`ListIterator`**, **`ListNode`**

A doubly-linked list with an `Array`-like API plus node-level access.

Every method here is O(1), or O(k) bounded by an explicit argument (a count, or the
size of the `Node`-reference range you pass in) — never a hidden O(n) full-list walk.
Search/lookup and functional-iteration convenience methods that Array offers
(`includes`, `indexOf`, `lastIndexOf`, `find`/`findLast`/`findIndex`/`findLastIndex`,
`every`, `some`, `filter`, `forEach`, `map`, `reduce`, `reduceRight`, `toReversed`, and
index-based `at()`) are deliberately **not** provided: a linked list can't do any of
these without visiting every node, and unlike `sort`/`clear`/`reverse` (below) that cost
isn't inherent to the operation's definition — it's just what a linked list happens to
be bad at. Use `values()`/`keys()`/`entries()` (each step O(1)) to write the equivalent
loop yourself, with the O(n) cost visible in your own code rather than hidden inside a
single call.

## List

```js
new List([iterable])   // length 1
```

### Structural methods

| Method | Args | Description |
| --- | --- | --- |
| `clear()` | 0 | Removes all nodes. O(n): must free every node. |
| `begin()` / `end()` | 0 | Iterators to the first / past-the-last node. |
| `rbegin()` / `rend()` | 0 | Reverse iterators. |
| `erase(it)` / `erase(start, end)` | 1–2 | Removes one node, or a `Node`-reference range. |
| `insert(after, ...values)` | 1+ | Inserts after a `Node` reference (or the tail if omitted). |
| `insertBefore(before, ...values)` | 1+ | Inserts before a `Node` reference. |
| `unique()` | 0 | Removes consecutive duplicates. O(n): must compare every adjacent pair once. |
| `merge(other)` | 1 | Merges another (sorted) list in, consuming it. O(n+m): must visit every element of both. |

### Array-style methods

| Method | Args | Description |
| --- | --- | --- |
| `push` / `pop` / `unshift` / `shift` | 0–1 | Add/remove at the ends. |
| `concat(...args)` | 0+ | Splices each `List` argument's entire contents in directly (O(1) per `List`), **consuming this list and every `List` argument** (they end up empty) — a deliberate departure from `Array.prototype.concat`'s non-mutating semantics, in exchange for genuine O(1) cost regardless of list size. A non-`List` iterable argument is still appended in O(m), m = its own length, since there's no linked structure to splice. |
| `slice(start, end)` | 2 | Copies the `Node`-reference range `[start, end)`; cost is O(k), k = range size. |
| `reverse()` | 0 | In-place reversal. O(n): must relink every node once. |
| `splice(start, end, ...items)` | 2+ | Removes the `Node`-reference range `[start, end)` (O(1) pointer splice) and inserts `items` in its place (O(i)). |
| `fill(value, start, end)` | 3 | Fills the `Node`-reference range `[start, end)` with `value`; cost is O(k), k = range size. |
| `rotate(node)` | 1 | Repositions the list's start to `node` in O(1) (moves only the sentinel, not the elements). |
| `sort([cmp])` | 0 | Sort in place. O(n log n): inherent to sorting, like `Array.prototype.sort`. |

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
