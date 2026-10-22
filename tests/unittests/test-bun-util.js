/* the Bun.* utilities of lib/bun.js and lib/bun/util.js; expected values come from bun 1.4.2.
 * runs under bun as well: bun tests/unittests/test-bun-util.js */
import * as fs from 'fs';
import { assert, eq, tests } from '../../lib/tinytest.js';

const Bun = globalThis.Bun ?? (await import('../../lib/bun.js')).default;
const ROOT = fs.mkdtempSync(`${globalThis.process?.env?.TMPDIR ?? '/tmp'}/qjs-bun-util-`);
const at = name => `${ROOT}/${name}`;
const bits = list => list.map(x => (x ? 1 : 0)).join('');

class P {
  constructor() {
    this.a = 1;
  }
}

/* [a, b] pairs and what Bun.deepEquals gives, loose then strict */
const EQUAL_CASES = [
  [1, 1], [1, '1'], [NaN, NaN], [0, -0], [{ a: 1 }, { a: 1 }], [{ a: undefined }, {}], [{}, { a: undefined }],
  [[1, , 3], [1, undefined, 3]], [[1, 2], [1, 2, 3]], [new P(), { a: 1 }], [new Date(1), new Date(1)], [new Date(1), new Date(2)],
  [/a/g, /a/g], [/a/g, /a/i], [new Map([[1, { x: 1 }]]), new Map([[1, { x: 1 }]])], [new Set([1, 2]), new Set([2, 1])],
  [new Uint8Array([1, 2]), new Uint8Array([1, 2])], [new Uint8Array([1]), new Int8Array([1])], [null, undefined], [{ a: [1, { b: 2 }] }, { a: [1, { b: 2 }] }],
  [Object.create(null), {}], [new Error('x'), new Error('x')], [[1], { 0: 1, length: 1 }], [new Number(1), 1], [{ a: 1, b: 2 }, { b: 2, a: 1 }],
  [Symbol.for('s'), Symbol.for('s')], [function f() {}, function f() {}], [new ArrayBuffer(2), new ArrayBuffer(2)],
];

const stream = () =>
  new ReadableStream({
    start(c) {
      c.enqueue(new TextEncoder().encode('{"a":'));
      c.enqueue('1}');
      c.close();
    },
  });

