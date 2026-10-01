import { mkdirSync } from 'fs';
import * as os from 'os';
import * as mmap from 'mmap';
import { TextEncoder } from 'textcode';
import { assert, eq, tests } from '../../lib/tinytest.js';

const TMP = '.tmp/test-mmap';

try {
  mkdirSync(TMP);
} catch(e) {}

const FILE = TMP + '/data.bin';
const CONTENTS = 'Hello, mmap!';

function writeFile(path, str) {
  const fd = os.open(path, os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o644);
  const buf = new TextEncoder().encode(str);
  os.write(fd, buf.buffer, 0, buf.byteLength);
  os.close(fd);
  return buf.byteLength;
}

const size = writeFile(FILE, CONTENTS);

tests({
  'constants are defined'() {
    eq(typeof mmap.PROT_READ, 'number');
    eq(typeof mmap.PROT_WRITE, 'number');
    eq(typeof mmap.MAP_SHARED, 'number');
    eq(typeof mmap.MAP_PRIVATE, 'number');
    eq(typeof mmap.MAP_ANONYMOUS, 'number');
  },
  'mmap() anonymous mapping returns a zeroed ArrayBuffer of the requested length'() {
    const map = mmap.mmap(null, 4096, mmap.PROT_READ | mmap.PROT_WRITE, mmap.MAP_PRIVATE | mmap.MAP_ANONYMOUS, -1, 0);

    assert(map instanceof ArrayBuffer);
    eq(map.byteLength, 4096);

    const view = new Uint8Array(map);
    eq(view[0], 0);

    mmap.munmap(map);
  },
  'mmap() a real file maps its actual contents, readable via toString()'() {
    const fd = os.open(FILE, os.O_RDONLY);
    const map = mmap.mmap(null, size, mmap.PROT_READ, mmap.MAP_PRIVATE, fd, 0);

    assert(map instanceof ArrayBuffer);
    eq(map.byteLength, size);
    eq(mmap.toString(map), CONTENTS);

    mmap.munmap(map);
    os.close(fd);
  },
  'filename() reports the backing file path for a file-backed mapping'() {
    const fd = os.open(FILE, os.O_RDONLY);
    const map = mmap.mmap(null, size, mmap.PROT_READ, mmap.MAP_PRIVATE, fd, 0);

    const name = mmap.filename(map);
    assert(typeof name === 'string' && name.indexOf('data.bin') !== -1);

    mmap.munmap(map);
    os.close(fd);
  },
  'msync() flushes a writable mapping back to its file'() {
    const fd = os.open(FILE, os.O_RDWR);
    const map = mmap.mmap(null, size, mmap.PROT_READ | mmap.PROT_WRITE, mmap.MAP_PRIVATE, fd, 0);

    const view = new Uint8Array(map);
    view[0] = 'h'.charCodeAt(0);

    eq(mmap.msync(map, size, mmap.MS_SYNC ?? 4), 0);

    mmap.munmap(map);
    os.close(fd);
  },
  'mprotect() changes the protection of a mapping'() {
    const map = mmap.mmap(null, 4096, mmap.PROT_READ, mmap.MAP_PRIVATE | mmap.MAP_ANONYMOUS, -1, 0);

    eq(mmap.mprotect(map, mmap.PROT_READ | mmap.PROT_WRITE), 0);

    mmap.munmap(map);
  },
  'toString() renders the mapped bytes as a string'() {
    const map = mmap.mmap(null, size, mmap.PROT_READ, mmap.MAP_PRIVATE, os.open(FILE, os.O_RDONLY), 0);
    eq(mmap.toString(map), CONTENTS);
    mmap.munmap(map);
  },
});
