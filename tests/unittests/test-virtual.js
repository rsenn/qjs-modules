import { VirtualProperties } from 'virtual';
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'constructor over a plain object'() {
    const v = new VirtualProperties({ a: 1, b: 2 });
    assert(v instanceof VirtualProperties);
    assert(v.has('a'));
    eq(v.get('a'), 1);
  },
  'constructor over an array'() {
    /* array-backed VirtualProperties treats the array as a list of
     * [key, value] entry pairs (like Map's iteration protocol), not as
     * index -> value like a plain array. */
    const v = new VirtualProperties([['a', 10], ['b', 20]]);
    assert(v.has('a'));
    eq(v.get('a'), 10);
  },
  'constructor over a Map'() {
    const m = new Map([['k', 'v']]);
    const v = new VirtualProperties(m);
    assert(v.has('k'));
    eq(v.get('k'), 'v');
  },
  'has()/get()/set()/delete() over a plain object'() {
    const target = { a: 1 };
    const v = new VirtualProperties(target);

    assert(v.has('a'));
    assert(!v.has('b'));

    v.set('b', 2);
    assert(v.has('b'));
    eq(v.get('b'), 2);
    eq(target.b, 2);

    v.delete('a');
    assert(!v.has('a'));
    eq(target.a, undefined);
  },
  'has()/get()/set()/delete() over an array'() {
    const target = [['a', 1], ['b', 2]];
    const v = new VirtualProperties(target);

    v.set('a', 99);
    eq(v.get('a'), 99);

    assert(!v.has('c'));
    v.set('c', 3);
    assert(v.has('c'));
    eq(v.get('c'), 3);

    v.delete('b');
    assert(!v.has('b'));
  },
  'has()/get()/set()/delete() over a Map'() {
    const target = new Map();
    const v = new VirtualProperties(target);

    v.set('k', 'v');
    assert(target.has('k'));
    eq(target.get('k'), 'v');
    eq(v.get('k'), 'v');

    v.delete('k');
    assert(!target.has('k'));
  },
  'keys() lists the backing store keys'() {
    const v = new VirtualProperties({ a: 1, b: 2, c: 3 });
    const keys = [...v.keys()];
    eq(keys.sort().join(','), 'a,b,c');
  },
  'static array() creates an array-backed VirtualProperties'() {
    const target = [['x', 1]];
    const v = VirtualProperties.array(target);
    assert(v instanceof VirtualProperties);
    eq(v.get('x'), 1);
    v.set('y', 2);
    eq(JSON.stringify(target), JSON.stringify([['x', 1], ['y', 2]]));
  },
  'static map() creates a Map-backed VirtualProperties'() {
    const target = new Map();
    const v = VirtualProperties.map(target);
    v.set('k', 1);
    eq(target.get('k'), 1);
  },
  'static object() creates a plain-object-backed VirtualProperties'() {
    const target = {};
    const v = VirtualProperties.object(target);
    v.set('k', 1);
    eq(target.k, 1);
  },
  'static from() auto-detects an array target'() {
    const target = [['k', 1]];
    const v = VirtualProperties.from(target);
    eq(v.get('k'), 1);
    v.set('j', 5);
    eq(JSON.stringify(target), JSON.stringify([['k', 1], ['j', 5]]));
  },
  'static from() auto-detects a Map target'() {
    const target = new Map();
    const v = VirtualProperties.from(target);
    v.set('k', 'v');
    eq(target.get('k'), 'v');
  },
  'static from() auto-detects a plain-object target'() {
    const target = {};
    const v = VirtualProperties.from(target);
    v.set('k', 'v');
    eq(target.k, 'v');
  },
});
