import inspect from 'inspect';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* colors are on by default (ANSI escapes embedded in the output) - use
 * colors:false throughout so assertions can compare plain text. */
const plain = (v, opts) => inspect(v, { colors: false, ...opts });

tests({
  'inspect(primitive)'() {
    eq(plain(42), '42');
    eq(plain('hi'), "'hi'");
    eq(plain(null), 'null');
    eq(plain(undefined), 'undefined');
    eq(plain(true), 'true');
  },
  'inspect(array)'() {
    eq(plain([1, 2, 3]), '[ 1, 2, 3 ]');
  },
  'inspect(object)'() {
    eq(plain({ a: 1, b: 2 }), '{ a: 1, b: 2 }');
  },
  'inspect(nested object)'() {
    const out = plain({ a: 1, b: { c: 2 } });
    assert(out.includes('a: 1'));
    assert(out.includes('c: 2'));
  },
  'inspect(circular object) does not crash, shows a loop marker'() {
    const obj = {};
    obj.self = obj;
    const out = plain(obj);
    assert(out.includes('loop'));
  },
  'inspect(Map) and inspect(Set)'() {
    const m = plain(new Map([[1, 'a'], [2, 'b']]));
    assert(m.startsWith('Map'));
    assert(m.includes("1 => 'a'"));

    const s = plain(new Set([1, 2, 3]));
    assert(s.startsWith('Set'));
    assert(s.includes('1'));
  },
  'inspect is also the module default export'() {
    eq(typeof inspect, 'function');
  },
  'compact: positive count-based - fits on one line under the threshold'() {
    const out = plain({ a: 1, b: 2, c: 3, d: 4 }, { compact: 5 });
    eq(out, '{ a: 1, b: 2, c: 3, d: 4 }');
  },
  'compact: positive count-based - expands over the threshold'() {
    const out = plain({ a: 1, b: 2, c: 3, d: 4, e: 5, f: 6 }, { compact: 5 });
    assert(out.includes('\n'));
    assert(out.includes('a: 1'));
    assert(out.includes('f: 6'));
  },
  'compact: negative depth-based - leaf below the threshold stays expanded'() {
    const obj = {
      name: 'lib.a:main.o',
      sections: [
        { name: '.text', size: 256, offset: 64 },
        { name: '.data', size: 0, offset: 320 },
      ],
    };
    const out = plain(obj, { compact: 1 });
    /* sections has 3 top-level entries (2 array items is fewer, but each
     * nested object has 3 keys > 1), so with compact:1 nothing here
     * collapses to one line - every nested object stays on its own lines. */
    assert(out.includes('name: \'.text\',\n'));
  },
  'compact: negative depth-based - leaf compacts to one line'() {
    const obj = {
      name: 'lib.a:main.o',
      sections: [
        { name: '.text', size: 256, offset: 64 },
        { name: '.data', size: 0, offset: 320 },
      ],
    };
    const out = plain(obj, { compact: -1 });
    assert(out.includes("{ name: '.text', size: 256, offset: 64 }"));
    assert(out.includes("{ name: '.data', size: 0, offset: 320 }"));
  },
  'compact: true always compacts'() {
    const out = plain({ a: 1, b: 2, c: 3, d: 4, e: 5, f: 6 }, { compact: true });
    assert(!out.includes('\n'));
  },
  'compact: false never compacts'() {
    const out = plain({ a: 1, b: 2 }, { compact: false });
    assert(out.includes('\n'));
  },
});
