import * as os from 'os';
import * as std from 'std';
import { Archive, ArchiveEntry, ArchiveMatch } from 'archive';
import { assert, eq, tests } from './tinytest.js';

const TAR = 'test_archive.tar';
const TARGZ = 'test_archive.tar.gz';

function readWhole(path) {
  const f = std.open(path, 'r');
  f.seek(0, std.SEEK_END);
  const size = f.tell();
  f.seek(0, std.SEEK_SET);
  const buf = new ArrayBuffer(size);
  f.read(buf, 0, size);
  f.close();
  return buf;
}

function cleanup() {
  for(const f of [TAR, TARGZ]) {
    try {
      os.remove(f);
    } catch(e) {}
  }
}

tests({
  'setup'() {
    cleanup();
  },

  'ArchiveEntry constructor and numeric properties'() {
    const e = new ArchiveEntry('file.txt', 42);
    eq(e.pathname, 'file.txt');
    eq(e.size, 42);

    e.mode = 0o100644;
    eq(e.mode, 0o100644);
    e.uid = 1000;
    eq(e.uid, 1000);
    e.gid = 1000;
    eq(e.gid, 1000);
    e.ino = 123;
    eq(e.ino, 123);
    e.nlink = 2;
    eq(e.nlink, 2);
    e.perm = 0o644;
    eq(e.perm, 0o644);
    e.dev = 5;
    eq(e.dev, 5);
    e.devmajor = 1;
    eq(e.devmajor, 1);
    e.devminor = 2;
    eq(e.devminor, 2);
  },

  'ArchiveEntry string properties round-trip'() {
    // Regression test: js_is_nullish() used to call JS_ToInt64() on any
    // non-null/undefined value and treat a NaN-coerced (i.e. non-numeric
    // string) result of 0 as "nullish", so every string setter below
    // silently cleared the field instead of setting it.
    const e = new ArchiveEntry();

    e.pathname = 'dir/subdir';
    eq(e.pathname, 'dir/subdir');

    e.uname = 'alice';
    eq(e.uname, 'alice');

    e.gname = 'staff';
    eq(e.gname, 'staff');

    e.symlink = 'target/path';
    eq(e.symlink, 'target/path');
    eq(e.link, 'target/path'); // link falls back to symlink when no hardlink is set

    e.hardlink = 'other/path';
    eq(e.hardlink, 'other/path');
    eq(e.link, 'other/path'); // link prefers hardlink when both are set

    e.pathname = null;
    eq(e.pathname, undefined);
  },

  'ArchiveEntry date properties'() {
    const e = new ArchiveEntry();
    eq(e.mtime, undefined);

    const d = new Date(1700000000000);
    e.mtime = d;
    assert(e.mtime instanceof Date);
    eq(e.mtime.getTime(), d.getTime());

    e.mtime = null;
    eq(e.mtime, undefined);
  },

  'ArchiveEntry.type / filetype'() {
    const e = new ArchiveEntry();
    e.type = 'directory';
    eq(e.type, 'directory');

    e.type = 'file';
    eq(e.type, 'file');

    e.type = 'link';
    eq(e.type, 'link');
  },

  'ArchiveEntry.clone()'() {
    const e = new ArchiveEntry('clone-me.txt', 5);
    const c = e.clone();

    assert(c !== e);
    eq(c.pathname, 'clone-me.txt');
    eq(c.size, 5);
  },

  'Archive.write()/Archive.read() round trip'() {
    const w = Archive.write(TAR);
    eq(w.mode, Archive.WRITE);

    const file = new ArchiveEntry('hello.txt', 13);
    file.mode = 0o100644;
    assert(w.write(file, 'Hello, world!'));

    const dir = new ArchiveEntry();
    dir.pathname = 'dir/';
    dir.filetype = 0o040000;
    assert(w.write(dir));

    w.close();

    const r = Archive.read(TAR);
    eq(r.mode, Archive.READ);

    const seen = [];

    for(const entry of r) {
      seen.push([entry.pathname, entry.type]);

      if(entry.type === 'file') {
        const buf = r.read();
        const text = String.fromCharCode(...new Uint8Array(buf));
        eq(text, 'Hello, world!');
      }
    }

    eq(JSON.stringify(seen), JSON.stringify([
      ['hello.txt', 'file'],
      ['dir/', 'directory'],
    ]));

    r.close();
  },

  'Archive read from in-memory buffer'() {
    const buf = readWhole(TAR);
    const a = new Archive();
    a.open(buf);

    const names = [];
    for(const entry of a) names.push(entry.pathname);

    eq(JSON.stringify(names), JSON.stringify(['hello.txt', 'dir/']));
    a.close();
  },

  'Archive gzip compression via filename extension'() {
    const w = Archive.write(TARGZ);
    const e = new ArchiveEntry('data.txt', 11);
    e.mode = 0o100644;
    w.write(e, 'hello gzip!');
    w.close();

    const r = Archive.read(TARGZ);
    const entry = r.next();
    eq(entry.pathname, 'data.txt');
    eq(r.compression, 'gzip');
    r.close();
  },

  'Archive.extract()'() {
    const dir = 'test_archive_extract_dir';

    try {
      os.remove(dir + '/data.txt');
    } catch(e) {}
    try {
      os.remove(dir);
    } catch(e) {}

    os.mkdir(dir);
    const cwd = os.getcwd()[0];
    os.chdir(dir);

    try {
      const r = Archive.read('../' + TARGZ);

      for(const entry of r) r.extract(entry, 0);

      r.close();

      const content = std.open('data.txt', 'r').readAsString();
      eq(content, 'hello gzip!');
    } finally {
      os.chdir(cwd);
      os.remove(dir + '/data.txt');
      os.remove(dir);
    }
  },

  'ArchiveMatch'() {
    // Regression test: js_archive_return() used to always fetch the opaque
    // data using the Archive class ID regardless of the caller's actual
    // class, so calling include()/exclude() on an ArchiveMatch (a
    // different class) threw a spurious "Archive object expected".
    const m = new ArchiveMatch();
    eq(m.include('*.txt'), Archive.OK);
    eq(m.exclude('*.log'), Archive.OK);
  },

  'Archive properties'() {
    const w = Archive.write(TAR);
    eq(typeof w.blockSize, 'number');
    assert(w.blockSize > 0);
    // libarchive rejects changing the block size after the archive is
    // already open (which Archive.write() always is), so this is only
    // checked for not throwing, not for the value actually changing.
    w.blockSize = 4096;
    w.close();

    const r = Archive.read(TAR);
    r.next();
    eq(typeof r.format, 'string');
    eq(typeof r.compression, 'string');
    assert(Array.isArray(r.filters));
    eq(typeof r.fileCount, 'number');
    eq(typeof r.position, 'number');
    eq(r.error, null);
    eq(r.errno, 0);
    r.close();
  },

  'Archive static constants'() {
    eq(Archive.READ, 0);
    eq(Archive.WRITE, 1);
    eq(typeof Archive.OK, 'number');
    eq(typeof Archive.EOF, 'number');
    eq(typeof Archive.FATAL, 'number');
    eq(typeof Archive.version, 'string');
    assert(Archive.version.length > 0);
  },

  'Archive instanceof / prototype linkage'() {
    // Regression test: js_archive_init() never called JS_SetConstructor(),
    // so Archive.prototype was undefined and `new Archive() instanceof
    // Archive` threw instead of returning true.
    const a = new Archive();
    assert(a instanceof Archive);
    assert(Archive.prototype === Object.getPrototypeOf(a));
  },

  'teardown'() {
    cleanup();
  },
});
