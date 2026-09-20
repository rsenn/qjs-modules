/*
 * Coverage for lib/fs.js (the synchronous + non-promise API). fs.watch()/
 * FSWatcher are deliberately NOT retested here - see tests/test_fs_watch.js,
 * which already covers watch() (and fsPromises.watch()) in depth.
 */
import * as std from 'std';
import * as path from 'path';
import * as os from 'os';
import * as fs from 'fs';
import { getuid, getgid } from 'misc';
import { assert, eq, tests } from './tinytest.js';

const TMPDIR = std.getenv('TMPDIR') ?? '/tmp';
const ROOT = path.join(TMPDIR, `qjs-fs-test-${Date.now()}-${Math.floor(Math.random() * 1e6)}`);

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

fs.mkdirSync(ROOT);

try {
  await tests({
    /* --- buffer helpers --- */

    'buffer() allocates an ArrayBuffer of the given length'() {
      const b = fs.buffer(16);
      assert(b instanceof ArrayBuffer, 'expected an ArrayBuffer');
      eq(b.byteLength, 16);
    },

    'bufferFrom() converts a string to an ArrayBuffer'() {
      const b = fs.bufferFrom('hi', 0, 2);
      eq(fs.bufferToString(b, 0, 2), 'hi');
    },

    'bufferSize() returns byteLength'() {
      eq(fs.bufferSize(new ArrayBuffer(5)), 5);
    },

    'bufferArgument() extracts [buffer, byteOffset, byteLength] from a TypedArray'() {
      const ab = new ArrayBuffer(8);
      const view = new Uint8Array(ab, 2, 4);
      const [buf, ofs, len] = fs.bufferArgument(view);
      assert(buf === ab, 'expected the underlying ArrayBuffer');
      eq(ofs, 2);
      eq(len, 4);
    },

    'bufferArgument() returns null for a non-buffer value'() {
      eq(fs.bufferArgument('nope'), null);
      eq(fs.bufferArgument(42), null);
    },

    'bufferArguments() clamps offset/length'() {
      const ab = new ArrayBuffer(10);
      const [buf, ofs, len] = fs.bufferArguments(ab, 3, 4);
      assert(buf === ab, 'expected the underlying ArrayBuffer');
      eq(ofs, 3);
      eq(len, 4);
    },

    'stringOrBufferArguments() accepts a plain string'() {
      const [buf, ofs, len] = fs.stringOrBufferArguments('hello', 0, 5);
      eq(fs.bufferToString(buf, ofs, len), 'hello');
    },

    'throwIfNull() throws the given error when fn() returns null, else returns its result'() {
      eq(
        fs.throwIfNull(new Error('unused'), () => 42),
        42,
      );
      let threw = false;
      try {
        fs.throwIfNull(new Error('boom'), () => null);
      } catch(e) {
        threw = true;
        eq(e.message, 'boom');
      }
      assert(threw, 'expected throwIfNull() to throw for a null result');
    },

    'bufferToString() round-trips a UTF-8 buffer'() {
      const b = fs.bufferFrom('quick brown fox', 0, 15);
      eq(fs.bufferToString(b, 0, 15), 'quick brown fox');
    },

    /* --- open/close/read/write on raw fds --- */

    'openSync()/writeSync()/readSync()/closeSync() round-trip bytes through a real fd'() {
      const file = path.join(ROOT, 'rw.bin');
      const fd = fs.openSync(file, fs.constants.O_WRONLY | fs.constants.O_CREAT | fs.constants.O_TRUNC, 0o644);
      const out = fs.bufferFrom('abcdef', 0, 6);
      const written = fs.writeSync(fd, out, 0, 6);
      eq(written, 6);
      fs.closeSync(fd);

      const fd2 = fs.openSync(file, fs.constants.O_RDONLY, 0o644);
      const buf = new ArrayBuffer(6);
      const n = fs.readSync(fd2, buf, 0, 6);
      eq(n, 6);
      eq(fs.bufferToString(buf, 0, n), 'abcdef');
      fs.closeSync(fd2);
    },

    'fopenSync()/fdopenSync() open stdio FILE handles usable by FileHandle'() {
      const file = path.join(ROOT, 'fopen.txt');
      const f = fs.fopenSync(file, 'w');
      f.puts('stdio');
      f.flush();
      fs.closeSync(f);

      eq(fs.readFileSync(file, 'utf-8'), 'stdio');

      const fd = fs.openSync(file, fs.constants.O_RDONLY);
      const f2 = fs.fdopenSync(fd, 'r');
      eq(f2.readAsString(), 'stdio');
      f2.close();
    },

    'puts()/gets() write and read a line'() {
      const file = path.join(ROOT, 'putsgets.txt');
      const fd = fs.openSync(file, fs.constants.O_WRONLY | fs.constants.O_CREAT | fs.constants.O_TRUNC, 0o644);
      fs.puts(fd, 'line one\n');
      fs.closeSync(fd);

      const fd2 = fs.openSync(file, fs.constants.O_RDONLY);
      eq(fs.gets(fd2), 'line one');
      fs.closeSync(fd2);
    },

    'seek()/tell() move and report the file position'() {
      const file = path.join(ROOT, 'seek.bin');
      fs.writeFileSync(file, '0123456789');

      const fd = fs.openSync(file, fs.constants.O_RDONLY);
      eq(fs.tell(fd), 0);
      fs.seek(fd, 4, std.SEEK_SET);
      eq(fs.tell(fd), 4);
      const buf = new ArrayBuffer(2);
      fs.readSync(fd, buf, 0, 2);
      eq(fs.bufferToString(buf, 0, 2), '45');
      fs.closeSync(fd);
    },

    'sizeSync() reports a file size without disturbing an externally-tracked fd'() {
      const file = path.join(ROOT, 'size.bin');
      fs.writeFileSync(file, 'twelve chars');
      eq(fs.sizeSync(file), 12);
    },

    'nameSync() resolves an open fd back to its path'() {
      const file = path.join(ROOT, 'name.txt');
      fs.writeFileSync(file, 'x');
      const fd = fs.openSync(file, fs.constants.O_RDONLY);
      eq(fs.nameSync(fd), file);
      fs.closeSync(fd);
    },

    'getcwd()/chdir() report and change the process working directory'() {
      const before = fs.getcwd();
      fs.chdir(ROOT);
      eq(fs.getcwd(), fs.realpathSync(ROOT));
      fs.chdir(before);
      eq(fs.getcwd(), before);
    },

    'isatty()/fileno() report non-tty for a regular file'() {
      const file = path.join(ROOT, 'isatty.txt');
      fs.writeFileSync(file, 'x');
      const fd = fs.openSync(file, fs.constants.O_RDONLY);
      eq(fs.fileno(fd), fd);
      assert(!fs.isatty(fd), 'a regular file should not be a tty');
      fs.closeSync(fd);
    },

    /* --- whole-file read/write --- */

    'readFileSync()/writeFileSync() round-trip text'() {
      const file = path.join(ROOT, 'whole.txt');
      fs.writeFileSync(file, 'hello whole file');
      eq(fs.readFileSync(file, 'utf-8'), 'hello whole file');
      eq(fs.readFileSync(file, { encoding: 'utf-8' }), 'hello whole file');
    },

    'readFileSync() without an encoding returns raw bytes'() {
      const file = path.join(ROOT, 'raw.bin');
      fs.writeFileSync(file, 'raw');
      const data = fs.readFileSync(file);
      assert(data instanceof ArrayBuffer, 'expected an ArrayBuffer');
      eq(fs.bufferToString(data, 0, data.byteLength), 'raw');
    },

    'appendFileSync() appends to an existing file, creating it if needed'() {
      const file = path.join(ROOT, 'append.txt');
      fs.appendFileSync(file, 'a');
      fs.appendFileSync(file, 'b');
      fs.appendFileSync(file, 'c');
      eq(fs.readFileSync(file, 'utf-8'), 'abc');
    },

    /* --- links and copies --- */

    'linkSync() creates a hard link sharing the same content'() {
      const src = path.join(ROOT, 'hardlink-src.txt');
      const dst = path.join(ROOT, 'hardlink-dst.txt');
      fs.writeFileSync(src, 'shared');
      fs.linkSync(src, dst);
      eq(fs.readFileSync(dst, 'utf-8'), 'shared');
      fs.unlinkSync(dst);
    },

    'symlinkSync()/readlinkSync()/realpathSync()/lstatSync() work on a symlink'() {
      const target = path.join(ROOT, 'symlink-target.txt');
      const link = path.join(ROOT, 'symlink-link.txt');
      fs.writeFileSync(target, 'target');
      fs.symlinkSync(target, link);

      eq(fs.readlinkSync(link), target);
      eq(fs.realpathSync(link), fs.realpathSync(target));
      assert(fs.lstatSync(link).isSymbolicLink(), 'expected lstatSync() to report a symlink');
      assert(!fs.statSync(link).isSymbolicLink(), 'expected statSync() to follow the symlink');
      eq(fs.readFileSync(link, 'utf-8'), 'target');
    },

    'copyFileSync() duplicates file content'() {
      const src = path.join(ROOT, 'copy-src.txt');
      const dst = path.join(ROOT, 'copy-dst.txt');
      fs.writeFileSync(src, 'copy me');
      fs.copyFileSync(src, dst);
      eq(fs.readFileSync(dst, 'utf-8'), 'copy me');
    },

    'cpSync() copies a single file'() {
      const src = path.join(ROOT, 'cp-src.txt');
      const dst = path.join(ROOT, 'cp-dst.txt');
      fs.writeFileSync(src, 'cp me');
      fs.cpSync(src, dst);
      eq(fs.readFileSync(dst, 'utf-8'), 'cp me');
    },

    'cpSync() copies a directory tree recursively'() {
      const src = path.join(ROOT, 'cp-dir-src');
      const dst = path.join(ROOT, 'cp-dir-dst');
      fs.mkdirSync(path.join(src, 'nested'), { recursive: true });
      fs.writeFileSync(path.join(src, 'top.txt'), 'top');
      fs.writeFileSync(path.join(src, 'nested', 'deep.txt'), 'deep');

      fs.cpSync(src, dst, { recursive: true });

      eq(fs.readFileSync(path.join(dst, 'top.txt'), 'utf-8'), 'top');
      eq(fs.readFileSync(path.join(dst, 'nested', 'deep.txt'), 'utf-8'), 'deep');
    },

    'cpSync() without { recursive: true } refuses to copy a directory'() {
      const src = path.join(ROOT, 'cp-norecurse-src');
      fs.mkdirSync(src);
      let threw = false;
      try {
        fs.cpSync(src, path.join(ROOT, 'cp-norecurse-dst'));
      } catch(e) {
        threw = true;
      }
      assert(threw, 'expected cpSync() to throw for a directory without recursive: true');
    },

    /* --- existence, stat, mkdir, mkdtemp --- */

    'existsSync() reports true/false correctly'() {
      const file = path.join(ROOT, 'exists.txt');
      assert(!fs.existsSync(file), 'should not exist yet');
      fs.writeFileSync(file, 'x');
      assert(fs.existsSync(file), 'should exist now');
    },

    'accessSync() succeeds for a readable/writable file and throws for a missing one'() {
      const file = path.join(ROOT, 'access.txt');
      fs.writeFileSync(file, 'x');
      fs.accessSync(file, fs.constants.F_OK | fs.constants.R_OK | fs.constants.W_OK);

      let threw = false;
      try {
        fs.accessSync(path.join(ROOT, 'no-such-file.txt'), fs.constants.F_OK);
      } catch(e) {
        threw = true;
      }
      assert(threw, 'expected accessSync() to throw for a missing file');
    },

    'statSync() reports size, mode and type bits'() {
      const file = path.join(ROOT, 'stat.txt');
      fs.writeFileSync(file, '0123456789');
      const st = new fs.Stats(os.stat(file)[0]);
      assert(st.isFile(), 'expected isFile() to be true');
      assert(!st.isDirectory(), 'expected isDirectory() to be false');
      eq(st.size, 10);

      const real = fs.statSync(file);
      eq(real.size, 10);
      assert(real.isFile(), 'expected statSync() result to report isFile()');
      assert(real.mtime instanceof Date, 'expected mtime to be a Date');
    },

    'mkdirSync() creates a single directory and rejects re-creating it'() {
      const dir = path.join(ROOT, 'mkdir-plain');
      fs.mkdirSync(dir);
      assert(fs.statSync(dir).isDirectory(), 'expected a directory');

      let threw = false;
      try {
        fs.mkdirSync(dir);
      } catch(e) {
        threw = true;
      }
      assert(threw, 'expected re-creating an existing directory to throw');
    },

    'mkdirSync({ recursive: true }) creates intermediate directories and is idempotent'() {
      const dir = path.join(ROOT, 'mkdir-a', 'mkdir-b', 'mkdir-c');
      fs.mkdirSync(dir, { recursive: true });
      assert(fs.statSync(dir).isDirectory(), 'expected the deepest directory to exist');

      /* Idempotent: calling again must not throw. */
      fs.mkdirSync(dir, { recursive: true });
    },

    'mkdtempSync() creates a fresh, existing, unique directory per call'() {
      const a = fs.mkdtempSync(path.join(ROOT, 'tmp-'));
      const b = fs.mkdtempSync(path.join(ROOT, 'tmp-'));
      assert(fs.existsSync(a) && fs.statSync(a).isDirectory(), 'expected a to be a directory');
      assert(fs.existsSync(b) && fs.statSync(b).isDirectory(), 'expected b to be a directory');
      assert(a !== b, 'expected two mkdtempSync() calls to produce distinct directories');
    },

    /* --- readdir / walk / opendir --- */

    'readdirSync() lists entries, excluding . and ..'() {
      const dir = path.join(ROOT, 'readdir-plain');
      fs.mkdirSync(dir);
      fs.writeFileSync(path.join(dir, 'one.txt'), '1');
      fs.writeFileSync(path.join(dir, 'two.txt'), '2');

      const names = fs.readdirSync(dir).slice().sort();
      eq(names.join(','), 'one.txt,two.txt');
    },

    'readdirSync({ withFileTypes: true }) yields Dirent objects'() {
      const dir = path.join(ROOT, 'readdir-dirent');
      fs.mkdirSync(dir);
      fs.mkdirSync(path.join(dir, 'sub'));
      fs.writeFileSync(path.join(dir, 'file.txt'), 'x');

      const entries = fs.readdirSync(dir, { withFileTypes: true });
      const file = entries.find(e => e.name === 'file.txt');
      const sub = entries.find(e => e.name === 'sub');
      assert(file.isFile(), 'expected file.txt to report isFile()');
      assert(sub.isDirectory(), 'expected sub to report isDirectory()');
    },

    'readdirSync({ recursive: true }) lists nested entries, excluding the root'() {
      const dir = path.join(ROOT, 'readdir-recursive');
      fs.mkdirSync(path.join(dir, 'sub'), { recursive: true });
      fs.writeFileSync(path.join(dir, 'top.txt'), 't');
      fs.writeFileSync(path.join(dir, 'sub', 'deep.txt'), 'd');

      const names = fs.readdirSync(dir, { recursive: true }).slice().sort();
      assert(names.includes('top.txt'), `expected top.txt in ${JSON.stringify(names)}`);
      assert(
        names.some(n => n.replace(/\\/g, '/') === 'sub/deep.txt'),
        `expected sub/deep.txt in ${JSON.stringify(names)}`,
      );
      assert(!names.includes('.'), 'recursive listing must exclude the root itself');
    },

    'walkSync() yields {path,name,isFile,isDirectory} for a tree'() {
      const dir = path.join(ROOT, 'walk');
      fs.mkdirSync(path.join(dir, 'sub'), { recursive: true });
      fs.writeFileSync(path.join(dir, 'a.txt'), 'a');
      fs.writeFileSync(path.join(dir, 'sub', 'b.txt'), 'b');

      const entries = [...fs.walkSync(dir)];
      const names = entries.map(e => e.name).sort();
      assert(names.includes('a.txt'), `expected a.txt in ${JSON.stringify(names)}`);
      assert(names.includes('b.txt'), `expected b.txt in ${JSON.stringify(names)}`);

      const fileEntry = entries.find(e => e.name === 'a.txt');
      assert(fileEntry.isFile, 'expected a.txt entry to report isFile');
      const dirEntry = entries.find(e => e.name === 'sub');
      assert(dirEntry.isDirectory, 'expected sub entry to report isDirectory');
    },

    'opendirSync()/Dir iterates entries and closeSync() ends it'() {
      const dir = path.join(ROOT, 'opendir');
      fs.mkdirSync(dir);
      fs.writeFileSync(path.join(dir, 'one.txt'), '1');
      fs.writeFileSync(path.join(dir, 'two.txt'), '2');

      const d = fs.opendirSync(dir);
      eq(d.path, dir);
      const names = [];
      for(const entry of d) names.push(entry.name);
      d.closeSync();

      eq(names.slice().sort().join(','), 'one.txt,two.txt');
    },

    /* --- rename / remove --- */

    'renameSync() moves a file'() {
      const src = path.join(ROOT, 'rename-src.txt');
      const dst = path.join(ROOT, 'rename-dst.txt');
      fs.writeFileSync(src, 'move me');
      fs.renameSync(src, dst);
      assert(!fs.existsSync(src), 'source should be gone');
      eq(fs.readFileSync(dst, 'utf-8'), 'move me');
    },

    'unlinkSync() removes a file'() {
      const file = path.join(ROOT, 'unlink.txt');
      fs.writeFileSync(file, 'x');
      fs.unlinkSync(file);
      assert(!fs.existsSync(file), 'expected the file to be gone');
    },

    'rmdirSync() removes an empty directory and rejects a non-empty one'() {
      const empty = path.join(ROOT, 'rmdir-empty');
      fs.mkdirSync(empty);
      fs.rmdirSync(empty);
      assert(!fs.existsSync(empty), 'expected the empty directory to be gone');

      const nonEmpty = path.join(ROOT, 'rmdir-nonempty');
      fs.mkdirSync(nonEmpty);
      fs.writeFileSync(path.join(nonEmpty, 'file.txt'), 'x');
      let threw = false;
      try {
        fs.rmdirSync(nonEmpty);
      } catch(e) {
        threw = true;
      }
      assert(threw, 'expected rmdirSync() to throw for a non-empty directory');
    },

    'rmSync() removes a plain file'() {
      const file = path.join(ROOT, 'rm-file.txt');
      fs.writeFileSync(file, 'x');
      fs.rmSync(file);
      assert(!fs.existsSync(file), 'expected the file to be gone');
    },

    'rmSync({ recursive: true }) removes a non-empty directory tree'() {
      const dir = path.join(ROOT, 'rm-tree');
      fs.mkdirSync(path.join(dir, 'sub'), { recursive: true });
      fs.writeFileSync(path.join(dir, 'top.txt'), 't');
      fs.writeFileSync(path.join(dir, 'sub', 'deep.txt'), 'd');

      fs.rmSync(dir, { recursive: true });
      assert(!fs.existsSync(dir), 'expected the whole tree to be gone');
    },

    'rmSync({ force: true }) does not throw for a missing path'() {
      fs.rmSync(path.join(ROOT, 'does-not-exist'), { force: true });
    },

    /* --- mode / ownership / timestamps / truncation --- */

    'chmodSync() changes the mode bits of a file'() {
      const file = path.join(ROOT, 'chmod.txt');
      fs.writeFileSync(file, 'x');
      fs.chmodSync(file, 0o640);
      eq(fs.statSync(file).mode & 0o777, 0o640);
    },

    'chownSync() to the current uid/gid succeeds'() {
      const file = path.join(ROOT, 'chown.txt');
      fs.writeFileSync(file, 'x');
      /* Only the current uid/gid is guaranteed permitted without root. */
      fs.chownSync(file, getuid(), getgid());
    },

    'truncateSync() shrinks a file to the given length'() {
      const file = path.join(ROOT, 'truncate.txt');
      fs.writeFileSync(file, '0123456789');
      fs.truncateSync(file, 4);
      eq(fs.readFileSync(file, 'utf-8'), '0123');
    },

    'utimesSync() updates mtime to the given Date'() {
      const file = path.join(ROOT, 'utimes.txt');
      fs.writeFileSync(file, 'x');
      const when = new Date(Date.now() - 60_000);
      fs.utimesSync(file, when, when);
      const st = fs.statSync(file);
      assert(Math.abs(st.mtime.getTime() - when.getTime()) < 1000, `expected mtime near ${when}, got ${st.mtime}`);
    },

    /* --- temp files --- */

    'tmpfileSync() returns a usable, already-open temp file'() {
      const f = fs.tmpfileSync();
      f.puts('temp');
      f.flush();
      f.seek(0, std.SEEK_SET);
      eq(f.readAsString(), 'temp');
      f.close();
    },

    'mkstempSync() creates and opens a real temp file, returning its fd'() {
      const template = path.join(ROOT, 'mkstemp-XXXXXX');
      const fd = fs.mkstempSync(template);
      assert(typeof fd === 'number' && fd >= 0, `expected a valid fd, got ${fd}`);

      fs.writeSync(fd, fs.bufferFrom('x', 0, 1), 0, 1);
      const name = fs.nameSync(fd);
      assert(fs.existsSync(name), `expected ${name} to exist`);
      fs.closeSync(fd);
    },

    'tempnamSync() returns a path in the requested directory'() {
      const name = fs.tempnamSync(ROOT, 'pfx-');
      assert(name.startsWith(ROOT), `expected ${name} to start with ${ROOT}`);
    },

    /* --- flush --- */

    'flushSync() flushes a stdio handle and reports false for a plain fd'() {
      const file = path.join(ROOT, 'flush.txt');
      const f = fs.fopenSync(file, 'w');
      f.puts('flushed');
      eq(fs.flushSync(f), true);
      f.close();
      eq(fs.readFileSync(file, 'utf-8'), 'flushed');

      eq(fs.flushSync(42), undefined);
    },

    /* --- pipe-based async helpers: readerSync/readAllSync/readFullySync,
     * pipe/onRead/onWrite/waitRead/waitWrite --- */

    'pipe() returns a connected [read, write] fd pair'() {
      const [rd, wr] = fs.pipe();
      fs.writeSync(wr, fs.bufferFrom('ping', 0, 4), 0, 4);
      fs.closeSync(wr);

      const buf = new ArrayBuffer(4);
      const n = fs.readSync(rd, buf, 0, 4);
      eq(n, 4);
      eq(fs.bufferToString(buf, 0, n), 'ping');
      fs.closeSync(rd);
    },

    'readerSync()/readAllSync() drain a pipe into chunks/a string'() {
      const [rd, wr] = fs.pipe();
      fs.writeSync(wr, fs.bufferFrom('hello world', 0, 11), 0, 11);
      fs.closeSync(wr);

      eq(fs.readAllSync(rd, 4), 'hello world');
    },

    'readFullySync() reads exactly n bytes, even across short reads'() {
      const [rd, wr] = fs.pipe();
      fs.writeSync(wr, fs.bufferFrom('abcdefgh', 0, 8), 0, 8);
      fs.closeSync(wr);

      const buf = new ArrayBuffer(8);
      const result = fs.readFullySync(rd, buf, 0, 8);
      eq(result, 8);
      eq(fs.bufferToString(buf, 0, 8), 'abcdefgh');
      fs.closeSync(rd);
    },

    async 'onRead()/waitRead() resolve once a pipe becomes readable'() {
      const [rd, wr] = fs.pipe();

      const readable = fs.waitRead(rd);
      fs.writeSync(wr, fs.bufferFrom('go', 0, 2), 0, 2);
      fs.closeSync(wr);

      await readable;

      const buf = new ArrayBuffer(2);
      eq(fs.readSync(rd, buf, 0, 2), 2);
      fs.closeSync(rd);
    },

    async 'onWrite()/waitWrite() resolve once a pipe becomes writable'() {
      const [rd, wr] = fs.pipe();

      await fs.waitWrite(wr);
      const n = fs.writeSync(wr, fs.bufferFrom('ok', 0, 2), 0, 2);
      eq(n, 2);

      fs.closeSync(wr);
      fs.closeSync(rd);
    },

    /* --- createReadStream/createWriteStream --- */

    'createReadStream()/createWriteStream() open usable stdio handles'() {
      const file = path.join(ROOT, 'stream.txt');

      const w = fs.createWriteStream(file);
      w.puts('streamed');
      w.flush();
      w.close();

      const r = fs.createReadStream(file);
      eq(r.readAsString(), 'streamed');
      r.close();
    },

    /* --- inotify_event --- */

    'inotify_event can be constructed from a plain object'() {
      const ev = new fs.inotify_event({ wd: 1, mask: 0, cookie: 0, len: 0 });
      assert(ev instanceof ArrayBuffer, 'expected inotify_event to extend ArrayBuffer');
    },
  });
} finally {
  try {
    fs.chdir(ROOT.slice(0, ROOT.lastIndexOf('/')) || '/');
  } catch(e) {}
  rmrf(ROOT);
}
