/* lib/json5.js and lib/jsonc.js: Bun.JSON5 and Bun.JSONC; expected values come from bun 1.4.2.
 * JSONC differs from Bun on purpose for three inputs: {'a':1}, 0x1 and text after the value. */
import { JSON5 } from '../../lib/json5.js';
import { JSONC } from '../../lib/jsonc.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

const parseCases = [
  '{a:1}', "{'a':'b'}", '[1,2,]', '{a:1,}', '0x1F', '+.5', '.5e1', 'Infinity', '-Infinity', 'NaN', '1.', "'a\\\nb'", '"\\x41"', '// c\n1', '/* c */ [1]',
  '{"a":1} x', '', '   ', '[1', '{a:}', '01', '" "', '{$_a:1}', '{ü:1}', '1 2', 'null', 'true', '"a"', '{"__proto__":1}', '[,]', '"\\u0041"', '{a:1, a:2}', '-0', '0b1', '1e400',
];
const jsoncCases = ['{"a":1}', '// c\n{"a":1}', '{"a":1,}', '[1,2,]', '{a:1}', "{'a':1}", '/* x */1', '0x1', 'NaN', '{"a":1} x', '', '[1'];
const stringifyCases = [
  [{ a: 1, 'b-c': 'x', '': 2 }], [[1, 'a', null]], [{ a: [1, { b: 2 }] }, null, 2], ["it's"], ['"q"'], [NaN], [Infinity], [-0], [undefined], [{ a: undefined, b: () => 1 }], [new Date(0)], [{ $a: 1, _b: 2, ü: 3, '1a': 4 }], [[1, 2], null, '\t'], [' '], [1e21], [{ a: 1 }, (k, v) => (k == 'a' ? 2 : v)], [{ a: 1, b: 2 }, ['a']],
];

/* results as Bun prints them: JSON with non-finite numbers and -0 as strings, SyntaxError for a throw */
const PARSE = ["{\"a\":1}", "{\"a\":\"b\"}", "[1,2]", "{\"a\":1}", "31", "0.5", "5", "\"Infinity\"", "\"-Infinity\"", "\"NaN\"", "1", "\"ab\"", "\"A\"", "1", "[1]", "SyntaxError", "SyntaxError", "SyntaxError", "SyntaxError", "SyntaxError", "SyntaxError", "\" \"", "{\"$_a\":1}", "{\"ü\":1}", "SyntaxError", "null", "true", "\"a\"", "{\"__proto__\":1}", "SyntaxError", "\"A\"", "{\"a\":2}", "\"-0\"", "SyntaxError", "\"Infinity\""];
const JSONC_RESULTS = ["{\"a\":1}", "{\"a\":1}", "{\"a\":1}", "[1,2]", "SyntaxError", "SyntaxError", "1", "SyntaxError", "SyntaxError", "SyntaxError", "SyntaxError", "SyntaxError"];
const STRINGIFY = ["{a:1,'b-c':'x','':2}", "[1,'a',null]", "{\n  a: [\n    1,\n    {\n      b: 2,\n    },\n  ],\n}", "'it\\'s'", "'\"q\"'", "NaN", "Infinity", "0", "undefined", "{}", "{}", "{$a:1,_b:2,ü:3,'1a':4}", "[\n\t1,\n\t2,\n]", "'\\u2028'", "1e+21", "Error: JSON5.stringify does not support the replacer argument", "Error: JSON5.stringify does not support the replacer argument"];

const show = f => {
  try {
    const v = f();

    return v === undefined ? 'undefined' : JSON.stringify(v, (k, x) => (typeof x == 'number' && !isFinite(x) ? String(x) : Object.is(x, -0) ? '-0' : x));
  } catch(e) {
    return e.name;
  }
};

tests({
  'JSON5.parse gives what Bun gives'() {
    parseCases.forEach((c, i) => eq(PARSE[i], show(() => JSON5.parse(c)), JSON.stringify(c)));
  },

  'JSONC.parse takes comments and trailing commas, nothing else'() {
    jsoncCases.forEach((c, i) => eq(JSONC_RESULTS[i], show(() => JSONC.parse(c)), JSON.stringify(c)));
  },

  'JSON5.stringify writes what Bun writes'() {
    stringifyCases.forEach((a, i) => {
      let got;

      try {
        got = JSON5.stringify(...a) ?? 'undefined';
      } catch(e) {
        got = e.name + ': ' + e.message;
      }

      eq(STRINGIFY[i], got, String(i));
    });
  },

  'a parse error is a SyntaxError with code and loc'() {
    let e;

    try {
      JSON5.parse('{a:}');
    } catch(err) {
      e = err;
    }

    assert(e instanceof SyntaxError);
    eq('unexpected-token', e.code);
    eq(1, e.loc.line);
    assert(e.message.startsWith('JSON5 Parse error: '), e.message);
  },

  'objects are plain and keep a __proto__ key'() {
    const o = JSON5.parse("{__proto__: 1, a: {b: [{}]}}");

    eq(Object.prototype, Object.getPrototypeOf(o));
    eq('__proto__,a', Object.getOwnPropertyNames(o).join());
    eq(Object.prototype, Object.getPrototypeOf(o.a.b[0]));
  },

  'stringify and parse round-trip'() {
    const v = { a: [1, 'it\'s', null, { 'b-c': true }], ü: -1.5 };

    eq(JSON.stringify(v), JSON.stringify(JSON5.parse(JSON5.stringify(v, null, 2))));
  },
});
