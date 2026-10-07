// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/globals.js: the web/Node globals; the same assertions must hold on node, bun and deno */
import { assert, eq, tests } from '../../lib/tinytest.js';

/* qjsm has none of these until lib/globals.js is imported */
if(typeof URL == 'undefined') await import('../../lib/globals.js');

tests({
  'the globals exist'() {
    for(const name of ['URL', 'URLSearchParams', 'TextEncoder', 'TextDecoder', 'AbortController', 'AbortSignal', 'EventTarget', 'Blob', 'ReadableStream', 'WritableStream', 'TransformStream', 'atob', 'btoa', 'queueMicrotask', 'structuredClone', 'setImmediate', 'clearImmediate', 'crypto'])
      assert(typeof globalThis[name] != 'undefined', name);
  },
  'URL parses'() {
    const u = new URL('https://u:p@example.com:8080/a/b?x=1&y=2#h');

    eq(u.hostname, 'example.com');
    eq(u.port, '8080');
    eq(u.pathname, '/a/b');
    eq(u.searchParams.get('y'), '2');
    eq(u.hash, '#h');
  },
  'TextEncoder and TextDecoder round-trip UTF-8'() {
    const bytes = new TextEncoder().encode('héllo €');

    eq(bytes.length, 10);
    eq(new TextDecoder().decode(bytes), 'héllo €');
  },
  'atob and btoa'() {
    eq(btoa('hello'), 'aGVsbG8=');
    eq(atob('aGVsbG8='), 'hello');
  },
  async 'queueMicrotask runs before a timer'() {
    const order = [];

    setTimeout(() => order.push('timer'), 0);
    queueMicrotask(() => order.push('micro'));
    await new Promise(r => setTimeout(r, 5));
    eq(order.join(), 'micro,timer');
  },
  'structuredClone copies deeply, keeps cycles and types'() {
    const src = { d: new Date(5), m: new Map([[1, [2]]]), s: new Set([1]), r: /a/g, u: new Uint8Array([1, 2]), n: { x: [1] } };

    src.self = src;

    const c = structuredClone(src);

    assert(c !== src && c.self === c);
    eq(c.d.getTime(), 5);
    eq(c.m.get(1)[0], 2);
    assert(c.s.has(1));
    eq(c.r.flags, 'g');
    eq(c.u.join(), '1,2');
    assert(c.n.x !== src.n.x);
  },
  'structuredClone rejects functions with DataCloneError'() {
    let name;

    try {
      structuredClone({ f() {} });
    } catch(e) {
      name = e.name;
    }
    eq(name, 'DataCloneError');
  },
  'structuredClone transfer, shared buffers, uncloneable objects, arity'() {
    const code = f => {
      try {
        f();
      } catch(e) {
        return e.name;
      }
    };
    const b = new ArrayBuffer(4), view = new Uint8Array(b, 1, 2);
    const c = structuredClone({ view }, { transfer: [b] });

    eq(b.byteLength, 0);
    eq(c.view.buffer.byteLength, 4);
    eq(c.view.byteOffset, 1);
    eq(code(() => structuredClone(b, { transfer: [b] })), 'DataCloneError');
    eq(code(() => structuredClone(1, { transfer: [{}] })), 'DataCloneError');

    const sab = new SharedArrayBuffer(2);

    new Uint8Array(sab)[0] = 7;
    eq(new Uint8Array(structuredClone(sab))[0], 7);
    eq(code(() => structuredClone(Promise.resolve())), 'DataCloneError');
    eq(code(() => structuredClone(new WeakMap())), 'DataCloneError');
    eq(code(() => structuredClone()), 'TypeError');
  },
  'crypto.getRandomValues and randomUUID'() {
    const a = crypto.getRandomValues(new Uint8Array(16));

    assert(a.some(x => x != 0));
    assert(/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(crypto.randomUUID()));
  },
  async 'AbortController aborts its signal'() {
    const ac = new AbortController();
    let fired = false;

    ac.signal.addEventListener('abort', () => (fired = true));
    ac.abort();
    assert(ac.signal.aborted && fired);
  },
  async 'Blob stream() pipes through a TextDecoderStream'() {
    const chunks = [];

    await new Blob(['ab', 'cd'])
      .stream()
      .pipeThrough(new TextDecoderStream())
      .pipeTo(new WritableStream({ write: c => void chunks.push(c) }));
    eq(chunks.join(''), 'abcd');
  },
  async 'Blob text and size'() {
    const b = new Blob(['ab', 'cd']);

    eq(b.size, 4);
    eq(await b.text(), 'abcd');
  },
});
