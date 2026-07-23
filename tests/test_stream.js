import {
  ReadableStream,
  WritableStream,
  TransformStream,
  ByteLengthQueuingStrategy,
  CountQueuingStrategy,
} from 'stream';
import { assert, eq, tests } from './tinytest.js';

/* ---------- helpers ---------- */

function withTimeout(p, ms, label) {
  let to;
  const timeout = new Promise((_, reject) => (to = setTimeout(() => reject(new Error('TIMEOUT: ' + label)), ms)));
  return Promise.race([p, timeout]).then(
    v => (clearTimeout(to), v),
    e => {
      clearTimeout(to);
      throw e;
    },
  );
}

async function assertRejects(p, msg) {
  try {
    await p;
  } catch(e) {
    return e;
  }
  throw new Error('assertRejects(): did not reject' + (msg ? ' - ' + msg : ''));
}

function assertThrows(fn, msg) {
  try {
    fn();
  } catch(e) {
    return e;
  }
  throw new Error('assertThrows(): did not throw' + (msg ? ' - ' + msg : ''));
}

/* tinytest's eq() uses != , which does reference comparison for arrays/objects - deep-compare via JSON instead. */
const eqArr = (actual, expected) => eq(JSON.stringify(actual), JSON.stringify(expected));

async function collect(rs) {
  const reader = rs.getReader();
  const out = [];
  for(;;) {
    const { value, done } = await withTimeout(reader.read(), 1000, 'collect() read');
    if(done) break;
    out.push(value);
  }
  reader.releaseLock();
  return out;
}

function decodeChunks(chunks) {
  let s = '';
  for(const chunk of chunks) for(const byte of chunk) s += String.fromCharCode(byte);
  return s;
}

