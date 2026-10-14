/* Bun.file(), Bun.write(), Bun.stdin/stdout/stderr: lib/bun.js.
 * uses the Bun global when the builtin is compiled in, else loads ./lib/bun.js. */
import * as fs from 'fs';
import { assert, eq, tests } from '../../lib/tinytest.js';

const Bun = globalThis.Bun ?? (await import('../../lib/bun.js')).default;
const { Blob } = globalThis.Blob ? globalThis : await import('blob');
const { URL } = globalThis.URL ? globalThis : await import('url');
const ROOT = fs.mkdtempSync('/tmp/qjs-bun-file-');
const at = name => `${ROOT}/${name}`;

async function rejection(promise) {
  try {
    await promise;
  } catch(e) {
    return e;
  }
}

tests({
  'Bun.file(path) is a lazy Blob with name, type and size'() {
    fs.writeFileSync(at('a.txt'), 'héllo');

    const f = Bun.file(at('a.txt'));

    assert(f instanceof Blob);
    eq(at('a.txt'), f.name);
    eq(6, f.size);
    eq('text/plain;charset=utf-8', f.type);
    assert(f.lastModified > 1e12);
  },

  'type comes from the extension, or from options.type'() {
    eq('application/json;charset=utf-8', Bun.file('x.json').type);
    eq('text/html;charset=utf-8', Bun.file('x.html').type);
    eq('image/png', Bun.file('x.png').type);
    eq('application/octet-stream', Bun.file('x.bin').type);
    eq('application/octet-stream', Bun.file('noext').type);
    eq('text/x', Bun.file('x.json', { type: 'text/x' }).type);
  },

  'a missing file has size 0, exists() false, lastModified 2**52-1'() {
    const f = Bun.file(at('missing'));

    eq(0, f.size);
    eq(2 ** 52 - 1, f.lastModified);
    return f.exists().then(v => eq(false, v));
  },

  async 'text, json, arrayBuffer and bytes read the file'() {
    await Bun.write(at('j.json'), '{"a":1}');

    const f = Bun.file(at('j.json'));

    eq('{"a":1}', await f.text());
    eq(1, (await f.json()).a);
    eq(7, (await f.arrayBuffer()).byteLength);
    eq(123, (await f.bytes())[0]);
  },

  async 'reading a missing file rejects with ENOENT fields'() {
    const e = await rejection(Bun.file(at('missing')).text());

    eq('ENOENT', e.code);
    eq(-2, e.errno);
    eq('open', e.syscall);
    eq(at('missing'), e.path);
    eq(`ENOENT: no such file or directory, open '${at('missing')}'`, e.message);
  },

  async 'reading a directory rejects with EISDIR'() {
    const e = await rejection(Bun.file(ROOT).text());

    eq('EISDIR', e.code);
    eq(false, await Bun.file(ROOT).exists());
  },

  async 'slice() is a window onto the file, negative counts from the end'() {
    await Bun.write(at('s.txt'), 'abcdefghij');

    const f = Bun.file(at('s.txt'));

    eq('cde', await f.slice(2, 5).text());
    eq(3, f.slice(2, 5).size);
    eq('hij', await f.slice(-3).text());
    eq('d', await f.slice(2, 5).slice(1, 2).text());
    eq('', await f.slice(5, 2).text());
    eq('a/b', f.slice(0, 1, 'a/b').type);
    eq(f.type, f.slice(0, 1).type);
  },

  async 'stream() yields the bytes in chunks'() {
    const big = 'x'.repeat(150000);

    await Bun.write(at('big.txt'), big);

    let n = 0;

    for await (const chunk of Bun.file(at('big.txt')).stream()) {
      assert(chunk instanceof Uint8Array);
      n += chunk.length;
    }

    eq(150000, n);
  },

  async 'stream() of a missing file errors on read'() {
    const e = await rejection(Bun.file(at('missing')).stream().getReader().read());

    eq('ENOENT', e.code);
  },

  async 'new Response-like and Blob bodies feed Bun.write'() {
    eq(3, await Bun.write(at('b'), new Blob(['xyz'])));
    eq('xyz', await Bun.file(at('b')).text());
    eq(3, await Bun.write(at('c'), Bun.file(at('b'))));
    eq('xyz', await Bun.file(at('c')).text());
  },

  async 'Bun.write returns the bytes written for strings and binary data'() {
    eq(6, await Bun.write(at('w1'), 'héllo'));
    eq(3, await Bun.write(at('w2'), new Uint8Array([1, 2, 3])));
    eq(4, await Bun.write(at('w3'), new ArrayBuffer(4)));
    eq(0, await Bun.write(at('w4'), ''));
    eq(2, (await Bun.file(at('w2')).bytes())[1]);
  },

  async 'Bun.write takes a BunFile or file: URL as destination'() {
    eq(3, await Bun.write(Bun.file(at('d1')), 'abc'));
    eq('abc', await Bun.file(at('d1')).text());
    eq(1, await Bun.write(new URL(`file://${at('d2')}`), 'z'));
    eq('z', await Bun.file(at('d2')).text());
  },

  async 'Bun.write creates missing parent directories'() {
    eq(1, await Bun.write(at('p/q/r.txt'), 'z'));
    eq('z', await Bun.file(at('p/q/r.txt')).text());
  },

  async 'Bun.write stringifies numbers and objects, rejects null'() {
    await Bun.write(at('n'), 5);
    eq('5', await Bun.file(at('n')).text());
    await Bun.write(at('o'), { a: 1 });
    eq('[object Object]', await Bun.file(at('o')).text());
    assert((await rejection(Bun.write(at('x'), null))) instanceof TypeError);
  },

  async 'delete() removes the file, rejects ENOENT the second time'() {
    await Bun.write(at('del'), 'x');
    await Bun.file(at('del')).delete();
    eq(false, await Bun.file(at('del')).exists());
    eq('ENOENT', (await rejection(Bun.file(at('del')).delete())).code);
  },

  async 'writer() buffers, flush() and end() report the bytes written'() {
    const w = Bun.file(at('w.txt')).writer();

    eq(2, w.write('ab'));
    eq(1, w.write(new Uint8Array([99])));
    eq(3, await w.flush());
    eq(0, await w.end());
    eq('abc', await Bun.file(at('w.txt')).text());
  },

  'Bun.file() rejects what is not a path, URL or fd'() {
    assert(throws(() => Bun.file()) instanceof TypeError);
    assert(throws(() => Bun.file({})) instanceof TypeError);
    assert(throws(() => Bun.file(5.5)) instanceof RangeError);
  },

  'Bun.stdin, stdout and stderr are BunFiles over fd 0, 1, 2'() {
    for(const f of [Bun.stdin, Bun.stdout, Bun.stderr]) {
      assert(f instanceof Blob);
      eq(undefined, f.name);
      eq('application/octet-stream', f.type);
    }

    eq(Infinity, Bun.stdin.size);
  },

  async 'cleanup'() {
    fs.rmSync(ROOT, { recursive: true });
    eq(false, fs.existsSync(ROOT));
  },
});

function throws(fn) {
  try {
    fn();
  } catch(e) {
    return e;
  }
}
