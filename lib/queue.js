const EMPTY = new Uint8Array(0);

/**
 * A queue iterator.
 *
 * @class      QueueIterator (name)
 *
 * @param  {Queue} queue    The queue
 */
export class QueueIterator {
  #queue;

  constructor(queue) {
    this.#queue = queue;
  }

  next() {
    const chunk = this.#queue.next();
    if(chunk === null) return { value: undefined, done: true };
    return { value: chunk, done: false };
  }

  [Symbol.iterator]() {
    return this;
  }

  get [Symbol.toStringTag]() {
    return 'QueueIterator';
  }
}

/**
 * A chunked byte queue (FIFO buffer of binary data).
 *
 * Mirrors the semantics of quickjs-queue.c: data is stored as a list of
 * independent chunks; reads/peeks/skips operate from the head and may consume
 * partial chunks; writes append chunks to the tail.
 *
 * Chunks are stored internally as Uint8Array views. To keep semantics simple
 * and stable, a chunk is *never* shared with caller-owned memory: writes copy
 * the input into a fresh Uint8Array.
 */
export class Queue {
  /** @type {Uint8Array[]} */
  #chunks = [];

  constructor() {
    /* length is 1 to match the C constructor signature; the C ctor ignores args. */
    if(arguments.length > 0) this.write(arguments[0]);
  }

  /**
   * Append data to the tail. Returns the number of bytes written.
   * @param {string|ArrayBuffer|ArrayBufferView|Uint8Array} data
   * @returns {number}
   */
  write(data) {
    const bytes = toBytes(data);

    if(bytes.length === 0) return 0;

    this.#chunks.push(bytes.slice());
    return bytes.length;
  }

