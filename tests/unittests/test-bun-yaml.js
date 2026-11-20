/* Bun.YAML of lib/bun/yaml.js and the .yaml/.yml imports of lib/bun.js; expected values come from bun 1.4.2.
 * runs under bun as well: bun tests/unittests/test-bun-yaml.js */
import * as fs from 'fs';
import { assert, eq, tests } from '../../lib/tinytest.js';

const bun = globalThis.Bun ? undefined : await import('../../lib/bun.js');
const Bun = globalThis.Bun ?? bun.default;
const { YAML } = Bun;
const ROOT = fs.mkdtempSync(`${globalThis.process?.env?.TMPDIR ?? '/tmp'}/qjs-bun-yaml-`);
const at = name => `${ROOT}/${name}`;
const json = v => JSON.stringify(v);
/* key order of a merged mapping differs between runtimes: compare sorted */
const sorted = v => JSON.stringify(v, (k, x) => (x && typeof x == 'object' && !Array.isArray(x) ? Object.fromEntries(Object.entries(x).sort(([a], [b]) => (a < b ? -1 : 1))) : x));

function fails(fn) {
  try {
    fn();
  } catch(e) {
    return e;
  }
}

tests({
  'YAML is on Bun and named in bun'() {
    assert(typeof YAML.parse == 'function' && typeof YAML.stringify == 'function');
    assert(Bun.YAML === YAML);

    if(bun) assert(bun.YAML === YAML);
  },

  'parse: the page example'() {
    eq(json(YAML.parse('\nname: John Doe\nage: 30\nemail: john@example.com\nhobbies:\n  - reading\n  - coding\n  - hiking\n')),
       '{"name":"John Doe","age":30,"email":"john@example.com","hobbies":["reading","coding","hiking"]}');
  },

  'parse: scalars'() {
    eq(json(YAML.parse('- 0x1F\n- 0o17\n- .5\n- +1\n- 1e3\n- 1_000\n- yes\n- Null\n- TRUE\n- 2001-01-01\n- 12:30\n')),
       '[31,15,0.5,1,1000,"1_000","yes",null,true,"2001-01-01","12:30"]');
    eq(json(YAML.parse('')), 'null');
    eq(YAML.parse(undefined), 'undefined');
  },

  'parse: several documents give an array'() {
    eq(json(YAML.parse('---\nname: Document 1\n---\nname: Document 2\n---\nname: Document 3\n')),
       '[{"name":"Document 1"},{"name":"Document 2"},{"name":"Document 3"}]');
  },

  'parse: anchors share identity and may be cyclic'() {
    const r = YAML.parse('x: &a [1, 2]\ny: *a\n');

    eq(json(r), '{"x":[1,2],"y":[1,2]}');
    assert(r.x === r.y);

    const c = YAML.parse('&a {b: *a}');

    assert(c.b === c);
  },

  'parse: tags and block scalars'() {
    const r = YAML.parse('config: !!str 123\ndescription: |\n  a\n  b\nsummary: >\n  c\n  d\n');

    eq(json(r), '{"config":"123","description":"a\\nb\\n","summary":"c d\\n"}');
    eq(json(YAML.parse('- !!int 5\n- !!float 1\n- !!bool true\n- !!bool yes\n- !!str true\n- !!int abc\n- !!null null\n- !!float .5\n- !!int 0x10\n- !!custom 5\n- !!int "5"\n')),
       '[5,1,true,"yes","true","abc",null,0.5,16,"5","5"]');
  },

  'parse: merge keys are expanded'() {
    const r = YAML.parse('defaults: &d {timeout: 5000, retries: 3}\ndev:\n  <<: *d\n  retries: 1\n  url: x\n');

    eq(sorted(r.dev), '{"retries":1,"timeout":5000,"url":"x"}');
    eq(sorted(YAML.parse('a: &a {x: 1, y: 2}\nb: &b {y: 3, z: 4}\nc:\n  <<: [*a, *b]\n  z: 9\n').c), '{"x":1,"y":2,"z":9}');
    eq(sorted(YAML.parse('a: &a {x: 1}\nc:\n  k: 0\n  <<: *a\n  z: 9\n').c), '{"k":0,"x":1,"z":9}');
    eq(json(YAML.parse('a: 1\n<<: 5\n')), '{"a":1,"<<":5}');
  },

  'parse: input other than a string'() {
    eq(json(YAML.parse(new TextEncoder().encode('a: 1'))), '{"a":1}');
    eq(YAML.parse(true), true);
    eq(YAML.parse(5), 5);
  },

  'parse: a syntax error is a SyntaxError'() {
    assert(fails(() => YAML.parse('invalid: yaml: content:')) instanceof SyntaxError);
    assert(fails(() => YAML.parse('[1,2')) instanceof SyntaxError);
  },

  'stringify: flow style without space'() {
    eq(YAML.stringify({ a: 1, b: [1, 'x y', true, null], c: { d: 'yes', e: '' } }), '{a: 1,b: [1,x y,true,null],c: {d: "yes",e: ""}}');
    eq(YAML.stringify([1, 2, [3]]), '[1,2,[3]]');
    eq(YAML.stringify({ a: { b: { c: [1, { d: 2 }] } } }), '{a: {b: {c: [1,{d: 2}]}}}');
    eq(YAML.stringify({}), '{}');
    eq(YAML.stringify([]), '[]');
  },

  'stringify: scalars'() {
    eq(YAML.stringify(1), '1');
    eq(YAML.stringify('a'), 'a');
    eq(YAML.stringify(null), 'null');
    eq(YAML.stringify(true), 'true');
    eq(YAML.stringify(1.5), '1.5');
    eq(YAML.stringify(NaN), '.nan');
    eq(YAML.stringify(Infinity), '.inf');
    eq(YAML.stringify(-Infinity), '-.inf');
    eq(YAML.stringify(-0), '-0');
    eq(YAML.stringify(undefined), undefined);
    eq(YAML.stringify(() => 1), undefined);
  },

  'stringify: quoting'() {
    const quoted = ['y', 'N', 'a,b', '[x]', '{x}', 'a#b', '#a', '-a', '- a', '?a', 'a:', 'a: b', '@a', '`a', '!a', '&a', '*a', '|a', '>a', '%a', "'a", '"a', 'null', 'Null', '~', 'true', 'yes', 'No', 'on', '1', '1.5', '-1', '.5', '0x1F', '1e3', '.inf', '.nan', ' a', 'a ', ''];
    const plain = ['a b', 'é', 'a/b', 'a.b', '2001-01-01', '12:30', '1_000'];

    for(const s of quoted) eq(YAML.stringify(s)[0], '"', s);
    for(const s of plain) eq(YAML.stringify(s), s);

    eq(YAML.stringify('a\tb'), '"a\\tb"');
    eq(YAML.stringify('a\nb'), '"a\\nb"');
    eq(YAML.stringify({ '1': 5, 'a b': 1, '': 6, null: 7 }), '{"1": 5,a b: 1,"": 6,"null": 7}');
  },

  'stringify: block style with space'() {
    eq(YAML.stringify({ a: 1, b: [1, 'x y', true, null], c: { d: 'yes', e: '' } }, null, 2), 'a: 1\nb: \n  - 1\n  - x y\n  - true\n  - null\nc: \n  d: "yes"\n  e: ""');
    eq(YAML.stringify({ a: { b: [1, { c: 2 }] } }, null, 4), 'a: \n    b: \n        - 1\n        - c: 2');
    eq(YAML.stringify([{ a: 1, b: 2 }, { c: [1, 2], d: { e: 3 } }], null, 2), '- a: 1\n  b: 2\n- c: \n    - 1\n    - 2\n  d: \n    e: 3');
    eq(YAML.stringify([{ a: 1, b: { c: [1, 2] } }, [1, [2]]], null, 4), '- a: 1\n    b: \n        c: \n            - 1\n            - 2\n- - 1\n    - - 2');
    eq(YAML.stringify({ x: [[1, 2], [3, [4]]], y: [{}], z: [[]] }, null, 2), 'x: \n  - - 1\n    - 2\n  - - 3\n    - - 4\n"y": \n  - {}\nz: \n  - []');
    eq(YAML.stringify({ f: [], g: {} }, null, 2), 'f: \n  []\ng: \n  {}');
    eq(YAML.stringify({ a: 'l1\nl2\n' }, null, 2), 'a: "l1\\nl2\\n"');
  },

  'stringify: space as number or string'() {
    eq(YAML.stringify({ a: { b: 1 } }, null, 1), 'a: \n b: 1');
    eq(YAML.stringify({ a: { b: 1 } }, null, 2.7), 'a: \n  b: 1');
    eq(YAML.stringify({ a: { b: 1 } }, null, 20), 'a: \n          b: 1');
    eq(YAML.stringify({ a: { b: 1 } }, null, '\t'), 'a: \n\tb: 1');
    eq(YAML.stringify({ a: { b: 1 } }, null, 'abcdefghijklmnop'), 'a: \nabcdefghijb: 1');

    for(const space of [0, '', -1, true, undefined]) eq(YAML.stringify({ a: { b: 1 } }, null, space), '{a: {b: 1}}');
  },

  'stringify: values that are left out or boxed'() {
    eq(YAML.stringify({ f() {}, a: 1, u: undefined }), '{a: 1}');
    eq(YAML.stringify([undefined, () => 1, 1]), '[1]');
    eq(YAML.stringify(new Date(0)), '{}');
    eq(YAML.stringify(new Map([[1, 2]])), '{}');
    eq(YAML.stringify(new Number(1)), '1');
    eq(YAML.stringify(new String('a')), 'a');
  },

  'stringify: BigInt and replacer throw'() {
    const big = fails(() => YAML.stringify(1n));
    const rep = fails(() => YAML.stringify({ a: 1 }, () => 1, 2));

    assert(big instanceof Error && /BigInt/.test(big.message));
    assert(rep instanceof Error && /replacer/.test(rep.message));
    assert(fails(() => YAML.stringify({ a: 1 }, ['a'], 2)) instanceof Error);
    eq(YAML.stringify({ a: 1 }, null, 2), 'a: 1');
    eq(YAML.stringify({ a: 1 }, undefined, 2), 'a: 1');
  },

  'stringify: parse reads back what it wrote'() {
    const v = { a: [1, 'x y', true, null, { b: 'yes', c: '1' }], d: { e: [], f: {}, g: 'l1\nl2' }, 'k k': 0.5 };

    eq(json(YAML.parse(YAML.stringify(v))), json(v));
    eq(json(YAML.parse(YAML.stringify(v, null, 2))), json(v));
  },

  async 'import: .yaml and .yml give default and named exports'() {
    fs.writeFileSync(at('config.yaml'), 'database:\n  host: localhost\n  port: 5432\nfeatures:\n  auth: true\nd-e: 3\n');
    fs.writeFileSync(at('config.yml'), 'a: 1\nlist: [1, 2]\n');
    fs.writeFileSync(at('list.yaml'), '- 1\n- 2\n');

    const m = await import(at('config.yaml'));

    eq(json(m.default), '{"database":{"host":"localhost","port":5432},"features":{"auth":true},"d-e":3}');
    eq(m.database.host, 'localhost');
    eq(m.features.auth, true);

    const y = await import(at('config.yml'));

    eq(y.a, 1);
    eq(json(y.list), '[1,2]');
    eq(json((await import(at('list.yaml'))).default), '[1,2]');
    fs.rmSync(ROOT, { recursive: true, force: true });
  },
});
