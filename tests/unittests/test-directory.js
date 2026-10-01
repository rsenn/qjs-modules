import { mkdirSync } from 'fs';
import * as os from 'os';
import { Directory } from 'directory';
import { assert, eq, tests } from '../../lib/tinytest.js';

const TMP = '.tmp/test-directory';

function ensureDir() {
  try {
    mkdirSync(TMP);
  } catch(e) {}
}

function writeFile(path) {
  const fd = os.open(path, os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o644);
  os.write(fd, new Uint8Array([1, 2, 3]).buffer, 0, 3);
  os.close(fd);
}

ensureDir();
writeFile(TMP + '/a.txt');
writeFile(TMP + '/b.txt');
try {
  mkdirSync(TMP + '/sub');
} catch(e) {}

function collect(dir) {
  const entries = [];
  for(const entry of dir) entries.push(entry);
  return entries;
}

tests({
  'static constants are defined'() {
    eq(typeof Directory.NAME, 'number');
    eq(typeof Directory.TYPE, 'number');
    eq(typeof Directory.BOTH, 'number');
    eq(typeof Directory.TYPE_DIR, 'number');
    eq(typeof Directory.TYPE_REG, 'number');
    eq(typeof Directory.TYPE_MASK, 'number');
  },
  'constructor(path) opens the directory and iterates [name, type] by default'() {
    const dir = new Directory(TMP);
    const entries = collect(dir);
    const names = entries.map(e => e[0]).sort();

    assert(names.includes('a.txt'));
    assert(names.includes('b.txt'));
    assert(names.includes('sub'));

    const subEntry = entries.find(e => e[0] === 'sub');
    eq(subEntry[1], Directory.TYPE_DIR);

    const fileEntry = entries.find(e => e[0] === 'a.txt');
    eq(fileEntry[1], Directory.TYPE_REG);
  },
  'next() yields only the name with flags=NAME'() {
    const dir = new Directory(TMP);
    let value;

    while(true) {
      const { value: v, done } = dir.next(Directory.NAME);
      if(done) break;
      value = v;
      if(v === 'a.txt') break;
    }

    eq(value, 'a.txt');
  },
  'next() yields only the type with flags=TYPE and mask filters entries'() {
    const dir = new Directory(TMP);
    const types = [];

    for(;;) {
      const { value, done } = dir.next(Directory.TYPE, Directory.TYPE_REG);
      if(done) break;
      types.push(value);
    }

    assert(types.length >= 2);
    assert(types.every(t => t === Directory.TYPE_REG));
  },
  'open() (re)opens a path on an existing instance'() {
    const dir = new Directory();
    dir.open(TMP);
    const entries = collect(dir);
    assert(entries.some(e => e[0] === 'a.txt'));
  },
  'adopt() wraps an already-open directory file descriptor'() {
    const fd = os.open(TMP, os.O_RDONLY);
    const dir = new Directory();
    dir.adopt(fd);
    const entries = collect(dir);
    assert(entries.some(e => e[0] === 'b.txt'));
  },
  'valueOf() returns the underlying descriptor'() {
    const dir = new Directory(TMP);
    const fd = dir.valueOf();
    eq(typeof fd, 'number');
    assert(fd >= 0);
  },
  'close() eventually ends iteration without error'() {
    const dir = new Directory(TMP);
    dir.next();
    dir.close();

    let done = false;
    for(let i = 0; i < 100 && !done; i++) done = dir.next().done;

    assert(done);
  },
  'return() ends the iterator'() {
    const dir = new Directory(TMP);
    const result = dir.return('stop');
    eq(result.done, true);
    eq(result.value, 'stop');
  },
  'throw() propagates an injected exception'() {
    const dir = new Directory(TMP);

    try {
      dir.throw('injected');
      assert(false, 'throw() should have thrown');
    } catch(e) {
      eq(e, 'injected');
    }
  },
  'throw() propagates an injected Error object'() {
    const dir = new Directory(TMP);
    const err = new Error('injected');

    try {
      dir.throw(err);
      assert(false, 'throw() should have thrown');
    } catch(e) {
      assert(e === err);
    }
  },
  '[Symbol.iterator]() returns itself'() {
    const dir = new Directory(TMP);
    eq(dir[Symbol.iterator](), dir);
  },
});
