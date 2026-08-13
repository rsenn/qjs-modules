import assert from './assert.js';

/*
 * Original from Chromium
 * https://chromium.googlesource.com/chromium/src/+/0aee4434a4dba42a42abaea9bfbc0cd196a63bc1/third_party/blink/renderer/core/streams/SimpleQueue.js
 */

const QUEUE_MAX_ARRAY_SIZE = 16384;

/**
 * Simple queue structure.
 *
 * Avoids scalability issues with using a packed array directly by using
 * multiple arrays in a linked list and keeping the array size bounded.
 */
export class SimpleQueue {
  #cursor;
  #size;
  #front;
  #back;

  constructor() {
    /* #front and #back are always defined. */
    this.#back = this.#front = {
      elements: [],
      next: undefined,
    };

    /**
     * The cursor is used to avoid calling Array.shift().
     * It contains the index of the front element of the array inside the
     * front-most node. It is always in the range [0, QUEUE_MAX_ARRAY_SIZE).
     */
    this.#cursor = 0;
    /** When there is only one node, size === elements.length - cursor. */
    this.#size = 0;
  }

  get length() {
    return this.#size;
  }

  /**
   * For exception safety, this method is structured in order:
   * 1. Read state
   * 2. Calculate required state mutations
   * 3. Perform state mutations
   */
  push(element) {
    const oldBack = this.#back;
    let newBack = oldBack;

    assert(oldBack.next === undefined);

    if(oldBack.elements.length === QUEUE_MAX_ARRAY_SIZE - 1) {
      newBack = {
        elements: [],
        next: undefined,
      };
    }

    /* push() is the mutation most likely to throw an exception, so it goes first. */
    oldBack.elements.push(element);

    if(newBack !== oldBack) {
      this.#back = newBack;
      oldBack.next = newBack;
    }

    ++this.#size;
  }

  /*
   * Like push(), shift() follows the read -> calculate -> mutate pattern for exception safety.
   */
  shift() {
    assert(this.#size > 0);

    const oldFront = this.#front,
      oldCursor = this.#cursor;
    let newFront = oldFront,
      newCursor = oldCursor + 1;

    const elements = oldFront.elements;
    const element = elements[oldCursor];

    if(newCursor === QUEUE_MAX_ARRAY_SIZE) {
      assert(elements.length === QUEUE_MAX_ARRAY_SIZE);
      assert(oldFront.next !== undefined);
      newFront = oldFront.next;
      newCursor = 0;
    }

    /* No mutations before this point. */
    --this.#size;
    this.#cursor = newCursor;

    if(oldFront !== newFront) this.#front = newFront;

    /* Permit shifted element to be garbage collected. */
    elements[oldCursor] = undefined;
    return element;
  }

  /*
   * The tricky thing about forEach() is that it can be called re-entrantly.
   * The queue may be mutated inside the callback. It is easy to see that push()
   * within the callback has no negative effects since the end of the queue is
   * checked for on every iteration. If shift() is called repeatedly within the
   * callback then the next iteration may return an element that has been removed.
   * In this case the callback will be called with undefined values until we either
   * "catch up" with elements that still exist or reach the back of the queue.
   */
  forEach(callback) {
    let i = this.#cursor,
      node = this.#front;
    let elements = node.elements;

    while(i !== elements.length || node.next !== undefined) {
      if(i === elements.length) {
        assert(node.next !== undefined);
        assert(i === QUEUE_MAX_ARRAY_SIZE);

        node = node.next;
        elements = node.elements;
        i = 0;

        if(elements.length === 0) break;
      }

      callback(elements[i]);
      ++i;
    }
  }

  /*
   * Return the element that would be returned if shift() was called now,
   * without modifying the queue.
   */
  peek() {
    assert(this.#size > 0);

    return this.#front.elements[this.#cursor];
  }

  [Symbol.iterator]() {
    return {
      next: () => {
        const done = this.length == 0;
        const value = done ? undefined : this.shift();
        return { done, value };
      },
    };
  }
}

SimpleQueue.prototype[Symbol.toStringTag] = 'SimpleQueue';
