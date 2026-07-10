import * as path from 'path';
import * as fs from 'fs';
import { Archive, ArchiveEntry } from 'archive';
import { toString } from 'util';
import { UnionFS, ArchiveFS } from '../lib/vfs.js';
import { assert, eq, tests } from './tinytest.js';

/*
 * Fixture layout (built fresh under a scratch directory for every run):
 *
 *   <root>/dirA/file1.txt   = "content-A-file1"
 *   <root>/dirA/sub/nested.txt = "content-A-nested"
 *   <root>/dirB/file1.txt   = "content-B-file1"   (shadows dirA's copy)
 *   <root>/dirB/file2.txt   = "content-B-file2"
 *   <root>/test.tar         = tar archive with entries one.txt / two.txt
 */

const TMPDIR = std.getenv('TMPDIR') ?? '/tmp';
const ROOT = path.join(TMPDIR, `qjs-vfs-test-${Date.now()}-${Math.floor(Math.random() * 1e6)}`);
const DIR_A = path.join(ROOT, 'dirA');
const DIR_B = path.join(ROOT, 'dirB');
const SUB_A = path.join(DIR_A, 'sub');
const TAR_PATH = path.join(ROOT, 'test.tar');

const CONTENT_A_FILE1 = 'content-A-file1';
const CONTENT_A_NESTED = 'content-A-nested';
const CONTENT_B_FILE1 = 'content-B-file1';
const CONTENT_B_FILE2 = 'content-B-file2';
const CONTENT_ONE = 'content-one';
const CONTENT_TWO = 'content-two';

function buildFixture() {
  fs.mkdirSync(ROOT);
  fs.mkdirSync(DIR_A);
  fs.mkdirSync(DIR_B);
  fs.mkdirSync(SUB_A);

  fs.writeFileSync(path.join(DIR_A, 'file1.txt'), CONTENT_A_FILE1);
  fs.writeFileSync(path.join(SUB_A, 'nested.txt'), CONTENT_A_NESTED);
  fs.writeFileSync(path.join(DIR_B, 'file1.txt'), CONTENT_B_FILE1);
  fs.writeFileSync(path.join(DIR_B, 'file2.txt'), CONTENT_B_FILE2);

  const ar = Archive.write(TAR_PATH);
  for(const [name, content] of [
    ['one.txt', CONTENT_ONE],
    ['two.txt', CONTENT_TWO],
  ]) {
    const entry = new ArchiveEntry(name, { type: 'file', perm: 0o644, size: content.length });
    ar.write(entry);
    ar.write(content);
  }
  ar.close();
}

function rmrf(p) {
  let st;
  try {
    st = fs.lstatSync(p);
  } catch(e) {
    return;
  }
  if(st.isDirectory()) {
    for(const name of fs.readdirSync(p)) if(name != '.' && name != '..') rmrf(path.join(p, name));
  }
  fs.unlinkSync(p);
}

buildFixture();

