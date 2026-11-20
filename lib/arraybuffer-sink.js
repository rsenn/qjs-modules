/**
 * ArrayBufferSink — incremental byte accumulator (Bun-style).
 *
 * Mirrors quickjs-arraybuffer-sink.c: write() appends to an internal growing
 * buffer; flush() returns everything accumulated so far and resets the buffer;
 * end() returns the final ArrayBuffer and permanently closes the sink
 * (subsequent write() calls throw, matching the C backend which NULLs
 * `realloc_func` on end()).
 *
 * Internally we keep a growable Uint8Array with a separate `length` marker,
 * mimicking DynBuf's (buf, size, allocated_size) triple. `flush()` copies out
 * the live bytes and resets `length` to 0 (DynBuf's `dbuf_toarraybuffer` in
 * the C code returns the buffer's *current* contents without discarding the
 * backing store — but it also resets `s->size` to 0 via `dbuf_toarraybuffer`,
 * so the observable effect is "reset the logical buffer").
 */

const DYNBUF_SYMBOL = Symbol.for('qjs.dynbuf');
const EMPTY_AB = new ArrayBuffer(0);

/** Normalise any accepted input into a Uint8Array view (no copy). */
function toBytes(data) {
  if(data == null) return new Uint8Array(0);
  if(data instanceof Uint8Array) return data;
  if(data instanceof ArrayBuffer) return new Uint8Array(data);
  if(ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  if(typeof data === 'string') {
    if(typeof TextEncoder !== 'undefined') return new TextEncoder().encode(data);
    return Buffer.from(data, 'utf8');
  }
  if(typeof TextEncoder !== 'undefined') return new TextEncoder().encode(String(data));
  return Buffer.from(String(data), 'utf8');
}

export class ArrayBufferSink {
  #buf; /* Uint8Array backing store */
  #size; /* number of live bytes in #buf */
  #ended; /* true after end() */
  #addr; /* stable pseudo-address for [Symbol.for('qjs.dynbuf')] */

  constructor(_options) {
    this.#buf = new Uint8Array(0);
    this.#size = 0;
    this.#ended = false;
    /* Stable-ish identity for C-side callers. Not a real pointer, but unique
       per instance and non-zero, so callers that just use it as an opaque key
       keep working. Real native address isn't available in pure JS. */
    this.#addr = (ArrayBufferSink.#nextAddr = ArrayBufferSink.#nextAddr + 1) | 0;
  }

  static #nextAddr = 0;

  /**
   * Append a chunk. Returns the number of bytes written.
   * Throws if the sink has been ended (matches the C backend, which checks
   * `realloc_func` and throws `"ArrayBufferSink has ended"`).
   *
   * @param {string|ArrayBuffer|ArrayBufferView|Uint8Array} chunk
   * @returns {number}
   */
  write(chunk) {
    if(this.#ended) throw new Error('ArrayBufferSink has ended');

    const bytes = toBytes(chunk);
    if(bytes.length === 0) return 0;

    this.#ensure(this.#size + bytes.length);
    this.#buf.set(bytes, this.#size);
    this.#size += bytes.length;
    return bytes.length;
  }

  /**
   * Return the bytes accumulated so far as a fresh ArrayBuffer and reset the
   * logical buffer. The sink remains usable after flush().
   *
   * @returns {ArrayBuffer}
   */
  flush() {
    if(this.#size === 0) return EMPTY_AB.slice(0);
    const out = this.#buf.buffer.slice(this.#buf.byteOffset, this.#buf.byteOffset + this.#size);
    this.#size = 0;
    return out;
  }

  /**
   * Finalize the sink: return the complete accumulated ArrayBuffer and mark
   * the sink as ended. Subsequent write() calls throw. flush() on an ended
   * sink still returns whatever bytes were buffered at end() time (which is
   * zero after end(), matching C).
   *
   * @returns {ArrayBuffer}
   */
  end() {
    const out = this.#size === 0 ? EMPTY_AB.slice(0) : this.#buf.buffer.slice(this.#buf.byteOffset, this.#buf.byteOffset + this.#size);
    this.#size = 0;
    this.#ended = true;
    return out;
  }

  /** Number of bytes currently buffered. */
  get size() {
    return this.#size;
  }

  get [Symbol.toStringTag]() {
    return 'ArrayBufferSink';
  }

  /**
   * C-callers use this to get the address of the underlying DynBuf and write
   * to it directly. In pure JS there is no DynBuf; we return a stable
   * per-instance identifier so code that treats the return as an opaque key
   * still works. Code that tries to dereference it as a real pointer will not
   * work — that's inherently a C-only feature.
   *
   * @returns {number}
   */
  [DYNBUF_SYMBOL]() {
    return this.#addr;
  }

  #ensure(capacity) {
    if(this.#buf.length >= capacity) return;
    let newCap = this.#buf.length === 0 ? 64 : this.#buf.length;
    while(newCap < capacity) newCap = (newCap * 3) >>> 1 || 1;
    const next = new Uint8Array(newCap);
    next.set(this.#buf.subarray(0, this.#size));
    this.#buf = next;
  }
}

/* Ensure the well-known symbol method is exposed under the same symbol the C
 * module uses — Symbol.for('qjs.dynbuf') — so `sink[Symbol.for('qjs.dynbuf')]()`
 * works without importing DYNBUF_SYMBOL. */
Object.defineProperty(ArrayBufferSink.prototype, DYNBUF_SYMBOL, {
  value: ArrayBufferSink.prototype[DYNBUF_SYMBOL],
  writable: true,
  configurable: true,
});

export default ArrayBufferSink;
