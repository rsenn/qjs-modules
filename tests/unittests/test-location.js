import { Location } from 'location';
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'constructor with no args'() {
    const loc = new Location();
    assert(loc instanceof Location);
  },
  'constructor with line, column, charOffset, file'() {
    const loc = new Location(3, 5, 42, 'foo.js');
    eq(loc.line, 3);
    eq(loc.column, 5);
    eq(loc.charOffset, 42);
    eq(loc.file, 'foo.js');
  },
  'properties are read-only'() {
    'use strict';
    const loc = new Location(1, 1, 0, 'a.js');
    for(const [prop, value] of [
      ['line', 10],
      ['column', 20],
      ['charOffset', 30],
      ['byteOffset', 40],
    ]) {
      let threw = false;
      try {
        loc[prop] = value;
      } catch(e) {
        threw = e /*instanceof TypeError*/;
      }
      assert(threw, `property .${prop} read-only`);
    }
    eq(loc.line, 1);
    eq(loc.column, 1);
    eq(loc.charOffset, 0);
    eq(loc.file, 'a.js');
  },
  'clone() returns an equal but distinct copy'() {
    const loc = new Location(2, 4, 6, 'c.js');
    const copy = loc.clone();
    assert(copy !== loc);
    assert(loc.equal(copy));
    eq(copy.line, 2);
  },
  'equal() compares locations'() {
    const a = new Location(1, 2, 3, 'x.js');
    const b = new Location(1, 2, 3, 'x.js');
    const c = new Location(1, 2, 3, 'y.js');
    assert(a.equal(b));
    assert(!a.equal(c));
  },
  'toString() renders file:line:column'() {
    const loc = new Location(7, 3, 0, 'foo.js');
    eq(loc.toString(), 'foo.js:7:3');
  },
  'Symbol.toPrimitive coerces to string form'() {
    const loc = new Location(7, 3, 0, 'foo.js');
    eq(`${loc}`, loc.toString());
    eq('' + loc, loc.toString());
  },
  'Location.count() scans a string for line/column info'() {
    const loc = Location.count('abc\ndef\nghi');
    assert(loc instanceof Location);
    eq(loc.line, 3);
  },
  'Location.count() on single-line input stays on line 1'() {
    const loc = Location.count('abcdef');
    eq(loc.line, 1);
    eq(loc.column, 7);
  },
});
