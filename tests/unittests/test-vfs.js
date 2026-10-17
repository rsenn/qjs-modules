import * as std from 'std';
import * as path from 'path';
import * as fs from 'fs';
import { Archive, ArchiveEntry } from 'archive';
import { TextDecoder, TextEncoder } from 'textcode';
import vfs, { ArchiveProvider, ComposableProvider, MemoryProvider, RealFSProvider, VirtualFileSystem, VirtualProvider, create } from '../../lib/vfs.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

const TMPDIR = std.getenv('TMPDIR') ?? '/tmp';
const ROOT = path.join(TMPDIR, `qjs-vfs-test-${Date.now()}-${Math.floor(Math.random() * 1e6)}`);
const REAL = path.join(ROOT, 'real');
const TAR_PATH = path.join(ROOT, 'test.tar');
const json = JSON.stringify;

function rmrf(p) {
  let st;
  try {
    st = fs.lstatSync(p);
  } catch(e) {
    return;
  }
  if(st.isDirectory()) for(const name of fs.readdirSync(p)) if(name != '.' && name != '..') rmrf(path.join(p, name));
  st.isDirectory() ? fs.rmdirSync(p) : fs.unlinkSync(p);
}

function throws(fn, code) {
  try {
    fn();
  } catch(e) {
    eq(e.code, code, `expected ${code}, got ${e.code}: ${e.message}`);
    return e;
  }
  throw new Error(`expected ${code}, nothing thrown`);
}

fs.mkdirSync(ROOT);
fs.mkdirSync(REAL);
fs.mkdirSync(path.join(REAL, 'sub'));
fs.writeFileSync(path.join(REAL, 'file1.txt'), 'real-file1');
fs.writeFileSync(path.join(REAL, 'sub', 'nested.txt'), 'real-nested');

{
  const ar = Archive.write(TAR_PATH);
  for(const [name, content] of [
    ['one.txt', 'content-one'],
    ['dir/two.txt', 'content-two'],
  ]) {
    ar.write(new ArchiveEntry(name, { type: 'file', perm: 0o644, size: content.length }));
    ar.write(content);
  }
  ar.close();
}

