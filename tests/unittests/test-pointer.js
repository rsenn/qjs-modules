import { DereferenceError, Pointer } from 'pointer';
import { assert, eq, tests } from '../../lib/tinytest.js';

const data = { a: { b: [10, 20, 30] } };

tests({
  'constructor from array of keys'() {
    const p = new Pointer(['a', 'b', 0]);
    eq(p.toArray().join(','), 'a,b,0');
  },
  'constructor from a pointer string'() {
    const p = new Pointer('a.b[0]');
    eq(p.toArray().join(','), 'a,b,0');
  },
  'constructor from another Pointer'() {
    const p1 = new Pointer(['a', 'b']);
    const p2 = new Pointer(p1);
    assert(p1.equal(p2));
  },
  'length getter'() {
    eq(new Pointer(['a', 'b', 0]).length, 3);
    eq(new Pointer([]).length, 0);
  },
  'path getter returns the atoms as an array'() {
    eq(new Pointer(['a', 'b', 0]).path.join(','), 'a,b,0');
  },
  'atoms getter returns something indexable of the same length'() {
    const p = new Pointer(['a', 'b', 0]);
    eq(p.atoms.length, 3);
  },
  'deref() resolves a value at the path'() {
    const p = new Pointer(['a', 'b', 1]);
    eq(p.deref(data), 20);
  },
  'deref() throws DereferenceError with pointer/root/pos on a missing path'() {
    const p = new Pointer(['a', 'z', 0]);
    let caught;
    try {
      p.deref(data);
    } catch(e) {
      caught = e;
    }
    assert(caught instanceof DereferenceError);
    assert(caught instanceof Error);
    assert(typeof caught.message === 'string');
    eq(caught.root, data);
  },
  'toString() and Pointer.from() round-trip'() {
    const p = new Pointer(['a', 'b', 0]);
    const s = p.toString();
    const p2 = Pointer.from(s);
    assert(p.equal(p2));
  },
  'toRFC6901() renders JSON-Pointer syntax'() {
    eq(new Pointer(['a', 'b', 0]).toRFC6901(), '/a/b/0');
  },
  'toArray() returns the atoms as a plain array'() {
    const arr = new Pointer(['a', 'b', 0]).toArray();
    assert(Array.isArray(arr));
    eq(arr.join(','), 'a,b,0');
  },
  'shift() removes and returns the first atom'() {
    const p = new Pointer(['a', 'b', 0]);
    const first = p.shift();
    eq(first, 'a');
    eq(p.toArray().join(','), 'b,0');
  },
  'unshift() prepends an atom'() {
    const p = new Pointer(['b', 0]);
    p.unshift('a');
    eq(p.toArray().join(','), 'a,b,0');
  },
  'pop() removes and returns the last atom'() {
    const p = new Pointer(['a', 'b', 0]);
    const last = p.pop();
    eq(last, 0);
    eq(p.toArray().join(','), 'a,b');
  },
  'push() appends an atom'() {
    const p = new Pointer(['a']);
    p.push('b');
    p.push(0);
    eq(p.toArray().join(','), 'a,b,0');
  },
  'values() iterates the atoms'() {
    const p = new Pointer(['a', 'b', 0]);
    eq([...p.values()].join(','), 'a,b,0');
  },
  '[Symbol.iterator]() iterates the atoms directly'() {
    const p = new Pointer(['a', 'b', 0]);
    eq([...p].join(','), 'a,b,0');
  },
  'hier() returns the hierarchy of prefixes'() {
    const h = new Pointer(['a', 'b', 0]).hier();
    eq(h.map(x => x.toString()).join('|'), 'a|a.b|a.b[0]');
  },
  'at() returns the atom at an index, including negative indices'() {
    const p = new Pointer(['a', 'b', 0]);
    eq(p.at(0), 'a');
    eq(p.at(1), 'b');
    eq(p.at(-1), 0);
  },
  'concat() appends another pointer'() {
    const p = new Pointer(['a']).concat(new Pointer(['b', 0]));
    eq(p.toArray().join(','), 'a,b,0');
  },
  'slice() returns a sub-range pointer'() {
    const p = new Pointer(['a', 'b', 0]);
    eq(p.slice(1).toArray().join(','), 'b,0');
    eq(p.slice(0, 2).toArray().join(','), 'a,b');
  },
  'splice() removes a range in place and returns the removed atoms'() {
    const p = new Pointer(['a', 'b', 0]);
    const removed = p.splice(1, 2);
    eq(removed.toArray().join(','), 'b');
    eq(p.toArray().join(','), 'a,0');
  },
  'up() keeps the first n atoms'() {
    const p = new Pointer(['a', 'b', 0]);
    eq(p.up(1).toArray().join(','), 'a');
    eq(p.up(2).toArray().join(','), 'a,b');
  },
  'up() with no args drops the last atom'() {
    const p = new Pointer(['a', 'b', 0]);
    eq(p.up().toArray().join(','), 'a,b');
  },
  'truncate() is an alias for up()'() {
    const p = new Pointer(['a', 'b', 0]);
    eq(p.truncate(1).toArray().join(','), p.up(1).toArray().join(','));
  },
  'down() extends the pointer downward'() {
    const p = new Pointer(['a']).down('b', 0);
    eq(p.toArray().join(','), 'a,b,0');
  },
  'equal() compares pointers by value'() {
    assert(new Pointer(['a', 'b']).equal(new Pointer(['a', 'b'])));
    assert(!new Pointer(['a', 'b']).equal(new Pointer(['a', 'c'])));
  },
  'compare() is negative for equal pointers, encoding the match length'() {
    eq(new Pointer(['a']).compare(new Pointer(['a'])), -2);
    eq(new Pointer(['a', 'b']).compare(new Pointer(['a', 'b'])), -3);
  },
  'common() returns the length of the longest common prefix'() {
    eq(new Pointer(['a', 'b', 'c']).common(new Pointer(['a', 'b', 'd'])), 2);
    eq(new Pointer(['a']).common(new Pointer(['x'])), 0);
  },
  'relativeTo() returns the path relative to a prefix'() {
    const p = new Pointer(['a', 'b', 'c']).relativeTo(new Pointer(['a']));
    eq(p.toArray().join(','), 'b,c');
  },
  'startsWith() and endsWith() test prefix/suffix'() {
    const p = new Pointer(['a', 'b', 'c']);
    assert(p.startsWith(new Pointer(['a', 'b'])));
    assert(!p.startsWith(new Pointer(['x'])));
    assert(p.endsWith(new Pointer(['b', 'c'])));
    assert(!p.endsWith(new Pointer(['x'])));
  },

  'Pointer.from() builds a pointer from a string or array'() {
    assert(Pointer.from('a.b[0]').equal(Pointer.from(['a', 'b', 0])));
  },
  'Pointer.fromAtoms() round-trips through atoms'() {
    const p = new Pointer(['a', 'b', 0]);
    const p2 = Pointer.fromAtoms(p.atoms);
    assert(p.equal(p2));
  },
  'Pointer.of() builds a pointer from argument keys'() {
    eq(Pointer.of('a', 'b', 0).toArray().join(','), 'a,b,0');
  },
  'Pointer.isPointer() identifies Pointer instances'() {
    assert(Pointer.isPointer(new Pointer(['a'])));
    assert(!Pointer.isPointer({}));
    assert(!Pointer.isPointer('a.b'));
  },
});