await tests({
  /* ==================== ReadableStream: construction & start() ==================== */

  async 'ReadableStream: start() receives a controller exactly once'() {
    let calls = 0;
    let ctrl;
    const rs = new ReadableStream({
      start(controller) {
        calls++;
        ctrl = controller;
      },
    });
    eq(calls, 1);
    assert(typeof ctrl.enqueue === 'function');
    assert(typeof ctrl.close === 'function');
    assert(typeof ctrl.error === 'function');
    rs.cancel();
  },

  async 'ReadableStream: enqueue() then read() delivers chunks in order'() {
    const rs = new ReadableStream({
      start(controller) {
        controller.enqueue('a');
        controller.enqueue('b');
        controller.enqueue('c');
        controller.close();
      },
    });
    eqArr(await collect(rs), ['a', 'b', 'c']);
  },

  async 'ReadableStream: read() after close() resolves {value: undefined, done: true}'() {
    const rs = new ReadableStream({
      start(controller) {
        controller.enqueue(1);
        controller.close();
      },
    });
    const reader = rs.getReader();
    eq((await withTimeout(reader.read(), 1000, 'first read')).value, 1);

    const second = await withTimeout(reader.read(), 1000, 'second read');
    eq(second.done, true);
    eq(second.value, undefined);

    const third = await withTimeout(reader.read(), 1000, 'third read (after done)');
    eq(third.done, true);
  },

  async 'ReadableStream: reader.closed resolves once the stream closes'() {
    const rs = new ReadableStream({
      start(controller) {
        controller.close();
      },
    });
    const reader = rs.getReader();
    await withTimeout(reader.closed, 1000, 'reader.closed');
    assert(true);
  },

  async 'ReadableStream: pull() is invoked to refill an empty queue'() {
    let pulls = 0;
    const rs = new ReadableStream({
      pull(controller) {
        pulls++;
        controller.enqueue(pulls);
        if(pulls >= 3) controller.close();
      },
    });
    eqArr(await collect(rs), [1, 2, 3]);
    assert(pulls >= 3);
  },

  async 'ReadableStream: cancel() invokes the underlying source cancel(reason)'() {
    let seenReason;
    const rs = new ReadableStream({
      start(controller) {
        controller.enqueue('x');
      },
      cancel(reason) {
        seenReason = reason;
      },
    });
    await withTimeout(rs.cancel('bye'), 1000, 'rs.cancel');
    eq(seenReason, 'bye');
  },

  async 'ReadableStream: reader.cancel() releases values and resolves'() {
    const rs = new ReadableStream({
      start(controller) {
        controller.enqueue(1);
      },
    });
    const reader = rs.getReader();
    await withTimeout(reader.cancel('nope'), 1000, 'reader.cancel');

    const { done } = await withTimeout(reader.read(), 1000, 'read after cancel');
    eq(done, true);
  },

  /* ==================== ReadableStream: locking ==================== */

  async 'ReadableStream: locked reflects whether a reader is held'() {
    const rs = new ReadableStream({
      start(c) {
        c.close();
      },
    });
    eq(rs.locked, false);
    const reader = rs.getReader();
    eq(rs.locked, true);
    reader.releaseLock();
    eq(rs.locked, false);
  },

  async 'ReadableStream: getReader() while locked throws'() {
    const rs = new ReadableStream({
      start(c) {
        c.close();
      },
    });
    rs.getReader();
    assertThrows(() => rs.getReader(), 'second getReader() while locked');
  },

  async 'ReadableStream: getReader() again after releaseLock() succeeds'() {
    const rs = new ReadableStream({
      start(controller) {
        controller.enqueue('only');
        controller.close();
      },
    });
    const r1 = rs.getReader();
    r1.releaseLock();
    const r2 = rs.getReader();
    const { value } = await withTimeout(r2.read(), 1000, 're-locked read');
    eq(value, 'only');
  },

  /* ==================== ReadableStream: errors ==================== */

  async 'ReadableStream: controller.error() rejects pending and future reads'() {
    const rs = new ReadableStream({
      start(controller) {
        controller.error(new Error('boom'));
      },
    });
    const reader = rs.getReader();
    const e = await assertRejects(withTimeout(reader.read(), 1000, 'read after error'));
    assert(/boom/.test(e.message), 'rejection should carry the error: ' + e);
  },

  async 'ReadableStream: a throwing start() throws synchronously out of the constructor'() {
    const e = assertThrows(() => {
      new ReadableStream({
        start() {
          throw new Error('start failed');
        },
      });
    });
    assert(/start failed/.test(e.message), String(e));
  },

  /* ==================== ReadableStream: async iteration ==================== */

  async 'ReadableStream: for-await-of yields enqueued chunks then ends'() {
    const rs = new ReadableStream({
      start(controller) {
        controller.enqueue(10);
        controller.enqueue(20);
        controller.close();
      },
    });
    const out = [];
    for await(const v of rs) out.push(v);
    eqArr(out, [10, 20]);
  },

  async 'ReadableStream: breaking out of for-await-of releases/cancels the stream'() {
    let cancelled = false;
    const rs = new ReadableStream({
      start(controller) {
        controller.enqueue(1);
        controller.enqueue(2);
        controller.enqueue(3);
      },
      cancel() {
        cancelled = true;
      },
    });
    for await(const v of rs) {
      if(v === 1) break;
    }
    await withTimeout(Promise.resolve(), 100, 'settle');
    assert(cancelled, 'breaking the loop should cancel the source');
  },

  /* ==================== WritableStream ==================== */

  async 'WritableStream: write() forwards chunks to the sink in order'() {
    const chunks = [];
    const ws = new WritableStream({
      write(chunk) {
        chunks.push(chunk);
      },
    });
    const writer = ws.getWriter();
    await withTimeout(writer.write('a'), 1000, 'write a');
    await withTimeout(writer.write('b'), 1000, 'write b');
    eqArr(chunks, ['a', 'b']);
  },

  async 'WritableStream: close() calls sink.close() and resolves writer.closed'() {
    let closed = false;
    const ws = new WritableStream({
      write() {},
      close() {
        closed = true;
      },
    });
    const writer = ws.getWriter();
    await withTimeout(writer.close(), 1000, 'writer.close');
    assert(closed);
    await withTimeout(writer.closed, 1000, 'writer.closed');
  },

  async 'WritableStream: abort() calls sink.abort(reason)'() {
    let seenReason;
    const ws = new WritableStream({
      write() {},
      abort(reason) {
        seenReason = reason;
      },
    });
    const writer = ws.getWriter();
    await withTimeout(writer.abort('stop'), 1000, 'writer.abort');
    eq(seenReason, 'stop');
  },

  async 'WritableStream: a rejecting write() errors the stream for subsequent writes'() {
    const ws = new WritableStream({
      write(chunk) {
        if(chunk === 'bad') return Promise.reject(new Error('write failed'));
      },
    });
    const writer = ws.getWriter();
    await assertRejects(withTimeout(writer.write('bad'), 1000, 'bad write'));
    await withTimeout(writer.closed, 1000, 'writer.closed after error').catch(() => {});
    await assertRejects(withTimeout(writer.write('after'), 1000, 'write after error'));
  },

  /* ==================== WritableStream: locking & getWriter ==================== */

  async 'WritableStream: locked reflects whether a writer is held'() {
    const ws = new WritableStream({ write() {} });
    eq(ws.locked, false);
    const writer = ws.getWriter();
    eq(ws.locked, true);
    writer.releaseLock();
    eq(ws.locked, false);
  },

  async 'WritableStream: getWriter() while locked throws'() {
    const ws = new WritableStream({ write() {} });
    ws.getWriter();
    assertThrows(() => ws.getWriter());
  },

  /* ==================== WritableStream: backpressure ==================== */

  async 'WritableStream: desiredSize/ready reflect backpressure under CountQueuingStrategy'() {
    const release = [];
    const ws = new WritableStream(
      {
        write() {
          return new Promise(resolve => release.push(resolve));
        },
      },
      new CountQueuingStrategy({ highWaterMark: 1 }),
    );
    const writer = ws.getWriter();

    await withTimeout(writer.ready, 1000, 'initial ready');

    const w1 = writer.write('a');
    const w2 = writer.write('b'); // exceeds highWaterMark=1 -> backpressure

    assert(writer.desiredSize <= 0, 'desiredSize should be <= 0 under backpressure, got ' + writer.desiredSize);

    let readyResolved = false;
    writer.ready.then(() => (readyResolved = true));
    await withTimeout(Promise.resolve(), 50, 'settle');
    assert(!readyResolved, 'ready should still be pending while backpressured');

    release.shift()();
    await withTimeout(w1, 1000, 'w1 settles');
    release.shift()();
    await withTimeout(w2, 1000, 'w2 settles');

    await withTimeout(writer.ready, 1000, 'ready resolves once queue drains');
  },

  /* ==================== TransformStream ==================== */

  async 'TransformStream: transform() maps each chunk before it reaches readable'() {
    const ts = new TransformStream({
      transform(chunk, controller) {
        controller.enqueue(chunk.toUpperCase());
      },
    });
    const writer = ts.writable.getWriter();
    const reader = ts.readable.getReader();

    writer.write('hi');
    writer.close();

    eq((await withTimeout(reader.read(), 1000, 'transform read')).value, 'HI');
    eq((await withTimeout(reader.read(), 1000, 'transform read done')).done, true);
  },

  async 'TransformStream: default identity transform passes chunks through'() {
    const ts = new TransformStream();
    const writer = ts.writable.getWriter();
    const reader = ts.readable.getReader();

    writer.write('passthrough');
    writer.close();

    eq((await withTimeout(reader.read(), 1000, 'identity read')).value, 'passthrough');
  },

  async 'TransformStream: flush() can enqueue a final chunk on close'() {
    const ts = new TransformStream({
      transform(chunk, controller) {
        controller.enqueue(chunk);
      },
      flush(controller) {
        controller.enqueue('flushed');
      },
    });
    const writer = ts.writable.getWriter();
    writer.write('x');
    writer.close();

    eqArr(await collect(ts.readable), ['x', 'flushed']);
  },

  async 'TransformStream: a throwing transform() errors both sides'() {
    const ts = new TransformStream({
      transform() {
        throw new Error('transform failed');
      },
    });
    const writer = ts.writable.getWriter();
    const reader = ts.readable.getReader();

    writer.write('x').catch(() => {});
    const e = await assertRejects(withTimeout(reader.read(), 1000, 'read after transform error'));
    assert(/transform failed/.test(e.message), String(e));
  },

  /* ==================== piping ==================== */

  async 'pipeTo(): transfers all chunks and resolves; destination closes by default'() {
    const rs = new ReadableStream({
      start(controller) {
        controller.enqueue(1);
        controller.enqueue(2);
        controller.close();
      },
    });
    const chunks = [];
    let closed = false;
    const ws = new WritableStream({
      write(c) {
        chunks.push(c);
      },
      close() {
        closed = true;
      },
    });

    await withTimeout(rs.pipeTo(ws), 1000, 'pipeTo');
    eqArr(chunks, [1, 2]);
    assert(closed, 'destination should be closed by default');
  },

  async 'pipeTo({preventClose: true}): destination is left open'() {
    const rs = new ReadableStream({
      start(controller) {
        controller.enqueue('x');
        controller.close();
      },
    });
    let closed = false;
    const ws = new WritableStream({
      write() {},
      close() {
        closed = true;
      },
    });

    await withTimeout(rs.pipeTo(ws, { preventClose: true }), 1000, 'pipeTo preventClose');
    assert(!closed, 'destination should not be closed when preventClose is set');

    const writer = ws.getWriter();
    await withTimeout(writer.close(), 1000, 'manual close');
  },

  async 'pipeThrough(): chains a TransformStream onto a ReadableStream'() {
    const rs = new ReadableStream({
      start(controller) {
        controller.enqueue('a');
        controller.enqueue('b');
        controller.close();
      },
    });
    const ts = new TransformStream({
      transform(chunk, controller) {
        controller.enqueue(chunk.toUpperCase());
      },
    });

    const out = await collect(rs.pipeThrough(ts));
    eqArr(out, ['A', 'B']);
  },

  /* ==================== tee() ==================== */

  async 'tee(): both branches independently observe every chunk'() {
    const rs = new ReadableStream({
      start(controller) {
        controller.enqueue(1);
        controller.enqueue(2);
        controller.close();
      },
    });
    const [a, b] = rs.tee();
    const [outA, outB] = await Promise.all([collect(a), collect(b)]);
    eqArr(outA, [1, 2]);
    eqArr(outB, [1, 2]);
  },

  /* ==================== queuing strategies ==================== */

  async 'CountQueuingStrategy: size() is 1 for every chunk'() {
    const strategy = new CountQueuingStrategy({ highWaterMark: 5 });
    eq(strategy.highWaterMark, 5);
    eq(strategy.size(), 1);
    eq(strategy.size('anything'), 1);
  },

  async 'ByteLengthQueuingStrategy: size() reflects chunk.byteLength'() {
    const strategy = new ByteLengthQueuingStrategy({ highWaterMark: 1024 });
    eq(strategy.highWaterMark, 1024);
    eq(strategy.size(new Uint8Array(16)), 16);
  },

  async 'ReadableStream: default queuing strategy applies backpressure to pull()'() {
    let pulls = 0;
    const rs = new ReadableStream(
      {
        pull(controller) {
          pulls++;
          controller.enqueue(pulls);
        },
      },
      new CountQueuingStrategy({ highWaterMark: 1 }),
    );
    const reader = rs.getReader();
    await withTimeout(reader.read(), 1000, 'first read');
    // pull() should be backpressure-limited by the highWaterMark, not run away filling the queue.
    assert(pulls <= 3, 'pull() should be backpressure-limited, got ' + pulls + ' calls');
  },

  /* ==================== BYOB (byte streams) ==================== */

  async 'BYOB: getReader({mode:"byob"}) requires type:"bytes"'() {
    const rs = new ReadableStream({ start(c) { c.close(); } });
    assertThrows(() => rs.getReader({ mode: 'byob' }));
  },

  async 'BYOB: read(view) fills the caller-supplied view from queued bytes'() {
    const rs = new ReadableStream({
      type: 'bytes',
      start(controller) {
        controller.enqueue(new Uint8Array([1, 2, 3, 4]));
        controller.close();
      },
    });
    const reader = rs.getReader({ mode: 'byob' });
    const buf = new Uint8Array(4);
    const { value, done } = await withTimeout(reader.read(buf), 1000, 'byob read');
    eqArr(Array.from(value), [1, 2, 3, 4]);
    eq(done, false);
    eq(value.buffer, buf.buffer, 'should be a view over the caller-supplied buffer (zero-copy)');

    const second = await withTimeout(reader.read(new Uint8Array(4)), 1000, 'byob read at EOF');
    eq(second.done, true);
  },

  async 'BYOB: read(view) delivers directly via byobRequest.respond() (zero-copy pull)'() {
    const source = new Uint8Array([9, 8, 7]);
    let sawByobRequest = false;
    const rs = new ReadableStream({
      type: 'bytes',
      pull(controller) {
        const req = controller.byobRequest;
        sawByobRequest = !!req;
        const view = req.view;
        view.set(source.subarray(0, view.byteLength));
        req.respond(Math.min(view.byteLength, source.byteLength));
      },
    });
    const reader = rs.getReader({ mode: 'byob' });
    const { value, done } = await withTimeout(reader.read(new Uint8Array(3)), 1000, 'byobRequest read');
    assert(sawByobRequest, 'pull() should see a byobRequest for a pending BYOB read');
    eqArr(Array.from(value), [9, 8, 7]);
    eq(done, false);
  },

  async 'BYOB: a plain (non-byob) reader still works on a byte stream, via autoAllocateChunkSize'() {
    const chunks = [4, 5];
    const rs = new ReadableStream({
      type: 'bytes',
      autoAllocateChunkSize: 4,
      pull(controller) {
        const req = controller.byobRequest;
        const view = req.view;
        if(chunks.length === 0) {
          controller.close();
          req.respond(0);
          return;
        }
        view[0] = chunks.shift();
        req.respond(1);
      },
    });
    const out = await collect(rs);
    eqArr(
      out.map(v => Array.from(v)),
      [[4], [5]],
    );
  },

  async 'BYOB: cancel() resolves pending BYOB reads with done:true'() {
    const rs = new ReadableStream({ type: 'bytes' });
    const reader = rs.getReader({ mode: 'byob' });
    const pending = reader.read(new Uint8Array(4));
    await withTimeout(reader.cancel('nope'), 1000, 'byob cancel');
    const { done } = await withTimeout(pending, 1000, 'pending byob read after cancel');
    eq(done, true);
  },

  /* ==================== ReadableStream.fromReader() (native-source integration) ==================== */

  async 'fromReader(): a string/buffer source is delivered as chunks that concatenate back to the original'() {
    const rs = ReadableStream.fromReader('hello world', 4);
    const chunks = await collect(rs);
    assert(chunks.length > 1, 'a small chunkSize should split the input into multiple chunks');
    eq(decodeChunks(chunks), 'hello world');
  },

  async 'fromReader(): a pull function fn(buf, len) -> bytesRead drives the stream'() {
    const data = 'streamed-via-function';
    let pos = 0;
    const fn = (buf, len) => {
      const view = new Uint8Array(buf);
      let n = 0;
      while(n < len && pos < data.length) view[n++] = data.charCodeAt(pos++);
      return n;
    };
    const chunks = await collect(ReadableStream.fromReader(fn, 5));
    eq(decodeChunks(chunks), data);
  },

  async 'fromReader(): an object exposing read(buf, len) as a method drives the stream'() {
    const data = 'via-method-object';
    let pos = 0;
    const source = {
      read(buf, len) {
        const view = new Uint8Array(buf);
        let n = 0;
        while(n < len && pos < data.length) view[n++] = data.charCodeAt(pos++);
        return n;
      },
    };
    const chunks = await collect(ReadableStream.fromReader(source, 6));
    eq(decodeChunks(chunks), data);
  },

  async 'fromReader(): delivered chunks are Uint8Array instances, not raw ArrayBuffers'() {
    const rs = ReadableStream.fromReader('x', 16);
    const reader = rs.getReader();
    const { value } = await withTimeout(reader.read(), 1000, 'fromReader chunk type');
    assert(value instanceof Uint8Array, 'chunk should be a Uint8Array');
  },
});
