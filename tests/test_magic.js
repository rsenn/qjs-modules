import * as os from 'os';
import * as std from 'std';
import { Magic } from 'magic';
import { assert, eq, tests } from './tinytest.js';

// A tiny, self-contained magic source rule so tests don't depend on the
// system's magic database (which may or may not have a compiled .mgc handy).
const SRC = 'test_magic.src';
const DB = 'test_magic.mgc';
const DATA = 'test_magic.dat';

function writeFile(path, content) {
  const f = std.open(path, 'w');
  f.puts(content);
  f.close();
}

function cleanup() {
  for(const f of [SRC, DB, DATA]) {
    try {
      os.remove(f);
    } catch(e) {}
  }
}

tests({
  'setup fixture'() {
    cleanup();
    writeFile(SRC, '0\tstring\tTESTMAGIC\ttest file type\n');
    writeFile(DATA, 'TESTMAGIC payload');

    const m = new Magic();
    m.compile(SRC); // produces test_magic.src.mgc
    os.rename('test_magic.src.mgc', DB);

    assert(os.stat(DB)[0].size > 0, 'compiled magic database should be non-empty');
  },

  'new Magic()'() {
    const m = new Magic();
    eq(typeof m.version, 'number');
    assert(m.version > 0);

    const m2 = new Magic(Magic.MIME);
    eq(typeof m2.version, 'number');
  },

  'load()/file()/buffer()'() {
    const m = new Magic();
    m.load(DB);

    eq(m.file(DATA), 'test file type');
    eq(m.buffer('TESTMAGIC inline'), 'test file type');
    assert(m.buffer('no match here') !== 'test file type');
  },

  'descriptor()'() {
    const m = new Magic();
    m.load(DB);

    const fd = os.open(DATA, os.O_RDONLY);
    eq(m.descriptor(fd), 'test file type');
    os.close(fd);
  },

  'callable instance'() {
    const m = new Magic();
    m.load(DB);

    // a string argument is treated as a file path (like .file()), not raw bytes
    eq(m(DATA), 'test file type');

    // a non-string buffer argument is identified by content (like .buffer())
    const bytes = Uint8Array.from('TESTMAGIC as buffer', c => c.charCodeAt(0));
    eq(m(bytes), 'test file type');
  },

  'getflags()/setflags()'() {
    const m = new Magic();
    m.load(DB);

    eq(m.getflags(), 0);
    m.setflags(Magic.MIME_TYPE);
    eq(m.getflags(), Magic.MIME_TYPE);
    eq(m.file(DATA), 'text/plain');
  },

  'getparam()/setparam()'() {
    const m = new Magic();
    const before = m.getparam(Magic.PARAM_NAME_MAX);
    eq(typeof before, 'number');

    m.setparam(Magic.PARAM_NAME_MAX, 16);
    eq(m.getparam(Magic.PARAM_NAME_MAX), 16);
  },

  'check()/compile()/list()'() {
    const m = new Magic();

    // valid source: none of these should throw
    m.check(SRC);
    m.list(SRC);
    m.compile(SRC);
    os.remove('test_magic.src.mgc');

    // invalid/nonexistent source: should throw and populate error/errno
    const m2 = new Magic();
    let threw = false;
    try {
      m2.check('/nonexistent/path/does-not-exist.magic');
    } catch(e) {
      threw = true;
    }
    assert(threw, 'check() on a nonexistent file should throw');
    assert(m2.error.length > 0);
  },

  'error/errno getters'() {
    const m = new Magic();

    let threw = false;
    try {
      m.file(DATA); // no database loaded yet
    } catch(e) {
      threw = true;
      assert(/no magic files loaded/.test(e.message));
    }
    assert(threw);
    eq(typeof m.error, 'string');
    eq(typeof m.errno, 'number');
  },

  'static constants'() {
    eq(typeof Magic.MIME, 'number');
    eq(typeof Magic.NONE, 'number');
    eq(typeof Magic.MIME_TYPE, 'number');
    eq(typeof Magic.PARAM_NAME_MAX, 'number');
    eq(typeof Magic.VERSION, 'number');
    eq(typeof Magic.DEFAULT_DB, 'string');
  },

  'multiple simultaneous instances'() {
    // Regression test: constructing more than one Magic instance used to
    // corrupt the heap and crash the process (js_magic_constructor freed
    // the shared class prototype via an unowned reference). Verified fixed
    // with valgrind; this exercises the same pattern under the test runner.
    const instances = [];

    for(let i = 0; i < 5; i++) {
      const m = new Magic();
      m.load(DB);
      instances.push(m);
    }

    for(const m of instances) eq(m.file(DATA), 'test file type');

    const withFlags = new Magic(Magic.MIME, DB);
    eq(withFlags.file(DATA), 'text/plain; charset=us-ascii');
  },

  'teardown fixture'() {
    cleanup();
  },
});
