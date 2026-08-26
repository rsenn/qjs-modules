import { write } from 'yaml';
import { assert, eq, tests } from './tinytest.js';

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
});
