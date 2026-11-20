import * as yaml from 'yaml';
import { assert, eq, tests } from '../../lib/tinytest.js';

const { write, stringify, parse } = yaml;
const same = (a, b) => eq(JSON.stringify(a), JSON.stringify(b));

tests({
  'write: scalars'() {
    eq(write(null), 'null\n');
    eq(write(true), 'true\n');
    eq(write(false), 'false\n');
    eq(write(42), '42\n');
    eq(write('hello'), 'hello\n');
  },

  'write: quoting'() {
    eq(write(''), '""\n');
    eq(write('123'), '"123"\n');
    eq(write('true'), '"true"\n');
    eq(write('null'), '"null"\n');
    eq(write('a: b'), '"a: b"\n');
    eq(write('#comment'), '"#comment"\n');
    eq(write('-dash'), '"-dash"\n');
    eq(write(' leading'), '" leading"\n');
    eq(write('trailing '), '"trailing "\n');
    eq(write('a "quote"'), 'a "quote"\n');
    eq(write('line\nbreak'), '"line\\nbreak"\n');
    eq(write('plain-ish_value.ok'), 'plain-ish_value.ok\n');
  },

  'write: empty collections'() {
    eq(write({}), '{}\n');
    eq(write([]), '[]\n');
  },

  'write: flat array'() {
    eq(write([1, 'two', true, null]), '- 1\n- two\n- true\n- null\n');
  },

  'write: flat object'() {
    eq(write({ a: 1, b: 'two', c: true }), 'a: 1\nb: two\nc: true\n');
  },

  'write: nested object'() {
    const value = {
      library: {
        name: 'basic-elements',
        devicesets: {
          resistor: {
            pins: ['1', '2'],
            package: '0805',
          },
        },
      },
    };

    const expected = 'library:\n' + '  name: basic-elements\n' + '  devicesets:\n' + '    resistor:\n' + '      pins:\n' + '        - "1"\n' + '        - "2"\n' + '      package: "0805"\n';

    eq(write(value), expected);
  },

  'write: nested array of objects'() {
    const value = [{ a: 1 }, { b: 2 }];
    const expected = '-\n  a: 1\n-\n  b: 2\n';

    eq(write(value), expected);
  },

  'write: custom indent'() {
    eq(write({ a: { b: 1 } }, 4), 'a:\n    b: 1\n');
  },
  'write: quotes what the parser would read as another type'() {
    eq(write('0x1F'), '"0x1F"\n');
    eq(write('.inf'), '".inf"\n');
    eq(write('-.Inf'), '"-.Inf"\n');
    eq(write('.NaN'), '".NaN"\n');
    eq(write('0o17'), '"0o17"\n');
    eq(write('.info'), '.info\n');
  },

  'stringify: indent as number or string'() {
    eq(stringify({ a: { b: 1 } }), 'a:\n  b: 1\n');
    eq(stringify({ a: { b: 1 } }, null, 4), 'a:\n    b: 1\n');
    eq(stringify({ a: { b: 1 } }, undefined, '   '), 'a:\n   b: 1\n');
  },

  'stringify: replacer is not supported'() {
    let err;

    try {
      stringify({}, () => 1);
    } catch(e) {
      err = e;
    }

    assert(err instanceof TypeError);
  },

  'parse: scalars'() {
    if(!parse) return;

    same(parse('a: 1\nb: 2.5\nc: true\nd: ~\ne: x\nf: 0x1F\n'), { a: 1, b: 2.5, c: true, d: null, e: 'x', f: 31 });
    same(parse('- "1"\n- \'true\'\n- !!str 12\n- null\n'), ['1', 'true', '12', null]);
    eq(parse('.inf'), Infinity);
    eq(parse(''), null);
  },

  'parse: a tag types a plain scalar'() {
    if(!parse) return;

    same(parse('- !!int 5\n- !!float 1\n- !!bool true\n- !!bool yes\n- !!str true\n- !!int abc\n- !!null null\n- !!int "5"\n'), [5, 1, true, 'yes', 'true', 'abc', null, '5']);
  },

  'parse: block scalars and nesting'() {
    if(!parse) return;

    same(parse('k: |\n  l1\n  l2\nl: [1, {a: b}]\nm:\n  - x\n  - y: z\n'), {
      k: 'l1\nl2\n',
      l: [1, { a: 'b' }],
      m: ['x', { y: 'z' }],
    });
  },

  'parse: several documents give an array'() {
    if(!parse) return;

    same(parse('a: 1\n---\nb: 2\n'), [{ a: 1 }, { b: 2 }]);
  },

  'parse: an alias shares the anchored value'() {
    if(!parse) return;

    const r = parse('x: &a [1, 2]\ny: *a\n');

    same(r, { x: [1, 2], y: [1, 2] });
    assert(r.x === r.y);

    const c = parse('&a {b: *a}');

    assert(c.b === c);
  },

  'parse: syntax errors'() {
    if(!parse) return;

    let err;

    try {
      parse('a: [');
    } catch(e) {
      err = e;
    }

    assert(err instanceof SyntaxError);
    assert(/line \d+, column \d+/.test(err.message));

    err = undefined;

    try {
      parse(5);
    } catch(e) {
      err = e;
    }

    assert(err instanceof TypeError);
  },

  'parse: write round trip'() {
    if(!parse) return;

    for(const v of [
      '', ' x', 'x ', '123', 'true', 'null', 'a: b', '#c', 'line\nbreak', 'q"uote', '0x1F', '.inf', 'ünï',
      [], {}, [[]], [{}],
      { 'a b': 1, '': 2, '123': 3, 'multi\nkey': 4 },
      [1, 2.5, -3, 1e21, 1e-7, 2 ** 53, 0.1, true, false, null],
      { a: [1, { b: [2, [3, { c: null }]] }] },
    ])
      for(const indent of [2, 4]) same(parse(write(v, indent)), v);
  },
  'module: exports'() {
    for(const name of ['write', 'stringify']) eq(typeof yaml[name], 'function');

    eq(JSON.stringify(Object.keys(yaml).filter(k => !['write', 'stringify', 'parse'].includes(k))), '[]');
  },

  'write: no argument and non-positive indent'() {
    eq(write(), 'null\n');
    eq(write({ a: { b: 1 } }, 0), 'a:\n  b: 1\n');
    eq(write({ a: { b: 1 } }, -3), 'a:\n  b: 1\n');
    eq(write({ a: { b: 1 } }, NaN), 'a:\n  b: 1\n');
    eq(write({ a: { b: 1 } }, 1), 'a:\n b: 1\n');
    eq(write({ a: { b: 1 } }, 2.9), 'a:\n  b: 1\n');
    eq(write({ a: { b: 1 } }, 8), 'a:\n        b: 1\n');
  },

  'write: escapes in quoted strings'() {
    eq(write('\x01\x1f'), '"\\x01\\x1f"\n');
    eq(write('\0'), '"\\x00"\n');
    eq(write('\r\n'), '"\\r\\n"\n');
    eq(write('a\tb'), '"a\\tb"\n');
    eq(write('say "hi"\n'), '"say \\"hi\\"\\n"\n');
  },

  'write: plain strings stay plain'() {
    for(const s of ['é☃😀', "it's", 'a\\b', 'a/b', 'a.b', 'a,b', 'a-b', 'x y', 'a:b', 'a#b', '2001-01-01', '12:30', '1_000', 'yess', 'nullable', '.info'])
      eq(write(s), s + '\n');
  },

  'write: quoted strings'() {
    for(const s of ['', ' ', ' x', 'x ', '123', '-1.5e10', '+.5', 'true', 'No', 'OFF', '~', 'null', '#c', '-d', '? q', ': x', ', x', '[x]', '{x}', '&a', '*a', '!t', '|x', '>x', "'x", '"x', '%x', '@x', '`x', 'a: b', 'a:', 'a #b', '0x1F', '0o7', '.inf', '-.inf', '.NaN'])
      eq(write(s), JSON.stringify(s) + '\n', s);
  },

  'write: numbers'() {
    eq(write([0, -0, 1, -2, 1.5, 1e21, 1e-7, 0.1 + 0.2, 123456789012345680000]), '- 0\n- 0\n- 1\n- -2\n- 1.5\n- 1e+21\n- 1e-7\n- 0.30000000000000004\n- 123456789012345680000\n');
  },

  'write: null and undefined inside collections'() {
    eq(write([undefined, null]), '- null\n- null\n');
    eq(write({ u: undefined, n: null }), 'u: null\nn: null\n');
  },

  'write: own enumerable string keys only'() {
    const o = Object.create({ inherited: 1 });

    o.own = 2;
    o[Symbol('s')] = 3;
    Object.defineProperty(o, 'hidden', { value: 4, enumerable: false });

    eq(write(o), 'own: 2\n');

    class A {
      constructor() {
        this.p = 1;
      }

      method() {}
    }

    eq(write(new A()), 'p: 1\n');
    eq(write({ 2: 'b', 1: 'a', z: 0 }), '"1": a\n"2": b\nz: 0\n');
  },

  'write: keys are quoted like strings'() {
    eq(write({ 'a b': 1, '': 2, '123': 3, true: 4, 'x:y': 5, 'x: y': 6, 'multi\nkey': 7 }), '"123": 3\na b: 1\n"": 2\n"true": 4\nx:y: 5\n"x: y": 6\n"multi\\nkey": 7\n');
  },

  'write: a function throws TypeError, at any depth'() {
    for(const v of [() => 1, function f() {}, class A {}, { f() {} }, { a: { b: () => 1 } }, [() => 1], [[1, { g: Math.max }]]]) {
      let err;

      try {
        write(v);
      } catch(e) {
        err = e;
      }

      assert(err instanceof TypeError, String(typeof v));
      assert(/unsupported value type/.test(err.message));
    }
  },

  'write: NaN and Infinity use the YAML 1.2 spelling'() {
    eq(write(NaN), '.nan\n');
    eq(write(Infinity), '.inf\n');
    eq(write(-Infinity), '-.inf\n');
    eq(write([NaN, Infinity, -Infinity, 1]), '- .nan\n- .inf\n- -.inf\n- 1\n');
    eq(write({ a: NaN, b: -Infinity }), 'a: .nan\nb: -.inf\n');

    if(parse) {
      const r = parse(write([NaN, Infinity, -Infinity]));

      assert(Number.isNaN(r[0]) && r[1] === Infinity && r[2] === -Infinity);
    }
  },

  'write: a value that contains itself throws TypeError'() {
    const o = { name: 'o' };
    const a = [1];

    o.self = o;
    a.push(a);

    for(const v of [o, a, { x: { y: [o] } }, [[a]]]) {
      let err;

      try {
        write(v);
      } catch(e) {
        err = e;
      }

      assert(err instanceof TypeError);
      assert(/circular structure or nesting too deep/.test(err.message));
    }
  },

  'write: nesting up to the limit works, past it throws'() {
    const nest = n => {
      let v = 'leaf';

      for(let i = 0; i < n; i++) v = [v];

      return v;
    };

    assert(write(nest(900)).endsWith('leaf\n'));

    let err;

    try {
      write(nest(5000));
    } catch(e) {
      err = e;
    }

    assert(err instanceof TypeError);
  },

  'write: a shared object is not a cycle'() {
    const shared = { x: 1 };

    eq(write({ a: shared, b: shared, c: [shared, shared] }), 'a:\n  x: 1\nb:\n  x: 1\nc:\n  -\n    x: 1\n  -\n    x: 1\n');
  },

  'write: symbol and bigint throw TypeError'() {
    for(const v of [Symbol('s'), 1n, { a: 1n }, [Symbol('x')], { nested: { deep: [1n] } }]) {
      let err;

      try {
        write(v);
      } catch(e) {
        err = e;
      }

      assert(err instanceof TypeError, String(typeof v));
    }
  },

  'write: sequences of sequences and mixed nesting'() {
    eq(write([[1, 2], [3]]), '-\n  - 1\n  - 2\n-\n  - 3\n');
    eq(write({ a: [[]], b: [{}], c: { d: [] } }), 'a:\n  - []\nb:\n  - {}\nc:\n  d: []\n');
  },

  'write: deep nesting'() {
    let v = 'leaf';

    for(let i = 0; i < 300; i++) v = i % 2 ? [v] : { k: v };

    const text = write(v);

    assert(text.endsWith('leaf\n'));
    assert(text.split('\n').length > 300);

    if(parse) same(parse(text), v);
  },

  'write: a long string and many items'() {
    eq(write('x'.repeat(100000)).length, 100001);

    const items = [];

    for(let i = 0; i < 2000; i++) items.push({ i, s: 'item ' + i, t: [i, i + 0.5, null, true] });

    const text = write(items);

    assert(text.length > 40000);

    if(parse) same(parse(text), items);
  },

  'stringify: space is capped at 10 and defaults to 2'() {
    const v = { a: { b: 1 } };

    eq(stringify(v, null, 20), 'a:\n' + ' '.repeat(10) + 'b: 1\n');
    eq(stringify(v, undefined, 'ab'), 'a:\n  b: 1\n');
    eq(stringify(v, null, 4), 'a:\n    b: 1\n');

    for(const space of ['', 0, -1, true, undefined, null, {}]) eq(stringify(v, null, space), 'a:\n  b: 1\n');
  },

  'stringify: same text as write'() {
    for(const v of [null, 'x', [1, 'a'], { a: [{ b: 1 }] }]) eq(stringify(v), write(v));
  },

  'parse: flow collections'() {
    if(!parse) return;

    same(parse('a: [1, [2, {b: c}], {}]\nb: {x: 1, y: [a]}\n'), { a: [1, [2, { b: 'c' }], {}], b: { x: 1, y: ['a'] } });
    same(parse('[1, 2,]'), [1, 2]);
    same(parse('{}'), {});
    same(parse('[]'), []);
  },

  'parse: quoted scalars and escapes'() {
    if(!parse) return;

    same(parse('a: "x\\ty\\u00e9\\x41\\n"'), { a: 'x\ty\u00e9A\n' });
    same(parse("a: 'it''s'"), { a: "it's" });
    eq(parse('"\\u00e9\\U0001F600"'), 'é😀');
    eq(parse('"multi\n  line"'), 'multi line');
    eq(parse("'123'"), '123');
    eq(parse('"true"'), 'true');
    eq(parse('""'), '');
  },

  'parse: block scalars'() {
    if(!parse) return;

    same(parse('a: >-\n  x\n  y\n\n  z\n'), { a: 'x y\nz' });
    same(parse('a: |+\n  x\n\n'), { a: 'x\n\n' });
    same(parse('a: |-\n  x\n'), { a: 'x' });
    same(parse('a: |\n  x\n  y\nb: 1\n'), { a: 'x\ny\n', b: 1 });
    eq(parse('plain\n  multi'), 'plain multi');
  },

  'parse: comments, directives and line ends'() {
    if(!parse) return;

    same(parse('a: 1 # trailing\n# c\nb: 2\n'), { a: 1, b: 2 });
    same(parse('%YAML 1.2\n---\na: 1\n'), { a: 1 });
    same(parse('a: b\r\nc: d\r\n'), { a: 'b', c: 'd' });
    same(parse('#c\na: "#not a comment"\n'), { a: '#not a comment' });
  },

  'parse: sequences'() {
    if(!parse) return;

    same(parse('k:\n- 1\n- 2\n'), { k: [1, 2] });
    same(parse('- - 1\n  - 2\n- - 3\n'), [[1, 2], [3]]);
    same(parse('- a: 1\n  b: 2\n- c: 3\n'), [{ a: 1, b: 2 }, { c: 3 }]);
    same(parse('- a\n b'), ['a b']);
  },

  'parse: null forms'() {
    if(!parse) return;

    same(parse('a: ~\nb:\nc: null\nd: Null\ne: NULL\n'), { a: null, b: null, c: null, d: null, e: null });
    eq(parse('a:\n').a, null);
    eq(parse('~'), null);
  },

  'parse: numbers'() {
    if(!parse) return;

    for(const [text, n] of [['-0', 0], ['1.0', 1], ['+.5', 0.5], ['00', 0], ['-5', -5], ['0x1F', 31], ['0o17', 15], ['1e3', 1000], ['-1.5e-3', -0.0015],
                            ['9007199254740993', 9007199254740992], ['18446744073709551615', 18446744073709552000], ['-9223372036854775808', -9223372036854776000]])
      eq(parse(text), n, text);

    eq(parse('.inf'), Infinity);
    eq(parse('-.inf'), -Infinity);
    eq(parse('+.inf'), Infinity);
    assert(Number.isNaN(parse('.NaN')));
    assert(Number.isNaN(parse('.nan')));
  },

  'parse: text that only looks like a number stays a string'() {
    if(!parse) return;

    for(const text of ['0o', '1e', '0xG', '0b1', '1,000', '1e400', '1_000', '12:30', '2001-01-01', '1.2.3', '--1', '0x', 'inf', 'nan', '.inf5'])
      eq(parse(text), text, text);
  },

  'parse: booleans'() {
    if(!parse) return;

    same(parse('[true, True, TRUE, false, False, FALSE]'), [true, true, true, false, false, false]);
    same(parse('[yes, No, on, OFF, y, n]'), ['yes', 'No', 'on', 'OFF', 'y', 'n']);
  },

  'parse: mapping keys'() {
    if(!parse) return;

    same(parse('? a\n: b\n'), { a: 'b' });
    same(parse('1: x\n2.5: y\ntrue: z\n~: w\n'), { 1: 'x', '2.5': 'y', true: 'z', null: 'w' });
    same(parse('é: ü\n'), { é: 'ü' });
    same(parse('"a b": 1\n"": 2\n'), { 'a b': 1, '': 2 });
    same(parse('__proto__: 1\n'), JSON.parse('{"__proto__": 1}'));
    eq(Object.getPrototypeOf(parse('__proto__: {x: 1}\n')), Object.prototype);
    same(parse('<<: {a: 1}\nb: 2\n'), { '<<': { a: 1 }, b: 2 });
  },

  'parse: a key that is not a scalar throws TypeError'() {
    if(!parse) return;

    for(const text of ['? [1, 2]\n: x', '? {a: 1}\n: x']) {
      let err;

      try {
        parse(text);
      } catch(e) {
        err = e;
      }

      assert(err instanceof TypeError, text);
    }
  },

  'parse: scalar anchors and aliases'() {
    if(!parse) return;

    same(parse('- &x 1\n- *x\n'), [1, 1]);
    same(parse('a: &s hello\nb: *s\n'), { a: 'hello', b: 'hello' });
    eq(parse('&a x'), 'x');

    const r = parse('base: &b {k: 1}\nlist:\n  - *b\n  - *b\n');

    assert(r.list[0] === r.base && r.list[1] === r.base);
  },

  'parse: an alias must be defined, and anchors do not cross documents'() {
    if(!parse) return;

    for(const text of ['a: *nope', 'a: &x 1\nb: *x\n---\nc: *x\n']) {
      let err;

      try {
        parse(text);
      } catch(e) {
        err = e;
      }

      assert(err instanceof SyntaxError, text);
    }

    same(parse('a: &x 1\nb: *x\n---\nc: &x 2\nd: *x\n'), [{ a: 1, b: 1 }, { c: 2, d: 2 }]);
  },

  'parse: empty and comment-only documents'() {
    if(!parse) return;

    eq(parse('# only\n'), null);
    eq(parse('...'), null);
    same(parse('---\n---\n'), [null, null]);
    same(parse('- x\n- y\n---\n- z'), [['x', 'y'], ['z']]);
    same(parse('a: 1\n---\n'), [{ a: 1 }, null]);
  },

  'parse: nesting depth'() {
    if(!parse) return;

    let v = parse('['.repeat(100) + ']'.repeat(100));
    let depth = 0;

    while(Array.isArray(v) && v.length) {
      v = v[0];
      depth++;
    }

    eq(depth, 99);

    let err;

    try {
      parse('['.repeat(2000));
    } catch(e) {
      err = e;
    }

    assert(err instanceof SyntaxError);
  },

  'parse: errors carry a position'() {
    if(!parse) return;

    for(const text of ['a: b: c', 'a: "unterminated', 'a: [unterminated', 'a: [1, 2', '{a: 1']) {
      let err;

      try {
        parse(text);
      } catch(e) {
        err = e;
      }

      assert(err instanceof SyntaxError, text);
      assert(/^yaml\.parse: .*\(line \d+, column \d+\)$/.test(err.message), err.message);
    }
  },

  'parse: only a string is accepted'() {
    if(!parse) return;

    for(const v of [undefined, null, 5, true, {}, [], new Uint8Array(2), Symbol('s')]) {
      let err;

      try {
        parse(v);
      } catch(e) {
        err = e;
      }

      assert(err instanceof TypeError, String(typeof v));
    }

    let none;

    try {
      parse();
    } catch(e) {
      none = e;
    }

    assert(none instanceof TypeError);
  },

  'parse: a long string and many documents'() {
    if(!parse) return;

    eq(parse(write('x'.repeat(100000))).length, 100000);
    eq(parse(Array.from({ length: 500 }, (_, i) => `n: ${i}`).join('\n---\n')).length, 500);
  },

  'parse: the values are plain and independent'() {
    if(!parse) return;

    const a = parse('x: [1, 2]');
    const b = parse('x: [1, 2]');

    assert(a !== b && a.x !== b.x);
    eq(Object.getPrototypeOf(a), Object.prototype);
    eq(Object.getPrototypeOf(a.x), Array.prototype);
    eq(JSON.stringify(Object.keys(parse('c: 1\nb: 2\na: 3\n'))), '["c","b","a"]');
  },

  'parse: write is stable through a second round'() {
    if(!parse) return;

    const text = 'a: 1\nb:\n  - x\n  - y: [1, 2]\n    z: "q"\nc: |\n  l1\n  l2\n';
    const once = write(parse(text));

    eq(write(parse(once)), once);
    same(parse(once), parse(text));
  },
});
