// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* Node's util surface: format, promisify, callbackify, parseArgs, isDeepStrictEqual */
import { callbackify, debuglog, deprecate, format, isDeepStrictEqual, parseArgs, promisify } from 'node:util';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* the message a call rejects with */
const rejects = async fn => {
  try {
    await fn();
  } catch(e) {
    return e.message;
  }
};

tests({
  'format: specifiers'() {
    eq(format('%s is %d', 'x', 42), 'x is 42');
    eq(format('%i %f', '42.5', '1.5x'), '42 1.5');
    eq(format('%j', { a: 1 }), '{"a":1}');
    eq(format('%s', 5n), '5n');
    eq(format('%c%s', 'css', 'y'), 'y');
    eq(format('%%s %s'), '%%s %s');
    eq(format('%s'), '%s');
  },
  'format: remaining arguments are appended'() {
    eq(format('a', 'b', 3), 'a b 3');
    eq(format(1, 2), '1 2');
    eq(format('%s', 'one', 'two'), 'one two');
  },
  async 'promisify wraps a node-style callback'() {
    const f = promisify((a, b, cb) => setTimeout(() => cb(null, a + b), 1));

    eq(await f(1, 2), 3);
    eq(await rejects(() => promisify(cb => cb(new Error('x')))()), 'x');
  },
  async 'promisify honours util.promisify.custom'() {
    const fn = () => {};

    fn[promisify.custom] = () => Promise.resolve('custom');
    eq(await promisify(fn)(), 'custom');
  },
  async 'callbackify'() {
    const r = await new Promise(resolve => callbackify(async x => x * 2)(4, (err, v) => resolve([err, v])));

    eq(JSON.stringify(r), '[null,8]');
  },
  'parseArgs: options, short flags, positionals'() {
    const r = parseArgs({
      args: ['-f', '--bar', 'b', '--n=3', 'x', '--', '-y'],
      options: { foo: { type: 'boolean', short: 'f' }, bar: { type: 'string' }, n: { type: 'string' } },
      allowPositionals: true,
    });

    eq(JSON.stringify(r.values), '{"foo":true,"bar":"b","n":"3"}');
    eq(r.positionals.join(), 'x,-y');
  },
  'parseArgs: multiple and defaults'() {
    const r = parseArgs({ args: ['--i', 'a', '--i', 'b'], options: { i: { type: 'string', multiple: true }, d: { type: 'string', default: 'z' } } });

    eq(JSON.stringify(r.values), '{"i":["a","b"],"d":"z"}');
  },
  'parseArgs: strict mode errors'() {
    const opts = { x: { type: 'boolean' }, s: { type: 'string' } };
    const code = args => {
      try {
        parseArgs({ args, options: opts });
      } catch(e) {
        return e.code;
      }
    };

    eq(code(['--nope']), 'ERR_PARSE_ARGS_UNKNOWN_OPTION');
    eq(code(['pos']), 'ERR_PARSE_ARGS_UNEXPECTED_POSITIONAL');
    eq(code(['--s']), 'ERR_PARSE_ARGS_INVALID_OPTION_VALUE');
  },
  'isDeepStrictEqual'() {
    assert(isDeepStrictEqual({ a: [1, new Map([[1, { b: 2 }]])], s: new Set([1]) }, { a: [1, new Map([[1, { b: 2 }]])], s: new Set([1]) }));
    assert(!isDeepStrictEqual({ a: 1 }, { a: '1' }));
    assert(!isDeepStrictEqual([1], { 0: 1, length: 1 }));
    assert(!isDeepStrictEqual(0, -0));
    assert(isDeepStrictEqual(NaN, NaN));
    assert(isDeepStrictEqual(new Date(5), new Date(5)));
    const a = {}, b = {};

    a.self = a;
    b.self = b;
    assert(isDeepStrictEqual(a, b));
  },
  'deprecate warns once and passes through'() {
    let n = 0;
    const f = deprecate(x => (n++, x + 1), 'old', 'DEP0');

    eq(f(1), 2);
    eq(f(2), 3);
    eq(n, 2);
  },
  'debuglog is disabled without NODE_DEBUG'() {
    const log = debuglog('qjs-modules-test');

    assert(!log.enabled);
    log('nothing');
  },
});
