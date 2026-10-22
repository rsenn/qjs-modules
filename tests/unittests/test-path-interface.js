/* path: the parts of node:path's interface beyond valid-input results
 * (those are test-path-node.js); qjsm only, since it pins down where qjsm
 * agrees with Node today. A case that fails is added with the fix for it:
 * win32, matchesGlob. */
import path from 'path';
import { assert, eq, tests } from '../../lib/tinytest.js';

const throwsType = fn => {
  try {
    fn();
  } catch(e) {
    return e instanceof TypeError;
  }

  return false;
};
const caught = fn => {
  try {
    fn();
  } catch(e) {
    return e;
  }
};
/* what node 22 says for each, "path" being the parameter name */
const received = [
  [undefined, 'undefined'],
  [null, 'null'],
  [5, 'type number (5)'],
  [true, 'type boolean (true)'],
  [10n, 'type bigint (10n)'],
  [Symbol('x'), 'type symbol (Symbol(x))'],
  [{}, 'an instance of Object'],
  [[], 'an instance of Array'],
  [new Date(0), 'an instance of Date'],
  [function foo() {}, 'function foo']
];

tests({
  'the names Node, Bun and Deno share are exported'() {
    for(const n of ['basename', 'dirname', 'extname', 'format', 'isAbsolute', 'join', 'normalize', 'parse', 'relative', 'resolve']) eq(typeof path[n], 'function', n);
    for(const n of ['delimiter', 'sep']) eq(typeof path[n], 'string', n);
    eq(typeof path.posix, 'object');
  },
  'Function.length as Node'() {
    for(const n of ['dirname', 'extname', 'format', 'isAbsolute', 'normalize', 'parse']) eq(path[n].length, 1, n);
    for(const [n, len] of [['basename', 2], ['join', 0], ['relative', 2], ['resolve', 0]]) eq(path[n].length, len, n);
  },
  'normalize, relative, resolve and format throw like Node'() {
    const code = 'ERR_INVALID_ARG_TYPE';

    for(const [v, text] of received) {
      const msg = (name, type = 'string') => `The "${name}" argument must be of type ${type}. Received ${text}`;
      const cases = [
        [() => path.normalize(v), msg('path')],
        [() => path.relative(v, '/a'), msg('from')],
        [() => path.relative('/a', v), msg('to')],
        [() => path.resolve(v), msg('paths[0]')],
        [() => path.resolve('/a', v), msg('paths[1]')]
      ];

      if(typeof v != 'object' || v === null) cases.push([() => path.format(v), msg('pathObject', 'object')]);

      for(const [fn, message] of cases) {
        const e = caught(fn);

        assert(e instanceof TypeError, message);
        eq(e.code, code, message);
        eq(e.message, message);
      }
    }

    eq(caught(() => path.relative('/a')).message, 'The "to" argument must be of type string. Received undefined');
  },
  'isAbsolute, dirname, extname, basename, parse and join throw like Node'() {
    for(const fn of ['isAbsolute', 'dirname', 'extname', 'basename', 'parse', 'join'])
      for(const [v, text] of received) {
        const e = caught(() => path[fn](v));

        assert(e instanceof TypeError, `${fn}(${text})`);
        eq(e.code, 'ERR_INVALID_ARG_TYPE', fn);
        eq(e.message, `The "path" argument must be of type string. Received ${text}`, fn);
      }
  },
  'basename checks its suffix, join every argument'() {
    eq(caught(() => path.basename('a', 3)).message, 'The "suffix" argument must be of type string. Received type number (3)');
    eq(path.basename('a.js', undefined), 'a.js');
    eq(caught(() => path.join('a', null)).message, 'The "path" argument must be of type string. Received null');
    eq(caught(() => path.join('a', 'b', 5)).code, 'ERR_INVALID_ARG_TYPE');
  },
  'toNamespacedPath and _makeLong are one identity function'() {
    for(const p of [path, path.posix]) {
      assert(p._makeLong === p.toNamespacedPath);
      eq(p.toNamespacedPath.length, 1);
      eq(p.toNamespacedPath('/a/b'), '/a/b');
      eq(p.toNamespacedPath(5), 5);
    }
  },
  async 'path/posix and node:path/posix are the module'() {
    for(const name of ['path/posix', 'node:path/posix', 'node:path']) {
      const m = await import(name);

      assert(m.default === path, name);
      assert(m._makeLong === m.toNamespacedPath, name);
    }
  }
});