try {
  await tests({
    /* --- module surface --- */

    'default export and named exports agree'() {
      assert(vfs.create === create);
      assert(vfs.MemoryProvider === MemoryProvider);
      assert(create() instanceof VirtualFileSystem);
      assert(create().provider instanceof MemoryProvider);
      throws(() => create({}), undefined);
    },

    /* --- MemoryProvider through VirtualFileSystem --- */

    'write, read and encodings'() {
      const v = create();
      v.writeFileSync('/a.txt', 'héllo');
      eq(v.readFileSync('/a.txt', 'utf8'), 'héllo');
      eq(v.readFileSync('/a.txt', { encoding: 'utf-8' }), 'héllo');
      const b = v.readFileSync('/a.txt');
      assert(b instanceof Uint8Array);
      eq(b.length, 6);
      v.writeFileSync('/b.bin', new Uint8Array([1, 2, 3]));
      eq(json([...v.readFileSync('/b.bin')]), '[1,2,3]');
    },

    'paths are normalized'() {
      const v = create();
      v.mkdirSync('/d');
      v.writeFileSync('d/../d/./x', '1');
      eq(v.readFileSync('//d//x', 'utf8'), '1');
      eq(v.readFileSync('/../../d/x', 'utf8'), '1');
    },

    'appendFileSync and flag a'() {
      const v = create();
      v.appendFileSync('/a', 'x');
      v.appendFileSync('/a', 'y');
      v.writeFileSync('/a', 'z', { flag: 'a' });
      eq(v.readFileSync('/a', 'utf8'), 'xyz');
      throws(() => v.writeFileSync('/a', 'q', { flag: 'wx' }), 'EEXIST');
    },

    'stat returns fs.Stats with sizes, types and ino'() {
      const v = create();
      v.mkdirSync('/d');
      v.writeFileSync('/d/f', '12345');
      const st = v.statSync('/d/f');
      assert(st instanceof fs.Stats);
      assert(st.isFile() && !st.isDirectory());
      eq(st.size, 5);
      assert(v.statSync('/d').isDirectory());
      assert(st.mtime instanceof Date);
      assert(st.ino != v.statSync('/d').ino);
      throws(() => v.statSync('/nope'), 'ENOENT');
      eq(v.statSync('/nope', { throwIfNoEntry: false }), undefined);
    },

    'errors carry code and path'() {
      const v = create();
      const e = throws(() => v.readFileSync('/missing'), 'ENOENT');
      eq(e.path, '/missing');
      v.writeFileSync('/f', '');
      throws(() => v.mkdirSync('/f/x'), 'ENOTDIR');
      throws(() => v.readFileSync('/'), 'EISDIR');
      throws(() => v.mkdirSync('/'), 'EEXIST');
    },

    'mkdirSync recursive returns first created dir'() {
      const v = create();
      eq(v.mkdirSync('/a/b/c', { recursive: true }), '/a');
      eq(v.mkdirSync('/a/b/c', { recursive: true }), undefined);
      eq(v.mkdirSync('/a/b/d', { recursive: true }), '/a/b/d');
      throws(() => v.mkdirSync('/x/y'), 'ENOENT');
      throws(() => v.mkdirSync('/a'), 'EEXIST');
    },

    'readdirSync: names, withFileTypes, recursive'() {
      const v = create();
      v.mkdirSync('/d/sub', { recursive: true });
      v.writeFileSync('/d/f', '');
      v.writeFileSync('/d/sub/g', '');
      eq(json(v.readdirSync('/d').sort()), '["f","sub"]');
      const ents = v.readdirSync('/d', { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1));
      eq(ents[0].name, 'f');
      assert(ents[0].isFile() && ents[1].isDirectory());
      eq(ents[0].parentPath, '/d');
      eq(json(v.readdirSync('/d', { recursive: true }).sort()), '["f","sub","sub/g"]');
      throws(() => v.readdirSync('/d/f'), 'ENOTDIR');
    },

    'rmdir, unlink, rm'() {
      const v = create();
      v.mkdirSync('/d/e', { recursive: true });
      v.writeFileSync('/d/f', '');
      throws(() => v.rmdirSync('/d'), 'ENOTEMPTY');
      throws(() => v.unlinkSync('/d'), 'EISDIR');
      v.unlinkSync('/d/f');
      v.rmdirSync('/d/e');
      assert(!v.existsSync('/d/f') && !v.existsSync('/d/e'));
      v.writeFileSync('/d/e', 'x');
      throws(() => v.rmSync('/d'), 'EISDIR');
      v.rmSync('/d', { recursive: true });
      assert(!v.existsSync('/d'));
      v.rmSync('/d', { force: true });
      throws(() => v.rmSync('/d'), 'ENOENT');
    },

    'rename files and directories'() {
      const v = create();
      v.mkdirSync('/a/b', { recursive: true });
      v.writeFileSync('/a/b/f', 'x');
      v.renameSync('/a', '/c');
      eq(v.readFileSync('/c/b/f', 'utf8'), 'x');
      assert(!v.existsSync('/a'));
      v.writeFileSync('/g', 'new');
      v.renameSync('/g', '/c/b/f');
      eq(v.readFileSync('/c/b/f', 'utf8'), 'new');
      throws(() => v.renameSync('/c', '/c/b/inside'), 'EINVAL');
      throws(() => v.renameSync('/missing', '/x'), 'ENOENT');
    },

    'copyFileSync, truncateSync'() {
      const v = create();
      v.writeFileSync('/a', 'abcdef');
      v.copyFileSync('/a', '/b');
      eq(v.readFileSync('/b', 'utf8'), 'abcdef');
      throws(() => v.copyFileSync('/a', '/b', fs.constants.COPYFILE_EXCL), 'EEXIST');
      v.truncateSync('/b', 3);
      eq(v.readFileSync('/b', 'utf8'), 'abc');
      v.truncateSync('/b', 5);
      eq(v.readFileSync('/b').length, 5);
    },

    'symlinks: readlink, stat vs lstat, loops, realpath'() {
      const v = create();
      v.mkdirSync('/real');
      v.writeFileSync('/real/f', 'data');
      v.symlinkSync('/real', '/link');
      v.symlinkSync('f', '/real/rel');
      eq(v.readlinkSync('/link'), '/real');
      eq(v.readFileSync('/link/f', 'utf8'), 'data');
      eq(v.readFileSync('/real/rel', 'utf8'), 'data');
      assert(v.lstatSync('/link').isSymbolicLink());
      assert(v.statSync('/link').isDirectory());
      eq(v.realpathSync('/link/rel'), '/real/f');
      v.symlinkSync('/l2', '/l1');
      v.symlinkSync('/l1', '/l2');
      const loop = throws(() => v.statSync('/l1'), null); // syscallerror has no ELOOP name
      eq(loop.errno, 40);
      throws(() => v.readlinkSync('/real/f'), 'EINVAL');
    },

    'chmod, utimes, mkdtemp, access'() {
      const v = create();
      v.writeFileSync('/f', '');
      v.chmodSync('/f', 0o600);
      eq(v.statSync('/f').mode & 0o777, 0o600);
      v.utimesSync('/f', 1000, 2000);
      eq(v.statSync('/f').mtimeMs, 2000000);
      const d = v.mkdtempSync('/tmp-');
      assert(d.startsWith('/tmp-') && v.statSync(d).isDirectory());
      v.accessSync('/f');
      throws(() => v.accessSync('/nope'), 'ENOENT');
    },

    'read-only memory provider throws EROFS'() {
      const p = new MemoryProvider();
      const v = create(p);
      v.writeFileSync('/seed', 'initial');
      p.setReadOnly();
      assert(v.readonly);
      throws(() => v.writeFileSync('/x', 'fail'), 'EROFS');
      throws(() => v.mkdirSync('/d'), 'EROFS');
      throws(() => v.unlinkSync('/seed'), 'EROFS');
      throws(() => v.openSync('/seed', 'r+'), 'EROFS');
      eq(v.readFileSync('/seed', 'utf8'), 'initial');
    },

    /* --- file descriptors --- */

    'fd: write, read back through another fd and a path read'() {
      const v = create();
      const fd = v.openSync('/f', 'w+');
      assert(fd >= 10000);
      eq(v.writeSync(fd, 'hello '), 6);
      eq(v.writeSync(fd, new TextEncoder().encode('world!!'), 0, 5), 5);
      eq(v.readFileSync('/f', 'utf8'), 'hello world', 'pending fd writes are visible to path calls');
      const buf = new Uint8Array(5);
      eq(v.readSync(fd, buf, 0, 5, 6), 5);
      eq(new TextDecoder().decode(buf), 'world');
      eq(v.fstatSync(fd).size, 11);
      v.closeSync(fd);
      throws(() => v.closeSync(fd), 'EBADF');
    },

    'fd: sequential reads move the position, 0 at EOF'() {
      const v = create();
      v.writeFileSync('/f', 'abcdef');
      const fd = v.openSync('/f');
      const buf = new Uint8Array(4);
      eq(v.readSync(fd, buf), 4);
      eq(v.readSync(fd, buf), 2);
      eq(v.readSync(fd, buf), 0);
      throws(() => v.writeSync(fd, 'x'), 'EBADF');
      v.closeSync(fd);
    },

    'fd: flags w truncates, a appends, x is exclusive, missing is ENOENT'() {
      const v = create();
      v.writeFileSync('/f', 'old');
      let fd = v.openSync('/f', 'a');
      v.writeSync(fd, '+new');
      v.closeSync(fd);
      eq(v.readFileSync('/f', 'utf8'), 'old+new');
      fd = v.openSync('/f', 'w');
      v.closeSync(fd);
      eq(v.readFileSync('/f').length, 0);
      throws(() => v.openSync('/f', 'wx'), 'EEXIST');
      throws(() => v.openSync('/nope', 'r'), 'ENOENT');
      throws(() => v.openSync('/f', 'zz'), undefined);
      fd = v.openSync('/num', fs.constants.O_CREAT | fs.constants.O_WRONLY);
      v.writeSync(fd, 'n');
      v.closeSync(fd);
      eq(v.readFileSync('/num', 'utf8'), 'n');
    },

    'fd: ftruncateSync'() {
      const v = create();
      const fd = v.openSync('/f', 'w+');
      v.writeSync(fd, 'abcdef');
      v.ftruncateSync(fd, 2);
      v.closeSync(fd);
      eq(v.readFileSync('/f', 'utf8'), 'ab');
    },

    /* --- callback and promise forms --- */

    async 'callbacks'() {
      const v = create();
      await new Promise((res, rej) => v.writeFile('/a', 'cb', e => (e ? rej(e) : res())));
      const data = await new Promise((res, rej) => v.readFile('/a', 'utf8', (e, d) => (e ? rej(e) : res(d))));
      eq(data, 'cb');
      const err = await new Promise(res => v.stat('/nope', e => res(e)));
      eq(err.code, 'ENOENT');
      const fd = await new Promise(res => v.open('/a', 'r', (e, fd) => res(fd)));
      const buf = new Uint8Array(2);
      const [n, b] = await new Promise(res => v.read(fd, buf, 0, 2, null, (e, n, b) => res([n, b])));
      eq(n, 2);
      assert(b === buf);
      throws(() => v.readFile('/a'), undefined);
    },

    async 'promises'() {
      const v = create();
      await v.promises.mkdir('/d/e', { recursive: true });
      await v.promises.writeFile('/d/e/f', 'p');
      eq(await v.promises.readFile('/d/e/f', 'utf8'), 'p');
      eq(json(await v.promises.readdir('/d')), '["e"]');
      assert((await v.promises.stat('/d')).isDirectory());
      let code;
      try {
        await v.promises.readFile('/nope');
      } catch(e) {
        code = e.code;
      }
      eq(code, 'ENOENT');
      assert(v.promises === v.promises);
    },

    /* --- custom provider: the primitives are enough --- */

    'a VirtualProvider subclass with only the primitives gets the whole API'() {
      class StaticProvider extends VirtualProvider {
        get readonly() {
          return true;
        }
        statSync(p) {
          const mk = mode => ({ isDirectory: () => mode == 1, isFile: () => mode == 2, isSymbolicLink: () => false, mode, size: 3 });
          if(p == '/') return mk(1);
          if(p == '/hi') return mk(2);
          throw Object.assign(new Error('nf'), { code: 'ENOENT' });
        }
        readdirSync(p) {
          return ['hi'];
        }
        readFileSync(p) {
          return new TextEncoder().encode('hey');
        }
      }
      const v = create(new StaticProvider());
      eq(v.readFileSync('/hi', 'utf8'), 'hey');
      assert(v.existsSync('/hi') && !v.existsSync('/no'));
      eq(json(v.readdirSync('/')), '["hi"]');
      throws(() => v.mkdirSync('/d'), 'EROFS');
      throws(() => v.statSync('/no'), 'ENOENT');
      class Bare extends VirtualProvider {}
      throws(() => create(new Bare()).statSync('/'), 'ERR_METHOD_NOT_IMPLEMENTED');
    },

    /* --- RealFSProvider --- */

    'RealFSProvider maps the root and reads/writes the real fs'() {
      const v = create(new RealFSProvider(REAL));
      eq(v.readFileSync('/file1.txt', 'utf8'), 'real-file1');
      eq(v.readFileSync('/../../sub/nested.txt', 'utf8'), 'real-nested', '.. stops at the root');
      assert(v.statSync('/sub').isDirectory());
      v.writeFileSync('/new.txt', 'made-by-vfs');
      eq(fs.readFileSync(path.join(REAL, 'new.txt'), 'utf-8'), 'made-by-vfs');
      v.mkdirSync('/m/n', { recursive: true });
      assert(fs.statSync(path.join(REAL, 'm', 'n')).isDirectory());
      v.renameSync('/new.txt', '/m/moved.txt');
      eq(v.readFileSync('/m/moved.txt', 'utf8'), 'made-by-vfs');
      v.rmSync('/m', { recursive: true });
      assert(!fs.existsSync(path.join(REAL, 'm')));
      eq(json(v.readdirSync('/').sort()), '["file1.txt","sub"]');
      throws(() => v.readFileSync('/nope'), 'ENOENT');
      throws(() => new RealFSProvider(''), undefined);
    },

    /* --- ComposableProvider --- */

    'ComposableProvider: first layer shadows, readdir merges'() {
      const top = new MemoryProvider();
      const v = create(new ComposableProvider([top, new RealFSProvider(REAL)]));
      eq(v.readFileSync('/file1.txt', 'utf8'), 'real-file1');
      v.writeFileSync('/file1.txt', 'shadow');
      eq(v.readFileSync('/file1.txt', 'utf8'), 'shadow');
      eq(fs.readFileSync(path.join(REAL, 'file1.txt'), 'utf-8'), 'real-file1', 'lower layer untouched');
      v.writeFileSync('/only-top', 't');
      eq(json(v.readdirSync('/').sort()), '["file1.txt","only-top","sub"]');
      assert(v.statSync('/sub').isDirectory());
    },

    'ComposableProvider: write into a lower directory creates parents in the top layer'() {
      const top = new MemoryProvider();
      const v = create(new ComposableProvider([top, new RealFSProvider(REAL)]));
      v.writeFileSync('/sub/added.txt', 'a');
      eq(json(v.readdirSync('/sub').sort()), '["added.txt","nested.txt"]');
      assert(top.existsSync('/sub/added.txt'));
      assert(!fs.existsSync(path.join(REAL, 'sub', 'added.txt')));
    },

    'ComposableProvider: removing hides lower copies, writing brings them back'() {
      const v = create(new ComposableProvider([new MemoryProvider(), new RealFSProvider(REAL)]));
      v.unlinkSync('/file1.txt');
      assert(!v.existsSync('/file1.txt'));
      throws(() => v.readFileSync('/file1.txt'), 'ENOENT');
      assert(!v.readdirSync('/').includes('file1.txt'));
      assert(fs.existsSync(path.join(REAL, 'file1.txt')));
      v.writeFileSync('/file1.txt', 'again');
      eq(v.readFileSync('/file1.txt', 'utf8'), 'again');
      throws(() => v.rmdirSync('/sub'), 'ENOTEMPTY');
      v.rmSync('/sub', { recursive: true });
      assert(!v.existsSync('/sub') && !v.existsSync('/sub/nested.txt'));
      assert(fs.existsSync(path.join(REAL, 'sub', 'nested.txt')));
    },

    'ComposableProvider: rename copies up from a lower layer'() {
      const top = new MemoryProvider();
      const v = create(new ComposableProvider([top, new RealFSProvider(REAL)]));
      v.renameSync('/sub', '/moved');
      eq(v.readFileSync('/moved/nested.txt', 'utf8'), 'real-nested');
      assert(!v.existsSync('/sub'));
      assert(top.existsSync('/moved/nested.txt'));
      assert(fs.existsSync(path.join(REAL, 'sub', 'nested.txt')));
    },

    'ComposableProvider: argument checks and providers copy'() {
      throws(() => new ComposableProvider([]), undefined);
      const p = new ComposableProvider([new MemoryProvider()]);
      p.providers.pop();
      eq(p.providers.length, 1);
    },

    /* --- ArchiveProvider --- */

    'ArchiveProvider: read-only view of a tar file'() {
      const v = create(new ArchiveProvider(TAR_PATH));
      assert(v.readonly);
      eq(json(v.readdirSync('/').sort()), '["dir","one.txt"]');
      eq(json(v.readdirSync('/dir')), '["two.txt"]');
      eq(v.readFileSync('/one.txt', 'utf8'), 'content-one');
      eq(v.readFileSync('/dir/two.txt', 'utf8'), 'content-two');
      eq(v.statSync('/one.txt').size, 11);
      assert(v.statSync('/dir').isDirectory());
      assert(v.statSync('/one.txt').isFile());
      assert(!v.existsSync('/nope'));
      throws(() => v.writeFileSync('/x', ''), 'EROFS');
      throws(() => v.readFileSync('/dir'), 'EISDIR');
      throws(() => v.statSync('/one.txt/x'), 'ENOTDIR');
    },

    'ArchiveProvider composes under a writable layer'() {
      const v = create(new ComposableProvider([new MemoryProvider(), new ArchiveProvider(TAR_PATH)]));
      v.writeFileSync('/one.txt', 'patched');
      v.writeFileSync('/dir/three.txt', 'three');
      eq(v.readFileSync('/one.txt', 'utf8'), 'patched');
      eq(v.readFileSync('/dir/two.txt', 'utf8'), 'content-two');
      eq(json(v.readdirSync('/dir').sort()), '["three.txt","two.txt"]');
    },
  });
} finally {
  rmrf(ROOT);
}