try {
  await tests({
    /* --- UnionFS: registering and inspecting paths --- */

    'UnionFS.appendPath() adds a path once'() {
      const vfs = new UnionFS();
      assert(vfs.appendPath(DIR_A) === true, 'first appendPath() should succeed');
      assert(!vfs.appendPath(DIR_A), 'appending the same path twice should not succeed again');
      assert(vfs.appendPath(DIR_B) === true, 'appendPath() for a second distinct path should succeed');
      assert(vfs.hasPath(DIR_A), 'hasPath() should find dirA');
      assert(vfs.hasPath(DIR_B), 'hasPath() should find dirB');
      assert(!vfs.hasPath(ROOT), 'hasPath() should not find an unregistered path');
    },

    'UnionFS.removePath() unregisters a path'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);
      vfs.appendPath(DIR_B);

      const [removedDir] = vfs.removePath(DIR_A);
      eq(removedDir, path.resolve(DIR_A));
      assert(!vfs.hasPath(DIR_A), 'dirA should no longer be registered');
      assert(vfs.hasPath(DIR_B), 'dirB should remain registered');
    },

    'UnionFS.prependPath() adds a path to the front, taking precedence'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);
      eq(vfs.readFileSync('file1.txt', 'utf-8'), CONTENT_A_FILE1);

      vfs.prependPath(DIR_B);
      assert(vfs.hasPath(DIR_B));
      eq(vfs.readFileSync('file1.txt', 'utf-8'), CONTENT_B_FILE1, 'dirB was prepended, so it now wins');
    },

    /* --- UnionFS: reading through the union --- */

    'UnionFS.existsSync() / readFileSync() resolve across members, first path wins'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);
      vfs.appendPath(DIR_B);

      assert(vfs.existsSync('file1.txt'), 'file1.txt exists in dirA');
      assert(vfs.existsSync('file2.txt'), 'file2.txt exists in dirB');

      /* file1.txt exists in both members; dirA was appended first so it wins */
      eq(vfs.readFileSync('file1.txt', 'utf-8'), CONTENT_A_FILE1);
      eq(vfs.readFileSync('file2.txt', { encoding: 'utf-8' }), CONTENT_B_FILE2);
    },

    'UnionFS.existsSync() returns false for a nonexistent relative path'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);
      vfs.appendPath(DIR_B);

      assert(!vfs.existsSync('nope.txt'), 'nope.txt does not exist anywhere in the union');
    },

    'UnionFS resolves absolute paths to the member that owns them'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);
      vfs.appendPath(DIR_B);

      assert(vfs.existsSync(path.join(DIR_A, 'file1.txt')));
      assert(vfs.existsSync(path.join(DIR_B, 'file2.txt')));
      assert(!vfs.existsSync(path.join(DIR_A, 'file2.txt')), 'file2.txt does not exist under dirA');
    },

    'UnionFS.statSync() / lstatSync() return usable Stats'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);

      const st = vfs.statSync('file1.txt');
      assert(st.isFile(), 'file1.txt should report as a file');
      eq(st.size, CONTENT_A_FILE1.length);

      const lst = vfs.lstatSync('file1.txt');
      assert(lst.isFile(), 'lstatSync should also report a file');
    },

    'UnionFS.sizeSync() / accessSync() work for absolute paths'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);

      eq(vfs.sizeSync(path.join(DIR_A, 'file1.txt')), CONTENT_A_FILE1.length);
      eq(vfs.accessSync(path.join(DIR_A, 'file1.txt'), 0), 0);
    },

    /* --- UnionFS: writing/mutating through the union --- */

    'UnionFS.writeFileSync() / unlinkSync() work for an absolute path under a member'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);

      const p = path.join(DIR_A, 'new-absolute.txt');
      vfs.writeFileSync(p, 'fresh content');
      eq(vfs.readFileSync(p, 'utf-8'), 'fresh content');

      vfs.unlinkSync(p);
      assert(!vfs.existsSync(p), 'file should be gone after unlinkSync');
    },

    'UnionFS.writeFileSync() with a brand-new relative path lands in the first member'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);
      vfs.appendPath(DIR_B);

      vfs.writeFileSync('brand-new-relative.txt', 'fresh');
      const p = path.join(DIR_A, 'brand-new-relative.txt');
      assert(fs.existsSync(p), 'the new file should be created under the first appended member');
      eq(vfs.readFileSync('brand-new-relative.txt', 'utf-8'), 'fresh');

      vfs.unlinkSync(p);
    },

    'UnionFS.mkdirSync() works for an absolute path under a member'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);

      const d = path.join(DIR_A, 'new-absolute-dir');
      vfs.mkdirSync(d);
      assert(vfs.existsSync(d));
      fs.unlinkSync(d);
    },

    'UnionFS.mkdirSync() with a brand-new relative path lands in the first member'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);

      vfs.mkdirSync('new-relative-dir');
      const d = path.join(DIR_A, 'new-relative-dir');
      assert(fs.existsSync(d), 'the new directory should be created under the first appended member');

      fs.unlinkSync(d);
    },

    'UnionFS.renameSync() / symlinkSync() / readlinkSync() / realpathSync() work for absolute paths'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);

      const src = path.join(DIR_A, 'to-rename.txt');
      const dst = path.join(DIR_A, 'renamed.txt');
      vfs.writeFileSync(src, 'rename me');
      vfs.renameSync(src, dst);
      assert(vfs.existsSync(dst));
      assert(!vfs.existsSync(src));

      const link = path.join(DIR_A, 'a-link.txt');
      vfs.symlinkSync(dst, link);
      eq(vfs.readlinkSync(link), dst);
      eq(vfs.realpathSync(link), path.resolve(dst));

      vfs.unlinkSync(link);
      vfs.unlinkSync(dst);
    },

    'UnionFS.readdirSync() lists a nested absolute directory'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);

      const entries = vfs.readdirSync(SUB_A);
      assert(entries.includes(path.join(SUB_A, 'nested.txt')), 'readdirSync() should list nested.txt');
    },

    'UnionFS.readdirSync() lists a union root path itself'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);

      const entries = vfs.readdirSync(DIR_A);
      assert(entries.includes(path.join(DIR_A, 'file1.txt')), 'readdirSync() should list file1.txt');
      assert(entries.includes(path.join(DIR_A, 'sub')), 'readdirSync() should list the sub directory');
    },

    'UnionFS.readdirSync(".") lists entries from every member'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);
      vfs.appendPath(DIR_B);

      const entries = vfs.readdirSync('.');
      assert(entries.includes(path.join(DIR_A, 'file1.txt')), 'should include dirA/file1.txt');
      assert(entries.includes(path.join(DIR_B, 'file2.txt')), 'should include dirB/file2.txt');
    },

    'UnionFS.tempnamSync() works for both a nested directory and a root path'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);

      const name = vfs.tempnamSync(SUB_A, 'pfx');
      assert(typeof name == 'string' && name.length > 0);

      const rootName = vfs.tempnamSync(DIR_A, 'pfx');
      assert(typeof rootName == 'string' && rootName.length > 0);
    },

    'UnionFS.mkstempSync() creates a unique file for an absolute template'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);

      const fd = vfs.mkstempSync(path.join(DIR_A, 'tmpXXXXXX'));
      assert(typeof fd == 'number' && fd >= 0);

      const name = vfs.nameSync(fd);
      vfs.closeSync(fd);
      assert(vfs.existsSync(name));
      fs.unlinkSync(name);
    },

    'UnionFS.tmpfileSync() creates a writable, readable file in the first member'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);

      const handle = vfs.tmpfileSync();
      const name = vfs.nameSync(handle);
      assert(name.startsWith(path.resolve(DIR_A)), 'tmpfile should be created under dirA');

      vfs.writeSync(handle, 'tmpfile content');
      vfs.closeSync(handle);

      eq(vfs.readFileSync(name, 'utf-8'), 'tmpfile content');
      fs.unlinkSync(name);
    },

    'UnionFS exposes raw fd-based IO passthroughs'() {
      const vfs = new UnionFS();
      vfs.appendPath(DIR_A);

      const fd = vfs.openSync('file1.txt', 'r');
      const content = vfs.readAllSync(fd);
      eq(content, CONTENT_A_FILE1);
    },

    /* --- ArchiveFS --- */

    'ArchiveFS default constructor (no explicit read flag) reads by default'() {
      const afs = new ArchiveFS(TAR_PATH);

      const entries = afs.readdirSync('.').slice().sort();
      eq(entries.join(','), 'one.txt,two.txt');
    },

    'ArchiveFS(path, false) reads directory entries and file contents'() {
      const afs = new ArchiveFS(TAR_PATH, false);

      const entries = afs.readdirSync('.').slice().sort();
      eq(entries.join(','), 'one.txt,two.txt');

      assert(afs.existsSync('one.txt'));
      assert(!afs.existsSync('nope.txt'));

      eq(afs.sizeSync('one.txt'), CONTENT_ONE.length);

      const st = afs.statSync('two.txt');
      eq(st.pathname, 'two.txt');
      eq(st.size, CONTENT_TWO.length);

      eq(afs.readFileSync('one.txt', { encoding: 'utf-8' }), CONTENT_ONE);

      const raw = afs.readFileSync('two.txt');
      assert(raw instanceof ArrayBuffer);
      eq(raw.byteLength, CONTENT_TWO.length);
    },

    'ArchiveFS(path, false).openSync() reads entry data'() {
      const afs = new ArchiveFS(TAR_PATH, false);

      const stream = afs.openSync('one.txt');
      const buf = new ArrayBuffer(64);
      const n = stream.read(buf, 0, 64);
      eq(n, CONTENT_ONE.length);
      eq(toString(buf, 0, n), CONTENT_ONE);
      assert(stream.eof(), 'stream should report eof after reading the whole entry');

      stream.close();
    },

    'ArchiveFS({file, mode: Archive.READ}).openSync() reads data and reports eof correctly'() {
      const afs = new ArchiveFS({ file: TAR_PATH, mode: Archive.READ });

      const stream = afs.openSync('one.txt');
      const buf = new ArrayBuffer(1);

      assert(!stream.eof(), 'should not be eof before any bytes are read');

      let out = '';
      let n;
      while((n = stream.read(buf, 0, 1)) > 0) out += toString(buf, 0, n);

      eq(out, CONTENT_ONE);
      assert(stream.eof(), 'should be eof once the whole entry has been read');

      stream.close();
    },

    'ArchiveFS write mode works via either constructor form'() {
      const writeTarget1 = path.join(ROOT, 'attempt1.tar');
      const writeTarget2 = path.join(ROOT, 'attempt2.tar');
      const payload = new Uint8Array([104, 105]).buffer; // "hi"

      /* String-form constructor with rw=true. */
      const w1 = new ArchiveFS(writeTarget1, true);
      const s1 = w1.openSync('demo.txt');
      eq(s1[Symbol.toStringTag], 'WriteStream');
      s1.write(payload, 0, 2);
      s1.close();

      const r1 = new ArchiveFS(writeTarget1, false);
      eq(r1.readdirSync('.').join(','), 'demo.txt');
      eq(r1.readFileSync('demo.txt', { encoding: 'utf-8' }), 'hi');

      /* Object-form constructor with an explicit numeric mode. */
      const w2 = new ArchiveFS({ file: writeTarget2, mode: Archive.WRITE });
      const s2 = w2.openSync('demo2.txt');
      eq(s2[Symbol.toStringTag], 'WriteStream');
      s2.write(payload, 0, 2);
      s2.close();

      const r2 = new ArchiveFS({ file: writeTarget2, mode: Archive.READ });
      eq(r2.readdirSync('.').join(','), 'demo2.txt');
      eq(r2.readFileSync('demo2.txt', { encoding: 'utf-8' }), 'hi');
    },
  });
} finally {
  rmrf(ROOT);
}

