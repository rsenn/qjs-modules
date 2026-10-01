import { Queue } from 'queue';
import { assert, eq, tests } from '../../lib/tinytest.js';

function str(arrayBuffer) {
  return [...new Uint8Array(arrayBuffer)].map(c => String.fromCharCode(c)).join('');
}

tests({
  'constructor with no args'() {
    const q = new Queue();
    assert(q instanceof Queue);
    eq(q.size, 0);
    eq(q.empty, true);
  },
  'write() appends data and returns the byte count'() {
    const q = new Queue();
    eq(q.write('hello'), 5);
    eq(q.size, 5);
    eq(q.empty, false);
  },
  'write() accepts a string, an ArrayBuffer and a typed array'() {
    const q = new Queue();
    eq(q.write('ab'), 2);
    eq(q.write(new Uint8Array([99, 100]).buffer), 2);
    eq(q.write(new Uint8Array([101, 102])), 2);
    eq(q.size, 6);
  },
  'read() consumes bytes in FIFO order into a destination buffer'() {
    const q = new Queue();
    q.write('hello world');

    const buf = new Uint8Array(5);
    eq(q.read(buf), 5);
    eq(str(buf.buffer), 'hello');
    eq(q.size, 6);

    const rest = new Uint8Array(20);
    const n = q.read(rest);
    eq(n, 6);
    eq(str(rest.buffer.slice(0, n)), ' world');
    eq(q.size, 0);
    eq(q.empty, true);
  },
  'peek() reads without consuming'() {
    const q = new Queue();
    q.write('abcdef');

    const buf = new Uint8Array(3);
    eq(q.peek(buf), 3);
    eq(str(buf.buffer), 'abc');
    eq(q.size, 6);

    /* peeking again returns the same leading bytes */
    const buf2 = new Uint8Array(3);
    eq(q.peek(buf2), 3);
    eq(str(buf2.buffer), 'abc');
  },
  'skip() discards bytes in FIFO order without returning them'() {
    const q = new Queue();
    q.write('abcdef');
    eq(q.skip(2), 2);
    eq(q.size, 4);

    const buf = new Uint8Array(4);
    q.read(buf);
    eq(str(buf.buffer), 'cdef');
  },
  'clear() empties the queue'() {
    const q = new Queue();
    q.write('data');
    q.clear();
    eq(q.size, 0);
    eq(q.empty, true);
  },
  'head/tail/chunks reflect the buffered chunks'() {
    /* counter-intuitively, `head` is the most recently written chunk and
     * `tail` is the oldest (the one read() consumes next) - verified
     * against actual behavior, not assumed from the name. */
    const q = new Queue();
    q.write('first');
    q.write('second');

    eq(q.chunks, 2);
    eq(str(q.head), 'second');
    eq(str(q.tail), 'first');

    const buf = new Uint8Array(5);
    q.read(buf);
    eq(str(buf.buffer), 'first');
  },
  'chunk(index) returns a specific buffered chunk'() {
    const q = new Queue();
    q.write('first');
    q.write('second');

    eq(str(q.chunk(0)), 'first');
    eq(str(q.chunk(1)), 'second');
    eq(q.chunk(99), null);
  },
  'at(offset) returns [chunk, offsetWithinChunk]'() {
    const q = new Queue();
    q.write('first');
    q.write('second');

    const [chunk, offset] = q.at(0);
    eq(str(chunk), 'first');
    eq(offset, 0);

    const [chunk2, offset2] = q.at(7);
    eq(str(chunk2), 'second');
    eq(offset2, 2);
  },
  'next() steps through chunks, returning null when exhausted'() {
    const q = new Queue();
    q.write('a');
    q.write('b');

    eq(str(q.next()), 'a');
    eq(str(q.next()), 'b');
    eq(q.next(), null);
  },
  '[Symbol.iterator] iterates the queued chunks'() {
    const q = new Queue();
    q.write('one');
    q.write('two');
    q.write('three');

    const chunks = [...q].map(str);
    eq(chunks.length, 3);
    eq(chunks.join(''), 'onetwothree');
  },
  'iterator is tagged QueueIterator, the Queue itself is tagged Queue'() {
    const q = new Queue();
    eq(Object.prototype.toString.call(q), '[object Queue]');
    eq(Object.prototype.toString.call(q[Symbol.iterator]()), '[object QueueIterator]');
  },
});
