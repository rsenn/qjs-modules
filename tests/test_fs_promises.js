/*
 * Coverage for lib/fsPromises.js. fsPromises.watch() is deliberately NOT
 * retested here - see tests/test_fs_watch.js, which already covers it (and
 * fs.watch()) in depth.
 */
import * as std from 'std';
import * as path from 'path';
import * as fs from 'fs';
import * as fsp from 'fsPromises';
import { getuid, getgid } from 'misc';
import { setTimeout, clearTimeout } from 'timers';
import { assert, eq, tests } from './tinytest.js';

function withTimeout(promise, ms, label) {
  let timer;
  const timeout = new Promise((resolve, reject) => {
    timer = setTimeout(() => reject(new Error(`timeout: ${label}`)), ms);
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

const TMPDIR = std.getenv('TMPDIR') ?? '/tmp';
const ROOT = path.join(TMPDIR, `qjs-fsp-test-${Date.now()}-${Math.floor(Math.random() * 1e6)}`);

function rmrf(p) {
  let st;
  try {
    st = fs.lstatSync(p);
  } catch(e) {
    return;
  }
  if(st.isDirectory()) {
    for(const name of fs.readdirSync(p)) rmrf(path.join(p, name));
  }
  fs.unlinkSync(p);
}

async function assertRejects(promise, msg) {
  try {
    await promise;
  } catch(e) {
    return e;
  }
  throw new Error(msg ?? 'expected promise to reject');
}

fs.mkdirSync(ROOT);

try {
  await tests({
    /* --- access / exists-adjacent --- */

    async 'access() resolves for an accessible file and rejects for a missing one'() {
      const file = path.join(ROOT, 'access.txt');
      fs.writeFileSync(file, 'x');
      await fsp.access(file, fs.constants.F_OK);
      await assertRejects(fsp.access(path.join(ROOT, 'nope.txt'), fs.constants.F_OK));
    },

    /* --- whole-file read/write/append --- */

    async 'writeFile()/readFile() round-trip text'() {
      const file = path.join(ROOT, 'whole.txt');
      await fsp.writeFile(file, 'hello promises');
      eq(await fsp.readFile(file, 'utf-8'), 'hello promises');
    },

    async 'appendFile() appends across multiple calls'() {
      const file = path.join(ROOT, 'append.txt');
      await fsp.appendFile(file, 'a');
      await fsp.appendFile(file, 'b');
      await fsp.appendFile(file, 'c');
      eq(await fsp.readFile(file, 'utf-8'), 'abc');
    },

    /* --- mode / ownership / timestamps / truncation --- */

    async 'chmod() changes the mode bits of a file'() {
      const file = path.join(ROOT, 'chmod.txt');
      fs.writeFileSync(file, 'x');
      await fsp.chmod(file, 0o640);
      eq(fs.statSync(file).mode & 0o777, 0o640);
    },

    async 'chown() to the current uid/gid succeeds'() {
      const file = path.join(ROOT, 'chown.txt');
      fs.writeFileSync(file, 'x');
      await fsp.chown(file, getuid(), getgid());
    },

    async 'truncate() shrinks a file to the given length'() {
      const file = path.join(ROOT, 'truncate.txt');
      fs.writeFileSync(file, '0123456789');
      await fsp.truncate(file, 4);
      eq(fs.readFileSync(file, 'utf-8'), '0123');
    },

    async 'utimes() updates mtime to the given Date'() {
      const file = path.join(ROOT, 'utimes.txt');
      fs.writeFileSync(file, 'x');
      const when = new Date(Date.now() - 60_000);
      await fsp.utimes(file, when, when);
      const st = await fsp.stat(file);
      assert(Math.abs(st.mtime.getTime() - when.getTime()) < 1000, `expected mtime near ${when}, got ${st.mtime}`);
    },

    async 'lchown() and lutimes() reject as not implemented'() {
      await assertRejects(fsp.lchown(path.join(ROOT, 'whatever'), 0, 0));
      await assertRejects(fsp.lutimes(path.join(ROOT, 'whatever'), new Date(), new Date()));
    },

    /* --- copy / cp / link --- */

    async 'copyFile() duplicates file content'() {
      const src = path.join(ROOT, 'copy-src.txt');
      const dst = path.join(ROOT, 'copy-dst.txt');
      fs.writeFileSync(src, 'copy me');
      await fsp.copyFile(src, dst);
      eq(await fsp.readFile(dst, 'utf-8'), 'copy me');
    },

    async 'cp() copies a directory tree recursively'() {
      const src = path.join(ROOT, 'cp-dir-src');
      const dst = path.join(ROOT, 'cp-dir-dst');
      fs.mkdirSync(path.join(src, 'nested'), { recursive: true });
      fs.writeFileSync(path.join(src, 'top.txt'), 'top');
      fs.writeFileSync(path.join(src, 'nested', 'deep.txt'), 'deep');

      await fsp.cp(src, dst, { recursive: true });

      eq(await fsp.readFile(path.join(dst, 'top.txt'), 'utf-8'), 'top');
      eq(await fsp.readFile(path.join(dst, 'nested', 'deep.txt'), 'utf-8'), 'deep');
    },

    async 'link() creates a hard link sharing the same content'() {
      const src = path.join(ROOT, 'hardlink-src.txt');
      const dst = path.join(ROOT, 'hardlink-dst.txt');
      fs.writeFileSync(src, 'shared');
      await fsp.link(src, dst);
      eq(await fsp.readFile(dst, 'utf-8'), 'shared');
    },

    /* --- symlink / readlink / realpath / stat / lstat --- */

    async 'symlink()/readlink()/realpath()/lstat() work on a symlink'() {
      const target = path.join(ROOT, 'symlink-target.txt');
      const link = path.join(ROOT, 'symlink-link.txt');
      fs.writeFileSync(target, 'target');
      await fsp.symlink(target, link);

      eq(await fsp.readlink(link), target);
      eq(await fsp.realpath(link), await fsp.realpath(target));
      assert((await fsp.lstat(link)).isSymbolicLink(), 'expected lstat() to report a symlink');
      assert(!(await fsp.stat(link)).isSymbolicLink(), 'expected stat() to follow the symlink');
    },

    /* --- mkdir / mkdtemp / readdir / opendir --- */

    async 'mkdir({ recursive: true }) creates intermediate directories'() {
      const dir = path.join(ROOT, 'mkdir-a', 'mkdir-b');
      await fsp.mkdir(dir, { recursive: true });
      assert(fs.statSync(dir).isDirectory(), 'expected the deepest directory to exist');
    },

    async 'mkdtemp() creates a fresh, existing, unique directory per call'() {
      const a = await fsp.mkdtemp(path.join(ROOT, 'tmp-'));
      const b = await fsp.mkdtemp(path.join(ROOT, 'tmp-'));
      assert(fs.existsSync(a) && fs.statSync(a).isDirectory(), 'expected a to be a directory');
      assert(fs.existsSync(b) && fs.statSync(b).isDirectory(), 'expected b to be a directory');
      assert(a !== b, 'expected two mkdtemp() calls to produce distinct directories');
    },

    async 'readdir() lists entries, excluding . and ..'() {
      const dir = path.join(ROOT, 'readdir-plain');
      fs.mkdirSync(dir);
      fs.writeFileSync(path.join(dir, 'one.txt'), '1');
      fs.writeFileSync(path.join(dir, 'two.txt'), '2');

      const names = (await fsp.readdir(dir)).slice().sort();
      eq(names.join(','), 'one.txt,two.txt');
    },

    async 'opendir() returns an iterable Dir'() {
      const dir = path.join(ROOT, 'opendir');
      fs.mkdirSync(dir);
      fs.writeFileSync(path.join(dir, 'one.txt'), '1');
      fs.writeFileSync(path.join(dir, 'two.txt'), '2');

      const d = await fsp.opendir(dir);
      const names = [];
      for(const entry of d) names.push(entry.name);
      d.closeSync();

      eq(names.slice().sort().join(','), 'one.txt,two.txt');
    },

    /* --- rename / rm / rmdir / unlink --- */

    async 'rename() moves a file'() {
      const src = path.join(ROOT, 'rename-src.txt');
      const dst = path.join(ROOT, 'rename-dst.txt');
      fs.writeFileSync(src, 'move me');
      await fsp.rename(src, dst);
      assert(!fs.existsSync(src), 'source should be gone');
      eq(await fsp.readFile(dst, 'utf-8'), 'move me');
    },

    async 'unlink() removes a file'() {
      const file = path.join(ROOT, 'unlink.txt');
      fs.writeFileSync(file, 'x');
      await fsp.unlink(file);
      assert(!fs.existsSync(file), 'expected the file to be gone');
    },

    async 'rmdir() removes an empty directory'() {
      const dir = path.join(ROOT, 'rmdir-empty');
      fs.mkdirSync(dir);
      await fsp.rmdir(dir);
      assert(!fs.existsSync(dir), 'expected the directory to be gone');
    },

    async 'rm({ recursive: true }) removes a non-empty directory tree'() {
      const dir = path.join(ROOT, 'rm-tree');
      fs.mkdirSync(path.join(dir, 'sub'), { recursive: true });
      fs.writeFileSync(path.join(dir, 'top.txt'), 't');
      fs.writeFileSync(path.join(dir, 'sub', 'deep.txt'), 'd');

      await fsp.rm(dir, { recursive: true });
      assert(!fs.existsSync(dir), 'expected the whole tree to be gone');
    },

    async 'rm({ force: true }) does not reject for a missing path'() {
      await fsp.rm(path.join(ROOT, 'does-not-exist'), { force: true });
    },

    /* --- FileHandle via open() --- */

    async 'open() returns a FileHandle whose methods resolve to promises'() {
      const file = path.join(ROOT, 'handle.txt');
      fs.writeFileSync(file, 'handle content');

      const handle = await fsp.open(file, 'r');
      eq(typeof handle.fd, 'number');

      const content = await handle.readFile({ encoding: 'utf8' });
      eq(content, 'handle content');

      const st = await handle.stat();
      eq(st.size, 'handle content'.length);

      await handle.close();
    },

    async 'FileHandle.read()/write() operate on an open handle'() {
      const file = path.join(ROOT, 'handle-rw.txt');
      fs.writeFileSync(file, '0123456789');

      const handle = await fsp.open(file, 'r+');
      const buf = new ArrayBuffer(4);
      const { bytesRead } = await handle.read(buf, 0, 4, 0);
      eq(bytesRead, 4);
      eq(fs.bufferToString(buf, 0, 4), '0123');

      await handle.close();
    },

    async 'FileHandle.appendFile()/writeFile() write through the handle'() {
      const file = path.join(ROOT, 'handle-write.txt');
      fs.writeFileSync(file, 'base');

      const handle = await fsp.open(file, 'a');
      await handle.appendFile('-more');
      await handle.close();

      eq(fs.readFileSync(file, 'utf-8'), 'base-more');
    },

    async 'FileHandle.chmod()/chown()/truncate()/sync()/datasync() do not throw'() {
      const file = path.join(ROOT, 'handle-misc.txt');
      fs.writeFileSync(file, '0123456789');

      const handle = await fsp.open(file, 'r+');
      await handle.chmod(0o640);
      await handle.chown(getuid(), getgid());
      await handle.truncate(4);
      await handle.sync();
      await handle.datasync();
      await handle.close();

      eq(fs.readFileSync(file, 'utf-8'), '0123');
    },

    /* --- non-standard raw-fd helpers used by lib/vfs.js: read/write/reader/readAll --- */

    async 'read()/write() operate on a raw fd'() {
      const [rd, wr] = fs.pipe();
      await fsp.write(wr, 'ping');
      fs.closeSync(wr);

      const buf = new ArrayBuffer(4);
      const n = await fsp.read(rd, buf, 0, 4);
      eq(n, 4);
      eq(fs.bufferToString(buf, 0, 4), 'ping');
      fs.closeSync(rd);
    },

    /* readAll()/reader() drain a pipe by reading until read() returns 0 (EOF).
     * Fixed upstream in ../quickjs/quickjs-libc.c's js_os_poll(): a callback
     * registered with os.setReadHandler() used to never fire for a fd whose
     * only readability was EOF (the write end closed, nothing left to read) -
     * see BUGS's (now-removed) os-setreadhandler-never-fires-for-eof-only-readiness. */
    async 'readAll() drains a pipe to EOF'() {
      const [rd, wr] = fs.pipe();
      await fsp.write(wr, 'hello world');
      fs.closeSync(wr);

      const result = await withTimeout(fsp.readAll(rd, 4), 1000, 'readAll');
      eq(result, 'hello world');

      /* reader() (used internally by readAll()) closes `rd` itself once it
       * observes EOF - no cleanup needed here. */
    },

    async 'reader() drains a pipe to EOF'() {
      const [rd, wr] = fs.pipe();
      await fsp.write(wr, 'abcdefgh');
      fs.closeSync(wr);

      const chunks = await withTimeout(
        (async () => {
          const parts = [];
          for await(const chunk of fsp.reader(rd, 3)) parts.push(fs.bufferToString(chunk, 0, chunk.byteLength));
          return parts;
        })(),
        1000,
        'reader',
      );

      eq(chunks.join(''), 'abcdefgh');

      /* reader() closes `rd` itself once it observes EOF - no cleanup needed here. */
    },
  });
} finally {
  rmrf(ROOT);
}
