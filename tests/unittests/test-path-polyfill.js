// @portable: also run on node, bun and deno by ctest
/* lib/path.js, the pure-JS counterpart of the native path module, gives
 * node:path's results; the table is checked against node v22 */
import * as path from '../../lib/path.js';
import { eq, tests } from '../../lib/tinytest.js';

/* [function, arguments, result] */
const golden = [
  ['join', ['a', '..', '..', 'b'], '../b'],
  ['join', ['a', '/b'], 'a/b'],
  ['join', ['/a', '/b'], '/a/b'],
  ['join', ['a/', 'b'], 'a/b'],
  ['join', ['a', 'b/'], 'a/b/'],
  ['join', ['a', '..'], '.'],
  ['join', ['/', 'a'], '/a'],
  ['join', ['a//b', '.', 'c'], 'a/b/c'],
  ['join', [], '.'],
  ['join', ['', ''], '.'],
  ['normalize', ['a//b/../c/.'], 'a/c'],
  ['normalize', ['/..'], '/'],
  ['normalize', ['../a'], '../a'],
  ['dirname', ['/a/b/'], '/a'],
  ['dirname', ['a'], '.'],
  ['basename', ['/a/b.txt', '.txt'], 'b'],
  ['basename', ['/a/b/'], 'b'],
  ['extname', ['a.b.c'], '.c'],
  ['extname', ['.bashrc'], ''],
  ['isAbsolute', ['/a'], true],
  ['isAbsolute', ['a'], false],
  ['relative', ['/a/b', '/a/c/d'], '../c/d'],
  ['relative', ['/a', '/a'], ''],
  ['resolve', ['/x', 'y', '../z'], '/x/z'],
];

tests({
  'results match node'() {
    for(const [fn, args, want] of golden) eq(JSON.stringify(path[fn](...args)), JSON.stringify(want), `${fn}(${JSON.stringify(args)})`);
  },

  'join rejects a non-string'() {
    let code;

    try {
      path.join('a', 5);
    } catch(e) {
      code = e.code;
    }

    eq(code, 'ERR_INVALID_ARG_TYPE');
  },
});
