/**
 * Doubly-linked list with Array-like API + node-level access.
 *
 * Mirrors quickjs-list.c. All operations are O(1) or bounded by an explicit
 * argument (a count, or the size of a Node-reference range) — no hidden O(n)
 * full-list walks except for methods whose O(n) cost is inherent to the
 * operation (clear, unique, merge, reverse, sort).
 */

const NORMAL = 0;
const REVERSE = 1;

/* --------------------------------------------------------------- ListNode */

export class ListNode {
  #value;
  #prev = null;
  #next = null;
  #linked = false;
  #sentinel = false;

  constructor(value) {
    /* Public constructor creates an unlinked, non-sentinel node. */
    this.#value = value;
  }

  /** Internal factory: a sentinel node for a list header. */
  static _sentinel() {
    const n = new ListNode(undefined);
    n.#sentinel = true;
    return n;
  }

  /** Internal: link a node between `prev` and `next`. */
  _link(prev, next) {
    this.#prev = prev;
    this.#next = next;
    prev.#next = this;
    next.#prev = this;
    this.#linked = true;
  }

  /** Internal: unlink this node. */
  _unlink() {
    if(!this.#linked) return;
    this.#prev.#next = this.#next;
    this.#next.#prev = this.#prev;
    this.#prev = this.#next = null;
    this.#linked = false;
  }

  equals(other) {
    if(other instanceof ListNode) return other === this;
    if(other instanceof ListIterator) return other._node === this;
    return false;
  }

  valueOf() {
    return this.#value;
  }

  get value() {
    return this.#value;
  }

  set value(v) {
    if(!this.#sentinel) this.#value = v;
  }

  get prev() {
    return this.#linked ? this.#prev : null;
  }

  get next() {
    return this.#linked ? this.#next : null;
  }

  get linked() {
    return this.#linked;
  }

  get sentinel() {
    return this.#sentinel;
  }

  get [Symbol.toStringTag]() {
    return 'Node';
  }
}

/* ----------------------------------------------------------- ListIterator */

export class ListIterator {
  _node; /* current ListNode */
  _header; /* sentinel of the owning list */
  _type; /* NORMAL or REVERSE */
  _kind; /* YIELD_VALUE / YIELD_KEY / YIELD_KEY_AND_VALUE */
  _index; /* index counter (increments forward, decrements reverse) */

  constructor(list, reverse = false) {
    if(!(list instanceof List)) throw new ReferenceError('argument 1 must be a List');
    this._type = reverse ? REVERSE : NORMAL;
    this._header = list._header;
    this._node = reverse ? this._header._prev : this._header._next;
    this._kind = YIELD_VALUE;
    this._index = reverse ? -1 : 0;
  }

  /** Internal: build an iterator positioned at `node`. */
  static _at(node, header, type, kind = YIELD_VALUE, index = type === REVERSE ? -1 : 0) {
    const it = Object.create(ListIterator.prototype);
    it._node = node;
    it._header = header;
    it._type = type;
    it._kind = kind;
    it._index = index;
    return it;
  }

  next() {
    if(this._node === this._header) return { value: undefined, done: true };

    let value;
    switch (this._kind) {
      case 'key':
        value = this._index;
        break;
      case 'entry':
        value = [this._index, this._node._valueOf()];
        break;
      default:
        value = this._node._valueOf();
        break;
    }

    if(this._type === NORMAL) {
      this._node = this._node._next;
      this._index++;
    } else {
      this._node = this._node._prev;
      this._index--;
    }

    return { value, done: false };
  }

  equals(other) {
    if(other instanceof ListNode) return other === this._node;
    if(other instanceof ListIterator) return other._node === this._node;
    return false;
  }

  copy() {
    return ListIterator._at(this._node, this._header, this._type, this._kind, this._index);
  }

  isAccessible() {
    return this._node !== this._header;
  }

  get container() {
    return this._header._owner;
  }

  get type() {
    return this._type;
  }

  [Symbol.iterator]() {
    return this;
  }

  get [Symbol.toStringTag]() {
    return 'ListIterator';
  }
}

/* ---------------------------------------------------------------- helpers */

/** Resolve a Node reference from a ListNode or ListIterator argument. */
function nodeGet(arg) {
  if(arg instanceof ListNode) return arg;
  if(arg instanceof ListIterator) return arg._node;
  throw new TypeError('argument must be ListIterator | ListNode');
}

/** Iterate Nodes from `start` (inclusive) to `end` (exclusive), inclusive of sentinel stop. */
function forRange(list, start, end, fn) {
  for(let n = start; n !== end && n !== list._header; n = n._next) fn(n);
}

/* ------------------------------------------------------------------- List */

export class List {
  _header; /* sentinel ListNode */
  _size;

  constructor(iterable) {
    this._header = ListNode._sentinel();
    this._header._owner = this;
    this._header._prev = this._header;
    this._header._next = this._header;
    this._size = 0;

    if(iterable !== undefined && iterable !== null) for(const v of iterable) this._insertTail(v);
  }

  _insertBefore(value, next) {
    const node = new ListNode(value);
    node._link(next._prev, next);
    this._size++;
    return node;
  }

  _insertAfter(value, prev) {
    const node = new ListNode(value);
    node._link(prev, prev._next);
    this._size++;
    return node;
  }

  _insertTail(value) {
    return this._insertBefore(value, this._header);
  }

  _insertHead(value) {
    return this._insertAfter(value, this._header);
  }

  _eraseNode(node) {
    node._unlink();
    this._size--;
    return node._valueOf();
  }

  clear() {
    let n = this._header._next;
    while(n !== this._header) {
      const next = n._next;
      n._prev = n._next = null;
      n._linked = false;
      n = next;
    }
    this._header._prev = this._header._next = this._header;
    this._size = 0;
  }

  begin() {
    return ListIterator._at(this._header._next, this._header, NORMAL);
  }

  end() {
    return ListIterator._at(this._header, this._header, NORMAL);
  }

  rbegin() {
    return ListIterator._at(this._header._prev, this._header, REVERSE);
  }

  rend() {
    return ListIterator._at(this._header, this._header, REVERSE);
  }

  erase(start, end) {
    const s = nodeGet(start);

    if(end === undefined) {
      const next = s._next;
      this._eraseNode(s);
      return ListIterator._at(next, this._header, NORMAL);
    }

    const e = nodeGet(end);
    let n = s;

    while(n !== this._header && n !== e) {
      const next = n._next;
      n._unlink();
      this._size--;
      n = next;
    }

    return ListIterator._at(e, this._header, NORMAL);
  }

  insert(after, ...values) {
    /* C signature: insert(after, ...values) → inserts after `after` (or at tail if nullish).
       Returns an iterator starting at the first inserted node. */
    const ref = after === null || after === undefined ? this._header : nodeGet(after);
    const first = ref._next;
    const inserted = [];
    let pos = ref;

    for(const v of values) {
      pos = this._insertAfter(v, pos);
      inserted.push(pos);
    }
    /* Insert in reverse order of values so `values[0]` ends up right after `ref`;
       but since we walk forward, inserted[i] correspond to values[i] already in order.
       The C code inserts in reverse (for i=argc-1..1), then returns an iterator
       pointing at the node *before* the last inserted. We mirror the visible
       ordering (values appear in argument order after `ref`) with a forward
       iterator to the first inserted node. */
    if(inserted.length === 0) return ListIterator._at(first, this._header, NORMAL);
    return ListIterator._at(inserted[0], this._header, NORMAL);
  }

  insertBefore(before, ...values) {
    const ref = before === null || before === undefined ? this._header : nodeGet(before);
    const firstPrev = ref._prev;
    let pos = ref;
    for(const v of values) pos = this._insertBefore(v, pos);

    /* Forward iterator starting at the first inserted node. */
    return ListIterator._at(firstPrev._next, this._header, REVERSE);
  }

  unique(cmp) {
    let n = this._header._next;
    while(n !== this._header) {
      while(n._next !== this._header) {
        const dup = cmp ? !!cmp(n._valueOf(), n._next._valueOf()) : Object.is(n._valueOf(), n._next._valueOf());
        if(!dup) break;
        const victim = n._next;
        victim._unlink();
        this._size--;
      }

      n = n._next;
    }

    return this._size;
  }

  merge(other, cmp) {
    if(!(other instanceof List)) throw new TypeError('argument 1 must be a List');
    const pred = cmp || ((a, b) => a <= b);

    let node = this._header._next;
    for(let el = other._header._next; el !== other._header; el = el._next) {
      const v = el._valueOf();

      while(node !== this._header && pred(node._valueOf(), v)) node = node._next;

      this._insertBefore(v, node);
    }
    return this;
  }

  push(...values) {
    for(const v of values) this._insertTail(v);
    return this._size;
  }

  pop() {
    if(this._header._prev === this._header) return undefined;
    return this._eraseNode(this._header._prev);
  }

  unshift(...values) {
    /* Match C: insert in reverse so values[0] ends up at the head. */
    for(let i = values.length - 1; i >= 0; i--) this._insertHead(values[i]);
    return this._size;
  }

  shift() {
    if(this._header._next === this._header) return undefined;
    return this._eraseNode(this._header._next);
  }

  concat(...args) {
    const out = new List();

    /* Move this list's nodes into out (O(1) splice). */
    if(this._header._next !== this._header) {
      const first = this._header._next;
      const last = this._header._prev;
      /* reset out header links */
      out._header._next = first;
      out._header._prev = last;
      first._prev = out._header;
      last._next = out._header;
      this._header._next = this._header;
      this._header._prev = this._header;
      out._size = this._size;
      this._size = 0;
    }

    for(let i = 0; i < args.length; i++) {
      const arg = args[i];

      if(arg instanceof List) {
        if(arg._header._next !== arg._header) {
          const first = arg._header._next;
          const last = arg._header._prev;
          const tail = out._header._prev;
          tail._next = first;
          first._prev = tail;
          last._next = out._header;
          out._header._prev = last;
          out._size += arg._size;
          arg._header._next = arg._header;
          arg._header._prev = arg._header;
          arg._size = 0;
        }
      } else if(arg != null && typeof arg[Symbol.iterator] === 'function') {
        for(const v of arg) out._insertTail(v);
      } else throw new TypeError(`argument ${i} not a List or iterable`);
    }
    return out;
  }

  slice(start, end) {
    const s = nodeGet(start);
    const e = nodeGet(end);
    const out = new List();
    forRange(this, s, e, n => out._insertTail(n._valueOf()));
    return out;
  }

  reverse() {
    if(this._size < 2) return this;

    /* Swap prev/next of every node including the sentinel. */
    let n = this._header;

    do {
      const tmp = n._next;
      n._next = n._prev;
      n._prev = tmp;
      n = n._next;
    } while(n !== this._header);

    return this;
  }

  splice(start, end, ...items) {
    const s = nodeGet(start);
    const e = nodeGet(end);

    /* Detach the range [s, e) into a new list. */
    const out = new List();

    if(s !== e) {
      const first = s;
      const last = e._prev;
      const before = first._prev;

      /* Unlink range from this list. */
      before._next = e;
      e._prev = before;

      /* Link it into out. */
      out._header._next = first;
      out._header._prev = last;
      first._prev = out._header;
      last._next = out._header;

      /* Count how many nodes were in the range. */
      let count = 0;
      for(let n = first; n !== e; n = n._next) count++;
      out._size = count;
      this._size -= count;
    }

    /* Insert items before `e`. */
    for(const v of items) this._insertBefore(v, e);

    return out;
  }

  fill(value, start, end) {
    const s = nodeGet(start);
    const e = nodeGet(end);
    forRange(this, s, e, n => {
      n._value = value;
    });
    return this;
  }

  rotate(node) {
    const n = nodeGet(node);
    if(n === this._header || n === this._header._next) return this;

    /* Move sentinel before `n`: effectively starting the list at `n`. */
    const first = this._header._next;
    const last = this._header._prev;
    const newLast = n._prev;

    /* Re-link: newLast ... last, then first ... newLast's previous segment. 
       Simpler: splice [n..last] to front. */
    const before = n._prev;
    before._next = this._header;
    this._header._prev = before;
    last._next = first;
    first._prev = last;
    n._prev = this._header;
    this._header._next = n;
    return this;
  }

  sort(cmp) {
    if(this._size < 2) return this;
    const pred = cmp || ((a, b) => a - b);
    const values = [];
    for(let n = this._header._next; n !== this._header; n = n._next) values.push(n._valueOf());
    values.sort(pred);
    let n = this._header._next;

    for(const v of values) {
      n._value = v;
      n = n._next;
    }
    return this;
  }

  values() {
    return ListIterator._at(this._header._next, this._header, NORMAL, 'value');
  }

  keys() {
    return ListIterator._at(this._header._next, this._header, NORMAL, 'key');
  }

  entries() {
    return ListIterator._at(this._header._next, this._header, NORMAL, 'entry');
  }

  [Symbol.iterator]() {
    return this.values();
  }

  find(pred, thisArg) {
    let i = 0;
    for(let n = this._header._next; n !== this._header; n = n._next, i++) if(pred.call(thisArg, n._valueOf(), i, this)) return n;

    return undefined;
  }

  findLast(pred, thisArg) {
    let i = this._size - 1;

    for(let n = this._header._prev; n !== this._header; n = n._prev, i--) if(pred.call(thisArg, n._valueOf(), i, this)) return n;

    return undefined;
  }

  get length() {
    return this._size;
  }

  get address() {
    return '0x' + (this._size * 0 + (this._header ? 0 : 0)).toString(16);
  }

  get [Symbol.toStringTag]() {
    return 'List';
  }

  static from(iterable) {
    const list = new List();
    if(iterable == null || typeof iterable[Symbol.iterator] !== 'function') throw new TypeError('argument 1 not iterable');
    for(const v of iterable) list._insertTail(v);
    return list;
  }

  static of(...items) {
    const list = new List();
    for(const v of items) list._insertTail(v);
    return list;
  }

  static isList(value) {
    return value instanceof List;
  }
}

/* Array.prototype helpers borrowed by the C module (join, toString, etc.) */
for(const name of ['join', 'toString', 'toLocaleString', 'flat', 'flatMap', 'copyWithin']) List.prototype[name] = Array.prototype[name];

List.NORMAL = NORMAL;
List.REVERSE = REVERSE;

export default List;