tests({
  'deepEquals and deepMatch agree with Bun'() {
    eq('1010111101101011100111001101', bits(EQUAL_CASES.map(([a, b]) => Bun.deepEquals(a, b))));
    eq('1010100000101011100111001101', bits(EQUAL_CASES.map(([a, b]) => Bun.deepEquals(a, b, true))));
    eq('1001', bits([Bun.deepMatch({ a: 1 }, { a: 1, b: 2 }), Bun.deepMatch({ a: 1, b: 2 }, { a: 1 }), Bun.deepMatch([1], [1, 2]), Bun.deepMatch({ a: [1, { b: 2 }] }, { a: [1, { b: 2, c: 3 }], d: 4 })]));
  },

  'escapeHTML, stripANSI and stringWidth'() {
    eq('&lt;a href=&quot;x&quot;&gt;&amp;&#x27;&lt;/a&gt;', Bun.escapeHTML(`<a href="x">&'</a>`));
    eq('5', Bun.escapeHTML(5));
    eq('["red x","alinkb","qr","z"]', JSON.stringify(['\x1b[31mred\x1b[0m \x1b]0;t\x07x', 'a\x1b]8;;http://x\x1b\\link\x1b]8;;\x1b\\b', '\x1b(Bq\x1b7r', '\x1b[?25lz'].map(Bun.stripANSI)));
    eq('5,2,5,1,2,2', [Bun.stringWidth('日本a'), Bun.stringWidth('\x1b[1mab'), Bun.stringWidth('\x1b[1mab', { countAnsiEscapeCodes: true }), Bun.stringWidth('é'), Bun.stringWidth('😀'), Bun.stringWidth('\x1b]0;t\x07ab')].join());
  },

  'concatArrayBuffers, allocUnsafe and indexOfLine'() {
    const ab = Bun.concatArrayBuffers([new Uint8Array([1]), new Uint8Array([2, 3]).buffer]);

    assert(ab instanceof ArrayBuffer);
    eq('1,2,3', [...new Uint8Array(ab)].join());
    eq('1,2', [...new Uint8Array(Bun.concatArrayBuffers([new Uint8Array([1, 2]), new Uint8Array([3])], 2))].join());
    assert(Bun.concatArrayBuffers([new Uint8Array([1])], undefined, true) instanceof Uint8Array);
    eq(3, Bun.allocUnsafe(3).length);

    const text = new TextEncoder().encode('ab\ncd\nef');

    eq('2,5,-1', [Bun.indexOfLine(text, 0), Bun.indexOfLine(text, 3), Bun.indexOfLine(new TextEncoder().encode('ab'), 0)].join());
  },

  'peek and peek.status'() {
    const rejected = Promise.reject(new Error('e'));

    rejected.catch(() => {});
    eq(4, Bun.peek(Promise.resolve(4)));
    eq(5, Bun.peek(5));
    assert(Bun.peek(rejected) instanceof Error);
    eq('fulfilled,pending,rejected', [Bun.peek.status(Promise.resolve(1)), Bun.peek.status(new Promise(() => {})), Bun.peek.status(rejected)].join());
  },

  async 'sleep takes ms or a Date, nanoseconds counts up'() {
    const t = Bun.nanoseconds();

    eq('number', typeof t);
    await Bun.sleep(new Date(Date.now() + 20));
    assert(Bun.nanoseconds() - t >= 15e6);
    eq(undefined, Bun.sleepSync(1));
  },

  'which walks PATH and checks the executable bit'() {
    fs.writeFileSync(at('tool'), '#!/bin/sh\n');
    fs.chmodSync(at('tool'), 0o755);
    fs.writeFileSync(at('data'), 'x');

    eq(at('tool'), Bun.which('tool', { PATH: `/nonexistent:${ROOT}` }));
    eq(null, Bun.which('data', { PATH: ROOT }));
    eq(null, Bun.which('nosuchcommand-qjs'));
    eq('/bin/sh', Bun.which('/bin/sh'));
    eq(at('tool'), Bun.which('./tool', { cwd: ROOT }));
  },

  async 'readableStreamTo* read strings and bytes alike'() {
    eq('{"a":1}', await Bun.readableStreamToText(stream()));
    eq(1, (await Bun.readableStreamToJSON(stream())).a);
    assert((await Bun.readableStreamToBytes(stream())) instanceof Uint8Array);
    assert((await Bun.readableStreamToArrayBuffer(stream())) instanceof ArrayBuffer);
    eq(7, (await Bun.readableStreamToBlob(stream())).size);
    eq(2, (await Bun.readableStreamToArray(stream())).length);
  },

  'Glob matches like Bun and scans a directory'() {
    const g = new Bun.Glob('**/*.js');

    eq('101', bits([g.match('sub/c.js'), g.match('a.ts'), g.match('.h.js')]));
    eq('1100', bits(['a/b', 'a/x/y/b', 'a/bc', 'b'].map(p => new Bun.Glob('a/**/b').match(p))));
    eq('011', bits(['src', 'src/a', 'src/a/b'].map(p => new Bun.Glob('src/**').match(p))));
    eq('0010', bits(['a.js', 'b.ts', 'c.md', 'x/a.ts'].map(p => new Bun.Glob('!{*.js,**/*.ts}').match(p))));

    fs.mkdirSync(at('g/sub'), { recursive: true });
    for(const f of ['a.js', 'b.ts', '.h.js', 'sub/c.js']) fs.writeFileSync(at('g/' + f), '');

    eq('a.js,sub/c.js', [...g.scanSync({ cwd: at('g') })].sort().join());
    eq('.h.js,a.js,sub/c.js', [...g.scanSync({ cwd: at('g'), dot: true })].sort().join());
    eq('a.js,b.ts,sub', [...new Bun.Glob('*').scanSync({ cwd: at('g'), onlyFiles: false })].sort().join());
    assert([...g.scanSync({ cwd: at('g'), absolute: true })].every(p => p.startsWith('/')));
  },

  'mmap maps a file read-write, shared by default'() {
    fs.writeFileSync(at('m'), 'hello');

    const m = Bun.mmap(at('m'));

    eq(5, m.length);
    m[0] = 72;
    eq('Hello', fs.readFileSync(at('m'), 'utf8'));

    const p = Bun.mmap(at('m'), { shared: false });

    p[0] = 74;
    eq('Hello', fs.readFileSync(at('m'), 'utf8'));

    let err;

    try {
      Bun.mmap(at('missing'));
    } catch(e) {
      err = e;
    }

    eq('ENOENT', err?.code);
    eq(`ENOENT: no such file or directory, open '${at('missing')}'`, err?.message);
  },

  'process properties and aliases are there'() {
    eq(process.env, Bun.env);
    eq(process.argv, Bun.argv);
    assert(Bun.main.endsWith('test-bun-util.js'));
    eq(true, Bun.isMainThread);
    eq('', Bun.origin);
    eq('boolean', typeof Bun.enableANSIColors);
    eq('string', typeof Bun.version);
    eq(Symbol.for('nodejs.util.inspect.custom'), Bun.inspect.custom);
    assert(/a: 1/.test(Bun.inspect({ a: 1 })));
    eq('function,function,function,function', ['hash', 'hashSync', 'verify', 'verifySync'].map(k => typeof Bun.password[k]).join());
    eq(2, Bun.JSONL.parse('1\n2\n').length);
    eq('function', typeof Bun.ArrayBufferSink);
    eq('/tmp/x', Bun.fileURLToPath('file:///tmp/x'));
  },
});
