// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* url.fileURLToPath / pathToFileURL and path.posix / default export, as in Node */
import path, { posix } from 'path';
import { fileURLToPath, pathToFileURL } from 'url';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* qjsm has no global URL until lib/globals.js is imported */
if(typeof URL == 'undefined') await import('../../lib/globals.js');

const code = fn => {
  try {
    fn();
  } catch(e) {
    return e.code;
  }
};

tests({
  'path default export and posix are the path functions'() {
    eq(path.join('a', 'b', 'c'), 'a/b/c');
    eq(posix.basename('/x/y.txt', '.txt'), 'y');
    eq(path.posix.extname('f.tar.gz'), '.gz');
    assert(typeof path.resolve == 'function');
    assert(typeof posix.relative == 'function');
  },
  'fileURLToPath'() {
    eq(fileURLToPath('file:///a%20b/c.txt'), '/a b/c.txt');
    eq(fileURLToPath(new URL('file:///x/y')), '/x/y');
    eq(fileURLToPath('file://localhost/etc/hosts'), '/etc/hosts');
  },
  'fileURLToPath rejects a bad scheme, host and encoded slash'() {
    eq(code(() => fileURLToPath('http://example.com/a')), 'ERR_INVALID_URL_SCHEME');
    eq(code(() => fileURLToPath('file://example.com/a')), 'ERR_INVALID_FILE_URL_HOST');
    eq(code(() => fileURLToPath('file:///a%2Fb')), 'ERR_INVALID_FILE_URL_PATH');
  },
  'pathToFileURL encodes like Node'() {
    eq(pathToFileURL('/a b/c#d?.txt').href, 'file:///a%20b/c%23d%3F.txt');
    eq(pathToFileURL('/x/a@b:c+d,e;f=g$h&i').href, 'file:///x/a@b:c+d,e;f=g$h&i');
    eq(pathToFileURL('/héllo').href, 'file:///h%C3%A9llo');
    eq(pathToFileURL('/dir/').href, 'file:///dir/');
  },
  'a relative path is resolved against the cwd'() {
    assert(pathToFileURL('rel.txt').href.startsWith('file:///'));
    assert(pathToFileURL('rel.txt').href.endsWith('/rel.txt'));
  },
});
