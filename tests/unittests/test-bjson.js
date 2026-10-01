import * as bjson from 'bjson';
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'write()/read() round-trip a number'() {
    const buf = bjson.write(42);
    assert(buf instanceof ArrayBuffer);
    eq(bjson.read(buf), 42);
  },
  'write()/read() round-trip a string'() {
    const buf = bjson.write('hello world');
    eq(bjson.read(buf), 'hello world');
  },
  'write()/read() round-trip an array'() {
    const buf = bjson.write([1, 2, 3]);
    eq(JSON.stringify(bjson.read(buf)), JSON.stringify([1, 2, 3]));
  },
  'write()/read() round-trip a plain object'() {
    const buf = bjson.write({ a: 1, b: 'two', c: [3, 4] });
    eq(JSON.stringify(bjson.read(buf)), JSON.stringify({ a: 1, b: 'two', c: [3, 4] }));
  },
  'write()/read() round-trip a Uint8Array/ArrayBuffer'() {
    const src = new Uint8Array([10, 20, 30, 40]);
    const buf = bjson.write(src.buffer);
    const out = bjson.read(buf);
    assert(out instanceof ArrayBuffer);
    eq(JSON.stringify([...new Uint8Array(out)]), JSON.stringify([10, 20, 30, 40]));
  },
  'read() respects offset/length to slice out an embedded payload'() {
    const payload = bjson.write({ x: 99 });
    const padded = new Uint8Array(10 + payload.byteLength + 10);
    padded.set(new Uint8Array(payload), 10);
    const value = bjson.read(padded.buffer, 10, payload.byteLength);
    eq(value.x, 99);
  },
  'write() with JS_WRITE_OBJ_BYTECODE rejects an ordinary closure'() {
    /* JS_WRITE_OBJ_BYTECODE serializes raw function-bytecode values (as
     * produced internally by a compile-only JS_Eval), not live JS closures -
     * there's no JS-callable way to produce that from this module's surface,
     * so an ordinary function is expected to be rejected here. */
    let threw = false;
    try {
      bjson.write(function add(a, b) { return a + b; }, bjson.JS_WRITE_OBJ_BYTECODE);
    } catch(e) {
      threw = true;
    }
    assert(threw);
  },
  'write()/read() with the boolean-shorthand reference flag round-trips a shared sub-object'() {
    const shared = { n: 7 };
    const root = { a: shared, b: shared };
    const buf = bjson.write(root, true);
    const out = bjson.read(buf, 0, buf.byteLength, true);
    eq(out.a.n, 7);
    eq(out.b.n, 7);
    assert(out.a === out.b, 'shared sub-object identity should be preserved by JS_WRITE_OBJ_REFERENCE/JS_READ_OBJ_REFERENCE');
  },
  'write()/read() with the JS_*_OBJ_REFERENCE integer flags round-trips a shared sub-object'() {
    const shared = { n: 8 };
    const root = [shared, shared];
    const buf = bjson.write(root, bjson.JS_WRITE_OBJ_REFERENCE);
    const out = bjson.read(buf, 0, buf.byteLength, bjson.JS_READ_OBJ_REFERENCE);
    assert(out[0] === out[1], 'shared sub-object identity should be preserved');
  },
  'constants are defined and numeric'() {
    for(const name of ['JS_READ_OBJ_BYTECODE', 'JS_READ_OBJ_ROM_DATA', 'JS_READ_OBJ_SAB', 'JS_READ_OBJ_REFERENCE', 'JS_WRITE_OBJ_BYTECODE', 'JS_WRITE_OBJ_BSWAP', 'JS_WRITE_OBJ_SAB', 'JS_WRITE_OBJ_REFERENCE']) {
      assert(typeof bjson[name] === 'number', `${name} should be a numeric constant`);
    }
  },
});
