/* Bun.YAML of lib/bun/yaml.js and the .yaml/.yml imports of lib/bun.js; expected values come from bun 1.4.2.
 * runs under bun as well: bun tests/unittests/test-bun-yaml.js */
import * as fs from 'fs';
import { assert, eq, tests } from '../../lib/tinytest.js';

const IS_BUN = !!globalThis.process?.versions?.bun;
const Bun = globalThis.Bun ?? (await import('../../lib/bun.js')).default;
const { YAML } = Bun;

/* the yaml module is optional, and its parse needs CYAML */
let usable = false;

try {
  usable = YAML?.parse('a: 1').a === 1;
} catch(e) {}

if(!usable) {
  console.log('skipped: Bun.YAML needs the yaml module built with CYAML');
  globalThis.process.exit(0);
}

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
  },
  'stringify: empty collections in flow and block style'() {
    eq(YAML.stringify({ a: [], b: {}, c: [[]], d: [{}] }), '{a: [],b: {},c: [[]],d: [{}]}');
    eq(YAML.stringify([], null, 2), '[]');
    eq(YAML.stringify({}, null, 2), '{}');
    eq(YAML.stringify([[]], null, 2), '- []');
    eq(YAML.stringify({ a: [] }, null, 2), 'a: \n  []');
  },

  'stringify: block style quotes keys like values'() {
    eq(YAML.stringify({ y: 1, 'a b': 2, '1': 3, '': 4, 'x:y': 5, 'x: y': 6, true: 7 }, null, 2), '"1": 3\ny: 1'.replace('y: 1', '"y": 1') + '\na b: 2\n"": 4\nx:y: 5\n"x: y": 6\n"true": 7');
  },

  'stringify: strings with control characters and unicode'() {
    eq(YAML.stringify('\x01'), '"\\x01"');
    eq(YAML.stringify(['\0', '\x07', '\b', '\f', '\v', '\x1b', '\x7f', '\u2028', '\x85', 'a\xa0b']), '["\\0","\\a","\\b","\\f","\\v","\\e","\\x7f","\\L","\\N","a\\_b"]');
    eq(YAML.stringify('é☃😀'), 'é☃😀');
    eq(YAML.stringify({ 'k\tk': 1 }), '{"k\\tk": 1}');
  },

  'stringify: numbers'() {
    eq(YAML.stringify([1, -2, 1.5, 1e21, 1e-7, 0.1 + 0.2, 123456789012345680000]), '[1,-2,1.5,1e+21,1e-7,0.30000000000000004,123456789012345680000]');
    eq(YAML.stringify([NaN, Infinity, -Infinity]), '[.nan,.inf,-.inf]');
  },

  'stringify: object keys are the own enumerable string keys'() {
    const o = Object.create({ inherited: 1 });

    o.own = 2;
    Object.defineProperty(o, 'hidden', { value: 4, enumerable: false });

    eq(YAML.stringify(o), '{own: 2}');
    eq(YAML.stringify({ b: 1, a: 2 }), '{b: 1,a: 2}');
  },

  'stringify: the same object twice is written twice'() {
    const shared = { x: 1 };

    /* bun writes an anchor and an alias for the second one */
    if(IS_BUN) return;

    eq(YAML.stringify({ a: shared, b: shared }), '{a: {x: 1},b: {x: 1}}');
    eq(YAML.stringify([shared, shared], null, 2), '- x: 1\n- x: 1');
  },

  'stringify: large input'() {
    const items = Array.from({ length: 1000 }, (_, i) => ({ i, s: 'item ' + i, t: [i, true, null] }));

    eq(json(YAML.parse(YAML.stringify(items))), json(items));
    eq(json(YAML.parse(YAML.stringify(items, null, 2))), json(items));
  },

  'stringify: a circular value'() {
    const o = {};

    o.o = o;

    if(IS_BUN) eq(YAML.stringify(o), '&root {o: *root}');
    else assert(fails(() => YAML.stringify(o)) instanceof TypeError);
  },

  'parse: merge keys of several kinds'() {
    const r = YAML.parse(`
base: &base {a: 1, b: 2}
other: &other {b: 3, c: 4}
one:
  <<: *base
  b: 9
many:
  <<: [*base, *other]
  d: 5
chained: &chain
  <<: *base
  e: 6
user:
  <<: *chain
  f: 7
list:
  - <<: *base
    z: 0
  - <<: *other
`);

    eq(sorted(r.one), '{"a":1,"b":9}');
    eq(sorted(r.many), '{"a":1,"b":2,"c":4,"d":5}');
    eq(sorted(r.chained), '{"a":1,"b":2,"e":6}');
    eq(sorted(r.user), '{"a":1,"b":2,"e":6,"f":7}');
    eq(sorted(r.list), '[{"a":1,"b":2,"z":0},{"b":3,"c":4}]');
    eq(sorted(r.base), '{"a":1,"b":2}');
    assert(!('<<' in r.one) && !('<<' in r.list[0]));
  },

  'parse: merge keys expand in every document'() {
    const docs = YAML.parse('a: &a {x: 1}\nb:\n  <<: *a\n---\nc: &c {y: 2}\nd:\n  <<: *c\n');

    eq(sorted(docs), '[{"a":{"x":1},"b":{"x":1}},{"c":{"y":2},"d":{"y":2}}]');
  },

  'parse: the page configuration example'() {
    const configs = YAML.parse(`
defaults: &defaults
  timeout: 5000
  retries: 3
  cache:
    enabled: true
    ttl: 3600

development:
  <<: *defaults
  api:
    url: http://localhost:4000
    key: dev_key_12345
  logging:
    level: debug
    pretty: true

production:
  <<: *defaults
  api:
    url: https://api.example.com
    key: \${PROD_API_KEY}
  cache:
    enabled: true
    ttl: 86400
  logging:
    level: error
    pretty: false
`);

    eq(configs.development.timeout, 5000);
    eq(configs.development.cache.ttl, 3600);
    eq(configs.development.api.url, 'http://localhost:4000');
    eq(configs.production.retries, 3);
    eq(configs.production.cache.ttl, 86400);
    eq(configs.production.api.key, '${PROD_API_KEY}');
    eq(configs.production.logging.pretty, false);
  },

  'parse: the page employee example'() {
    const data = YAML.parse(`
# Employee record
employee: &emp
  name: Jane Smith
  department: Engineering
  skills:
    - JavaScript
    - TypeScript
    - React

manager: *emp  # Reference to employee

config: !!str 123  # Explicit string type

description: |
  This is a multi-line
  literal string that preserves
  line breaks and spacing.

summary: >
  This is a folded string
  that joins lines with spaces
  unless there are blank lines.
`);

    assert(data.manager === data.employee);
    eq(data.employee.name, 'Jane Smith');
    eq(json(data.employee.skills), '["JavaScript","TypeScript","React"]');
    eq(data.config, '123');
    eq(data.description, 'This is a multi-line\nliteral string that preserves\nline breaks and spacing.\n');
    eq(data.summary, 'This is a folded string that joins lines with spaces unless there are blank lines.\n');
  },

  'parse: byte input'() {
    const bytes = new TextEncoder().encode('a: é');

    eq(json(YAML.parse(bytes)), '{"a":"é"}');
    eq(json(YAML.parse(bytes.buffer)), '{"a":"é"}');
    eq(json(YAML.parse(new DataView(bytes.buffer))), '{"a":"é"}');
  },

  'parse: other values are coerced to a string'() {
    eq(YAML.parse(null), null);
    eq(YAML.parse(1.5), 1.5);
    eq(YAML.parse([1, 2]), '1,2');
    eq(json(YAML.parse({ toString: () => 'x: 1' })), '{"x":1}');
    eq(YAML.parse(undefined), 'undefined');
  },

  'parse: errors are SyntaxError with a message'() {
    for(const text of ['invalid: yaml: content:', '[1,2', 'a: "unterminated', '{a: 1', 'a: *nope']) {
      const e = fails(() => YAML.parse(text));

      assert(e instanceof SyntaxError, text);
      assert(typeof e.message == 'string' && e.message.length > 0);
    }
  },

  'parse: a value read back is writable again'() {
    const v = YAML.parse('a: &x [1, {b: c}]\nd: *x\n');

    eq(json(YAML.parse(YAML.stringify(v, null, 2))), json(v));
  },

  async 'import: the page configuration example'() {
    fs.writeFileSync(
      at('page.yaml'),
      'database:\n  host: localhost\n  port: 5432\n  name: myapp\n\nredis:\n  host: localhost\n  port: 6379\n\nfeatures:\n  auth: true\n  rateLimit: true\n  analytics: false\n',
    );

    const { default: config, database, redis, features } = await import(at('page.yaml'));

    eq(config.database.host, 'localhost');
    eq(config.redis.port, 6379);
    eq(database.port, 5432);
    eq(redis.host, 'localhost');
    eq(features.analytics, false);
    assert(config.database === database);
  },

  async 'import: anchors and merge keys in a file'() {
    fs.writeFileSync(at('env.yaml'), 'defaults: &defaults\n  timeout: 5000\n  retries: 3\n\ndevelopment:\n  <<: *defaults\n  retries: 1\n\nproduction:\n  <<: *defaults\n');

    const configs = (await import(at('env.yaml'))).default;

    eq(sorted(configs.development), '{"retries":1,"timeout":5000}');
    eq(sorted(configs.production), '{"retries":3,"timeout":5000}');
  },

  async 'import: a template string path picks the file'() {
    fs.writeFileSync(at('development.yaml'), 'name: dev\n');
    fs.writeFileSync(at('production.yaml'), 'name: prod\n');

    for(const env of ['development', 'production']) {
      const { default: config } = await import(`${ROOT}/${env}.yaml`);

      eq(config.name, env == 'development' ? 'dev' : 'prod');
    }
  },

  async 'import: names that are not identifiers are only on default'() {
    fs.writeFileSync(at('names.yaml'), 'ok: 1\nd-e: 2\n1x: 3\nclass: 4\nvalid_name: 5\n');

    const m = await import(at('names.yaml'));

    eq(m.ok, 1);
    eq(m.valid_name, 5);
    eq(json(m.default), '{"ok":1,"d-e":2,"1x":3,"class":4,"valid_name":5}');
    /* qjsm has no string export names, bun has them */
    if(!IS_BUN) assert(!('d-e' in m));
    eq(m.class, 4);
  },

  async 'import: the default export is the whole value'() {
    fs.writeFileSync(at('own-default.yaml'), 'default: shadowed\nother: 1\n');
    fs.writeFileSync(at('scalar.yaml'), 'just text\n');
    fs.writeFileSync(at('empty.yaml'), '# nothing\n');
    fs.writeFileSync(at('multi.yaml'), 'a: 1\n---\nb: 2\n');

    eq(json((await import(at('own-default.yaml'))).default), '{"default":"shadowed","other":1}');
    eq((await import(at('own-default.yaml'))).other, 1);
    eq((await import(at('scalar.yaml'))).default, 'just text');
    eq((await import(at('empty.yaml'))).default, null);
    eq(json((await import(at('multi.yaml'))).default), '[{"a":1},{"b":2}]');
  },

  async 'import: the module is evaluated once'() {
    fs.writeFileSync(at('once.yaml'), 'n: 1\n');

    const a = await import(at('once.yaml'));
    const b = await import(at('once.yaml'));

    assert(a.default === b.default);
  },

  async 'import: invalid yaml rejects with SyntaxError'() {
    fs.writeFileSync(at('bad.yaml'), 'a: [1, 2\n');

    let err;

    try {
      await import(at('bad.yaml'));
    } catch(e) {
      err = e;
    }

    assert(err);

    if(!IS_BUN) assert(err instanceof SyntaxError);
  },

  async 'import: other files are not touched'() {
    fs.writeFileSync(at('plain.mjs'), 'export default 7;\nexport const name = "plain";\n');
    fs.writeFileSync(at('note.yaml.mjs'), 'export default "not yaml";\n');

    const m = await import(at('plain.mjs'));

    eq(m.default, 7);
    eq(m.name, 'plain');
    eq((await import(at('note.yaml.mjs'))).default, 'not yaml');
  },
  'cleanup'() {
    fs.rmSync(ROOT, { recursive: true, force: true });
  },
});
