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
});
