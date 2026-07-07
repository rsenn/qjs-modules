import { Console } from 'console';
import { read, write, JsonParser } from 'json';
import { exit } from 'std';

let passed = 0,
  failed = 0;

const assert = (name, cond, detail) => {
  if(cond) {
    passed++;
    console.log(`\x1b[32mPASS\x1b[0m ${name}`);
  } else {
    failed++;
    console.log(`\x1b[31mFAIL\x1b[0m ${name}${detail !== undefined ? ': ' + detail : ''}`);
  }
};

const assertEq = (name, actual, expected) => assert(name, JSON.stringify(actual) === JSON.stringify(expected), `expected=${JSON.stringify(expected)} actual=${JSON.stringify(actual)}`);

const assertThrows = (name, fn) => {
  try {
    fn();
    assert(name, false, 'did not throw');
  } catch(e) {
    assert(name, true);
  }
};

async function main() {
  globalThis.console = new Console({ inspectOptions: { compact: true, customInspect: true } });

  /* ---------- read: primitives ---------- */
  assertEq('read null', read('null'), null);
  assertEq('read true', read('true'), true);
  assertEq('read false', read('false'), false);
  assertEq('read int', read('42'), 42);
  assertEq('read negative', read('-17'), -17);
  assertEq('read float', read('3.14'), 3.14);
  assertEq('read exponent', read('1e3'), 1000);
  assert('read neg exponent', Math.abs(read('1.5e-2') - 0.015) < 1e-12);
  assertEq('read zero', read('0'), 0);
  assertEq('read string', read('"hello"'), 'hello');
  assertEq('read empty string', read('""'), '');

  /* ---------- read: empty containers ---------- */
  assertEq('read empty array', read('[]'), []);
  assertEq('read empty object', read('{}'), {});

  /* ---------- read: simple containers ---------- */
  assertEq('read array of ints', read('[1,2,3]'), [1, 2, 3]);
  assertEq('read object one key', read('{"a":1}'), { a: 1 });
  assertEq('read array of strings', read('["a","b","c"]'), ['a', 'b', 'c']);
  assertEq('read array of bools', read('[true,false,null]'), [true, false, null]);
  assertEq('read object many keys', read('{"a":1,"b":2,"c":3}'), { a: 1, b: 2, c: 3 });

  /* ---------- read: nested ---------- */
  assertEq('read nested array', read('[[1,2],[3,4]]'), [[1, 2], [3, 4]]);
  assertEq('read nested object', read('{"a":{"b":1}}'), { a: { b: 1 } });
  assertEq('read mixed nested', read('{"a":1,"b":[2,3],"c":{"d":4}}'), { a: 1, b: [2, 3], c: { d: 4 } });

  /* ---------- read: whitespace handling ---------- */
  assertEq('read with whitespace', read('  [ 1 , 2 , 3 ]  '), [1, 2, 3]);
  assertEq('read with newlines', read('{\n  "a": 1,\n  "b": 2\n}'), { a: 1, b: 2 });

  /* ---------- read: deep nesting (iterative — no stack overflow) ---------- */
  {
    const depth = 1000;
    let s = '['.repeat(depth) + '42' + ']'.repeat(depth);
    let r = read(s);
    let cur = r;
    let actualDepth = 0;

    while(Array.isArray(cur)) {
      actualDepth++;
      cur = cur[0];
    }

    assert(`read deep array nesting (${depth} levels)`, actualDepth === depth && cur === 42);
  }

  {
    const depth = 1000;
    let s = '{"a":'.repeat(depth) + '42' + '}'.repeat(depth);
    let r = read(s);
    let cur = r;
    let actualDepth = 0;

    while(typeof cur === 'object' && cur !== null && 'a' in cur) {
      actualDepth++;
      cur = cur.a;
    }

    assert(`read deep object nesting (${depth} levels)`, actualDepth === depth && cur === 42);
  }

  /* ---------- read: wide containers ---------- */
  {
    const count = 5000;
    let arr = new Array(count).fill(0).map((_, i) => i);
    let s = '[' + arr.join(',') + ']';
    let r = read(s);

    assert(`read wide array (${count} elements)`, r.length === count && r[count - 1] === count - 1);
  }

  /* ---------- read: errors ---------- */
  assertThrows('read malformed: stray ]', () => read(']'));
  assertThrows('read malformed: stray }', () => read('}'));
  assertThrows('read malformed: unclosed array', () => read('[1,2'));
  assertThrows('read malformed: unclosed string', () => read('"abc'));
  assertThrows('read malformed: unknown token', () => read('xyz'));

  /* ---------- write: primitives ---------- */
  assertEq('write null', read(write(null)), null);
  assertEq('write true', read(write(true)), true);
  assertEq('write false', read(write(false)), false);
  assertEq('write int', read(write(42)), 42);
  assertEq('write float', read(write(3.14)), 3.14);
  assertEq('write negative', read(write(-100)), -100);
  assertEq('write zero', read(write(0)), 0);
  assertEq('write string', read(write('hello')), 'hello');
  assertEq('write empty string', read(write('')), '');

  /* ---------- write: containers ---------- */
  assertEq('write empty array', write([]), '[]');
  assertEq('write empty object', write({}), '{}');
  assertEq('write array of ints', write([1, 2, 3]), '[1,2,3]');
  assertEq('write object one key', write({ a: 1 }), '{"a":1}');

  /* ---------- write: string escapes ---------- */
  assertEq('write string with quote', write('a"b'), '"a\\"b"');
  assertEq('write string with backslash', write('a\\b'), '"a\\\\b"');
  assertEq('write string with newline', write('a\nb'), '"a\\nb"');
  assertEq('write string with tab', write('a\tb'), '"a\\tb"');
  assertEq('write string with CR', write('a\rb'), '"a\\rb"');
  assertEq('write string with backspace', write('a\bb'), '"a\\bb"');
  assertEq('write string with formfeed', write('a\fb'), '"a\\fb"');
  assertEq('write string with control', write('\x01\x02\x1f'), '"\\u0001\\u0002\\u001f"');

  /* ---------- write: special numbers ---------- */
  assertEq('write NaN -> null', write(NaN), 'null');
  assertEq('write Infinity -> null', write(Infinity), 'null');
  assertEq('write -Infinity -> null', write(-Infinity), 'null');

  /* ---------- write: non-JSON values ---------- */
  assertEq('write undefined -> null', write(undefined), 'null');
  assertEq('write function -> null', write(() => 1), 'null');
  assertEq('write symbol -> null', write(Symbol('s')), 'null');
  assertEq('write array with undef -> null', write([1, undefined, 2]), '[1,null,2]');
  assertEq('write array with NaN -> null', write([1, NaN, 2]), '[1,null,2]');

  /* ---------- write: nested ---------- */
  assertEq('write nested array', write([[1, 2], [3, 4]]), '[[1,2],[3,4]]');
  assertEq('write nested object', write({ a: { b: 1 } }), '{"a":{"b":1}}');
  assertEq('write mixed nested', write({ a: 1, b: [2, 3], c: { d: 4 } }), '{"a":1,"b":[2,3],"c":{"d":4}}');

  /* ---------- write: deep nesting (iterative — no stack overflow) ---------- */
  {
    const depth = 1000;
    let v = 42;

    for(let i = 0; i < depth; i++) v = [v];

    let s = write(v);
    let r = read(s);
    let cur = r;
    let actualDepth = 0;

    while(Array.isArray(cur)) {
      actualDepth++;
      cur = cur[0];
    }

    assert(`write deep array nesting (${depth} levels)`, actualDepth === depth && cur === 42);
  }

  {
    const depth = 1000;
    let v = 42;

    for(let i = 0; i < depth; i++) v = { a: v };

    let s = write(v);
    let r = read(s);
    let cur = r;
    let actualDepth = 0;

    while(typeof cur === 'object' && cur !== null && 'a' in cur) {
      actualDepth++;
      cur = cur.a;
    }

    assert(`write deep object nesting (${depth} levels)`, actualDepth === depth && cur === 42);
  }

  /* ---------- write: wide containers ---------- */
  {
    const count = 5000;
    let arr = new Array(count).fill(0).map((_, i) => i);
    let s = write(arr);
    let r = read(s);

    assert(`write wide array (${count} elements)`, r.length === count && r[count - 1] === count - 1);
  }

  /* ---------- write: circular reference safety ---------- */
  {
    let a = { x: 1 };
    a.self = a;

    let s = write(a);
    let r = read(s);

    assert('write circular object (self) -> truncated, parses back', typeof r === 'object' && r.x === 1);
  }

  {
    let a = [1, 2];
    a.push(a);

    let s = write(a);
    let r = read(s);

    assert('write circular array (self) -> truncated, parses back', Array.isArray(r) && r[0] === 1 && r[1] === 2);
  }

  {
    let a = { name: 'a' },
      b = { name: 'b', a: a };
    a.b = b;

    let s = write(a);
    let r = read(s);

    assert('write mutually circular -> parses back', typeof r === 'object' && r.name === 'a');
  }

  /* ---------- round-trip parity (no string escapes — see escape-decoding note below) ---------- */
  {
    const cases = [
      null,
      true,
      false,
      0,
      42,
      -17,
      3.14,
      '',
      'hello',
      [],
      {},
      [1, 2, 3],
      { a: 1 },
      [1, 'two', true, null, [3]],
      { name: 'Alice', age: 30, hobbies: ['reading', 'coding'], address: { city: 'NY', zip: 10001 } },
    ];

    let ok = 0;

    for(let v of cases) {
      let s = write(v);
      let r = read(s);
      let lhs = JSON.stringify(v),
        rhs = JSON.stringify(r);

      if(lhs === rhs) ok++;
      else console.log(`  round-trip diff: orig=${lhs} wrote=${s} read=${rhs}`);
    }

    assert(`round-trip ${ok}/${cases.length} cases`, ok === cases.length);
  }

  /* ---------- write: circular value whose toString would overflow ---------- */
  /*   write_json_primitive's fallback calls JS_ToCString on the circular
   *   value; for a deeply nested element this triggers Array.prototype.join
   *   recursion that hits QuickJS's "stack overflow" InternalError. The
   *   writer must catch that and clear the exception. */
  {
    let inner = 42;
    for(let i = 0; i < 800; i++) inner = [inner];

    let root = [inner, null];
    root[1] = root;

    let s = write(root);

    assert('write circular containing deep array does not crash', typeof s === 'string' && s.length > 0);
  }

  /* ---------- known limitation: SJ reader does NOT decode JSON string escapes ---------- */
  /*   The underlying sj.h library returns the raw byte range between the surrounding
   *   quotes. So \n in the JSON source stays as literal backslash-n in the JS string.
   *   The writer DOES escape correctly. These tests pin down current behavior. */
  assertEq('reader keeps \\n literal', read('"a\\nb"'), 'a\\nb');
  assertEq('reader keeps \\" literal', read('"a\\"b"'), 'a\\"b');
  assertEq('reader keeps \\\\ literal', read('"a\\\\b"'), 'a\\\\b');

  /* ---------- JsonParser class is exported ---------- */
  assert('JsonParser is a constructor', typeof JsonParser === 'function');

  /* ---------- summary ---------- */
  console.log(`\n--- Results: ${passed} passed, ${failed} failed ---`);

  if(failed > 0) exit(1);
}

try {
  main().catch(err => {
    console.log(`FAIL: ${err.message}\n${err.stack}`);
    exit(1);
  });
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  exit(1);
} finally {
  console.log('SUCCESS');
}
