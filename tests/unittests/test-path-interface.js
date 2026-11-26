/* path: the parts of node:path's interface beyond valid-input results
 * (those are test-path-node.js); qjsm only, since it pins down where qjsm
 * agrees with Node today. A case that fails is added with the fix for it. */
import path from 'path';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* [[path, pattern], posix result, win32 result] from node v23.11 */
const globCases = [
  [["a/b.js","a/*.js"], true, true],
  [["a/b/c.js","a/*.js"], false, false],
  [["a/b/c.js","a/**/*.js"], true, true],
  [["a/c.js","a/**/*.js"], true, true],
  [[".a","*"], false, false],
  [["a/.b","a/*"], false, false],
  [["a/.b","a/.*"], true, true],
  [["a","a/**"], false, false],
  [["a/b","a/**"], true, true],
  [["x.js","*.{js,ts}"], true, true],
  [["x.ts","*.{js,ts}"], true, true],
  [["x.md","*.{js,ts}"], false, false],
  [["ab","a?"], true, true],
  [["abc","a?"], false, false],
  [["a","[abc]"], true, true],
  [["d","[!abc]"], true, true],
  [["!a","!a"], true, true],
  [["b","!a"], false, false],
  [["a","!a"], false, false],
  [["a\\b","a/b"], false, true],
  [["A.JS","a.js"], false, false],
  [["a/b","a\\b"], true, true],
  [["a","a/"], false, false],
  [["a/b/","a/b"], true, true],
  [["","*"], false, false],
  [["a.js","*.js"], true, true],
  [["dir/.hid/x","dir/**/x"], false, false],
  [["a/b","**"], true, true],
  [["/a/b","/a/*"], true, true],
  [["a//b","a/*"], true, true],
];

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
  },
  'path/win32 and node:path/win32 are path.win32, linked both ways'() {
    eq(typeof path.win32, 'object');
    assert(path.win32.win32 === path.win32);
    assert(path.win32.posix === path);
    assert(path.posix.win32 === path.win32);
  },
  async 'import of path/win32 gives the object of path.win32'() {
    for(const name of ['path/win32', 'node:path/win32']) {
      const m = await import(name);

      assert(m.default === path.win32, name);
      assert(m.win32 === path.win32, name);
      assert(m.join === path.win32.join, name);
    }
  },
  'named exports win32 and matchesGlob exist'() {
    assert(typeof path.matchesGlob == 'function' && path.matchesGlob.length == 2);
    assert(path.posix.matchesGlob === path.matchesGlob);
    eq(path.win32.matchesGlob.length, 2);
  },
  'matchesGlob() equals the results of node'() {
    for(const [[p, g], posix, win32] of globCases) {
      eq(path.matchesGlob(p, g), posix, JSON.stringify([p, g]));
      eq(path.win32.matchesGlob(p, g), win32, JSON.stringify([p, g]) + ' win32');
    }
  },
  'matchesGlob() with a non-string throws ERR_INVALID_ARG_TYPE'() {
    for(const args of [[1, 'a'], ['a', null], [undefined, 'a']]) {
      let err;

      try {
        path.matchesGlob(...args);
      } catch(e) {
        err = e;
      }

      eq(err && err.code, 'ERR_INVALID_ARG_TYPE');
    }
  }
});
