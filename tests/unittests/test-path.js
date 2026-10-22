import * as path from 'path';
import * as std from 'std';
import * as fs from 'fs';
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
  'fnmatch() bracket expressions and classes'() {
    const m = (p, s, f = 0) => path.fnmatch(p, s, f) === 0;

    assert(m('[abc]', 'b') && !m('[abc]', 'd') && m('[!abc]', 'd') && m('[a-c]', 'b'));
    assert(m('[]a]', ']') && m('[\\]]', ']') && m('[', '['));
    assert(m('[[:alpha:]]', 'a') && !m('[[:alpha:]]', '1') && m('[[:digit:]]*', '9z'));
    assert(m('[[:upper:][:digit:]]', 'A') && m('[[:upper:][:digit:]]', '7') && !m('[[:upper:][:digit:]]', 'a'));
    assert(!m('[[:foo:]]', 'a'), 'an unknown class matches nothing');
    assert(m('*', 'a/b') && !m('*', 'a/b', path.FNM_PATHNAME));
    assert(!m('*', '.a', path.FNM_PERIOD) && m('.*', '.a', path.FNM_PERIOD));
  },
  'getcwd()'() {
    eq(path.getcwd(), path.resolve('.'));
  },
  'gethome()'() {
    eq(std.getenv('HOME'), path.gethome());
  },
  'isAbsolute()'() {
    assert(path.isAbsolute('/tmp/test/'));
    assert(!path.isAbsolute('tmp/test/'));
  },
  'isDirectory()'() {
    assert(path.isDirectory('.'));
    assert(!path.isDirectory(scriptArgs[0]));
  },
  'isFile()'() {
    assert(!path.isFile('.'));
    assert(path.isFile(scriptArgs[0]));
  },
  'isSymlink()'() {
    assert(path.isSymlink('/dev/fd'));
    assert(!path.isSymlink('.'));
  },
  'type tests and exists() follow symlinks, only isSymlink() does not'() {
    const dir = fs.mkdtempSync(path.join(std.getenv('TMPDIR') ?? '/tmp', 'qjs-path-'));
    const [d, f, ld, lf, bad] = ['d', 'f', 'ld', 'lf', 'bad'].map(n => path.join(dir, n));

    try {
      fs.mkdirSync(d);
      fs.writeFileSync(f, 'x');
      fs.symlinkSync(d, ld);
      fs.symlinkSync(f, lf);
      fs.symlinkSync(path.join(dir, 'missing'), bad);

      assert(path.isDirectory(d) && path.isDirectory(ld));
      assert(path.isFile(f) && path.isFile(lf));
      assert(!path.isFile(ld) && !path.isDirectory(lf));
      assert(path.isSymlink(ld) && path.isSymlink(lf) && !path.isSymlink(d) && !path.isSymlink(f));
      assert(path.isSymlink(bad) && !path.isFile(bad) && !path.isDirectory(bad));
      assert(!path.isDirectory(path.join(dir, 'missing')));
      assert(path.exists(ld) && path.exists(lf) && !path.exists(bad), 'a dangling symlink does not exist');

      for(const n of ['isDirectory', 'isFile', 'isSymlink']) {
        let err;
        try {
          path[n]();
        } catch(e) {
          err = e;
        }
        assert(err instanceof TypeError, n + '() without an argument throws TypeError');
      }
    } finally {
      for(const n of [ld, lf, bad, f]) fs.unlinkSync(n);
      fs.rmdirSync(d);
      fs.rmdirSync(dir);
    }
  },
  'skip()'() {
    eq(path.skip('/tmp/test/'), 1);
    eq(path.skip('/tmp/test/', 1), 5);
    eq(path.skip('/tmp/test/', 5), -1);
  },
  'realpath()'() {
    eq(path.realpath('/proc/self/cwd'), path.getcwd());
  },
  'relative()'() {
    const base = path.basename(path.getcwd());

    eq(path.relative('..', '.'), path.basename(path.getcwd()));
    eq(path.relative('.', '..'), '..');

    // differs from Node.js before the fix (api-compatibility-nodejs.md, B)
    eq(path.relative('/a/b', '/a/b'), '');
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
  'offsets()'() {},
  'lengths()'() {},
  'ranges()'() {},
});