/* ------------------------------------------------------------------ */
/* Demo: combine an "overrides" directory with a "defaults" directory  */
/* into a single read-through filesystem, then browse a tar archive    */
/* the same way an on-disk directory would be browsed.                 */
/* ------------------------------------------------------------------ */

function demo() {
  const root = path.join(TMPDIR, `qjs-vfs-demo-${Date.now()}-${Math.floor(Math.random() * 1e6)}`);
  const defaults = path.join(root, 'defaults');
  const overrides = path.join(root, 'overrides');

  fs.mkdirSync(root);
  fs.mkdirSync(defaults);
  fs.mkdirSync(overrides);
  fs.writeFileSync(path.join(defaults, 'config.json'), '{"mode":"default"}');
  fs.writeFileSync(path.join(defaults, 'readme.txt'), 'default readme');
  fs.writeFileSync(path.join(overrides, 'config.json'), '{"mode":"override"}');

  const cfg = new UnionFS();
  /* Whatever is appended first wins when the same name exists in both. */
  cfg.appendPath(overrides);
  cfg.appendPath(defaults);

  console.log('=== UnionFS demo ===');
  console.log('config.json (from overrides):', cfg.readFileSync('config.json', 'utf-8'));
  console.log('readme.txt  (falls through to defaults):', cfg.readFileSync('readme.txt', 'utf-8'));

  rmrf(root);

  const tarPath = path.join(TMPDIR, `qjs-vfs-demo-${Date.now()}-${Math.floor(Math.random() * 1e6)}.tar`);
  const ar = Archive.write(tarPath);
  const greeting = 'hello from inside a tar file';
  ar.write(new ArchiveEntry('greeting.txt', { type: 'file', perm: 0o644, size: greeting.length }));
  ar.write(greeting);
  ar.close();

  const afs = new ArchiveFS(tarPath);

  console.log('=== ArchiveFS demo ===');
  console.log('entries:', afs.readdirSync('.'));
  console.log('greeting.txt:', afs.readFileSync('greeting.txt', { encoding: 'utf-8' }));

  fs.unlinkSync(tarPath);
}

demo();