  /**
   * Remove and return up to `size` bytes from the head.
   * @param {number} size
   * @returns {Uint8Array}
   */
  read(size) {
    size = Math.max(0, Math.floor(Number(size) || 0));
    if(size === 0 || this.#chunks.length === 0) return EMPTY;

    const out = [];
    let remaining = size;
    let total = 0;

    while(remaining > 0 && this.#chunks.length > 0) {
      const head = this.#chunks[0];

      if(head.length <= remaining) {
        out.push(head);
        total += head.length;
        remaining -= head.length;
        this.#chunks.shift();
      } else {
        out.push(head.subarray(0, remaining));
        total += remaining;
        this.#chunks[0] = head.subarray(remaining);
        remaining = 0;
      }
    }

    return concat(out, total);
  }

  /**
   * Return up to `size` bytes from the head without consuming them.
   * @param {number} size
   * @returns {Uint8Array}
   */
  peek(size) {
    size = Math.max(0, Math.floor(Number(size) || 0));
    if(size === 0 || this.#chunks.length === 0) return EMPTY;

    const out = [];
    let remaining = size;
    let total = 0;

    for(const c of this.#chunks) {
      if(remaining <= 0) break;
      const take = Math.min(c.length, remaining);
      out.push(c.subarray(0, take));
      total += take;
      remaining -= take;
    }

    return concat(out, total);
  }

  /**
   * Discard up to `size` bytes from the head. Returns bytes discarded.
   * @param {number} size
   * @returns {number}
   */
  skip(size) {
    size = Math.max(0, Math.floor(Number(size) || 0));
    if(size === 0) return 0;

    let skipped = 0;

    while(skipped < size && this.#chunks.length > 0) {
      const head = this.#chunks[0];
      const remaining = size - skipped;

      if(head.length <= remaining) {
        skipped += head.length;
        this.#chunks.shift();
      } else {
        this.#chunks[0] = head.subarray(remaining);
        skipped += remaining;
      }
    }

    return skipped;
  }

  /**
   * Empty the queue.
   */
  clear() {
    this.#chunks = [];
  }

  /**
   * Return and remove the next whole chunk from the head. Returns null when
   * the queue is empty.
   * @returns {Uint8Array|null}
   */
  next() {
    if(this.#chunks.length === 0) return null;
    return this.#chunks.shift();
  }

  /**
   * Return a specific buffered chunk by index. Negative indices count from
   * the tail (-1 = last chunk). Returns null when out of range.
   *
   * When `returnPos` is true, returns the absolute byte position of that
   * chunk's start (relative to the head of the queue) instead of the chunk
   * data, mirroring C's `chunk_headpos()`/`chunk_tailpos()`.
   *
   * @param {number} index
   * @param {boolean} [returnPos=false]
   * @returns {Uint8Array|number|null}
   */
  chunk(index, returnPos = false) {
    index = Math.trunc(Number(index));
    const n = this.#chunks.length;
    const idx = index < 0 ? n + index : index;

    if(idx < 0 || idx >= n) return null;

    if(returnPos) {
      let pos = 0;
      for(let i = 0; i < idx; i++) pos += this.#chunks[i].length;
      return pos;
    }

    return this.#chunks[idx];
  }

  /**
   * Return the chunk containing the byte at absolute offset `offset` from the
   * head of the queue, plus the offset within that chunk, as `[chunk, skip]`.
   * Negative offsets count from the tail. Returns null when out of range.
   *
   * @param {number} offset
   * @returns {[Uint8Array, number]|null}
   */
  at(offset) {
    offset = Math.trunc(Number(offset));
    const n = this.#chunks.length;
    if(n === 0) return null;

    const total = this.size;
    let target;

    if(offset < 0) {
      target = total + offset;
      if(target < 0) return null;
    } else {
      target = offset;
      if(target >= total) return null;
    }

    let pos = 0;

    for(let i = 0; i < n; i++) {
      const c = this.#chunks[i];

      if(pos + c.length > target) return [c, target - pos];

      pos += c.length;
    }

    return null;
  }

  /** Total number of buffered bytes. */
  get size() {
    let total = 0;
    for(const c of this.#chunks) total += c.length;
    return total;
  }

  /** Whether the queue holds no data. */
  get empty() {
    return this.#chunks.length === 0;
  }

  /** The head chunk (first buffered chunk), or undefined when empty. */
  get head() {
    return this.#chunks.length > 0 ? this.#chunks[0] : undefined;
  }

  /** The tail chunk (last buffered chunk), or undefined when empty. */
  get tail() {
    return this.#chunks.length > 0 ? this.#chunks[this.#chunks.length - 1] : undefined;
  }

  /**
   * The list of buffered chunks (defensive copy).
   *
   * The C getter returns the *count* (`queue->nchunks`); the md docs describe
   * it as "the list of buffered chunks". We follow the docs. If you want the
   * raw C-number semantics, change to `return this.#chunks.length;`.
   */
  get chunks() {
    return this.#chunks.slice();
  }

  /**
   * Return a new QueueIterator over the queue's chunks.
   * @returns {QueueIterator}
   */
  [Symbol.iterator]() {
    return new QueueIterator(this);
  }

  get [Symbol.toStringTag]() {
    return 'Queue';
  }
}

/** Normalise any accepted input into a Uint8Array view (no copy). */
function toBytes(data) {
  if(data == null) return EMPTY;
  if(data instanceof Uint8Array) return data;

  if(typeof ArrayBuffer !== 'undefined') {
    if(data instanceof ArrayBuffer) return new Uint8Array(data);

    if(ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  }

  if(typeof data === 'string') {
    if(typeof TextEncoder !== 'undefined') return new TextEncoder().encode(data);

    return Buffer.from(data, 'utf8');
  }

  if(typeof TextEncoder !== 'undefined') return new TextEncoder().encode(String(data));

  return Buffer.from(String(data), 'utf8');
}

/** Concatenate a list of Uint8Arrays into one new Uint8Array. */
function concat(chunks, total) {
  if(total === 0) return EMPTY;
  if(chunks.length === 1) return chunks[0].slice();

  const out = new Uint8Array(total);
  let off = 0;

  for(const c of chunks) {
    out.set(c, off);
    off += c.length;
  }

  return out;
}

export default Queue;
