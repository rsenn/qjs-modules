import { List, ListIterator, ListNode } from 'list';
import { assert, eq, tests } from './tinytest.js';

const arr = (list) => [...list];
const eqArr = (list, expected) => eq(JSON.stringify(arr(list)), JSON.stringify(expected));

tests({
  'new List()'() {
    eqArr(new List(), []);
    eqArr(new List([1, 2, 3]), [1, 2, 3]);
    eqArr(new List('ab'), ['a', 'b']);

    let threw = false;
    try {
      new List(42);
    } catch(e) {
      threw = e instanceof TypeError;
    }
    assert(threw, 'new List(42) should throw TypeError (not iterable)');
  },

  'length'() {
    const l = new List([1, 2, 3]);
    eq(l.length, 3);
    l.push(4);
    eq(l.length, 4);
    l.clear();
    eq(l.length, 0);
  },

  'address'() {
    const l = new List([1]);
    eq(typeof l.address, 'string');
    assert(l.address.startsWith('0x'), 'address should look like a pointer');
  },

  'push()/pop()/unshift()/shift()'() {
    const l = new List();
    eq(l.push(1), 1);
    eq(l.push(2, 3), 3);
    eqArr(l, [1, 2, 3]);

    eq(l.pop(), 3);
    eqArr(l, [1, 2]);

    eq(l.unshift(0), 3);
    eqArr(l, [0, 1, 2]);

    eq(l.shift(), 0);
    eqArr(l, [1, 2]);

    const empty = new List();
    eq(empty.pop(), undefined);
    eq(empty.shift(), undefined);
  },

  'index access (get/set/has)'() {
    const l = new List([1, 2, 3]);

    eq(l[0], 1);
    eq(l[2], 3);
    eq(l[-1], 3);
    eq(l[-3], 1);

    assert(0 in l);
    assert(!(5 in l));
    assert(!(-4 in l));
    assert(-3 in l);

    l[1] = 'X';
    eqArr(l, [1, 'X', 3]);

    // setting out of range (positive) pads with undefined
    const l2 = new List([1, 2, 3]);
    l2[5] = 99;
    eqArr(l2, [1, 2, 3, undefined, undefined, 99]);
    eq(l2.length, 6);

    // setting out of range (negative) pads at the head
    const l3 = new List([1, 2, 3]);
    l3[-5] = 'Y';
    eqArr(l3, ['Y', undefined, 1, 2, 3]);
  },

  'clear()'() {
    const l = new List([1, 2, 3]);
    l.clear();
    eqArr(l, []);
    eq(l.length, 0);
  },

  'begin()/end()/rbegin()/rend()'() {
    const l = new List([1, 2, 3]);

    let it = l.begin();
    assert(it.isAccessible());
    eq(it.next().value, 1);

    const end = l.end();
    assert(!end.isAccessible());

    let rit = l.rbegin();
    assert(rit.isAccessible());
    eq(rit.next().value, 3);

    const rend = l.rend();
    assert(!rend.isAccessible());
  },

  'values()/keys()/entries()/[Symbol.iterator]'() {
    const l = new List(['a', 'b', 'c']);

    eqArr(l.values(), ['a', 'b', 'c']);
    eqArr(l.keys(), [0, 1, 2]);
    eqArr(l.entries(), [
      [0, 'a'],
      [1, 'b'],
      [2, 'c'],
    ]);
    eqArr(l, ['a', 'b', 'c']); // default iterator === values()
  },

  'erase() single node'() {
    const l = new List([1, 2, 3]);
    const it = l.begin(); // points at 1

    const ret = l.erase(it);
    eqArr(l, [2, 3]);
    eq(ret.next().value, 2); // returned iterator points to the node after the erased one
  },

  'erase() range'() {
    const l = new List([1, 2, 3, 4, 5]);
    const start = l.begin();
    start.next(); // index 1 (value 2)
    const end = l.begin();
    end.next();
    end.next();
    end.next(); // index 3 (value 4)

    const ret = l.erase(start, end); // erases [2,3), i.e. values 2 and 3
    eqArr(l, [1, 4, 5]);
    eq(ret.next().value, 4); // returned iterator points at the retained `end` node

    // erasing an empty range [x, x) removes nothing and returns an iterator to x
    const l2 = new List([1, 2, 3, 4]);
    const s2 = l2.begin();
    s2.next();
    const e2 = l2.begin();
    e2.next();
    const ret2 = l2.erase(s2, e2);
    eqArr(l2, [1, 2, 3, 4]);
    eq(ret2.next().value, 2);
  },

  'insert()'() {
    // insert(after, ...values): inserts after the given node (or at the head if omitted)
    const l = new List([1, 2, 3]);
    l.insert(undefined, 10, 20);
    eqArr(l, [10, 20, 1, 2, 3]);

    const l2 = new List([1, 2, 3]);
    const it = l2.begin(); // points at 1
    l2.insert(it, 10, 20);
    eqArr(l2, [1, 10, 20, 2, 3]);
  },

  'insertBefore()'() {
    const l = new List([1, 2, 3]);
    const it = l.begin();
    it.next(); // points at 2
    l.insertBefore(it, 10, 20);
    eqArr(l, [1, 10, 20, 2, 3]);

    // insertBefore(undefined, ...) inserts before the sentinel, i.e. at the tail
    const l2 = new List([1, 2, 3]);
    l2.insertBefore(undefined, 10, 20);
    eqArr(l2, [1, 2, 3, 10, 20]);
  },

  'unique()'() {
    const l = new List([1, 1, 2, 2, 2, 3, 1]);
    const size = l.unique();
    eqArr(l, [1, 2, 3, 1]);
    eq(size, l.length);

    // Custom predicate follows the natural "are these equal" reading: truthy
    // removes the second element (duplicate), falsy keeps it (distinct).
    const l2 = new List([1, 2, 3, 4, 5]);
    l2.unique((a, b) => a === b); // all distinct -> nothing removed
    eqArr(l2, [1, 2, 3, 4, 5]);

    const l3 = new List([1, 1, 2, 3, 3, 3]);
    l3.unique((a, b) => a === b);
    eqArr(l3, [1, 2, 3]);

    const l4 = new List([1, 2, 3, 4, 5]);
    l4.unique((a, b) => true); // "always equal" collapses to one element
    eqArr(l4, [1]);
  },

  'merge()'() {
    const a = new List([1, 3, 5]);
    const b = new List([2, 4, 6]);
    const ret = a.merge(b);
    eqArr(a, [1, 2, 3, 4, 5, 6]);
    assert(ret === a);

    // NB: unlike concat(), merge() copies values from `other` rather than
    // splicing/consuming its nodes - `other` is left fully intact.
    eqArr(b, [2, 4, 6]);

    // custom comparator (both lists sorted descending here)
    const c = new List([5, 3, 1]);
    const d = new List([6, 4, 2]);
    c.merge(d, (x, y) => x >= y);
    eqArr(c, [6, 5, 4, 3, 2, 1]);
    eqArr(d, [6, 4, 2]);
  },

  'concat()'() {
    // List arguments: spliced in destructively in O(1), consuming `this` and each List arg
    const a = new List([1, 2]);
    const b = new List([3, 4]);
    const result = a.concat(b);
    eqArr(result, [1, 2, 3, 4]);
    eqArr(a, []);
    eqArr(b, []);

    // Plain iterable arguments: appended in O(m), left unmodified
    const c = new List([1, 2]);
    const plain = [3, 4];
    const result2 = c.concat(plain);
    eqArr(result2, [1, 2, 3, 4]);
    eqArr(c, []); // `this` is still consumed
    eqArr(plain, [3, 4]); // but a non-List argument is untouched

    let threw = false;
    try {
      new List([1]).concat(42);
    } catch(e) {
      threw = e instanceof TypeError;
    }
    assert(threw, 'concat() with a non-List, non-iterable argument should throw');
  },

  'slice()'() {
    const l = new List([1, 2, 3, 4, 5]);
    const start = l.begin();
    start.next(); // index 1
    const end = l.begin();
    end.next();
    end.next();
    end.next();
    end.next(); // index 4

    const sl = l.slice(start, end);
    eqArr(sl, [2, 3, 4]);
    eqArr(l, [1, 2, 3, 4, 5]); // non-destructive
  },

  'splice()'() {
    const l = new List([1, 2, 3, 4, 5]);
    const start = l.begin();
    start.next();
    const end = l.begin();
    end.next();
    end.next();
    end.next();
    end.next();

    const removed = l.splice(start, end, 100, 200);
    eqArr(l, [1, 100, 200, 5]);
    eqArr(removed, [2, 3, 4]);
  },

  'fill()'() {
    const l = new List([1, 2, 3, 4, 5]);
    const start = l.begin();
    start.next();
    const end = l.begin();
    end.next();
    end.next();
    end.next();
    end.next();

    const ret = l.fill(0, start, end);
    eqArr(l, [1, 0, 0, 0, 5]);
    assert(ret === l);
  },

  'rotate()'() {
    const l = new List([1, 2, 3, 4, 5]);
    const node = l.begin();
    node.next();
    node.next(); // index 2 (value 3)

    const ret = l.rotate(node);
    eqArr(l, [3, 4, 5, 1, 2]);
    assert(ret === l);

    let threw = false;
    try {
      l.rotate(l.end());
    } catch(e) {
      threw = true;
    }
    assert(threw, 'rotate() on the sentinel should throw');
  },

  'reverse()'() {
    const l = new List([1, 2, 3]);
    const ret = l.reverse();
    eqArr(l, [3, 2, 1]);
    assert(ret === l);
  },

  'sort()'() {
    const l = new List([3, 1, 2]);
    const ret = l.sort();
    eqArr(l, [1, 2, 3]);
    assert(ret === l);

    const l2 = new List([3, 1, 2]);
    l2.sort((a, b) => b - a);
    eqArr(l2, [3, 2, 1]);
  },

  'List.from()/List.of()/List.isList()'() {
    eqArr(List.from([1, 2, 3]), [1, 2, 3]);
    eqArr(List.from('ab'), ['a', 'b']);
    eqArr(List.of(1, 2, 3), [1, 2, 3]);
    eqArr(List.of(), []);

    assert(List.isList(new List()));
    assert(!List.isList([1, 2]));
    assert(!List.isList({}));
  },

  'removed methods are absent'() {
    const removed = [
      'includes',
      'indexOf',
      'lastIndexOf',
      'findIndex',
      'findLastIndex',
      'find',
      'findLast',
      'every',
      'some',
      'filter',
      'forEach',
      'map',
      'reduce',
      'reduceRight',
      'toReversed',
      'at',
    ];

    for(const name of removed)
      assert(typeof List.prototype[name] === 'undefined', `List.prototype.${name} should not exist`);
  },

  'ListIterator'() {
    const l = new List([1, 2, 3]);

    const it1 = l.begin();
    const it2 = l.begin();
    assert(it1.equals(it2));
    it2.next();
    assert(!it1.equals(it2));

    const copy = it1.copy();
    assert(copy.equals(it1));
    copy.next();
    assert(!copy.equals(it1));

    eq(l.begin().type, List.NORMAL);
    eq(l.rbegin().type, List.REVERSE);

    // container aliases the same underlying list (not the same JS wrapper object)
    const container = l.begin().container;
    assert(container !== l);
    container.push(4);
    eqArr(l, [1, 2, 3, 4]);
  },

  'ListNode'() {
    const l = new List([1, 2, 3]);
    const node = new ListNode(l.begin());

    eq(node.value, 1);
    eq(node.valueOf(), 1);
    eq(node.next.value, 2);
    assert(node.linked);
    assert(!node.sentinel);
    assert(node.equals(l.begin()));
    eq(typeof node.address, 'string');

    node.value = 100;
    eqArr(l, [100, 2, 3]);

    const endNode = new ListNode(l.end());
    assert(endNode.sentinel);
    assert(List.isList(endNode.valueOf()));
  },
});
