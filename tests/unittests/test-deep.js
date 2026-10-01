import * as deep from 'deep';
import { assert, eq, tests } from '../../lib/tinytest.js';

function fixture() {
  return { a: 1, b: { c: 2, d: [3, 4] }, e: 'x' };
}

tests({
  'find() returns the first [value, path] matching predicate'() {
    const root = fixture();
    const result = deep.find(root, v => v === 4);
    assert(Array.isArray(result));
    eq(result[0], 4);
    eq(JSON.stringify(result[1]), JSON.stringify(['b', 'd', 1]));
  },
  'find() returns undefined when nothing matches'() {
    const root = fixture();
    eq(deep.find(root, v => v === 'nope'), undefined);
  },
  'select() returns all [value, path] pairs matching predicate'() {
    const root = fixture();
    const results = deep.select(root, v => typeof v === 'number');
    const values = results.map(([v]) => v).sort();
    eq(values.join(','), '1,2,3,4');
  },
  'get() reads the value at a path'() {
    const root = fixture();
    eq(deep.get(root, ['b', 'c']), 2);
    eq(deep.get(root, ['b', 'd', 1]), 4);
  },
  'set() writes a value at a path'() {
    const root = fixture();
    deep.set(root, ['b', 'd', 1], 99);
    eq(root.b.d[1], 99);
  },
  'set() creates intermediate nodes'() {
    const root = {};
    deep.set(root, ['x', 'y'], 42);
    eq(root.x.y, 42);
  },
  'unset() deletes the value at a path'() {
    const root = fixture();
    deep.unset(root, ['b', 'c']);
    assert(!('c' in root.b));
  },
  'flatten() produces a flat map of path to value'() {
    const root = { a: 1, b: { c: 2 } };
    const flat = deep.flatten(root);
    eq(flat['a'], 1);
    eq(flat['b.c'], 2);
  },
  'pathOf() returns the path at which a value occurs'() {
    const root = fixture();
    const path = deep.pathOf(root, root.b.d);
    eq(JSON.stringify(path), JSON.stringify(['b', 'd']));
  },
  'equals() true for deeply equal structures'() {
    assert(deep.equals({ a: [1, 2], b: 3 }, { a: [1, 2], b: 3 }));
  },
  'equals() false for differing structures'() {
    assert(!deep.equals({ a: 1 }, { a: 2 }));
    assert(!deep.equals({ a: 1 }, { a: 1, b: 2 }));
  },
  'forEach() invokes fn(value, path, root) for every node'() {
    const root = { a: 1, b: { c: 2 } };
    const seen = [];
    deep.forEach(root, (value, path) => seen.push(`${path.join('.')}=${value}`));
    assert(seen.includes('a=1'));
    assert(seen.includes('b.c=2'));
  },
  'clone() makes a deep copy'() {
    const root = fixture();
    const copy = deep.clone(root);
    assert(copy !== root);
    assert(copy.b !== root.b);
    eq(copy.b.c, root.b.c);
    copy.b.c = 999;
    eq(root.b.c, 2);
  },
  // DeepIterator/deep.iterate() only reliably recurses into array roots -
  // see deepiterator-never-descends-plain-object-root in BUGS.
  'iterate() returns a DeepIterator over all nodes'() {
    const root = [1, 2];
    const it = deep.iterate(root);
    const values = [];
    for(const [value] of it) values.push(value);
    assert(values.includes(1));
    assert(values.includes(2));
  },
  'DeepIterator.next() yields [value, path] and reports done'() {
    const it = new deep.DeepIterator([1]);
    const step = it.next();
    eq(step.done, false);
    assert(Array.isArray(step.value));
    let last;
    do {
      last = it.next();
    } while(!last.done);
    eq(last.done, true);
  },
  'DeepIterator.path getter reflects current position'() {
    const it = new deep.DeepIterator([[1]]);
    it.next();
    assert(Array.isArray(it.path));
  },
  'DeepIterator[Symbol.iterator]() returns itself'() {
    const it = new deep.DeepIterator([1]);
    eq(it[Symbol.iterator](), it);
  },
  'DeepIterator.return() ends iteration'() {
    const it = new deep.DeepIterator([1, 2]);
    it.next();
    const result = it.return();
    eq(result.done, true);
    eq(it.next().done, true);
  },
  'DeepIterator.skip() prevents values from the skipped subtree being yielded'() {
    const it = new deep.DeepIterator([[1, 2], 3]);
    const values = [];
    let step = it.next();

    values.push(step.value[0]);
    it.skip();

    while(!(step = it.next()).done) values.push(step.value[0]);

    assert(!values.includes(1));
    assert(!values.includes(2));
  },
});
