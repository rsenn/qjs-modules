import * as std from 'std';
import { ArrayBufferSink } from 'arraybuffer_sink';
import { assert, eq, tests } from '../../lib/tinytest.js';

function bytes(arrayBuffer) {
  return [...new Uint8Array(arrayBuffer)];
}

tests({
  'constructor with no args'() {
    const sink = new ArrayBufferSink();
    assert(sink instanceof ArrayBufferSink);
    eq(sink.size, 0);
  },
  'flush() on an empty sink returns undefined'() {
    const sink = new ArrayBufferSink();
    eq(sink.flush(), undefined);
  },
  'end() on an empty sink returns undefined'() {
    const sink = new ArrayBufferSink();
    eq(sink.end(), undefined);
  },
  'write() accepts a string, an ArrayBuffer and a typed array'() {
    const sink = new ArrayBufferSink();
    eq(sink.write('abc'), 3);
    eq(sink.write(new Uint8Array([1, 2, 3]).buffer), 3);
    eq(sink.write(new Uint8Array([9, 9])), 2);
    eq(sink.size, 8);
  },
  'flush() returns accumulated bytes and resets the buffer'() {
    const sink = new ArrayBufferSink();
    sink.write('abc');
    sink.write(new Uint8Array([1, 2, 3]));

    const flushed = sink.flush();
    eqArr(bytes(flushed), [97, 98, 99, 1, 2, 3]);
    eq(sink.size, 0);

    /* the sink keeps accepting writes after a flush */
    eq(sink.write('xy'), 2);
    eq(sink.size, 2);
  },
  'end() finalizes the sink and returns the complete buffer'() {
    const sink = new ArrayBufferSink();
    sink.write('ab');
    sink.write('cd');

    const ended = sink.end();
    eqArr(bytes(ended), [97, 98, 99, 100]);
  },
  'write() throws after end()'() {
    const sink = new ArrayBufferSink();
    sink.write('x');
    sink.end();

    let threw = false;
    try {
      sink.write('y');
    } catch(e) {
      threw = true;
    }
    assert(threw);
  },
  'size getter tracks buffered bytes across writes and flush'() {
    const sink = new ArrayBufferSink();
    eq(sink.size, 0);
    sink.write('12345');
    eq(sink.size, 5);
    sink.write('67');
    eq(sink.size, 7);
    sink.flush();
    eq(sink.size, 0);
  },
  'discarded flush()/end() buffers can be garbage collected'() {
    for(let i = 0; i < 50; i++) {
      const sink = new ArrayBufferSink();

      sink.write('a'.repeat(1000 + i));
      sink.flush();
      sink.write('b');
      sink.end();
    }

    std.gc();
  },
});

function eqArr(actual, expected) {
  eq(JSON.stringify(actual), JSON.stringify(expected));
}
