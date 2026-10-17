/* path: the parts of node:path's interface beyond valid-input results
 * (those are test-path-node.js); qjsm only, since it pins down where qjsm
 * agrees with Node today. A case that fails is added with the fix for it:
 * Function.length of basename/join/resolve, ERR_INVALID_ARG_TYPE and
 * Node's message text, win32, toNamespacedPath, matchesGlob. */
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
const bad = [undefined, null, 5, {}];

tests({
  'the names Node, Bun and Deno share are exported'() {
    for(const n of ['basename', 'dirname', 'extname', 'format', 'isAbsolute', 'join', 'normalize', 'parse', 'relative', 'resolve']) eq(typeof path[n], 'function', n);
    for(const n of ['delimiter', 'sep']) eq(typeof path[n], 'string', n);
    eq(typeof path.posix, 'object');
  },
  'Function.length as Node'() {
    for(const n of ['dirname', 'extname', 'format', 'isAbsolute', 'normalize', 'parse']) eq(path[n].length, 1, n);
    eq(path.relative.length, 2);
  },
  'normalize, resolve and format throw TypeError for a non-string'() {
    for(const b of bad) {
      assert(throwsType(() => path.normalize(b)), `normalize(${b})`);
      assert(throwsType(() => path.resolve(b)), `resolve(${b})`);
    }

    for(const b of [undefined, null, 5]) assert(throwsType(() => path.format(b)), `format(${b})`);
  }
});
