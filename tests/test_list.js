import { List, ListNode } from 'list';
import { assert, eq, tests } from './tinytest.js';

await tests({
  'List() constructs from an iterable, exposes length'() {
    const l = new List([1, 2, 3]);
    eq(l.length, 3);
    eq([...l].join(','), '1,2,3');
  },

  'push() / pop() / unshift() / shift() act on the ends'() {
    const l = new List([2, 3]);
    l.push(4);
    l.unshift(1);
    eq([...l].join(','), '1,2,3,4');

    eq(l.pop(), 4);
    eq(l.shift(), 1);
    eq([...l].join(','), '2,3');
  },

  'includes() / find() / findLast() work like Array'() {
    const l = new List([1, 2, 3]);
    assert(l.includes(2));
    assert(!l.includes(9));
    eq(l.find(x => x > 1), 2);
    eq(l.findLast(x => x > 1), 3);
    eq(l.find(x => x > 10), undefined);
  },

  'indexOf() / lastIndexOf() / findIndex() / findLastIndex() return ListIterators, not numbers'() {
    /* Unlike Array, these return a positional ListIterator rather than a
     * numeric index - there is no O(1) index concept for a linked list. */
    const l = new List([1, 2, 3, 2]);

    const found = l.indexOf(2);
    assert(found.isAccessible(), 'indexOf() of a present value should be accessible');
    eq(found.next().value, 2);

    const notFound = l.indexOf(99);
    assert(!notFound.isAccessible(), 'indexOf() of a missing value should not be accessible');

    const last = l.lastIndexOf(2);
    assert(last.isAccessible());

    const fi = l.findIndex(x => x == 3);
    assert(fi.isAccessible());
    eq(fi.next().value, 3);
  },

  'concat() returns a new List, leaves the original unchanged'() {
    const l = new List([1, 2]);
    const c = l.concat([3, 4]);
    eq([...c].join(','), '1,2,3,4');
    eq([...l].join(','), '1,2');
  },

  'slice() / splice() take ListIterators as bounds, not integer indices'() {
    const l = new List([1, 2, 3, 4, 5]);

    const middle = l.slice(l.begin(), l.indexOf(4));
    eq([...middle].join(','), '1,2,3');
    eq([...l].join(','), '1,2,3,4,5', 'slice() does not mutate the source list');

    const l2 = new List([1, 2, 3, 4, 5]);
    const removed = l2.splice(l2.indexOf(2), l2.indexOf(4));
    eq([...removed].join(','), '2,3');
    eq([...l2].join(','), '1,4,5', 'splice() removes the spliced range in place');
  },

  'fill() overwrites a range in place'() {
    const l = new List([1, 2, 3]);
    l.fill(9, l.begin(), l.end());
    eq([...l].join(','), '9,9,9');
  },

  'rotate() re-anchors the list at a given node, in place'() {
    const l = new List([1, 2, 3, 4]);
    l.rotate(l.indexOf(3));
    eq([...l].join(','), '3,4,1,2');
  },

  'reverse() mutates in place, toReversed() returns a copy'() {
    const l = new List([1, 2, 3]);
    const copy = l.toReversed();
    eq([...copy].join(','), '3,2,1');
    eq([...l].join(','), '1,2,3', 'toReversed() must not mutate the source');

    l.reverse();
    eq([...l].join(','), '3,2,1');
  },

  'sort() requires an explicit comparator'() {
    const l = new List([3, 1, 2]);
    l.sort((a, b) => a - b);
    eq([...l].join(','), '1,2,3');

    let threw = false;
    try {
      new List([3, 1, 2]).sort();
    } catch(e) {
      threw = true;
    }
    assert(threw, 'sort() without a comparator is currently expected to throw');
  },

  'unique() removes consecutive duplicates in place, returns the removed count'() {
    const l = new List([1, 1, 2, 2, 3]);
    const removedCount = l.unique();
    eq(removedCount, 2);
    eq([...l].join(','), '1,2,3');
  },

  'merge() absorbs another list in place'() {
    const l = new List([1, 3]);
    l.merge(new List([2, 4]));
    eq([...l].join(','), '1,2,3,4');
  },

  'every() / some() / filter() / forEach() / map() / reduce() / reduceRight()'() {
    const l = new List([1, 2, 3]);
    assert(l.every(x => x > 0));
    assert(l.some(x => x > 2));
    assert(!l.some(x => x > 10));
    eq([...l.filter(x => x % 2 == 1)].join(','), '1,3');

    const seen = [];
    l.forEach(x => seen.push(x));
    eq(seen.join(','), '1,2,3');

    eq([...l.map(x => x * 10)].join(','), '10,20,30');
    eq(l.reduce((a, b) => a + b, 0), 6);
    eq(l.reduceRight((a, b) => a + '-' + b), '3-2-1');
  },

  'values() / keys() / entries() / Symbol.iterator'() {
    const l = new List(['a', 'b']);
    eq([...l.values()].join(','), 'a,b');
    eq([...l.keys()].join(','), '0,1');
    eq(JSON.stringify([...l.entries()]), JSON.stringify([[0, 'a'], [1, 'b']]));
  },

  'begin() / end() / rbegin() / rend() give boundary iterators'() {
    const l = new List([1, 2, 3]);
    assert(l.begin().isAccessible());
    assert(!l.end().isAccessible(), 'end() is a past-the-end sentinel');
    assert(l.rbegin().isAccessible());
    assert(!l.rend().isAccessible());

    eq(l.rbegin().next().value, 3, 'rbegin() starts from the last element');
  },

  'clear() / erase() / insert() / insertBefore() mutate structure'() {
    const l = new List([1, 2, 3]);
    const afterErase = l.erase(l.indexOf(2));
    eq([...l].join(','), '1,3');
    eq(afterErase.next().value, 3, 'erase() returns an iterator to the following element');

    /* insert()/insertBefore() take (position, value), despite the 1-arg
     * doc signature - see BUGS.md. */
    l.insert(l.begin(), 0);
    eq([...l].join(','), '0,1,3');

    l.insertBefore(l.end(), 99);
    eq([...l].join(','), '0,1,3,99');

    l.clear();
    eq(l.length, 0);
    eq([...l].join(','), '');
  },

  'List.from() / List.of() / List.isList()'() {
    eq([...List.from([9, 8, 7])].join(','), '9,8,7');
    eq([...List.of(1, 2, 3)].join(','), '1,2,3');
    assert(List.isList(new List()));
    assert(!List.isList([1, 2, 3]));
  },

  'ListIterator: next() / copy() / equals() / isAccessible() / container / type'() {
    const l = new List([1, 2, 3]);
    const it = l.begin();
    const copy = it.copy();
    assert(it.equals(copy), 'a fresh copy() should compare equal to its source');

    const step = it.next();
    eq(step.value, 1);
    eq(step.done, false);
    assert(!it.equals(copy), 'advancing one iterator should no longer equal the untouched copy');

    /* container getter returns a List wrapping the same underlying nodes,
     * not the identical original List instance (see BUGS.md). */
    eq([...it.container].join(','), [...l].join(','));
  },

  'ListNode: value getter/setter, linked'() {
    const node = new ListNode(42);
    eq(node.value, 42);
    node.value = 43;
    eq(node.value, 43);
    assert(!node.linked, 'a freshly constructed node is not part of any list');
  },
});

/* ------------------------------------------------------------------ */
/* Original demo: push a few generated letters, walk an iterator while */
/* inserting new elements at the current position.                    */
/* ------------------------------------------------------------------ */

let l = new List([6, 5, 4, 3, 2, 1]);

let letter = 'a';

const next = () => {
  let r = letter;
  letter = String.fromCodePoint(letter.codePointAt(0) + 1);
  return r;
};

const append = () => {
  l.push(next());
  console.log('l', l);
};

const insert = () => {
  l.insert(it, next());
  console.log('l', l);
};

append();
append();
append();
let it = l[Symbol.iterator](1);

const skip = () => {
  let { done, value } = it.next();
  console.log('skip', value, done);
  return done;
};

skip();
insert();
while(!skip()) {}

insert();
console.log('it', it);
