import * as path from 'path';
import * as std from 'std';
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'basename()'() {
    eq(path.basename('/tmp/test/'), 'test');
    eq(path.basename('/tmp/test.obj', '.obj'), 'test');
    eq(path.basename('/tmp/test.obj'), 'test.obj');
    eq(path.basename('test'), 'test');
    eq(path.basename('test.obj', '.obj'), 'test');

    // differs from Node.js before the fix (api-compatibility-nodejs.md, B)
    eq(path.basename(''), '');
    eq(path.parse('').base, '');
    eq(path.basename('a.html', 'a.html'), '');
    eq(path.basename('aaa', 'aaa'), '');
  },
  'normalize()'() {
    eq(path.normalize('////tmp////other//..//test'), '/tmp/test');

    // differs from Node.js before the fix (api-compatibility-nodejs.md, B)
    eq(path.normalize(''), '.');
    eq(path.normalize('/..'), '/');
    eq(path.normalize('/a/b/../../..'), '/');
    eq(path.normalize('a/./b/.'), 'a/b');
    eq(path.normalize('./a/./'), 'a/');
  },
  'dirname()'() {
    eq(path.dirname('/tmp/../'), '/tmp');
    eq(path.dirname('/tmp/test/'), '/tmp');
    eq(path.dirname('/tmp/other/../test/../'), '/tmp/other/../test');

    // differs from Node.js before the fix (api-compatibility-nodejs.md, B)
    eq(path.dirname('/foo'), '/');
    eq(path.dirname('/'), '/');
    eq(path.dirname('//a'), '//');
    eq(path.dirname('a//b'), 'a/');
  },
  'exists()'() {
    assert(path.exists('.'));
    assert(path.exists(scriptArgs[0]));
    assert(!path.exists('fPf6EB6FhEoF6fGH0m2qej8f14qJwCNgbVQ0AlYViTkY0fI3MUyST8ZsUfqfGPGC'));
  },
  'extname()'() {
    eq(path.extname('test.obj'), '.obj');
    eq(path.extname('/tmp/test.obj'), '.obj');

    // differs from Node.js before the fix (api-compatibility-nodejs.md, B)
    eq(path.extname('..'), '');
    eq(path.parse('..').ext, '');
    eq(path.extname('a.b/'), '.b');
  },

  'fnmatch()'() {
    eq(path.fnmatch('/tmp/*.obj', '/tmp/test.obj'), 0);
  },
  'getcwd()'() {
    eq(path.getcwd(), path.resolve('.'));
  },
  'gethome()'() {
    eq(std.getenv('HOME'), path.gethome());
  },
  'getsep()'() {
    eq(path.getsep('/tmp/test/'), '/');
  },
  'isAbsolute()'() {
    assert(path.isAbsolute('/tmp/test/'));
    assert(!path.isAbsolute('tmp/test/'));
  },
  'isRelative()'() {
    assert(!path.isRelative('/tmp/test/'));
    assert(path.isRelative('tmp/test/'));
  },
  'isDirectory()'() {
    assert(path.isDirectory('.'));
    assert(!path.isDirectory(scriptArgs[0]));
  },
  'isFile()'() {
    assert(!path.isFile('.'));
    assert(path.isFile(scriptArgs[0]));
  },
  'isCharDev()'() {
    assert(path.isCharDev('/dev/null'));
  },
  'isBlockDev()'() {
    assert(path.isBlockDev('/dev/loop0'));
  },
  'isFIFO()'() {
    assert(path.isFIFO('/run/initctl'));
    assert(!path.isFIFO('/dev/null'));
  },
  'isSocket()'() {
    assert(path.isSocket('/run/mysqld/mysqld.sock'));
  },
  'isSymlink()'() {
    assert(path.isSymlink('/dev/fd'));
    assert(!path.isSymlink('.'));
  },
  'length()'() {
    eq(path.length('/tmp/test.obj'), 3);
    eq(path.length('./test.obj'), 2);
    eq(path.length(''), 0);
    eq(path.length('.'), 1);
    eq(path.length('..//..///'), 2);
    eq(path.length('///..//..///'), 3);
  },
  'components()'() {
    eq(path.components('/tmp/test.obj'), 2);
    eq(path.components('./test.obj'), 2);
    eq(path.components(''), 0);
    eq(path.components('.'), 1);
    eq(path.components('..//..///'), 2);
    eq(path.components('///..//..///'), 2);
  },
  'readlink()'() {
    eq(path.readlink('/dev/fd'), '/proc/self/fd');
    eq(path.readlink('.'), undefined);
  },
  'right()'() {
    eq(path.right('/tmp/test/'), '/tmp/test/'.indexOf('test'));
    eq(path.right('/tmp/test//..///'), '/tmp/test//..///'.indexOf('..'));
  },
  'skip()'() {
    eq(path.skip('/tmp/test/'), 1);
    eq(path.skip('/tmp/test/', 1), 5);
    eq(path.skip('/tmp/test/', 5), -1);
  },
  'skipSeparator()'() {
    eq(path.skipSeparator('/tmp/test/'), 1);
  },
  'isSeparator()'() {
    assert(path.isSeparator('/'));
    assert(path.isSeparator('///'));
    assert(!path.isSeparator('.'));
    assert(!path.isSeparator('/..'));
    assert(!path.isSeparator('../'));
  },
  'absolute()'() {
    eq(path.absolute('test'), path.join(path.getcwd(), 'test'));
  },
  'realpath()'() {
    eq(path.realpath('/proc/self/cwd'), path.getcwd());
  },
  'at()'() {
    eq(path.at('////tmp////other//..//test', 0), '');
    eq(path.at('////tmp////other//..//test', 1), 'tmp');
    eq(path.at('////tmp////other//..//test', 2), 'other');
    eq(path.at('////tmp////other//..//test', 3), '..');
    eq(path.at('////tmp////other//..//test', 4), 'test');

    eq(path.at('tmp////other//..//test', 0), 'tmp');
    eq(path.at('tmp////other//..//test', 1), 'other');
    eq(path.at('tmp////other//..//test', 2), '..');
    eq(path.at('tmp////other//..//test', 3), 'test');
  },
  'search()'() {
    const searchPath = [path.getcwd(), path.resolve('..')].join(path.delimiter);

    eq(path.search(searchPath, path.basename(path.getcwd())), path.getcwd());
  },
  'relative()'() {
    const base = path.basename(path.getcwd());

    eq(path.relative('..', '.'), path.basename(path.getcwd()));
    eq(path.relative('.', '..'), '..');

    // differs from Node.js before the fix (api-compatibility-nodejs.md, B)
    eq(path.relative('/a/b', '/a/b'), '');
  },
  'slice()'() {
    eq(path.slice('////tmp////other//..//test', 1, 3), 'tmp/other');
    eq(path.slice('////tmp////other//..//test', 2, 4), 'other/..');
    eq(path.slice('////tmp////other//..//test', -3, -1), 'other/..');
    eq(path.slice('////tmp////other//..//test', 4), 'test');
    eq(path.slice('////tmp////other//..//test', -1), 'test');
    eq(path.slice('////tmp////other//..//test', 3, 5), '../test');
    eq(path.slice('////tmp////other//..//test', -2), '../test');
  },
  'join()'() {
    eq(path.join('', 'tmp', 'test'), 'tmp/test');
    eq(path.join('/', 'tmp', 'test'), '/tmp/test');
    eq(path.join('/tmp', 'test'), '/tmp/test');

    // differs from Node.js before the fix (api-compatibility-nodejs.md, B)
    eq(path.join('/foo', 'bar', 'baz/asdf', 'quux', '..'), '/foo/bar/baz/asdf');
    eq(path.join(), '.');
    eq(path.join(''), '.');
    eq(path.join('/a/', '/b/'), '/a/b/');
    eq(path.join('a//b', 'c/'), 'a/b/c/');
  },
  'parse()'() {
    const s = '/tmp/test.obj';
    const { root, dir, base, ext, name } = path.parse(s);

    eq(root, '/');
    eq(dir, path.dirname(s));
    eq(base, path.basename(s));
    eq(ext, path.extname(s));
    eq(name, path.basename(s, path.extname(s)));

    // differs from Node.js before the fix (api-compatibility-nodejs.md, B)
    eq(path.parse('/').dir, '/');
  },
  'format()'() {
    eq(path.format({ dir: '/tmp', base: 'test.obj' }), '/tmp/test.obj');
    eq(path.format({ dir: '/tmp', name: 'test', ext: '.obj' }), '/tmp/test.obj');
    eq(path.format({ name: 'test', ext: '.obj' }), 'test.obj');
    eq(path.format({ base: 'test.obj' }), 'test.obj');

    // differs from Node.js before the fix (api-compatibility-nodejs.md, B)
    eq(path.format({ root: '/ignored', dir: '/home/user/dir', base: 'file.txt' }), '/home/user/dir/file.txt');
    eq(path.format({ root: '/', base: 'file.txt', ext: 'ignored' }), '/file.txt');
    eq(path.format({ root: '/', name: 'file', ext: '.txt' }), '/file.txt');
    eq(path.format({ root: '/', name: 'file', ext: 'txt' }), '/file.txt');
    eq(path.format({ root: '/' }), '/');
    eq(path.format({ ext: '.e' }), '.e');
  },
  'resolve()'() {
    eq(path.resolve('/proc', 'self', 'cwd'), '/proc/self/cwd');
    eq(path.resolve('/proc', 'self', '/tmp', 'test'), '/tmp/test');

    // differs from Node.js before the fix (api-compatibility-nodejs.md, B)
    eq(path.resolve('/a', '', 'b'), '/a/b');
    eq(path.resolve('/a/b', '../../../..'), '/');
    eq(path.resolve('/'), '/');
  },
  'isin()'() {
    assert(path.isin('/tmp/test.obj', '/tmp'));
    assert(path.isin('/tmp/test.obj', '/'));
    assert(path.isin('/tmp', '/tmp'));
    assert(path.isin('/tmp', '/'));
    assert(!path.isin('/tmp/test.obj', '/var'));
  },
  'equal()'() {
    assert(path.equal('/tmp/test.obj', '///tmp////test.obj'));
    assert(path.equal('/./tmp/./test.obj', '///tmp////test.obj'));
    assert(path.equal('.////test.obj', './test.obj'));
    assert(path.equal('./test.obj', 'test.obj'));
  },
  'toArray()'() {
    eq(path.toArray('/tmp/test.obj').join('/'), '/tmp/test.obj');
    eq(path.toArray('./../../..') + '', '.,..,..,..');
  },
  'offsets()'() {},
  'lengths()'() {},
  'ranges()'() {},
});