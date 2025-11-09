import { assert } from './assert.js';
import { AbortController, AbortSignal } from './abort.js';
import { isPrototypeOf, typeIsObject, declare, assign, setFunctionName } from './util.js';

const DEBUG = false;
const QUEUE_MAX_ARRAY_SIZE = 16384;

/**
 * Simple queue structure.
 *
 * Avoids scalability issues with using a packed array directly by using
 * multiple arrays in a linked list and keeping the array size bounded.
 */
class SimpleQueue {
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

declare(SimpleQueue.prototype, { [Symbol.toStringTag]: 'SimpleQueue' });

const noop = () => {};
const closeSentinel = {};

const rethrowAssertionErrorRejection = DEBUG
  ? e => {
      if(e && e instanceof AssertionError) {
        setTimeout(() => {
          throw e;
        }, 0);
      }
    }
  : noop;

// src/stub/number-isfinite.ts
const NumberIsFinite = Number.isFinite ?? (x => typeof x == 'number' && isFinite(x));

// src/stub/math-trunc.ts
const MathTrunc = Math.trunc ?? (v => (v < 0 ? Math.ceil(v) : Math.floor(v)));

// src/lib/validators/basic.ts
function isDictionary(x) {
  return typeof x == 'object' || typeof x == 'function';
}

function assertDictionary(obj, context) {
  if(obj !== undefined && !isDictionary(obj)) throw new TypeError(`${context} is not an object.`);
}

function assertFunction(x, context) {
  if(typeof x != 'function') throw new TypeError(`${context} is not a function.`);
}

function assertObject(x, context) {
  if(!typeIsObject(x)) throw new TypeError(`${context} is not an object.`);
}

function assertRequiredArgument(x, position, context) {
  if(x === undefined) throw new TypeError(`Parameter ${position} is required in '${context}'.`);
}

function assertRequiredField(x, field, context) {
  if(x === undefined) throw new TypeError(`${field} is required in '${context}'.`);
}

function convertUnrestrictedDouble(value) {
  return Number(value);
}

function censorNegativeZero(x) {
  return x === 0 ? 0 : x;
}

function integerPart(x) {
  return censorNegativeZero(MathTrunc(x));
}

function convertUnsignedLongLongWithEnforceRange(value, context) {
  const lowerBound = 0;
  const upperBound = Number.MAX_SAFE_INTEGER;
  let x = Number(value);
  x = censorNegativeZero(x);
  if(!NumberIsFinite(x)) throw new TypeError(`${context} is not a finite number`);
  x = integerPart(x);
  if(x < lowerBound || x > upperBound) throw new TypeError(`${context} is outside the accepted range of ${lowerBound} to ${upperBound}, inclusive`);
  if(!NumberIsFinite(x) || x === 0) return 0;
  return x;
}

// src/lib/helpers/webidl.ts
const originalPromise = Promise;
const originalPromiseResolve = Promise.resolve.bind(originalPromise);
const originalPromiseThen = Promise.prototype.then;
const originalPromiseReject = Promise.reject.bind(originalPromise);
const promiseResolve = originalPromiseResolve;

function newPromise(executor) {
  return new originalPromise(executor);
}

function promiseResolvedWith(value) {
  return newPromise(resolve => resolve(value));
}

function promiseRejectedWith(reason) {
  return originalPromiseReject(reason);
}

function PerformPromiseThen(promise, onFulfilled, onRejected) {
  return originalPromiseThen.call(promise, onFulfilled, onRejected);
}

function uponPromise(promise, onFulfilled, onRejected) {
  PerformPromiseThen(PerformPromiseThen(promise, onFulfilled, onRejected), undefined, rethrowAssertionErrorRejection);
}

function uponFulfillment(promise, onFulfilled) {
  uponPromise(promise, onFulfilled);
}

function uponRejection(promise, onRejected) {
  uponPromise(promise, undefined, onRejected);
}

function transformPromiseWith(promise, fulfillmentHandler, rejectionHandler) {
  return PerformPromiseThen(promise, fulfillmentHandler, rejectionHandler);
}

function setPromiseIsHandledToTrue(promise) {
  PerformPromiseThen(promise, undefined, rethrowAssertionErrorRejection);
}

let _queueMicrotask = callback => {
  if(typeof queueMicrotask == 'function') {
    _queueMicrotask = queueMicrotask;
  } else {
    const resolvedPromise = promiseResolvedWith(undefined);
    _queueMicrotask = cb => PerformPromiseThen(resolvedPromise, cb);
  }

  return _queueMicrotask(callback);
};

function reflectCall(F, V, args) {
  if(typeof F != 'function') throw new TypeError('Argument is not a function');
  return Function.prototype.apply.call(F, V, args);
}

function promiseCall(F, V, args) {
  assert(typeof F == 'function');
  assert(V !== undefined);
  assert(Array.isArray(args));
  try {
    return promiseResolvedWith(/*reflectCall(F, V, args)*/ F.apply(V, args));
  } catch(value) {
    return promiseRejectedWith(value);
  }
}

// src/lib/abstract-ops/internal-methods.ts
const AbortSteps = Symbol('[[AbortSteps]]');
const ErrorSteps = Symbol('[[ErrorSteps]]');
const CancelSteps = Symbol('[[CancelSteps]]');
const PullSteps = Symbol('[[PullSteps]]');
const ReleaseSteps = Symbol('[[ReleaseSteps]]');

// src/lib/readable-stream/generic-reader.ts
function ReadableStreamReaderGenericInitialize(reader, stream) {
  declare(reader, { _ownerReadableStream: stream });
  declare(stream, { _reader: reader });
  if(stream._state == 'readable') {
    defaultReaderClosedPromiseInitialize(reader);
  } else if(stream._state == 'closed') {
    defaultReaderClosedPromiseInitialize(reader);
    defaultReaderClosedPromiseResolve(reader);
  } else {
    assert(stream._state == 'errored');
    defaultReaderClosedPromiseInitializeAsRejected(reader, stream._storedError);
  }
}

function ReadableStreamReaderGenericCancel(reader, reason) {
  const stream = reader._ownerReadableStream;
  assert(stream !== undefined);
  return ReadableStreamCancel(stream, reason);
}

function ReadableStreamReaderGenericRelease(reader) {
  const stream = reader._ownerReadableStream;
  assert(stream !== undefined);
  assert(stream._reader === reader);
  if(stream._state == 'readable') {
    defaultReaderClosedPromiseReject(reader, new TypeError(`Reader was released and can no longer be used to monitor the stream's closedness`));
  } else {
    assert(reader._closedPromise_resolve === undefined);
    assert(reader._closedPromise_reject === undefined);
    defaultReaderClosedPromiseInitializeAsRejected(reader, new TypeError(`Reader was released and can no longer be used to monitor the stream's closedness`));
  }
  stream._readableStreamController[ReleaseSteps]();
  stream._reader = undefined;
  reader._ownerReadableStream = undefined;
}

function readerLockException(name) {
  return new TypeError('Cannot ' + name + ' a stream using a released reader');
}

function defaultReaderClosedPromiseInitialize(reader) {
  declare(reader, {
    _closedPromise: newPromise((resolve, reject) => {
      declare(reader, { _closedPromise_resolve: resolve });
      declare(reader, { _closedPromise_reject: reject });
    }),
  });
}

function defaultReaderClosedPromiseInitializeAsRejected(reader, reason) {
  defaultReaderClosedPromiseInitialize(reader);
  defaultReaderClosedPromiseReject(reader, reason);
}

function defaultReaderClosedPromiseReject(reader, reason) {
  if(reader._closedPromise_reject === undefined) return;
  setPromiseIsHandledToTrue(reader._closedPromise);
  reader._closedPromise_reject(reason);
  reader._closedPromise_resolve = undefined;
  reader._closedPromise_reject = undefined;
}

function defaultReaderClosedPromiseResolve(reader) {
  if(reader._closedPromise_resolve === undefined) return;
  reader._closedPromise_resolve(undefined);
  reader._closedPromise_resolve = undefined;
  reader._closedPromise_reject = undefined;
}

// src/lib/validators/readable-stream.ts
function assertReadableStream(x, context) {
  if(!IsReadableStream(x)) throw new TypeError(`${context} is not a ReadableStream.`);
}

// src/lib/readable-stream/default-reader.ts
function AcquireReadableStreamDefaultReader(stream) {
  return new ReadableStreamDefaultReader(stream);
}

function ReadableStreamAddReadRequest(stream, readRequest) {
  assert(IsReadableStreamDefaultReader(stream._reader));
  assert(stream._state == 'readable');
  stream._reader._readRequests.push(readRequest);
}

function ReadableStreamFulfillReadRequest(stream, chunk, done) {
  const reader = stream._reader;
  assert(reader._readRequests.length > 0);
  const readRequest = reader._readRequests.shift();
  if(done) readRequest._closeSteps();
  else readRequest._chunkSteps(chunk);
}

function ReadableStreamGetNumReadRequests(stream) {
  return stream._reader._readRequests.length;
}

function ReadableStreamHasDefaultReader(stream) {
  const reader = stream._reader;
  if(reader === undefined) return false;
  if(!IsReadableStreamDefaultReader(reader)) return false;
  return true;
}

export class ReadableStreamDefaultReader {
  constructor(stream) {
    assertRequiredArgument(stream, 1, 'ReadableStreamDefaultReader');
    assertReadableStream(stream, 'First parameter');
    if(IsReadableStreamLocked(stream)) throw new TypeError('This stream has already been locked for exclusive reading by another reader');
    ReadableStreamReaderGenericInitialize(this, stream);
    declare(this, { _readRequests: new SimpleQueue() });
  }

  /**
   * Returns a promise that will be fulfilled when the stream becomes closed,
   * or rejected if the stream ever errors or the reader's lock is released before the stream finishes closing.
   */
  get closed() {
    if(!IsReadableStreamDefaultReader(this)) return promiseRejectedWith(defaultReaderBrandCheckException('closed'));
    return this._closedPromise;
  }

  /**
   * If the reader is active, behaves the same as {@link ReadableStream.cancel | stream.cancel(reason)}.
   */
  cancel(reason = undefined) {
    if(!IsReadableStreamDefaultReader(this)) return promiseRejectedWith(defaultReaderBrandCheckException('cancel'));
    if(this._ownerReadableStream === undefined) return promiseRejectedWith(readerLockException('cancel'));
    return ReadableStreamReaderGenericCancel(this, reason);
  }

  /**
   * Returns a promise that allows access to the next chunk from the stream's internal queue, if available.
   *
   * If reading a chunk causes the queue to become empty, more data will be pulled from the underlying source.
   */
  read() {
    if(!IsReadableStreamDefaultReader(this)) return promiseRejectedWith(defaultReaderBrandCheckException('read'));
    if(this._ownerReadableStream === undefined) return promiseRejectedWith(readerLockException('read from'));
    let resolvePromise;
    let rejectPromise;
    const promise = newPromise((resolve, reject) => {
      resolvePromise = resolve;
      rejectPromise = reject;
    });
    const readRequest = {
      _chunkSteps: chunk => resolvePromise({ value: chunk, done: false }),
      _closeSteps: () => resolvePromise({ value: undefined, done: true }),
      _errorSteps: e => rejectPromise(e),
    };
    ReadableStreamDefaultReaderRead(this, readRequest);
    return promise;
  }

  /**
   * Releases the reader's lock on the corresponding stream. After the lock is released, the reader is no longer active.
   * If the associated stream is errored when the lock is released, the reader will appear errored in the same way
   * from now on; otherwise, the reader will appear closed.
   *
   * A reader's lock cannot be released while it still has a pending read request, i.e., if a promise returned by
   * the reader's {@link ReadableStreamDefaultReader.read | read()} method has not yet been settled. Attempting to
   * do so will throw a `TypeError` and leave the reader locked to the stream.
   */
  releaseLock() {
    if(!IsReadableStreamDefaultReader(this)) throw defaultReaderBrandCheckException('releaseLock');
    if(this._ownerReadableStream === undefined) return;
    ReadableStreamReaderGenericRelease(this);
    const e = new TypeError('Reader was released');
    ReadableStreamDefaultReaderErrorReadRequests(this, e);
  }
}

/*Object.defineProperties(ReadableStreamDefaultReader.prototype, {
  cancel: { enumerable: true },
  read: { enumerable: true },
  releaseLock: { enumerable: true },
  closed: { enumerable: true },
});*/

setFunctionName(ReadableStreamDefaultReader.prototype.cancel, 'cancel');
setFunctionName(ReadableStreamDefaultReader.prototype.read, 'read');
setFunctionName(ReadableStreamDefaultReader.prototype.releaseLock, 'releaseLock');

declare(ReadableStreamDefaultReader.prototype, { [Symbol.toStringTag]: 'ReadableStreamDefaultReader' });

function IsReadableStreamDefaultReader(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_readRequests')) return false;
  return x instanceof ReadableStreamDefaultReader;
}

function ReadableStreamDefaultReaderRead(reader, readRequest) {
  const stream = reader._ownerReadableStream;
  assert(stream !== undefined);
  stream._disturbed = true;
  if(stream._state == 'closed') {
    readRequest._closeSteps();
  } else if(stream._state == 'errored') {
    readRequest._errorSteps(stream._storedError);
  } else {
    assert(stream._state == 'readable');
    stream._readableStreamController[PullSteps](readRequest);
  }
}

function ReadableStreamDefaultReaderErrorReadRequests(reader, e) {
  const readRequests = reader._readRequests;
  reader._readRequests = new SimpleQueue();
  readRequests.forEach(readRequest => readRequest._errorSteps(e));
}

function defaultReaderBrandCheckException(name) {
  return new TypeError(`ReadableStreamDefaultReader.prototype.${name} can only be used on a ReadableStreamDefaultReader`);
}

// src/lib/abstract-ops/ecmascript.ts
function CreateArrayFromList(elements) {
  return elements.slice();
}

function CanCopyDataBlockBytes(toBuffer, toIndex, fromBuffer, fromIndex, count) {
  return toBuffer !== fromBuffer && !IsDetachedBuffer(toBuffer) && !IsDetachedBuffer(fromBuffer) && toIndex + count <= toBuffer.byteLength && fromIndex + count <= fromBuffer.byteLength;
}

function CopyDataBlockBytes(dest, destOffset, src, srcOffset, n) {
  new Uint8Array(dest).set(new Uint8Array(src, srcOffset, n), destOffset);
}

const TransferArrayBuffer = O => {
  if(typeof O.transfer == 'function') TransferArrayBuffer = buffer => buffer.transfer();
  else if(typeof structuredClone == 'function') TransferArrayBuffer = buffer => structuredClone(buffer, { transfer: [buffer] });
  else TransferArrayBuffer = buffer => buffer;
  return TransferArrayBuffer(O);
};

function CanTransferArrayBuffer(O) {
  return !IsDetachedBuffer(O);
}

const IsDetachedBuffer = O => {
  if(typeof O.detached == 'boolean') IsDetachedBuffer = buffer => buffer.detached;
  else IsDetachedBuffer = buffer => buffer.byteLength === 0;
  return IsDetachedBuffer(O);
};

function ArrayBufferSlice(buffer, begin, end) {
  if(buffer.slice) return buffer.slice(begin, end);
  const length = end - begin;
  const slice = new ArrayBuffer(length);
  CopyDataBlockBytes(slice, 0, buffer, begin, length);
  return slice;
}

function GetMethod(receiver, prop) {
  const func = receiver[prop];
  if(func === undefined || func === null) return undefined;
  if(typeof func != 'function') throw new TypeError(`${String(prop)} is not a function`);
  return func;
}

function CreateAsyncFromSyncIterator(syncIteratorRecord) {
  const asyncIterator = {
    // https://tc39.es/ecma262/#sec-%asyncfromsynciteratorprototype%.next
    next() {
      let result;
      try {
        result = IteratorNext(syncIteratorRecord);
      } catch(e) {
        return promiseRejectedWith(e);
      }
      return AsyncFromSyncIteratorContinuation(result);
    },
    // https://tc39.es/ecma262/#sec-%asyncfromsynciteratorprototype%.return
    return(value) {
      let result;
      try {
        const returnMethod = GetMethod(syncIteratorRecord.iterator, 'return');
        if(returnMethod === undefined) return promiseResolvedWith({ done: true, value });
        result = reflectCall(returnMethod, syncIteratorRecord.iterator, [value]);
      } catch(e) {
        return promiseRejectedWith(e);
      }
      if(!typeIsObject(result)) return promiseRejectedWith(new TypeError('The iterator.return() method must return an object'));
      return AsyncFromSyncIteratorContinuation(result);
    },
    // Note: throw() is never used by the Streams spec.
  };
  const nextMethod = asyncIterator.next;
  return { iterator: asyncIterator, nextMethod, done: false };
}

function AsyncFromSyncIteratorContinuation(result) {
  try {
    const done = result.done;
    const value = result.value;
    const valueWrapper = promiseResolve(value);
    return PerformPromiseThen(valueWrapper, v => ({ done, value: v }));
  } catch(e) {
    return promiseRejectedWith(e);
  }
}

function GetIterator(obj, hint = 'sync', method) {
  assert(hint == 'sync' || hint == 'async');
  if(method === undefined) {
    if(hint == 'async') {
      method = GetMethod(obj, Symbol.asyncIterator);
      if(method === undefined) {
        const syncMethod = GetMethod(obj, Symbol.iterator);
        const syncIteratorRecord = GetIterator(obj, 'sync', syncMethod);
        return CreateAsyncFromSyncIterator(syncIteratorRecord);
      }
    } else {
      method = GetMethod(obj, Symbol.iterator);
    }
  }

  if(method === undefined) throw new TypeError('The object is not iterable');
  const iterator = reflectCall(method, obj, []);
  if(!typeIsObject(iterator)) throw new TypeError('The iterator method must return an object');
  const nextMethod = iterator.next;
  return { iterator, nextMethod, done: false };
}

function IteratorNext(iteratorRecord) {
  const result = reflectCall(iteratorRecord.nextMethod, iteratorRecord.iterator, []);
  if(!typeIsObject(result)) throw new TypeError('The iterator.next() method must return an object');
  return result;
}

// src/lib/readable-stream/async-iterator.ts
export class ReadableStreamAsyncIteratorImpl {
  constructor(reader, preventCancel) {
    declare(this, { _ongoingPromise: undefined, _isFinished: false, _reader: reader, _preventCancel: preventCancel });
  }

  next() {
    const nextSteps = () => this._nextSteps();
    this._ongoingPromise = this._ongoingPromise ? transformPromiseWith(this._ongoingPromise, nextSteps, nextSteps) : nextSteps();
    return this._ongoingPromise;
  }

  return(value) {
    const returnSteps = () => this._returnSteps(value);
    this._ongoingPromise = this._ongoingPromise ? transformPromiseWith(this._ongoingPromise, returnSteps, returnSteps) : returnSteps();
    return this._ongoingPromise;
  }

  _nextSteps() {
    if(this._isFinished) return Promise.resolve({ value: undefined, done: true });
    const reader = this._reader;
    assert(reader._ownerReadableStream !== undefined);
    let resolvePromise;
    let rejectPromise;
    const promise = newPromise((resolve, reject) => {
      resolvePromise = resolve;
      rejectPromise = reject;
    });
    const readRequest = {
      _chunkSteps: chunk => {
        this._ongoingPromise = undefined;
        _queueMicrotask(() => resolvePromise({ value: chunk, done: false }));
      },
      _closeSteps: () => {
        this._ongoingPromise = undefined;
        this._isFinished = true;
        ReadableStreamReaderGenericRelease(reader);
        resolvePromise({ value: undefined, done: true });
      },
      _errorSteps: reason => {
        this._ongoingPromise = undefined;
        this._isFinished = true;
        ReadableStreamReaderGenericRelease(reader);
        rejectPromise(reason);
      },
    };
    ReadableStreamDefaultReaderRead(reader, readRequest);
    return promise;
  }

  _returnSteps(value) {
    if(this._isFinished) return Promise.resolve({ value, done: true });
    this._isFinished = true;
    const reader = this._reader;
    assert(reader._ownerReadableStream !== undefined);
    assert(reader._readRequests.length === 0);
    if(!this._preventCancel) {
      const result = ReadableStreamReaderGenericCancel(reader, value);
      ReadableStreamReaderGenericRelease(reader);
      return transformPromiseWith(result, () => ({ value, done: true }));
    }
    ReadableStreamReaderGenericRelease(reader);
    return promiseResolvedWith({ value, done: true });
  }
}

const ReadableStreamAsyncIteratorPrototype = {
  next() {
    if(!IsReadableStreamAsyncIterator(this)) return promiseRejectedWith(streamAsyncIteratorBrandCheckException('next'));
    return this._asyncIteratorImpl.next();
  },
  return(value) {
    if(!IsReadableStreamAsyncIterator(this)) return promiseRejectedWith(streamAsyncIteratorBrandCheckException('return'));
    return this._asyncIteratorImpl.return(value);
  },
  // 25.1.3.1 %AsyncIteratorPrototype% [ @@asyncIterator ] ( )
  // https://tc39.github.io/ecma262/#sec-asynciteratorprototype-asynciterator
  [Symbol.asyncIterator]() {
    return this;
  },
};

Object.defineProperty(ReadableStreamAsyncIteratorPrototype, Symbol.asyncIterator, {
  enumerable: false,
});

function AcquireReadableStreamAsyncIterator(stream, preventCancel) {
  const reader = AcquireReadableStreamDefaultReader(stream);
  const impl = new ReadableStreamAsyncIteratorImpl(reader, preventCancel);
  const iterator = Object.create(ReadableStreamAsyncIteratorPrototype);
  iterator._asyncIteratorImpl = impl;
  return iterator;
}

function IsReadableStreamAsyncIterator(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_asyncIteratorImpl')) return false;
  try {
    return x._asyncIteratorImpl instanceof ReadableStreamAsyncIteratorImpl;
  } catch(e) {
    return false;
  }
}

function streamAsyncIteratorBrandCheckException(name) {
  return new TypeError(`ReadableStreamAsyncIterator.${name} can only be used on a ReadableSteamAsyncIterator`);
}

// src/stub/number-isnan.ts
const NumberIsNaN = Number.isNaN || (x => x !== x);

// src/lib/abstract-ops/miscellaneous.ts
function IsNonNegativeNumber(v) {
  if(typeof v != 'number') return false;
  if(NumberIsNaN(v)) return false;
  if(v < 0) return false;
  return true;
}

function CloneAsUint8Array(O) {
  const buffer = ArrayBufferSlice(O.buffer, O.byteOffset, O.byteOffset + O.byteLength);
  return new Uint8Array(buffer);
}

// src/lib/abstract-ops/queue-with-sizes.ts
function DequeueValue(container) {
  assert('_queue' in container && '_queueTotalSize' in container);
  assert(container._queue.length > 0);
  const pair = container._queue.shift();
  container._queueTotalSize -= pair.size;
  if(container._queueTotalSize < 0) container._queueTotalSize = 0;
  return pair.value;
}

function EnqueueValueWithSize(container, value, size) {
  assert('_queue' in container && '_queueTotalSize' in container);
  if(!IsNonNegativeNumber(size) || size === Infinity) throw new RangeError('Size must be a finite, non-NaN, non-negative number.');
  container._queue.push({ value, size });
  container._queueTotalSize += size;
}

function PeekQueueValue(container) {
  assert('_queue' in container && '_queueTotalSize' in container);
  assert(container._queue.length > 0);
  const pair = container._queue.peek();
  return pair.value;
}

function ResetQueue(container) {
  assert('_queue' in container && '_queueTotalSize' in container);
  declare(container, { _queue: new SimpleQueue(), _queueTotalSize: 0 });
}

// src/stub/number-isinteger.ts
const NumberIsInteger = Number.isInteger ?? (value => typeof value == 'number' && isFinite(value) && Math.floor(value) === value);

// src/lib/helpers/array-buffer-view.ts
function isDataView(view) {
  if(isPrototypeOf(DataView.prototype, view)) return true;
  return view.constructor == DataView;
}

function arrayBufferViewElementSize(ctor) {
  if(isDataViewConstructor(ctor)) return 1;
  return ctor.BYTES_PER_ELEMENT;
}

// src/lib/readable-stream/byte-stream-controller.ts
export class ReadableStreamBYOBRequest {
  constructor() {
    throw new TypeError('Illegal constructor');
  }

  /**
   * Returns the view for writing in to, or `null` if the BYOB request has already been responded to.
   */
  get view() {
    if(!IsReadableStreamBYOBRequest(this)) throw byobRequestBrandCheckException('view');
    return this._view;
  }

  respond(bytesWritten) {
    if(!IsReadableStreamBYOBRequest(this)) throw byobRequestBrandCheckException('respond');
    assertRequiredArgument(bytesWritten, 1, 'respond');
    bytesWritten = convertUnsignedLongLongWithEnforceRange(bytesWritten, 'First parameter');
    if(this._associatedReadableByteStreamController === undefined) throw new TypeError('This BYOB request has been invalidated');
    if(IsDetachedBuffer(this._view.buffer)) throw new TypeError(`The BYOB request's buffer has been detached and so cannot be used as a response`);
    assert(this._view.byteLength > 0);
    assert(this._view.buffer.byteLength > 0);
    ReadableByteStreamControllerRespond(this._associatedReadableByteStreamController, bytesWritten);
  }

  respondWithNewView(view) {
    if(!IsReadableStreamBYOBRequest(this)) throw byobRequestBrandCheckException('respondWithNewView');
    assertRequiredArgument(view, 1, 'respondWithNewView');
    if(!ArrayBuffer.isView(view)) throw new TypeError('You can only respond with array buffer views');
    if(this._associatedReadableByteStreamController === undefined) throw new TypeError('This BYOB request has been invalidated');
    if(IsDetachedBuffer(view.buffer)) throw new TypeError("The given view's buffer has been detached and so cannot be used as a response");
    ReadableByteStreamControllerRespondWithNewView(this._associatedReadableByteStreamController, view);
  }
}

/*Object.defineProperties(ReadableStreamBYOBRequest.prototype, {
  respond: { enumerable: true },
  respondWithNewView: { enumerable: true },
  view: { enumerable: true },
});*/

setFunctionName(ReadableStreamBYOBRequest.prototype.respond, 'respond');
setFunctionName(ReadableStreamBYOBRequest.prototype.respondWithNewView, 'respondWithNewView');

declare(ReadableStreamBYOBRequest.prototype, { [Symbol.toStringTag]: 'ReadableStreamBYOBRequest' });

export class ReadableByteStreamController {
  constructor() {
    throw new TypeError('Illegal constructor');
  }

  /**
   * Returns the current BYOB pull request, or `null` if there isn't one.
   */
  get byobRequest() {
    if(!IsReadableByteStreamController(this)) throw byteStreamControllerBrandCheckException('byobRequest');
    return ReadableByteStreamControllerGetBYOBRequest(this);
  }

  /**
   * Returns the desired size to fill the controlled stream's internal queue. It can be negative, if the queue is
   * over-full. An underlying byte source ought to use this information to determine when and how to apply backpressure.
   */
  get desiredSize() {
    if(!IsReadableByteStreamController(this)) throw byteStreamControllerBrandCheckException('desiredSize');
    return ReadableByteStreamControllerGetDesiredSize(this);
  }

  /**
   * Closes the controlled readable stream. Consumers will still be able to read any previously-enqueued chunks from
   * the stream, but once those are read, the stream will become closed.
   */
  close() {
    if(!IsReadableByteStreamController(this)) throw byteStreamControllerBrandCheckException('close');
    if(this._closeRequested) throw new TypeError('The stream has already been closed; do not close it again!');
    const state = this._controlledReadableByteStream._state;
    if(state != 'readable') throw new TypeError(`The stream (in ${state} state) is not in the readable state and cannot be closed`);
    ReadableByteStreamControllerClose(this);
  }

  enqueue(chunk) {
    if(!IsReadableByteStreamController(this)) throw byteStreamControllerBrandCheckException('enqueue');
    assertRequiredArgument(chunk, 1, 'enqueue');
    if(!ArrayBuffer.isView(chunk)) throw new TypeError('chunk must be an array buffer view');
    if(chunk.byteLength === 0) throw new TypeError('chunk must have non-zero byteLength');
    if(chunk.buffer.byteLength === 0) throw new TypeError(`chunk's buffer must have non-zero byteLength`);
    if(this._closeRequested) throw new TypeError('stream is closed or draining');
    const state = this._controlledReadableByteStream._state;
    if(state != 'readable') throw new TypeError(`The stream (in ${state} state) is not in the readable state and cannot be enqueued to`);
    ReadableByteStreamControllerEnqueue(this, chunk);
  }

  /**
   * Errors the controlled readable stream, making all future interactions with it fail with the given error `e`.
   */
  error(e = undefined) {
    if(!IsReadableByteStreamController(this)) throw byteStreamControllerBrandCheckException('error');
    ReadableByteStreamControllerError(this, e);
  }

  /** @internal */
  [CancelSteps](reason) {
    ReadableByteStreamControllerClearPendingPullIntos(this);
    ResetQueue(this);
    const result = this._cancelAlgorithm(reason);
    ReadableByteStreamControllerClearAlgorithms(this);
    return result;
  }

  /** @internal */
  [PullSteps](readRequest) {
    const stream = this._controlledReadableByteStream;
    assert(ReadableStreamHasDefaultReader(stream));
    if(this._queueTotalSize > 0) {
      assert(ReadableStreamGetNumReadRequests(stream) === 0);
      ReadableByteStreamControllerFillReadRequestFromQueue(this, readRequest);
      return;
    }
    const autoAllocateChunkSize = this._autoAllocateChunkSize;
    if(autoAllocateChunkSize !== undefined) {
      let buffer;
      try {
        buffer = new ArrayBuffer(autoAllocateChunkSize);
      } catch(bufferE) {
        readRequest._errorSteps(bufferE);
        return;
      }
      const pullIntoDescriptor = {
        buffer,
        bufferByteLength: autoAllocateChunkSize,
        byteOffset: 0,
        byteLength: autoAllocateChunkSize,
        bytesFilled: 0,
        minimumFill: 1,
        elementSize: 1,
        viewConstructor: Uint8Array,
        readerType: 'default',
      };
      this._pendingPullIntos.push(pullIntoDescriptor);
    }
    ReadableStreamAddReadRequest(stream, readRequest);
    ReadableByteStreamControllerCallPullIfNeeded(this);
  }

  /** @internal */
  [ReleaseSteps]() {
    if(this._pendingPullIntos.length > 0) {
      const firstPullInto = this._pendingPullIntos.peek();
      firstPullInto.readerType = 'none';
      this._pendingPullIntos = new SimpleQueue();
      this._pendingPullIntos.push(firstPullInto);
    }
  }
}

/*Object.defineProperties(ReadableByteStreamController.prototype, {
  close: { enumerable: true },
  enqueue: { enumerable: true },
  error: { enumerable: true },
  byobRequest: { enumerable: true },
  desiredSize: { enumerable: true },
});*/

setFunctionName(ReadableByteStreamController.prototype.close, 'close');
setFunctionName(ReadableByteStreamController.prototype.enqueue, 'enqueue');
setFunctionName(ReadableByteStreamController.prototype.error, 'error');

declare(ReadableByteStreamController.prototype, { [Symbol.toStringTag]: 'ReadableByteStreamController' });

function IsReadableByteStreamController(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_controlledReadableByteStream')) return false;
  return x instanceof ReadableByteStreamController;
}

function IsReadableStreamBYOBRequest(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_associatedReadableByteStreamController')) return false;
  return x instanceof ReadableStreamBYOBRequest;
}

function ReadableByteStreamControllerCallPullIfNeeded(controller) {
  const stream = controller._controlledReadableByteStream;
  if(stream._state != 'readable') return false;
  if(controller._closeRequested) return false;
  if(!controller._started) return false;
  if(ReadableStreamHasDefaultReader(stream) && ReadableStreamGetNumReadRequests(stream) > 0) return true;
  if(ReadableStreamHasBYOBReader(stream) && ReadableStreamGetNumReadIntoRequests(stream) > 0) return true;
  const desiredSize = ReadableByteStreamControllerGetDesiredSize(controller);
  assert(desiredSize !== null);
  const shouldPull = desiredSize > 0;

  if(!shouldPull) return;
  if(controller._pulling) {
    controller._pullAgain = true;
    return;
  }

  assert(!controller._pullAgain);
  controller._pulling = true;
  const pullPromise = controller._pullAlgorithm();
  uponPromise(
    pullPromise,
    () => {
      controller._pulling = false;
      if(controller._pullAgain) {
        controller._pullAgain = false;
        ReadableByteStreamControllerCallPullIfNeeded(controller);
      }
      return null;
    },
    e => {
      ReadableByteStreamControllerError(controller, e);
      return null;
    },
  );
}

function ReadableByteStreamControllerClearPendingPullIntos(controller) {
  ReadableByteStreamControllerInvalidateBYOBRequest(controller);
  controller._pendingPullIntos = new SimpleQueue();
}

function ReadableByteStreamControllerCommitPullIntoDescriptor(stream, pullIntoDescriptor) {
  assert(stream._state != 'errored');
  assert(pullIntoDescriptor.readerType != 'none');
  let done = false;
  if(stream._state == 'closed') {
    assert(pullIntoDescriptor.bytesFilled % pullIntoDescriptor.elementSize === 0);
    done = true;
  }

  const filledView = ReadableByteStreamControllerConvertPullIntoDescriptor(pullIntoDescriptor);
  if(pullIntoDescriptor.readerType == 'default') {
    ReadableStreamFulfillReadRequest(stream, filledView, done);
  } else {
    assert(pullIntoDescriptor.readerType == 'byob');

    const reader = stream._reader;
    assert(reader._readIntoRequests.length > 0);
    const readIntoRequest = reader._readIntoRequests.shift();
    if(done) readIntoRequest._closeSteps(filledView);
    else readIntoRequest._chunkSteps(filledView);
  }
}

function ReadableByteStreamControllerCommitPullIntoDescriptors(stream, pullIntoDescriptors) {
  for(let i = 0; i < pullIntoDescriptors.length; ++i) ReadableByteStreamControllerCommitPullIntoDescriptor(stream, pullIntoDescriptors[i]);
}

function ReadableByteStreamControllerConvertPullIntoDescriptor(pullIntoDescriptor) {
  const bytesFilled = pullIntoDescriptor.bytesFilled;
  const elementSize = pullIntoDescriptor.elementSize;
  assert(bytesFilled <= pullIntoDescriptor.byteLength);
  assert(bytesFilled % elementSize === 0);
  return new pullIntoDescriptor.viewConstructor(pullIntoDescriptor.buffer, pullIntoDescriptor.byteOffset, bytesFilled / elementSize);
}

function ReadableByteStreamControllerEnqueueChunkToQueue(controller, buffer, byteOffset, byteLength) {
  controller._queue.push({ buffer, byteOffset, byteLength });
  controller._queueTotalSize += byteLength;
}

function ReadableByteStreamControllerEnqueueClonedChunkToQueue(controller, buffer, byteOffset, byteLength) {
  let clonedChunk;
  try {
    clonedChunk = ArrayBufferSlice(buffer, byteOffset, byteOffset + byteLength);
  } catch(cloneE) {
    ReadableByteStreamControllerError(controller, cloneE);
    throw cloneE;
  }

  ReadableByteStreamControllerEnqueueChunkToQueue(controller, clonedChunk, 0, byteLength);
}

function ReadableByteStreamControllerEnqueueDetachedPullIntoToQueue(controller, firstDescriptor) {
  assert(firstDescriptor.readerType == 'none');
  if(firstDescriptor.bytesFilled > 0) ReadableByteStreamControllerEnqueueClonedChunkToQueue(controller, firstDescriptor.buffer, firstDescriptor.byteOffset, firstDescriptor.bytesFilled);
  ReadableByteStreamControllerShiftPendingPullInto(controller);
}

function ReadableByteStreamControllerFillPullIntoDescriptorFromQueue(controller, pullIntoDescriptor) {
  const maxBytesToCopy = Math.min(controller._queueTotalSize, pullIntoDescriptor.byteLength - pullIntoDescriptor.bytesFilled);
  const maxBytesFilled = pullIntoDescriptor.bytesFilled + maxBytesToCopy;
  let totalBytesToCopyRemaining = maxBytesToCopy;
  let ready = false;
  assert(!IsDetachedBuffer(pullIntoDescriptor.buffer));
  assert(pullIntoDescriptor.bytesFilled < pullIntoDescriptor.minimumFill);
  const remainderBytes = maxBytesFilled % pullIntoDescriptor.elementSize;
  const maxAlignedBytes = maxBytesFilled - remainderBytes;
  if(maxAlignedBytes >= pullIntoDescriptor.minimumFill) {
    totalBytesToCopyRemaining = maxAlignedBytes - pullIntoDescriptor.bytesFilled;
    ready = true;
  }

  const queue = controller._queue;
  while(totalBytesToCopyRemaining > 0) {
    const headOfQueue = queue.peek();
    const bytesToCopy = Math.min(totalBytesToCopyRemaining, headOfQueue.byteLength);
    const destStart = pullIntoDescriptor.byteOffset + pullIntoDescriptor.bytesFilled;
    assert(CanCopyDataBlockBytes(pullIntoDescriptor.buffer, destStart, headOfQueue.buffer, headOfQueue.byteOffset, bytesToCopy));
    CopyDataBlockBytes(pullIntoDescriptor.buffer, destStart, headOfQueue.buffer, headOfQueue.byteOffset, bytesToCopy);
    if(headOfQueue.byteLength === bytesToCopy) {
      queue.shift();
    } else {
      headOfQueue.byteOffset += bytesToCopy;
      headOfQueue.byteLength -= bytesToCopy;
    }
    controller._queueTotalSize -= bytesToCopy;
    ReadableByteStreamControllerFillHeadPullIntoDescriptor(controller, bytesToCopy, pullIntoDescriptor);
    totalBytesToCopyRemaining -= bytesToCopy;
  }

  if(!ready) {
    assert(controller._queueTotalSize === 0);
    assert(pullIntoDescriptor.bytesFilled > 0);
    assert(pullIntoDescriptor.bytesFilled < pullIntoDescriptor.minimumFill);
  }

  return ready;
}

function ReadableByteStreamControllerFillHeadPullIntoDescriptor(controller, size, pullIntoDescriptor) {
  assert(controller._pendingPullIntos.length === 0 || controller._pendingPullIntos.peek() === pullIntoDescriptor);
  assert(controller._byobRequest === null);
  pullIntoDescriptor.bytesFilled += size;
}

function ReadableByteStreamControllerHandleQueueDrain(controller) {
  assert(controller._controlledReadableByteStream._state == 'readable');
  if(controller._queueTotalSize === 0 && controller._closeRequested) {
    ReadableByteStreamControllerClearAlgorithms(controller);
    ReadableStreamClose(controller._controlledReadableByteStream);
  } else {
    ReadableByteStreamControllerCallPullIfNeeded(controller);
  }
}

function ReadableByteStreamControllerInvalidateBYOBRequest(controller) {
  if(controller._byobRequest === null) return;
  controller._byobRequest._associatedReadableByteStreamController = undefined;
  controller._byobRequest._view = null;
  controller._byobRequest = null;
}

function ReadableByteStreamControllerProcessPullIntoDescriptorsUsingQueue(controller) {
  assert(!controller._closeRequested);
  const filledPullIntos = [];
  while(controller._pendingPullIntos.length > 0) {
    if(controller._queueTotalSize === 0) break;
    const pullIntoDescriptor = controller._pendingPullIntos.peek();
    assert(pullIntoDescriptor.readerType != 'none');
    if(ReadableByteStreamControllerFillPullIntoDescriptorFromQueue(controller, pullIntoDescriptor)) {
      ReadableByteStreamControllerShiftPendingPullInto(controller);
      filledPullIntos.push(pullIntoDescriptor);
    }
  }

  return filledPullIntos;
}

function ReadableByteStreamControllerRespondInternal(controller, bytesWritten) {
  const firstDescriptor = controller._pendingPullIntos.peek();
  assert(CanTransferArrayBuffer(firstDescriptor.buffer));
  ReadableByteStreamControllerInvalidateBYOBRequest(controller);
  const state = controller._controlledReadableByteStream._state;
  if(state == 'closed') {
    assert(bytesWritten === 0);

    assert(firstDescriptor.bytesFilled % firstDescriptor.elementSize === 0);
    if(firstDescriptor.readerType == 'none') ReadableByteStreamControllerShiftPendingPullInto(controller);
    const stream = controller._controlledReadableByteStream;
    if(ReadableStreamHasBYOBReader(stream)) {
      const filledPullIntos = [];
      for(let i = 0; i < ReadableStreamGetNumReadIntoRequests(stream); ++i) filledPullIntos.push(ReadableByteStreamControllerShiftPendingPullInto(controller));
      ReadableByteStreamControllerCommitPullIntoDescriptors(stream, filledPullIntos);
    }
  } else {
    assert(state == 'readable');
    assert(bytesWritten > 0);

    assert(firstDescriptor.bytesFilled + bytesWritten <= firstDescriptor.byteLength);
    ReadableByteStreamControllerFillHeadPullIntoDescriptor(controller, bytesWritten, firstDescriptor);
    if(firstDescriptor.readerType == 'none') {
      ReadableByteStreamControllerEnqueueDetachedPullIntoToQueue(controller, firstDescriptor);
      const filledPullIntos2 = ReadableByteStreamControllerProcessPullIntoDescriptorsUsingQueue(controller);
      ReadableByteStreamControllerCommitPullIntoDescriptors(controller._controlledReadableByteStream, filledPullIntos2);
      return;
    }

    if(firstDescriptor.bytesFilled < firstDescriptor.minimumFill) return;
    ReadableByteStreamControllerShiftPendingPullInto(controller);
    const remainderSize = firstDescriptor.bytesFilled % firstDescriptor.elementSize;
    if(remainderSize > 0) {
      const end = firstDescriptor.byteOffset + firstDescriptor.bytesFilled;
      ReadableByteStreamControllerEnqueueClonedChunkToQueue(controller, firstDescriptor.buffer, end - remainderSize, remainderSize);
    }

    firstDescriptor.bytesFilled -= remainderSize;
    const filledPullIntos = ReadableByteStreamControllerProcessPullIntoDescriptorsUsingQueue(controller);
    ReadableByteStreamControllerCommitPullIntoDescriptor(controller._controlledReadableByteStream, firstDescriptor);
    ReadableByteStreamControllerCommitPullIntoDescriptors(controller._controlledReadableByteStream, filledPullIntos);
  }

  ReadableByteStreamControllerCallPullIfNeeded(controller);
}

function ReadableByteStreamControllerShiftPendingPullInto(controller) {
  assert(controller._byobRequest === null);
  const descriptor = controller._pendingPullIntos.shift();
  return descriptor;
}

function ReadableByteStreamControllerClearAlgorithms(controller) {
  controller._pullAlgorithm = undefined;
  controller._cancelAlgorithm = undefined;
}

function ReadableByteStreamControllerClose(controller) {
  const stream = controller._controlledReadableByteStream;
  if(controller._closeRequested || stream._state != 'readable') return;
  if(controller._queueTotalSize > 0) {
    controller._closeRequested = true;
    return;
  }

  if(controller._pendingPullIntos.length > 0) {
    const firstPendingPullInto = controller._pendingPullIntos.peek();
    if(firstPendingPullInto.bytesFilled % firstPendingPullInto.elementSize !== 0) {
      const e = new TypeError('Insufficient bytes to fill elements in the given buffer');
      ReadableByteStreamControllerError(controller, e);
      throw e;
    }
  }

  ReadableByteStreamControllerClearAlgorithms(controller);
  ReadableStreamClose(stream);
}

function ReadableByteStreamControllerEnqueue(controller, chunk) {
  const stream = controller._controlledReadableByteStream;
  if(controller._closeRequested || stream._state != 'readable') return;
  const { buffer, byteOffset, byteLength } = chunk;
  if(IsDetachedBuffer(buffer)) throw new TypeError("chunk's buffer is detached and so cannot be enqueued");
  const transferredBuffer = TransferArrayBuffer(buffer);
  if(controller._pendingPullIntos.length > 0) {
    const firstPendingPullInto = controller._pendingPullIntos.peek();
    if(IsDetachedBuffer(firstPendingPullInto.buffer)) throw new TypeError("The BYOB request's buffer has been detached and so cannot be filled with an enqueued chunk");
    ReadableByteStreamControllerInvalidateBYOBRequest(controller);
    firstPendingPullInto.buffer = TransferArrayBuffer(firstPendingPullInto.buffer);
    if(firstPendingPullInto.readerType == 'none') ReadableByteStreamControllerEnqueueDetachedPullIntoToQueue(controller, firstPendingPullInto);
  }

  if(ReadableStreamHasDefaultReader(stream)) {
    const reader = controller._controlledReadableByteStream._reader;
    assert(IsReadableStreamDefaultReader(reader));
    while(reader._readRequests.length > 0) {
      if(controller._queueTotalSize === 0) return;
      const readRequest = reader._readRequests.shift();
      ReadableByteStreamControllerFillReadRequestFromQueue(controller, readRequest);
    }

    if(ReadableStreamGetNumReadRequests(stream) === 0) {
      assert(controller._pendingPullIntos.length === 0);
      ReadableByteStreamControllerEnqueueChunkToQueue(controller, transferredBuffer, byteOffset, byteLength);
    } else {
      assert(controller._queue.length === 0);
      if(controller._pendingPullIntos.length > 0) {
        assert(controller._pendingPullIntos.peek().readerType == 'default');
        ReadableByteStreamControllerShiftPendingPullInto(controller);
      }
      const transferredView = new Uint8Array(transferredBuffer, byteOffset, byteLength);
      ReadableStreamFulfillReadRequest(stream, transferredView, false);
    }
  } else if(ReadableStreamHasBYOBReader(stream)) {
    ReadableByteStreamControllerEnqueueChunkToQueue(controller, transferredBuffer, byteOffset, byteLength);
    const filledPullIntos = ReadableByteStreamControllerProcessPullIntoDescriptorsUsingQueue(controller);
    ReadableByteStreamControllerCommitPullIntoDescriptors(controller._controlledReadableByteStream, filledPullIntos);
  } else {
    assert(!IsReadableStreamLocked(stream));
    ReadableByteStreamControllerEnqueueChunkToQueue(controller, transferredBuffer, byteOffset, byteLength);
  }

  ReadableByteStreamControllerCallPullIfNeeded(controller);
}

function ReadableByteStreamControllerError(controller, e) {
  const stream = controller._controlledReadableByteStream;
  if(stream._state != 'readable') return;
  ReadableByteStreamControllerClearPendingPullIntos(controller);
  ResetQueue(controller);
  ReadableByteStreamControllerClearAlgorithms(controller);
  ReadableStreamError(stream, e);
}

function ReadableByteStreamControllerFillReadRequestFromQueue(controller, readRequest) {
  assert(controller._queueTotalSize > 0);
  const entry = controller._queue.shift();
  controller._queueTotalSize -= entry.byteLength;
  ReadableByteStreamControllerHandleQueueDrain(controller);
  const view = new Uint8Array(entry.buffer, entry.byteOffset, entry.byteLength);
  readRequest._chunkSteps(view);
}

function ReadableByteStreamControllerGetBYOBRequest(controller) {
  if(controller._byobRequest === null && controller._pendingPullIntos.length > 0) {
    const firstDescriptor = controller._pendingPullIntos.peek();
    const view = new Uint8Array(firstDescriptor.buffer, firstDescriptor.byteOffset + firstDescriptor.bytesFilled, firstDescriptor.byteLength - firstDescriptor.bytesFilled);
    const byobRequest = Object.create(ReadableStreamBYOBRequest.prototype);

    assert(IsReadableByteStreamController(controller));
    assert(typeof view == 'object');
    assert(ArrayBuffer.isView(view));
    assert(!IsDetachedBuffer(view.buffer));
    byobRequest._associatedReadableByteStreamController = controller;
    byobRequest._view = view;

    controller._byobRequest = byobRequest;
  }

  return controller._byobRequest;
}

function ReadableByteStreamControllerGetDesiredSize(controller) {
  const state = controller._controlledReadableByteStream._state;
  if(state == 'errored') return null;
  if(state == 'closed') return 0;
  return controller._strategyHWM - controller._queueTotalSize;
}

function ReadableByteStreamControllerRespond(controller, bytesWritten) {
  assert(controller._pendingPullIntos.length > 0);
  const firstDescriptor = controller._pendingPullIntos.peek();
  const state = controller._controlledReadableByteStream._state;
  if(state == 'closed') {
    if(bytesWritten !== 0) throw new TypeError('bytesWritten must be 0 when calling respond() on a closed stream');
  } else {
    assert(state == 'readable');
    if(bytesWritten === 0) throw new TypeError('bytesWritten must be greater than 0 when calling respond() on a readable stream');
    if(firstDescriptor.bytesFilled + bytesWritten > firstDescriptor.byteLength) throw new RangeError('bytesWritten out of range');
  }

  firstDescriptor.buffer = TransferArrayBuffer(firstDescriptor.buffer);
  ReadableByteStreamControllerRespondInternal(controller, bytesWritten);
}

function ReadableByteStreamControllerRespondWithNewView(controller, view) {
  assert(controller._pendingPullIntos.length > 0);
  assert(!IsDetachedBuffer(view.buffer));
  const firstDescriptor = controller._pendingPullIntos.peek();
  const state = controller._controlledReadableByteStream._state;
  if(state == 'closed') {
    if(view.byteLength !== 0) throw new TypeError("The view's length must be 0 when calling respondWithNewView() on a closed stream");
  } else {
    assert(state == 'readable');
    if(view.byteLength === 0) throw new TypeError("The view's length must be greater than 0 when calling respondWithNewView() on a readable stream");
  }

  if(firstDescriptor.byteOffset + firstDescriptor.bytesFilled !== view.byteOffset) throw new RangeError('The region specified by view does not match byobRequest');
  if(firstDescriptor.bufferByteLength !== view.buffer.byteLength) throw new RangeError('The buffer of view has different capacity than byobRequest');
  if(firstDescriptor.bytesFilled + view.byteLength > firstDescriptor.byteLength) throw new RangeError('The region specified by view is larger than byobRequest');
  const viewByteLength = view.byteLength;
  firstDescriptor.buffer = TransferArrayBuffer(view.buffer);
  ReadableByteStreamControllerRespondInternal(controller, viewByteLength);
}

function SetUpReadableByteStreamController(stream, controller, startAlgorithm, pullAlgorithm, cancelAlgorithm, highWaterMark, autoAllocateChunkSize) {
  assert(stream._readableStreamController === undefined);
  if(autoAllocateChunkSize !== undefined) {
    assert(NumberIsInteger(autoAllocateChunkSize));
    assert(autoAllocateChunkSize > 0);
  }

  controller._controlledReadableByteStream = stream;
  controller._pullAgain = false;
  controller._pulling = false;
  controller._byobRequest = null;
  controller._queue = controller._queueTotalSize = undefined;
  ResetQueue(controller);
  controller._closeRequested = false;
  controller._started = false;
  controller._strategyHWM = highWaterMark;
  controller._pullAlgorithm = pullAlgorithm;
  controller._cancelAlgorithm = cancelAlgorithm;
  controller._autoAllocateChunkSize = autoAllocateChunkSize;
  controller._pendingPullIntos = new SimpleQueue();
  declare(stream, { _readableStreamController: controller });
  const startResult = startAlgorithm();
  uponPromise(
    promiseResolvedWith(startResult),
    () => {
      controller._started = true;
      assert(!controller._pulling);
      assert(!controller._pullAgain);
      ReadableByteStreamControllerCallPullIfNeeded(controller);
      return null;
    },
    r => {
      ReadableByteStreamControllerError(controller, r);
      return null;
    },
  );
}

function byobRequestBrandCheckException(name) {
  return new TypeError(`ReadableStreamBYOBRequest.prototype.${name} can only be used on a ReadableStreamBYOBRequest`);
}

function byteStreamControllerBrandCheckException(name) {
  return new TypeError(`ReadableByteStreamController.prototype.${name} can only be used on a ReadableByteStreamController`);
}

// src/lib/validators/reader-options.ts

function convertReadableStreamReaderMode(mode, context) {
  mode = `${mode}`;
  if(mode != 'byob') throw new TypeError(`${context} '${mode}' is not a valid enumeration value for ReadableStreamReaderMode`);
  return mode;
}

// src/lib/readable-stream/byob-reader.ts
function AcquireReadableStreamBYOBReader(stream) {
  return new ReadableStreamBYOBReader(stream);
}

function ReadableStreamAddReadIntoRequest(stream, readIntoRequest) {
  assert(IsReadableStreamBYOBReader(stream._reader));
  assert(stream._state == 'readable' || stream._state == 'closed');
  stream._reader._readIntoRequests.push(readIntoRequest);
}

function ReadableStreamGetNumReadIntoRequests(stream) {
  return stream._reader._readIntoRequests.length;
}

function ReadableStreamHasBYOBReader(stream) {
  const reader = stream._reader;
  if(reader === undefined) return false;
  if(!IsReadableStreamBYOBReader(reader)) return false;
  return true;
}

export class ReadableStreamBYOBReader {
  constructor(stream) {
    assertRequiredArgument(stream, 1, 'ReadableStreamBYOBReader');
    assertReadableStream(stream, 'First parameter');
    if(IsReadableStreamLocked(stream)) throw new TypeError('This stream has already been locked for exclusive reading by another reader');
    if(!IsReadableByteStreamController(stream._readableStreamController)) throw new TypeError('Cannot construct a ReadableStreamBYOBReader for a stream not constructed with a byte source');
    ReadableStreamReaderGenericInitialize(this, stream);
    declare(this, { _readIntoRequests: new SimpleQueue() });
  }

  /**
   * Returns a promise that will be fulfilled when the stream becomes closed, or rejected if the stream ever errors or
   * the reader's lock is released before the stream finishes closing.
   */
  get closed() {
    if(!IsReadableStreamBYOBReader(this)) return promiseRejectedWith(byobReaderBrandCheckException('closed'));
    return this._closedPromise;
  }

  /**
   * If the reader is active, behaves the same as {@link ReadableStream.cancel | stream.cancel(reason)}.
   */
  cancel(reason = undefined) {
    if(!IsReadableStreamBYOBReader(this)) return promiseRejectedWith(byobReaderBrandCheckException('cancel'));
    if(this._ownerReadableStream === undefined) return promiseRejectedWith(readerLockException('cancel'));
    return ReadableStreamReaderGenericCancel(this, reason);
  }

  read(view, rawOptions = {}) {
    if(!IsReadableStreamBYOBReader(this)) return promiseRejectedWith(byobReaderBrandCheckException('read'));
    if(!ArrayBuffer.isView(view)) return promiseRejectedWith(new TypeError('view must be an array buffer view'));
    if(view.byteLength === 0) return promiseRejectedWith(new TypeError('view must have non-zero byteLength'));
    if(view.buffer.byteLength === 0) return promiseRejectedWith(new TypeError(`view's buffer must have non-zero byteLength`));
    if(IsDetachedBuffer(view.buffer)) return promiseRejectedWith(new TypeError("view's buffer has been detached"));
    let options;
    try {
      options = ((options, context) => {
        var _a2;
        assertDictionary(options, context);
        const min = (_a2 = options == null ? undefined : options.min) != null ? _a2 : 1;
        return {
          min: convertUnsignedLongLongWithEnforceRange(min, `${context} has member 'min' that`),
        };
      })(rawOptions, 'options');
    } catch(e) {
      return promiseRejectedWith(e);
    }
    const min = options.min;
    if(min === 0) return promiseRejectedWith(new TypeError('options.min must be greater than 0'));
    if(!isDataView(view))
      if(min > view.length) return promiseRejectedWith(new RangeError("options.min must be less than or equal to view's length"));
      else if(min > view.byteLength) return promiseRejectedWith(new RangeError("options.min must be less than or equal to view's byteLength"));
    if(this._ownerReadableStream === undefined) return promiseRejectedWith(readerLockException('read from'));
    let resolvePromise;
    let rejectPromise;
    const promise = newPromise((resolve, reject) => {
      resolvePromise = resolve;
      rejectPromise = reject;
    });
    const readIntoRequest = {
      _chunkSteps: chunk => resolvePromise({ value: chunk, done: false }),
      _closeSteps: chunk => resolvePromise({ value: chunk, done: true }),
      _errorSteps: e => rejectPromise(e),
    };
    ReadableStreamBYOBReaderRead(this, view, min, readIntoRequest);
    return promise;
  }

  /**
   * Releases the reader's lock on the corresponding stream. After the lock is released, the reader is no longer active.
   * If the associated stream is errored when the lock is released, the reader will appear errored in the same way
   * from now on; otherwise, the reader will appear closed.
   *
   * A reader's lock cannot be released while it still has a pending read request, i.e., if a promise returned by
   * the reader's {@link ReadableStreamBYOBReader.read | read()} method has not yet been settled. Attempting to
   * do so will throw a `TypeError` and leave the reader locked to the stream.
   */
  releaseLock() {
    if(!IsReadableStreamBYOBReader(this)) throw byobReaderBrandCheckException('releaseLock');
    if(this._ownerReadableStream === undefined) return;

    ReadableStreamReaderGenericRelease(this);
    const e = new TypeError('Reader was released');
    ReadableStreamBYOBReaderErrorReadIntoRequests(this, e);
  }
}

/*Object.defineProperties(ReadableStreamBYOBReader.prototype, {
  cancel: { enumerable: true },
  read: { enumerable: true },
  releaseLock: { enumerable: true },
  closed: { enumerable: true },
});*/

setFunctionName(ReadableStreamBYOBReader.prototype.cancel, 'cancel');
setFunctionName(ReadableStreamBYOBReader.prototype.read, 'read');
setFunctionName(ReadableStreamBYOBReader.prototype.releaseLock, 'releaseLock');

declare(ReadableStreamBYOBReader.prototype, { [Symbol.toStringTag]: 'ReadableStreamBYOBReader' });

function IsReadableStreamBYOBReader(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_readIntoRequests')) return false;
  return x instanceof ReadableStreamBYOBReader;
}

function ReadableStreamBYOBReaderRead(reader, view, min, readIntoRequest) {
  const stream = reader._ownerReadableStream;
  assert(stream !== undefined);
  stream._disturbed = true;
  if(stream._state == 'errored') readIntoRequest._errorSteps(stream._storedError);
  else {
    const ctor = view.constructor;
    const elementSize = arrayBufferViewElementSize(ctor);
    const { byteOffset, byteLength } = view;
    const minimumFill = min * elementSize;
    assert(minimumFill >= elementSize && minimumFill <= byteLength);
    assert(minimumFill % elementSize === 0);
    let buffer;
    try {
      buffer = TransferArrayBuffer(view.buffer);
    } catch(e) {
      readIntoRequest._errorSteps(e);
      return;
    }

    const pullIntoDescriptor = {
      buffer,
      bufferByteLength: buffer.byteLength,
      byteOffset,
      byteLength,
      bytesFilled: 0,
      minimumFill,
      elementSize,
      viewConstructor: ctor,
      readerType: 'byob',
    };
    if(stream._readableStreamController._pendingPullIntos.length > 0) {
      stream._readableStreamController._pendingPullIntos.push(pullIntoDescriptor);
      ReadableStreamAddReadIntoRequest(stream, readIntoRequest);
      return;
    }

    if(stream._state == 'closed') {
      const emptyView = new ctor(pullIntoDescriptor.buffer, pullIntoDescriptor.byteOffset, 0);
      readIntoRequest._closeSteps(emptyView);
      return;
    }

    if(stream._readableStreamController._queueTotalSize > 0) {
      if(ReadableByteStreamControllerFillPullIntoDescriptorFromQueue(stream._readableStreamController, pullIntoDescriptor)) {
        const filledView = ReadableByteStreamControllerConvertPullIntoDescriptor(pullIntoDescriptor);
        ReadableByteStreamControllerHandleQueueDrain(stream._readableStreamController);
        readIntoRequest._chunkSteps(filledView);
        return;
      }
      if(stream._readableStreamController._closeRequested) {
        const e = new TypeError('Insufficient bytes to fill elements in the given buffer');
        ReadableByteStreamControllerError(stream._readableStreamController, e);
        readIntoRequest._errorSteps(e);
        return;
      }
    }

    stream._readableStreamController._pendingPullIntos.push(pullIntoDescriptor);
    ReadableStreamAddReadIntoRequest(stream, readIntoRequest);
    ReadableByteStreamControllerCallPullIfNeeded(stream._readableStreamController);
  }
}

function ReadableStreamBYOBReaderErrorReadIntoRequests(reader, e) {
  const readIntoRequests = reader._readIntoRequests;
  reader._readIntoRequests = new SimpleQueue();
  readIntoRequests.forEach(readIntoRequest => readIntoRequest._errorSteps(e));
}

function byobReaderBrandCheckException(name) {
  return new TypeError(`ReadableStreamBYOBReader.prototype.${name} can only be used on a ReadableStreamBYOBReader`);
}

// src/lib/abstract-ops/queuing-strategy.ts
function ExtractHighWaterMark(strategy, defaultHWM) {
  const { highWaterMark } = strategy;
  if(highWaterMark === undefined) return defaultHWM;
  if(NumberIsNaN(highWaterMark) || highWaterMark < 0) throw new RangeError('Invalid highWaterMark');
  return highWaterMark;
}

function ExtractSizeAlgorithm(strategy) {
  const { size } = strategy;
  if(!size) return () => 1;
  return size;
}

// src/lib/validators/queuing-strategy.ts
function convertQueuingStrategy(init, context) {
  assertDictionary(init, context);
  const highWaterMark = init == null ? undefined : init.highWaterMark;
  const size = init == null ? undefined : init.size;
  return {
    highWaterMark: highWaterMark === undefined ? undefined : convertUnrestrictedDouble(highWaterMark),
    size: size === undefined ? undefined : convertQueuingStrategySize(size, `${context} has member 'size' that`),
  };
}

function convertQueuingStrategySize(fn, context) {
  assertFunction(fn, context);
  return chunk => convertUnrestrictedDouble(fn(chunk));
}

// src/lib/validators/writable-stream.ts
function assertWritableStream(x, context) {
  if(!IsWritableStream(x)) throw new TypeError(`${context} is not a WritableStream.`);
}

// src/lib/abort-signal.ts
function isAbortSignal(value) {
  if(typeof value != 'object' || value === null) return false;
  if(isPrototypeOf(AbortSignal.prototype, value)) return true;
  try {
    return typeof value.aborted == 'boolean';
  } catch(e) {
    return false;
  }
}

// src/lib/writable-stream.ts
export class WritableStream {
  constructor(rawUnderlyingSink = {}, rawStrategy = {}) {
    if(rawUnderlyingSink === undefined) rawUnderlyingSink = null;
    else assertObject(rawUnderlyingSink, 'First parameter');
    const strategy = convertQueuingStrategy(rawStrategy, 'Second parameter');
    const underlyingSink = ((original, context) => {
      assertDictionary(original, context);
      const abort = original == null ? undefined : original.abort;
      const close = original == null ? undefined : original.close;
      const start = original == null ? undefined : original.start;
      const type = original == null ? undefined : original.type;
      const write = original == null ? undefined : original.write;
      return {
        abort: abort === undefined ? undefined : (assertFunction(abort, `${context} has member 'abort' that`), reason => promiseCall(abort, original, [reason])),
        close: close === undefined ? undefined : (assertFunction(close, `${context} has member 'close' that`), () => promiseCall(close, original, [])),
        start: start === undefined ? undefined : (assertFunction(start, `${context} has member 'start' that`), controller => reflectCall(start, original, [controller])),
        write: write === undefined ? undefined : (assertFunction(write, `${context} has member 'write' that`), (chunk, controller) => promiseCall(write, original, [chunk, controller])),
        type,
      };
    })(rawUnderlyingSink, 'First parameter');
    InitializeWritableStream(this);
    const type = underlyingSink.type;
    if(type !== undefined) throw new RangeError('Invalid type is specified');
    const sizeAlgorithm = ExtractSizeAlgorithm(strategy);
    const highWaterMark = ExtractHighWaterMark(strategy, 1);

    const controller = Object.create(WritableStreamDefaultController.prototype);
    const startAlgorithm = underlyingSink.start !== undefined ? () => underlyingSink.start(controller) : () => undefined;
    const writeAlgorithm = underlyingSink.write !== undefined ? chunk => underlyingSink.write(chunk, controller) : () => promiseResolvedWith(undefined);
    const closeAlgorithm = underlyingSink.close !== undefined ? () => underlyingSink.close() : () => promiseResolvedWith(undefined);
    const abortAlgorithm = underlyingSink.abort !== undefined ? reason => underlyingSink.abort(reason) : () => promiseResolvedWith(undefined);
    SetUpWritableStreamDefaultController(this, controller, startAlgorithm, writeAlgorithm, closeAlgorithm, abortAlgorithm, highWaterMark, sizeAlgorithm);
  }

  /**
   * Returns whether or not the writable stream is locked to a writer.
   */
  get locked() {
    if(!IsWritableStream(this)) throw streamBrandCheckException('locked');
    return IsWritableStreamLocked(this);
  }

  /**
   * Aborts the stream, signaling that the producer can no longer successfully write to the stream and it is to be
   * immediately moved to an errored state, with any queued-up writes discarded. This will also execute any abort
   * mechanism of the underlying sink.
   *
   * The returned promise will fulfill if the stream shuts down successfully, or reject if the underlying sink signaled
   * that there was an error doing so. Additionally, it will reject with a `TypeError` (without attempting to cancel
   * the stream) if the stream is currently locked.
   */
  abort(reason = undefined) {
    if(!IsWritableStream(this)) return promiseRejectedWith(streamBrandCheckException('abort'));
    if(IsWritableStreamLocked(this)) return promiseRejectedWith(new TypeError('Cannot abort a stream that already has a writer'));
    return WritableStreamAbort(this, reason);
  }

  /**
   * Closes the stream. The underlying sink will finish processing any previously-written chunks, before invoking its
   * close behavior. During this time any further attempts to write will fail (without erroring the stream).
   *
   * The method returns a promise that will fulfill if all remaining chunks are successfully written and the stream
   * successfully closes, or rejects if an error is encountered during this process. Additionally, it will reject with
   * a `TypeError` (without attempting to cancel the stream) if the stream is currently locked.
   */
  close() {
    if(!IsWritableStream(this)) return promiseRejectedWith(streamBrandCheckException('close'));
    if(IsWritableStreamLocked(this)) return promiseRejectedWith(new TypeError('Cannot close a stream that already has a writer'));
    if(WritableStreamCloseQueuedOrInFlight(this)) return promiseRejectedWith(new TypeError('Cannot close an already-closing stream'));
    return WritableStreamClose(this);
  }

  /**
   * Creates a {@link WritableStreamDefaultWriter | writer} and locks the stream to the new writer. While the stream
   * is locked, no other writer can be acquired until this one is released.
   *
   * This functionality is especially useful for creating abstractions that desire the ability to write to a stream
   * without interruption or interleaving. By getting a writer for the stream, you can ensure nobody else can write at
   * the same time, which would cause the resulting written data to be unpredictable and probably useless.
   */
  getWriter() {
    if(!IsWritableStream(this)) throw streamBrandCheckException('getWriter');
    return AcquireWritableStreamDefaultWriter(this);
  }
}

/*Object.defineProperties(WritableStream.prototype, {
  abort: { enumerable: true },
  close: { enumerable: true },
  getWriter: { enumerable: true },
  locked: { enumerable: true },
});*/

setFunctionName(WritableStream.prototype.abort, 'abort');
setFunctionName(WritableStream.prototype.close, 'close');
setFunctionName(WritableStream.prototype.getWriter, 'getWriter');

declare(WritableStream.prototype, { [Symbol.toStringTag]: 'WritableStream' });

function AcquireWritableStreamDefaultWriter(stream) {
  return new WritableStreamDefaultWriter(stream);
}

function InitializeWritableStream(stream) {
  declare(stream, {
    _state: 'writable',
    _storedError: undefined,
    _writer: undefined,
    _writableStreamController: undefined,
    _writeRequests: new SimpleQueue(),
    _inFlightWriteRequest: undefined,
    _closeRequest: undefined,
    _inFlightCloseRequest: undefined,
    _pendingAbortRequest: undefined,
    _backpressure: false,
  });
}

function CreateWritableStream(startAlgorithm, writeAlgorithm, closeAlgorithm, abortAlgorithm, highWaterMark = 1, sizeAlgorithm = () => 1) {
  assert(IsNonNegativeNumber(highWaterMark));
  const stream = Object.create(WritableStream.prototype);
  InitializeWritableStream(stream);
  const controller = Object.create(WritableStreamDefaultController.prototype);
  SetUpWritableStreamDefaultController(stream, controller, startAlgorithm, writeAlgorithm, closeAlgorithm, abortAlgorithm, highWaterMark, sizeAlgorithm);
  return stream;
}

function IsWritableStream(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_writableStreamController')) return false;
  return x instanceof WritableStream;
}

function IsWritableStreamLocked(stream) {
  assert(IsWritableStream(stream));
  if(stream._writer === undefined) return false;
  return true;
}

function WritableStreamAbort(stream, reason) {
  var _a2;
  if(stream._state == 'closed' || stream._state == 'errored') return promiseResolvedWith(undefined);
  stream._writableStreamController._abortReason = reason;
  (_a2 = stream._writableStreamController._abortController) == null ? undefined : _a2.abort(reason);
  const state = stream._state;
  if(state == 'closed' || state == 'errored') return promiseResolvedWith(undefined);
  if(stream._pendingAbortRequest !== undefined) return stream._pendingAbortRequest._promise;
  assert(state == 'writable' || state == 'erroring');
  let wasAlreadyErroring = false;
  if(state == 'erroring') {
    wasAlreadyErroring = true;
    reason = undefined;
  }

  const promise = newPromise((resolve, reject) => {
    stream._pendingAbortRequest = {
      _promise: undefined,
      _resolve: resolve,
      _reject: reject,
      _reason: reason,
      _wasAlreadyErroring: wasAlreadyErroring,
    };
  });
  stream._pendingAbortRequest._promise = promise;
  if(!wasAlreadyErroring) WritableStreamStartErroring(stream, reason);
  return promise;
}

function WritableStreamClose(stream) {
  const state = stream._state;
  if(state == 'closed' || state == 'errored') return promiseRejectedWith(new TypeError(`The stream (in ${state} state) is not in the writable state and cannot be closed`));
  assert(state == 'writable' || state == 'erroring');
  assert(!WritableStreamCloseQueuedOrInFlight(stream));
  const promise = newPromise((resolve, reject) => {
    const closeRequest = {
      _resolve: resolve,
      _reject: reject,
    };
    stream._closeRequest = closeRequest;
  });
  const writer = stream._writer;
  if(writer !== undefined && stream._backpressure && state == 'writable') defaultWriterReadyPromiseResolve(writer);

  EnqueueValueWithSize(stream._writableStreamController, closeSentinel, 0);
  WritableStreamDefaultControllerAdvanceQueueIfNeeded(stream._writableStreamController);

  return promise;
}

function WritableStreamDealWithRejection(stream, error) {
  const state = stream._state;
  if(state == 'writable') {
    WritableStreamStartErroring(stream, error);
    return;
  }

  assert(state == 'erroring');
  WritableStreamFinishErroring(stream);
}

function WritableStreamStartErroring(stream, reason) {
  assert(stream._storedError === undefined);
  assert(stream._state == 'writable');
  const controller = stream._writableStreamController;
  assert(controller !== undefined);
  stream._state = 'erroring';
  stream._storedError = reason;
  const writer = stream._writer;
  if(writer !== undefined) WritableStreamDefaultWriterEnsureReadyPromiseRejected(writer, reason);
  if(!WritableStreamHasOperationMarkedInFlight(stream) && controller._started) WritableStreamFinishErroring(stream);
}

function WritableStreamFinishErroring(stream) {
  assert(stream._state == 'erroring');
  assert(!WritableStreamHasOperationMarkedInFlight(stream));
  stream._state = 'errored';
  stream._writableStreamController[ErrorSteps]();
  const storedError = stream._storedError;
  stream._writeRequests.forEach(writeRequest => writeRequest._reject(storedError));
  stream._writeRequests = new SimpleQueue();
  if(stream._pendingAbortRequest === undefined) {
    WritableStreamRejectCloseAndClosedPromiseIfNeeded(stream);
    return;
  }

  const abortRequest = stream._pendingAbortRequest;
  stream._pendingAbortRequest = undefined;
  if(abortRequest._wasAlreadyErroring) {
    abortRequest._reject(storedError);
    WritableStreamRejectCloseAndClosedPromiseIfNeeded(stream);
    return;
  }

  const promise = stream._writableStreamController[AbortSteps](abortRequest._reason);
  uponPromise(
    promise,
    () => {
      abortRequest._resolve();
      WritableStreamRejectCloseAndClosedPromiseIfNeeded(stream);
      return null;
    },
    reason => {
      abortRequest._reject(reason);
      WritableStreamRejectCloseAndClosedPromiseIfNeeded(stream);
      return null;
    },
  );
}

function WritableStreamCloseQueuedOrInFlight(stream) {
  if(stream._closeRequest === undefined && stream._inFlightCloseRequest === undefined) return false;
  return true;
}

function WritableStreamHasOperationMarkedInFlight(stream) {
  if(stream._inFlightWriteRequest === undefined && stream._inFlightCloseRequest === undefined) return false;
  return true;
}

function WritableStreamRejectCloseAndClosedPromiseIfNeeded(stream) {
  assert(stream._state == 'errored');
  if(stream._closeRequest !== undefined) {
    assert(stream._inFlightCloseRequest === undefined);
    stream._closeRequest._reject(stream._storedError);
    stream._closeRequest = undefined;
  }

  const writer = stream._writer;
  if(writer !== undefined) defaultWriterClosedPromiseReject(writer, stream._storedError);
}

function WritableStreamUpdateBackpressure(stream, backpressure) {
  assert(stream._state == 'writable');
  assert(!WritableStreamCloseQueuedOrInFlight(stream));
  const writer = stream._writer;
  if(writer !== undefined && backpressure !== stream._backpressure) {
    if(backpressure) {
      assert(writer._readyPromise_resolve === undefined);
      assert(writer._readyPromise_reject === undefined);
      defaultWriterReadyPromiseInitialize(writer);
    } else {
      assert(!backpressure);
      defaultWriterReadyPromiseResolve(writer);
    }
  }

  stream._backpressure = backpressure;
}

export class WritableStreamDefaultWriter {
  constructor(stream) {
    assertRequiredArgument(stream, 1, 'WritableStreamDefaultWriter');
    assertWritableStream(stream, 'First parameter');
    if(IsWritableStreamLocked(stream)) throw new TypeError('This stream has already been locked for exclusive writing by another writer');
    declare(this, { _ownerWritableStream: stream });
    declare(stream, { _writer: this });
    const state = stream._state;
    if(state == 'writable') {
      if(!WritableStreamCloseQueuedOrInFlight(stream) && stream._backpressure) defaultWriterReadyPromiseInitialize(this);
      else defaultWriterReadyPromiseInitializeAsResolved(this);
      defaultWriterClosedPromiseInitialize(this);
    } else if(state == 'erroring') {
      defaultWriterReadyPromiseInitializeAsRejected(this, stream._storedError);
      defaultWriterClosedPromiseInitialize(this);
    } else if(state == 'closed') {
      defaultWriterReadyPromiseInitializeAsResolved(this);

      defaultWriterClosedPromiseInitialize(this);
      defaultWriterClosedPromiseResolve(this);
    } else {
      assert(state == 'errored');
      const storedError = stream._storedError;
      defaultWriterReadyPromiseInitializeAsRejected(this, storedError);
      defaultWriterClosedPromiseInitializeAsRejected(this, storedError);
    }
  }

  /**
   * Returns a promise that will be fulfilled when the stream becomes closed, or rejected if the stream ever errors or
   * the writer’s lock is released before the stream finishes closing.
   */
  get closed() {
    if(!IsWritableStreamDefaultWriter(this)) return promiseRejectedWith(defaultWriterBrandCheckException('closed'));
    return this._closedPromise;
  }

  /**
   * Returns the desired size to fill the stream’s internal queue. It can be negative, if the queue is over-full.
   * A producer can use this information to determine the right amount of data to write.
   *
   * It will be `null` if the stream cannot be successfully written to (due to either being errored, or having an abort
   * queued up). It will return zero if the stream is closed. And the getter will throw an exception if invoked when
   * the writer’s lock is released.
   */
  get desiredSize() {
    if(!IsWritableStreamDefaultWriter(this)) throw defaultWriterBrandCheckException('desiredSize');
    if(this._ownerWritableStream === undefined) throw defaultWriterLockException('desiredSize');

    const stream = this._ownerWritableStream;
    const state = stream._state;
    if(state == 'errored' || state == 'erroring') return null;
    if(state == 'closed') return 0;
    return WritableStreamDefaultControllerGetDesiredSize(stream._writableStreamController);
  }

  /**
   * Returns a promise that will be fulfilled when the desired size to fill the stream’s internal queue transitions
   * from non-positive to positive, signaling that it is no longer applying backpressure. Once the desired size dips
   * back to zero or below, the getter will return a new promise that stays pending until the next transition.
   *
   * If the stream becomes errored or aborted, or the writer’s lock is released, the returned promise will become
   * rejected.
   */
  get ready() {
    if(!IsWritableStreamDefaultWriter(this)) return promiseRejectedWith(defaultWriterBrandCheckException('ready'));
    return this._readyPromise;
  }

  /**
   * If the reader is active, behaves the same as {@link WritableStream.abort | stream.abort(reason)}.
   */
  abort(reason = undefined) {
    if(!IsWritableStreamDefaultWriter(this)) return promiseRejectedWith(defaultWriterBrandCheckException('abort'));
    if(this._ownerWritableStream === undefined) return promiseRejectedWith(defaultWriterLockException('abort'));

    const stream = this._ownerWritableStream;
    assert(stream !== undefined);
    return WritableStreamAbort(stream, reason);
  }

  /**
   * If the reader is active, behaves the same as {@link WritableStream.close | stream.close()}.
   */
  close() {
    if(!IsWritableStreamDefaultWriter(this)) return promiseRejectedWith(defaultWriterBrandCheckException('close'));
    const stream = this._ownerWritableStream;
    if(stream === undefined) return promiseRejectedWith(defaultWriterLockException('close'));
    if(WritableStreamCloseQueuedOrInFlight(stream)) return promiseRejectedWith(new TypeError('Cannot close an already-closing stream'));
    return WritableStreamDefaultWriterClose(this);
  }

  /**
   * Releases the writer’s lock on the corresponding stream. After the lock is released, the writer is no longer active.
   * If the associated stream is errored when the lock is released, the writer will appear errored in the same way from
   * now on; otherwise, the writer will appear closed.
   *
   * Note that the lock can still be released even if some ongoing writes have not yet finished (i.e. even if the
   * promises returned from previous calls to {@link WritableStreamDefaultWriter.write | write()} have not yet settled).
   * It’s not necessary to hold the lock on the writer for the duration of the write; the lock instead simply prevents
   * other producers from writing in an interleaved manner.
   */
  releaseLock() {
    if(!IsWritableStreamDefaultWriter(this)) throw defaultWriterBrandCheckException('releaseLock');
    const stream = this._ownerWritableStream;
    if(stream === undefined) return;
    assert(stream._writer !== undefined);
    WritableStreamDefaultWriterRelease(this);
  }

  write(chunk = undefined) {
    if(!IsWritableStreamDefaultWriter(this)) return promiseRejectedWith(defaultWriterBrandCheckException('write'));
    if(this._ownerWritableStream === undefined) return promiseRejectedWith(defaultWriterLockException('write to'));
    return WritableStreamDefaultWriterWrite(this, chunk);
  }
}

/*Object.defineProperties(WritableStreamDefaultWriter.prototype, {
  abort: { enumerable: true },
  close: { enumerable: true },
  releaseLock: { enumerable: true },
  write: { enumerable: true },
  closed: { enumerable: true },
  desiredSize: { enumerable: true },
  ready: { enumerable: true },
});*/

setFunctionName(WritableStreamDefaultWriter.prototype.abort, 'abort');
setFunctionName(WritableStreamDefaultWriter.prototype.close, 'close');
setFunctionName(WritableStreamDefaultWriter.prototype.releaseLock, 'releaseLock');
setFunctionName(WritableStreamDefaultWriter.prototype.write, 'write');

declare(WritableStreamDefaultWriter.prototype, { [Symbol.toStringTag]: 'WritableStreamDefaultWriter' });

function IsWritableStreamDefaultWriter(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_ownerWritableStream')) return false;
  return x instanceof WritableStreamDefaultWriter;
}

function WritableStreamDefaultWriterClose(writer) {
  const stream = writer._ownerWritableStream;
  assert(stream !== undefined);
  return WritableStreamClose(stream);
}

function WritableStreamDefaultWriterEnsureReadyPromiseRejected(writer, error) {
  if(writer._readyPromiseState == 'pending') {
    defaultWriterReadyPromiseReject(writer, error);
  } else {
    assert(writer._readyPromise_resolve === undefined);
    assert(writer._readyPromise_reject === undefined);
    defaultWriterReadyPromiseInitializeAsRejected(writer, error);
  }
}

function WritableStreamDefaultWriterRelease(writer) {
  const stream = writer._ownerWritableStream;
  assert(stream !== undefined);
  assert(stream._writer === writer);
  const releasedError = new TypeError(`Writer was released and can no longer be used to monitor the stream's closedness`);
  WritableStreamDefaultWriterEnsureReadyPromiseRejected(writer, releasedError);

  if(writer._closedPromiseState == 'pending') defaultWriterClosedPromiseReject(writer, releasedError);
  else {
    assert(writer._closedPromise_resolve === undefined);
    assert(writer._closedPromise_reject === undefined);
    assert(writer._closedPromiseState != 'pending');
    defaultWriterClosedPromiseInitializeAsRejected(writer, releasedError);
  }

  stream._writer = undefined;
  writer._ownerWritableStream = undefined;
}

function WritableStreamDefaultWriterWrite(writer, chunk) {
  const stream = writer._ownerWritableStream;
  assert(stream !== undefined);
  const controller = stream._writableStreamController;

  let chunkSize;

  if(controller._strategySizeAlgorithm === undefined) {
    assert(controller._controlledWritableStream._state == 'erroring' || controller._controlledWritableStream._state == 'errored');
    return 1;
  }
  try {
    chunkSize = controller._strategySizeAlgorithm(chunk);
  } catch(chunkSizeE) {
    WritableStreamDefaultControllerErrorIfNeeded(controller, chunkSizeE);
    chunkSize = 1;
  }

  if(stream !== writer._ownerWritableStream) return promiseRejectedWith(defaultWriterLockException('write to'));
  const state = stream._state;
  if(state == 'errored') return promiseRejectedWith(stream._storedError);
  if(WritableStreamCloseQueuedOrInFlight(stream) || state == 'closed') return promiseRejectedWith(new TypeError('The stream is closing or closed and cannot be written to'));
  if(state == 'erroring') return promiseRejectedWith(stream._storedError);
  assert(state == 'writable');

  assert(IsWritableStreamLocked(stream));
  assert(stream._state == 'writable');
  const promise = newPromise((resolve, reject) =>
    stream._writeRequests.push({
      _resolve: resolve,
      _reject: reject,
    }),
  );

  try {
    EnqueueValueWithSize(controller, chunk, chunkSize);
  } catch(enqueueE) {
    WritableStreamDefaultControllerErrorIfNeeded(controller, enqueueE);
    return;
  }

  //const stream = controller._controlledWritableStream;
  if(!WritableStreamCloseQueuedOrInFlight(stream) && stream._state == 'writable') {
    const backpressure = WritableStreamDefaultControllerGetBackpressure(controller);
    WritableStreamUpdateBackpressure(stream, backpressure);
  }

  WritableStreamDefaultControllerAdvanceQueueIfNeeded(controller);

  return promise;
}

export class WritableStreamDefaultController {
  constructor() {
    throw new TypeError('Illegal constructor');
  }

  /**
   * The reason which was passed to `WritableStream.abort(reason)` when the stream was aborted.
   *
   * @deprecated
   *  This property has been removed from the specification, see https://github.com/whatwg/streams/pull/1177.
   *  Use {@link WritableStreamDefaultController.signal}'s `reason` instead.
   */
  get abortReason() {
    if(!IsWritableStreamDefaultController(this)) throw defaultControllerBrandCheckException('abortReason');
    return this._abortReason;
  }

  /**
   * An `AbortSignal` that can be used to abort the pending write or close operation when the stream is aborted.
   */
  get signal() {
    if(!IsWritableStreamDefaultController(this)) throw defaultControllerBrandCheckException('signal');
    if(this._abortController === undefined) throw new TypeError('WritableStreamDefaultController.prototype.signal is not supported');
    return this._abortController.signal;
  }

  /**
   * Closes the controlled writable stream, making all future interactions with it fail with the given error `e`.
   *
   * This method is rarely used, since usually it suffices to return a rejected promise from one of the underlying
   * sink's methods. However, it can be useful for suddenly shutting down a stream in response to an event outside the
   * normal lifecycle of interactions with the underlying sink.
   */
  error(e = undefined) {
    if(!IsWritableStreamDefaultController(this)) throw defaultControllerBrandCheckException('error');
    const state = this._controlledWritableStream._state;
    if(state != 'writable') return;
    WritableStreamDefaultControllerError(this, e);
  }

  /** @internal */
  [AbortSteps](reason) {
    const result = this._abortAlgorithm(reason);
    WritableStreamDefaultControllerClearAlgorithms(this);
    return result;
  }

  /** @internal */
  [ErrorSteps]() {
    ResetQueue(this);
  }
}

/*Object.defineProperties(WritableStreamDefaultController.prototype, {
  abortReason: { enumerable: true },
  signal: { enumerable: true },
  error: { enumerable: true },
});*/

declare(WritableStreamDefaultController.prototype, { [Symbol.toStringTag]: 'WritableStreamDefaultController' });

function IsWritableStreamDefaultController(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_controlledWritableStream')) return false;
  return x instanceof WritableStreamDefaultController;
}

function SetUpWritableStreamDefaultController(stream, controller, startAlgorithm, writeAlgorithm, closeAlgorithm, abortAlgorithm, highWaterMark, sizeAlgorithm) {
  assert(IsWritableStream(stream));
  assert(stream._writableStreamController === undefined);
  declare(controller, { _controlledWritableStream: stream });
  declare(stream, { _writableStreamController: controller });
  declare(controller, {
    _queue: undefined,
    _queueTotalSize: undefined,
  });
  ResetQueue(controller);
  declare(controller, {
    _abortReason: undefined,
    _abortController: new AbortController(),
    _started: false,
    _strategySizeAlgorithm: sizeAlgorithm,
    _strategyHWM: highWaterMark,
    _writeAlgorithm: writeAlgorithm,
    _closeAlgorithm: closeAlgorithm,
    _abortAlgorithm: abortAlgorithm,
  });
  const backpressure = WritableStreamDefaultControllerGetBackpressure(controller);
  WritableStreamUpdateBackpressure(stream, backpressure);
  const startResult = startAlgorithm();
  const startPromise = promiseResolvedWith(startResult);
  uponPromise(
    startPromise,
    () => {
      assert(stream._state == 'writable' || stream._state == 'erroring');
      controller._started = true;
      WritableStreamDefaultControllerAdvanceQueueIfNeeded(controller);
      return null;
    },
    r => {
      assert(stream._state == 'writable' || stream._state == 'erroring');
      controller._started = true;
      WritableStreamDealWithRejection(stream, r);
      return null;
    },
  );
}

function WritableStreamDefaultControllerClearAlgorithms(controller) {
  controller._writeAlgorithm = undefined;
  controller._closeAlgorithm = undefined;
  controller._abortAlgorithm = undefined;
  controller._strategySizeAlgorithm = undefined;
}

function WritableStreamDefaultControllerClose(controller) {
  EnqueueValueWithSize(controller, closeSentinel, 0);
  WritableStreamDefaultControllerAdvanceQueueIfNeeded(controller);
}

function WritableStreamDefaultControllerGetDesiredSize(controller) {
  return controller._strategyHWM - controller._queueTotalSize;
}

function WritableStreamDefaultControllerAdvanceQueueIfNeeded(controller) {
  const stream = controller._controlledWritableStream;
  if(!controller._started) return;
  if(stream._inFlightWriteRequest !== undefined) return;
  const state = stream._state;
  assert(state != 'closed' && state != 'errored');
  if(state == 'erroring') {
    WritableStreamFinishErroring(stream);
    return;
  }

  if(controller._queue.length === 0) return;
  const value = PeekQueueValue(controller);
  if(value === closeSentinel) {
    assert(stream._inFlightCloseRequest === undefined);
    assert(stream._closeRequest !== undefined);
    stream._inFlightCloseRequest = stream._closeRequest;
    stream._closeRequest = undefined;

    DequeueValue(controller);
    assert(controller._queue.length === 0);
    const sinkClosePromise = controller._closeAlgorithm();
    WritableStreamDefaultControllerClearAlgorithms(controller);
    uponPromise(
      sinkClosePromise,
      () => {
        assert(stream._inFlightCloseRequest !== undefined);
        stream._inFlightCloseRequest._resolve(undefined);
        stream._inFlightCloseRequest = undefined;
        const state = stream._state;
        assert(state == 'writable' || state == 'erroring');
        if(state == 'erroring') {
          stream._storedError = undefined;
          if(stream._pendingAbortRequest !== undefined) {
            stream._pendingAbortRequest._resolve();
            stream._pendingAbortRequest = undefined;
          }
        }

        stream._state = 'closed';
        const writer = stream._writer;
        if(writer !== undefined) defaultWriterClosedPromiseResolve(writer);
        assert(stream._pendingAbortRequest === undefined);
        assert(stream._storedError === undefined);

        return null;
      },
      reason => {
        assert(stream._inFlightCloseRequest !== undefined);
        stream._inFlightCloseRequest._reject(reason);
        stream._inFlightCloseRequest = undefined;
        assert(stream._state == 'writable' || stream._state == 'erroring');
        if(stream._pendingAbortRequest !== undefined) {
          stream._pendingAbortRequest._reject(reason);
          stream._pendingAbortRequest = undefined;
        }

        WritableStreamDealWithRejection(stream, reason);
        return null;
      },
    );
  } else {
    assert(stream._inFlightWriteRequest === undefined);
    assert(stream._writeRequests.length !== 0);
    stream._inFlightWriteRequest = stream._writeRequests.shift();

    const sinkWritePromise = controller._writeAlgorithm(value);
    uponPromise(
      sinkWritePromise,
      () => {
        assert(stream._inFlightWriteRequest !== undefined);
        stream._inFlightWriteRequest._resolve(undefined);
        stream._inFlightWriteRequest = undefined;

        const state = stream._state;
        assert(state == 'writable' || state == 'erroring');
        DequeueValue(controller);
        if(!WritableStreamCloseQueuedOrInFlight(stream) && state == 'writable') {
          const backpressure = WritableStreamDefaultControllerGetBackpressure(controller);
          WritableStreamUpdateBackpressure(stream, backpressure);
        }
        WritableStreamDefaultControllerAdvanceQueueIfNeeded(controller);
        return null;
      },
      reason => {
        if(stream._state == 'writable') WritableStreamDefaultControllerClearAlgorithms(controller);

        assert(stream._inFlightWriteRequest !== undefined);
        stream._inFlightWriteRequest._reject(error);
        stream._inFlightWriteRequest = undefined;
        assert(stream._state == 'writable' || stream._state == 'erroring');
        WritableStreamDealWithRejection(stream, error);

        return null;
      },
    );
  }
}

function WritableStreamDefaultControllerErrorIfNeeded(controller, error) {
  if(controller._controlledWritableStream._state == 'writable') WritableStreamDefaultControllerError(controller, error);
}

function WritableStreamDefaultControllerGetBackpressure(controller) {
  const desiredSize = WritableStreamDefaultControllerGetDesiredSize(controller);
  return desiredSize <= 0;
}

function WritableStreamDefaultControllerError(controller, error) {
  const stream = controller._controlledWritableStream;
  assert(stream._state == 'writable');
  WritableStreamDefaultControllerClearAlgorithms(controller);
  WritableStreamStartErroring(stream, error);
}

function streamBrandCheckException(name) {
  return new TypeError(`WritableStream.prototype.${name} can only be used on a WritableStream`);
}

function defaultControllerBrandCheckException(name) {
  return new TypeError(`WritableStreamDefaultController.prototype.${name} can only be used on a WritableStreamDefaultController`);
}

function defaultWriterBrandCheckException(name) {
  return new TypeError(`WritableStreamDefaultWriter.prototype.${name} can only be used on a WritableStreamDefaultWriter`);
}

function defaultWriterLockException(name) {
  return new TypeError('Cannot ' + name + ' a stream using a released writer');
}

function defaultWriterClosedPromiseInitialize(writer) {
  declare(writer, {
    _closedPromise: newPromise((resolve, reject) => {
      declare(writer, { _closedPromise_resolve: resolve });
      declare(writer, { _closedPromise_reject: reject });
      declare(writer, { _closedPromiseState: 'pending' });
    }),
  });
}

function defaultWriterClosedPromiseInitializeAsRejected(writer, reason) {
  defaultWriterClosedPromiseInitialize(writer);
  defaultWriterClosedPromiseReject(writer, reason);
}

function defaultWriterClosedPromiseReject(writer, reason) {
  if(writer._closedPromise_reject === undefined) return;
  assert(writer._closedPromiseState == 'pending');
  setPromiseIsHandledToTrue(writer._closedPromise);
  writer._closedPromise_reject(reason);
  writer._closedPromise_resolve = undefined;
  writer._closedPromise_reject = undefined;
  writer._closedPromiseState = 'rejected';
}

function defaultWriterClosedPromiseResolve(writer) {
  if(writer._closedPromise_resolve === undefined) return;
  assert(writer._closedPromiseState == 'pending');
  writer._closedPromise_resolve(undefined);
  writer._closedPromise_resolve = undefined;
  writer._closedPromise_reject = undefined;
  writer._closedPromiseState = 'resolved';
}

function defaultWriterReadyPromiseInitialize(writer) {
  declare(writer, {
    _readyPromise: newPromise((resolve, reject) => declare(writer, { _readyPromise_resolve: resolve, _readyPromise_reject: reject })),
    _readyPromiseState: 'pending',
  });
}

function defaultWriterReadyPromiseInitializeAsRejected(writer, reason) {
  defaultWriterReadyPromiseInitialize(writer);
  defaultWriterReadyPromiseReject(writer, reason);
}

function defaultWriterReadyPromiseInitializeAsResolved(writer) {
  defaultWriterReadyPromiseInitialize(writer);
  defaultWriterReadyPromiseResolve(writer);
}

function defaultWriterReadyPromiseReject(writer, reason) {
  if(writer._readyPromise_reject === undefined) return;
  setPromiseIsHandledToTrue(writer._readyPromise);
  writer._readyPromise_reject(reason);
  writer._readyPromise_resolve = undefined;
  writer._readyPromise_reject = undefined;
  writer._readyPromiseState = 'rejected';
}

function defaultWriterReadyPromiseResolve(writer) {
  if(writer._readyPromise_resolve === undefined) return;
  writer._readyPromise_resolve(undefined);
  writer._readyPromise_resolve = undefined;
  writer._readyPromise_reject = undefined;
  writer._readyPromiseState = 'fulfilled';
}

// src/globals.ts
function getGlobals() {
  if(typeof globalThis != 'undefined') return globalThis;
  else if(typeof self != 'undefined') return self;
  else if(typeof global != 'undefined') return global;
  return undefined;
}

const globals = getGlobals();

// src/stub/dom-exception.ts
function isDOMExceptionConstructor(ctor) {
  if(!(typeof ctor == 'function' || typeof ctor == 'object')) return false;
  if(ctor.name != 'DOMException') return false;
  try {
    new ctor();
    return true;
  } catch(e) {
    return false;
  }
}

function getFromGlobal() {
  let _a2;
  const ctor = (_a2 = globals) == null ? undefined : _a2.DOMException;
  return isDOMExceptionConstructor(ctor) ? ctor : undefined;
}

function createPolyfill() {
  const ctor = function DOMException2(message, name) {
    this.message = message || '';
    this.name = name || 'Error';
    if(Error.captureStackTrace) Error.captureStackTrace(this, this.constructor);
  };
  setFunctionName(ctor, 'DOMException');
  ctor.prototype = Object.create(Error.prototype);
  Object.defineProperty(ctor.prototype, 'constructor', { value: ctor, writable: true, configurable: true });
  return ctor;
}

const DOMException = getFromGlobal() ?? createPolyfill();

// src/lib/readable-stream/pipe.ts
function ReadableStreamPipeTo(source, dest, preventClose, preventAbort, preventCancel, signal) {
  assert(IsReadableStream(source));
  assert(IsWritableStream(dest));
  assert(typeof preventClose == 'boolean');
  assert(typeof preventAbort == 'boolean');
  assert(typeof preventCancel == 'boolean');
  assert(signal === undefined || isAbortSignal(signal));
  assert(!IsReadableStreamLocked(source));
  assert(!IsWritableStreamLocked(dest));
  const reader = AcquireReadableStreamDefaultReader(source);
  const writer = AcquireWritableStreamDefaultWriter(dest);
  source._disturbed = true;
  let shuttingDown = false;
  let currentWrite = promiseResolvedWith(undefined);
  return newPromise((resolve, reject) => {
    let abortAlgorithm;
    if(signal !== undefined) {
      abortAlgorithm = () => {
        const error = signal.reason !== undefined ? signal.reason : new DOMException('Aborted', 'AbortError');
        const actions = [];
        if(!preventAbort) {
          actions.push(() => {
            if(dest._state == 'writable') return WritableStreamAbort(dest, error);
            return promiseResolvedWith(undefined);
          });
        }
        if(!preventCancel) {
          actions.push(() => {
            if(source._state == 'readable') return ReadableStreamCancel(source, error);
            return promiseResolvedWith(undefined);
          });
        }
        shutdownWithAction(() => Promise.all(actions.map(action => action())), true, error);
      };
      if(signal.aborted) {
        abortAlgorithm();
        return;
      }
      signal.addEventListener('abort', abortAlgorithm);
    }
    function pipeLoop() {
      return newPromise((resolveLoop, rejectLoop) => {
        function next(done) {
          if(done) resolveLoop();
          else PerformPromiseThen(pipeStep(), next, rejectLoop);
        }
        next(false);
      });
    }
    function pipeStep() {
      if(shuttingDown) return promiseResolvedWith(true);
      return PerformPromiseThen(writer._readyPromise, () => {
        return newPromise((resolveRead, rejectRead) => {
          ReadableStreamDefaultReaderRead(reader, {
            _chunkSteps: chunk => {
              currentWrite = PerformPromiseThen(WritableStreamDefaultWriterWrite(writer, chunk), undefined, noop);
              resolveRead(false);
            },
            _closeSteps: () => resolveRead(true),
            _errorSteps: rejectRead,
          });
        });
      });
    }
    isOrBecomesErrored(source, reader._closedPromise, storedError => {
      if(!preventAbort) shutdownWithAction(() => WritableStreamAbort(dest, storedError), true, storedError);
      else shutdown(true, storedError);
      return null;
    });
    isOrBecomesErrored(dest, writer._closedPromise, storedError => {
      if(!preventCancel) shutdownWithAction(() => ReadableStreamCancel(source, storedError), true, storedError);
      else shutdown(true, storedError);
      return null;
    });
    isOrBecomesClosed(source, reader._closedPromise, () => {
      if(!preventClose)
        shutdownWithAction(() => {
          const stream = writer._ownerWritableStream;
          assert(stream !== undefined);
          const state = stream._state;
          if(WritableStreamCloseQueuedOrInFlight(stream) || state == 'closed') return promiseResolvedWith(undefined);
          if(state == 'errored') return promiseRejectedWith(stream._storedError);
          assert(state == 'writable' || state == 'erroring');
          return WritableStreamDefaultWriterClose(writer);
        });
      else shutdown();
      return null;
    });
    if(WritableStreamCloseQueuedOrInFlight(dest) || dest._state == 'closed') {
      const destClosed = new TypeError('the destination writable stream closed before all data could be piped to it');
      if(!preventCancel) shutdownWithAction(() => ReadableStreamCancel(source, destClosed), true, destClosed);
      else shutdown(true, destClosed);
    }
    setPromiseIsHandledToTrue(pipeLoop());
    function waitForWritesToFinish() {
      const oldCurrentWrite = currentWrite;
      return PerformPromiseThen(currentWrite, () => (oldCurrentWrite !== currentWrite ? waitForWritesToFinish() : undefined));
    }
    function isOrBecomesErrored(stream, promise, action) {
      if(stream._state == 'errored') action(stream._storedError);
      else uponRejection(promise, action);
    }
    function isOrBecomesClosed(stream, promise, action) {
      if(stream._state == 'closed') action();
      else uponFulfillment(promise, action);
    }
    function shutdownWithAction(action, originalIsError, originalError) {
      if(shuttingDown) return;
      shuttingDown = true;
      if(dest._state == 'writable' && !WritableStreamCloseQueuedOrInFlight(dest)) uponFulfillment(waitForWritesToFinish(), doTheRest);
      else doTheRest();
      function doTheRest() {
        uponPromise(
          action(),
          () => finalize(originalIsError, originalError),
          newError => finalize(true, newError),
        );
        return null;
      }
    }
    function shutdown(isError, error) {
      if(shuttingDown) return;
      shuttingDown = true;
      if(dest._state == 'writable' && !WritableStreamCloseQueuedOrInFlight(dest)) uponFulfillment(waitForWritesToFinish(), () => finalize(isError, error));
      else finalize(isError, error);
    }
    function finalize(isError, error) {
      WritableStreamDefaultWriterRelease(writer);
      ReadableStreamReaderGenericRelease(reader);
      if(signal !== undefined) signal.removeEventListener('abort', abortAlgorithm);
      if(isError) reject(error);
      else resolve(undefined);
      return null;
    }
  });
}

// src/lib/readable-stream/default-controller.ts
export class ReadableStreamDefaultController {
  constructor() {
    throw new TypeError('Illegal constructor');
  }

  /**
   * Returns the desired size to fill the controlled stream's internal queue. It can be negative, if the queue is
   * over-full. An underlying source ought to use this information to determine when and how to apply backpressure.
   */
  get desiredSize() {
    if(!IsReadableStreamDefaultController(this)) throw defaultControllerBrandCheckException2('desiredSize');
    return ReadableStreamDefaultControllerGetDesiredSize(this);
  }

  /**
   * Closes the controlled readable stream. Consumers will still be able to read any previously-enqueued chunks from
   * the stream, but once those are read, the stream will become closed.
   */
  close() {
    if(!IsReadableStreamDefaultController(this)) throw defaultControllerBrandCheckException2('close');
    if(!ReadableStreamDefaultControllerCanCloseOrEnqueue(this)) throw new TypeError('The stream is not in a state that permits close');
    ReadableStreamDefaultControllerClose(this);
  }

  enqueue(chunk = undefined) {
    if(!IsReadableStreamDefaultController(this)) throw defaultControllerBrandCheckException2('enqueue');
    if(!ReadableStreamDefaultControllerCanCloseOrEnqueue(this)) throw new TypeError('The stream is not in a state that permits enqueue');
    return ReadableStreamDefaultControllerEnqueue(this, chunk);
  }

  /**
   * Errors the controlled readable stream, making all future interactions with it fail with the given error `e`.
   */
  error(e = undefined) {
    if(!IsReadableStreamDefaultController(this)) throw defaultControllerBrandCheckException2('error');
    ReadableStreamDefaultControllerError(this, e);
  }

  /** @internal */
  [CancelSteps](reason) {
    ResetQueue(this);
    const result = this._cancelAlgorithm(reason);
    ReadableStreamDefaultControllerClearAlgorithms(this);
    return result;
  }

  /** @internal */
  [PullSteps](readRequest) {
    const stream = this._controlledReadableStream;
    if(this._queue.length > 0) {
      const chunk = DequeueValue(this);
      if(this._closeRequested && this._queue.length === 0) {
        ReadableStreamDefaultControllerClearAlgorithms(this);
        ReadableStreamClose(stream);
      } else {
        ReadableStreamDefaultControllerCallPullIfNeeded(this);
      }
      readRequest._chunkSteps(chunk);
    } else {
      ReadableStreamAddReadRequest(stream, readRequest);
      ReadableStreamDefaultControllerCallPullIfNeeded(this);
    }
  }

  /** @internal */
  [ReleaseSteps]() {}
}

/*Object.defineProperties(ReadableStreamDefaultController.prototype, {
  close: { enumerable: true },
  enqueue: { enumerable: true },
  error: { enumerable: true },
  desiredSize: { enumerable: true },
});*/

setFunctionName(ReadableStreamDefaultController.prototype.close, 'close');
setFunctionName(ReadableStreamDefaultController.prototype.enqueue, 'enqueue');
setFunctionName(ReadableStreamDefaultController.prototype.error, 'error');

declare(ReadableStreamDefaultController.prototype, { [Symbol.toStringTag]: 'ReadableStreamDefaultController' });

function IsReadableStreamDefaultController(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_controlledReadableStream')) return false;
  return x instanceof ReadableStreamDefaultController;
}

function ReadableStreamDefaultControllerCallPullIfNeeded(controller) {
  const shouldPull = ReadableStreamDefaultControllerShouldCallPull(controller);
  if(!shouldPull) return;
  if(controller._pulling) {
    controller._pullAgain = true;
    return;
  }

  assert(!controller._pullAgain);
  controller._pulling = true;
  const pullPromise = controller._pullAlgorithm();
  uponPromise(
    pullPromise,
    () => {
      controller._pulling = false;
      if(controller._pullAgain) {
        controller._pullAgain = false;
        ReadableStreamDefaultControllerCallPullIfNeeded(controller);
      }
      return null;
    },
    e => {
      ReadableStreamDefaultControllerError(controller, e);
      return null;
    },
  );
}

function ReadableStreamDefaultControllerShouldCallPull(controller) {
  const stream = controller._controlledReadableStream;
  if(!ReadableStreamDefaultControllerCanCloseOrEnqueue(controller)) return false;
  if(!controller._started) return false;
  if(IsReadableStreamLocked(stream) && ReadableStreamGetNumReadRequests(stream) > 0) return true;
  const desiredSize = ReadableStreamDefaultControllerGetDesiredSize(controller);
  assert(desiredSize !== null);
  return desiredSize > 0;
}

function ReadableStreamDefaultControllerClearAlgorithms(controller) {
  controller._pullAlgorithm = undefined;
  controller._cancelAlgorithm = undefined;
  controller._strategySizeAlgorithm = undefined;
}

function ReadableStreamDefaultControllerClose(controller) {
  if(!ReadableStreamDefaultControllerCanCloseOrEnqueue(controller)) return;
  const stream = controller._controlledReadableStream;
  controller._closeRequested = true;
  if(controller._queue.length === 0) {
    ReadableStreamDefaultControllerClearAlgorithms(controller);
    ReadableStreamClose(stream);
  }
}

function ReadableStreamDefaultControllerEnqueue(controller, chunk) {
  if(!ReadableStreamDefaultControllerCanCloseOrEnqueue(controller)) return;
  const stream = controller._controlledReadableStream;
  if(IsReadableStreamLocked(stream) && ReadableStreamGetNumReadRequests(stream) > 0) {
    ReadableStreamFulfillReadRequest(stream, chunk, false);
  } else {
    let chunkSize;
    try {
      chunkSize = controller._strategySizeAlgorithm(chunk);
    } catch(chunkSizeE) {
      ReadableStreamDefaultControllerError(controller, chunkSizeE);
      throw chunkSizeE;
    }
    try {
      EnqueueValueWithSize(controller, chunk, chunkSize);
    } catch(enqueueE) {
      ReadableStreamDefaultControllerError(controller, enqueueE);
      throw enqueueE;
    }
  }

  ReadableStreamDefaultControllerCallPullIfNeeded(controller);
}

function ReadableStreamDefaultControllerError(controller, e) {
  const stream = controller._controlledReadableStream;
  if(stream._state != 'readable') return;
  ResetQueue(controller);
  ReadableStreamDefaultControllerClearAlgorithms(controller);
  ReadableStreamError(stream, e);
}

function ReadableStreamDefaultControllerGetDesiredSize(controller) {
  const state = controller._controlledReadableStream._state;
  if(state == 'errored') return null;
  if(state == 'closed') return 0;
  return controller._strategyHWM - controller._queueTotalSize;
}

function ReadableStreamDefaultControllerHasBackpressure(controller) {
  return !ReadableStreamDefaultControllerShouldCallPull(controller);
}

function ReadableStreamDefaultControllerCanCloseOrEnqueue(controller) {
  const state = controller._controlledReadableStream._state;
  if(!controller._closeRequested && state == 'readable') return true;
  return false;
}

function SetUpReadableStreamDefaultController(stream, controller, startAlgorithm, pullAlgorithm, cancelAlgorithm, highWaterMark, sizeAlgorithm) {
  assert(stream._readableStreamController === undefined);
  declare(controller, {
    _controlledReadableStream: stream,
    _queue: undefined,
    _queueTotalSize: undefined,
  });
  ResetQueue(controller);
  declare(controller, {
    _started: false,
    _closeRequested: false,
    _pullAgain: false,
    _pulling: false,
    _strategySizeAlgorithm: sizeAlgorithm,
    _strategyHWM: highWaterMark,
    _pullAlgorithm: pullAlgorithm,
    _cancelAlgorithm: cancelAlgorithm,
  });
  declare(stream, { _readableStreamController: controller });
  const startResult = startAlgorithm();
  uponPromise(
    promiseResolvedWith(startResult),
    () => {
      controller._started = true;
      assert(!controller._pulling);
      assert(!controller._pullAgain);
      ReadableStreamDefaultControllerCallPullIfNeeded(controller);
      return null;
    },
    r => {
      ReadableStreamDefaultControllerError(controller, r);
      return null;
    },
  );
}

function defaultControllerBrandCheckException2(name) {
  return new TypeError(`ReadableStreamDefaultController.prototype.${name} can only be used on a ReadableStreamDefaultController`);
}

// src/lib/readable-stream/tee.ts

function ReadableStreamDefaultTee(stream, cloneForBranch2) {
  assert(IsReadableStream(stream));
  assert(typeof cloneForBranch2 == 'boolean');
  const reader = AcquireReadableStreamDefaultReader(stream);
  let reading = false;
  let readAgain = false;
  let canceled1 = false;
  let canceled2 = false;
  let reason1;
  let reason2;
  let branch1;
  let branch2;
  let resolveCancelPromise;
  const cancelPromise = newPromise(resolve => (resolveCancelPromise = resolve));
  function pullAlgorithm() {
    if(reading) {
      readAgain = true;
      return promiseResolvedWith(undefined);
    }
    reading = true;
    const readRequest = {
      _chunkSteps: chunk => {
        _queueMicrotask(() => {
          readAgain = false;
          const chunk1 = chunk;
          const chunk2 = chunk;
          if(!canceled1) ReadableStreamDefaultControllerEnqueue(branch1._readableStreamController, chunk1);
          if(!canceled2) ReadableStreamDefaultControllerEnqueue(branch2._readableStreamController, chunk2);
          reading = false;
          if(readAgain) pullAlgorithm();
        });
      },
      _closeSteps: () => {
        reading = false;
        if(!canceled1) ReadableStreamDefaultControllerClose(branch1._readableStreamController);
        if(!canceled2) ReadableStreamDefaultControllerClose(branch2._readableStreamController);
        if(!canceled1 || !canceled2) resolveCancelPromise(undefined);
      },
      _errorSteps: () => (reading = false),
    };
    ReadableStreamDefaultReaderRead(reader, readRequest);
    return promiseResolvedWith(undefined);
  }

  function cancel1Algorithm(reason) {
    canceled1 = true;
    reason1 = reason;
    if(canceled2) {
      const compositeReason = CreateArrayFromList([reason1, reason2]);
      const cancelResult = ReadableStreamCancel(stream, compositeReason);
      resolveCancelPromise(cancelResult);
    }
    return cancelPromise;
  }

  function cancel2Algorithm(reason) {
    canceled2 = true;
    reason2 = reason;
    if(canceled1) {
      const compositeReason = CreateArrayFromList([reason1, reason2]);
      const cancelResult = ReadableStreamCancel(stream, compositeReason);
      resolveCancelPromise(cancelResult);
    }
    return cancelPromise;
  }

  function startAlgorithm() {}
  branch1 = CreateReadableStream(startAlgorithm, pullAlgorithm, cancel1Algorithm);
  branch2 = CreateReadableStream(startAlgorithm, pullAlgorithm, cancel2Algorithm);
  uponRejection(reader._closedPromise, r => {
    ReadableStreamDefaultControllerError(branch1._readableStreamController, r);
    ReadableStreamDefaultControllerError(branch2._readableStreamController, r);
    if(!canceled1 || !canceled2) resolveCancelPromise(undefined);
    return null;
  });
  return [branch1, branch2];
}

function ReadableByteStreamTee(stream) {
  assert(IsReadableStream(stream));
  assert(IsReadableByteStreamController(stream._readableStreamController));
  let reader = AcquireReadableStreamDefaultReader(stream);
  let reading = false;
  let readAgainForBranch1 = false;
  let readAgainForBranch2 = false;
  let canceled1 = false;
  let canceled2 = false;
  let reason1;
  let reason2;
  let branch1;
  let branch2;
  let resolveCancelPromise;
  const cancelPromise = newPromise(resolve => (resolveCancelPromise = resolve));
  function forwardReaderError(thisReader) {
    uponRejection(thisReader._closedPromise, r => {
      if(thisReader !== reader) return null;
      ReadableByteStreamControllerError(branch1._readableStreamController, r);
      ReadableByteStreamControllerError(branch2._readableStreamController, r);
      if(!canceled1 || !canceled2) resolveCancelPromise(undefined);
      return null;
    });
  }

  function pullWithDefaultReader() {
    if(IsReadableStreamBYOBReader(reader)) {
      assert(reader._readIntoRequests.length === 0);
      ReadableStreamReaderGenericRelease(reader);
      reader = AcquireReadableStreamDefaultReader(stream);
      forwardReaderError(reader);
    }
    const readRequest = {
      _chunkSteps: chunk => {
        _queueMicrotask(() => {
          readAgainForBranch1 = false;
          readAgainForBranch2 = false;
          const chunk1 = chunk;
          let chunk2 = chunk;
          if(!canceled1 && !canceled2) {
            try {
              chunk2 = CloneAsUint8Array(chunk);
            } catch(cloneE) {
              ReadableByteStreamControllerError(branch1._readableStreamController, cloneE);
              ReadableByteStreamControllerError(branch2._readableStreamController, cloneE);
              resolveCancelPromise(ReadableStreamCancel(stream, cloneE));
              return;
            }
          }
          if(!canceled1) ReadableByteStreamControllerEnqueue(branch1._readableStreamController, chunk1);
          if(!canceled2) ReadableByteStreamControllerEnqueue(branch2._readableStreamController, chunk2);
          reading = false;
          if(readAgainForBranch1) pull1Algorithm();
          else if(readAgainForBranch2) pull2Algorithm();
        });
      },
      _closeSteps: () => {
        reading = false;
        if(!canceled1) ReadableByteStreamControllerClose(branch1._readableStreamController);
        if(!canceled2) ReadableByteStreamControllerClose(branch2._readableStreamController);
        if(branch1._readableStreamController._pendingPullIntos.length > 0) ReadableByteStreamControllerRespond(branch1._readableStreamController, 0);
        if(branch2._readableStreamController._pendingPullIntos.length > 0) ReadableByteStreamControllerRespond(branch2._readableStreamController, 0);
        if(!canceled1 || !canceled2) resolveCancelPromise(undefined);
      },
      _errorSteps: () => (reading = false),
    };
    ReadableStreamDefaultReaderRead(reader, readRequest);
  }

  function pullWithBYOBReader(view, forBranch2) {
    if(IsReadableStreamDefaultReader(reader)) {
      assert(reader._readRequests.length === 0);
      ReadableStreamReaderGenericRelease(reader);
      reader = AcquireReadableStreamBYOBReader(stream);
      forwardReaderError(reader);
    }
    const byobBranch = forBranch2 ? branch2 : branch1;
    const otherBranch = forBranch2 ? branch1 : branch2;
    const readIntoRequest = {
      _chunkSteps: chunk => {
        _queueMicrotask(() => {
          readAgainForBranch1 = false;
          readAgainForBranch2 = false;
          const byobCanceled = forBranch2 ? canceled2 : canceled1;
          const otherCanceled = forBranch2 ? canceled1 : canceled2;
          if(!otherCanceled) {
            let clonedChunk;
            try {
              clonedChunk = CloneAsUint8Array(chunk);
            } catch(cloneE) {
              ReadableByteStreamControllerError(byobBranch._readableStreamController, cloneE);
              ReadableByteStreamControllerError(otherBranch._readableStreamController, cloneE);
              resolveCancelPromise(ReadableStreamCancel(stream, cloneE));
              return;
            }
            if(!byobCanceled) ReadableByteStreamControllerRespondWithNewView(byobBranch._readableStreamController, chunk);
            ReadableByteStreamControllerEnqueue(otherBranch._readableStreamController, clonedChunk);
          } else if(!byobCanceled) {
            ReadableByteStreamControllerRespondWithNewView(byobBranch._readableStreamController, chunk);
          }
          reading = false;
          if(readAgainForBranch1) pull1Algorithm();
          else if(readAgainForBranch2) pull2Algorithm();
        });
      },
      _closeSteps: chunk => {
        reading = false;
        const byobCanceled = forBranch2 ? canceled2 : canceled1;
        const otherCanceled = forBranch2 ? canceled1 : canceled2;
        if(!byobCanceled) ReadableByteStreamControllerClose(byobBranch._readableStreamController);
        if(!otherCanceled) ReadableByteStreamControllerClose(otherBranch._readableStreamController);
        if(chunk !== undefined) {
          assert(chunk.byteLength === 0);
          if(!byobCanceled) ReadableByteStreamControllerRespondWithNewView(byobBranch._readableStreamController, chunk);
          if(!otherCanceled && otherBranch._readableStreamController._pendingPullIntos.length > 0) ReadableByteStreamControllerRespond(otherBranch._readableStreamController, 0);
        }
        if(!byobCanceled || !otherCanceled) resolveCancelPromise(undefined);
      },
      _errorSteps: () => (reading = false),
    };
    ReadableStreamBYOBReaderRead(reader, view, 1, readIntoRequest);
  }

  function pull1Algorithm() {
    if(reading) {
      readAgainForBranch1 = true;
      return promiseResolvedWith(undefined);
    }
    reading = true;
    const byobRequest = ReadableByteStreamControllerGetBYOBRequest(branch1._readableStreamController);
    if(byobRequest === null) pullWithDefaultReader();
    else pullWithBYOBReader(byobRequest._view, false);
    return promiseResolvedWith(undefined);
  }

  function pull2Algorithm() {
    if(reading) {
      readAgainForBranch2 = true;
      return promiseResolvedWith(undefined);
    }
    reading = true;
    const byobRequest = ReadableByteStreamControllerGetBYOBRequest(branch2._readableStreamController);
    if(byobRequest === null) pullWithDefaultReader();
    else pullWithBYOBReader(byobRequest._view, true);
    return promiseResolvedWith(undefined);
  }

  function cancel1Algorithm(reason) {
    canceled1 = true;
    reason1 = reason;
    if(canceled2) {
      const compositeReason = CreateArrayFromList([reason1, reason2]);
      const cancelResult = ReadableStreamCancel(stream, compositeReason);
      resolveCancelPromise(cancelResult);
    }
    return cancelPromise;
  }

  function cancel2Algorithm(reason) {
    canceled2 = true;
    reason2 = reason;
    if(canceled1) {
      const compositeReason = CreateArrayFromList([reason1, reason2]);
      const cancelResult = ReadableStreamCancel(stream, compositeReason);
      resolveCancelPromise(cancelResult);
    }
    return cancelPromise;
  }

  function startAlgorithm() {
    return;
  }

  branch1 = CreateReadableByteStream(startAlgorithm, pull1Algorithm, cancel1Algorithm);
  branch2 = CreateReadableByteStream(startAlgorithm, pull2Algorithm, cancel2Algorithm);
  forwardReaderError(reader);
  return [branch1, branch2];
}

// src/lib/validators/underlying-source.ts

function convertReadableStreamType(type, context) {
  type = `${type}`;
  if(type != 'bytes') throw new TypeError(`${context} '${type}' is not a valid enumeration value for ReadableStreamType`);
  return type;
}

// src/lib/validators/pipe-options.ts
function convertPipeOptions(options, context) {
  assertDictionary(options, context);
  const preventAbort = options == null ? undefined : options.preventAbort;
  const preventCancel = options == null ? undefined : options.preventCancel;
  const preventClose = options == null ? undefined : options.preventClose;
  const signal = options == null ? undefined : options.signal;
  if(signal !== undefined) assertAbortSignal(signal, `${context} has member 'signal' that`);
  return {
    preventAbort: Boolean(preventAbort),
    preventCancel: Boolean(preventCancel),
    preventClose: Boolean(preventClose),
    signal,
  };
}

function assertAbortSignal(signal, context) {
  if(!isAbortSignal(signal)) throw new TypeError(`${context} is not an AbortSignal.`);
}

// src/lib/readable-stream.ts
export class ReadableStream {
  constructor(rawUnderlyingSource = {}, rawStrategy = {}) {
    if(rawUnderlyingSource === undefined) rawUnderlyingSource = null;
    else assertObject(rawUnderlyingSource, 'First parameter');
    const strategy = convertQueuingStrategy(rawStrategy, 'Second parameter');
    const underlyingSource = ((source, context) => {
      assertDictionary(source, context);
      const original = source;
      const autoAllocateChunkSize = original == null ? undefined : original.autoAllocateChunkSize;
      const cancel = original == null ? undefined : original.cancel;
      const pull = original == null ? undefined : original.pull;
      const start = original == null ? undefined : original.start;
      const type = original == null ? undefined : original.type;
      return {
        autoAllocateChunkSize: autoAllocateChunkSize === undefined ? undefined : convertUnsignedLongLongWithEnforceRange(autoAllocateChunkSize, `${context} has member 'autoAllocateChunkSize' that`),
        cancel: cancel === undefined ? undefined : (assertFunction(cancel, `${context} has member 'cancel' that`), reason => promiseCall(cancel, original, [reason])),
        pull: pull === undefined ? undefined : (assertFunction(pull, `${context} has member 'pull' that`), controller => promiseCall(pull, original, [controller])),
        start: start === undefined ? undefined : (assertFunction(start, `${context} has member 'start' that`), controller => reflectCall(start, original, [controller])),
        type: type === undefined ? undefined : convertReadableStreamType(type, `${context} has member 'type' that`),
      };
    })(rawUnderlyingSource, 'First parameter');

    InitializeReadableStream(this);

    if(underlyingSource.type == 'bytes') {
      if(strategy.size !== undefined) throw new RangeError('The strategy for a byte stream cannot have a size function');
      const highWaterMark = ExtractHighWaterMark(strategy, 0);

      const controller = Object.create(ReadableByteStreamController.prototype);
      const startAlgorithm = underlyingSource.start !== undefined ? () => underlyingSource.start(controller) : () => undefined;
      const pullAlgorithm = underlyingSource.pull !== undefined ? () => underlyingSource.pull(controller) : () => promiseResolvedWith(undefined);
      const cancelAlgorithm = underlyingSource.cancel !== undefined ? reason => underlyingSource.cancel(reason) : () => promiseResolvedWith(undefined);
      const autoAllocateChunkSize = underlyingSource.autoAllocateChunkSize;
      if(autoAllocateChunkSize === 0) throw new TypeError('autoAllocateChunkSize must be greater than 0');
      SetUpReadableByteStreamController(this, controller, startAlgorithm, pullAlgorithm, cancelAlgorithm, highWaterMark, autoAllocateChunkSize);
    } else {
      assert(underlyingSource.type === undefined);
      const sizeAlgorithm = ExtractSizeAlgorithm(strategy);
      const highWaterMark = ExtractHighWaterMark(strategy, 1);

      const controller = Object.create(ReadableStreamDefaultController.prototype);
      const startAlgorithm = underlyingSource.start !== undefined ? () => underlyingSource.start(controller) : () => undefined;
      const pullAlgorithm = underlyingSource.pull !== undefined ? () => underlyingSource.pull(controller) : () => promiseResolvedWith(undefined);
      const cancelAlgorithm = underlyingSource.cancel !== undefined ? reason => underlyingSource.cancel(reason) : () => promiseResolvedWith(undefined);
      SetUpReadableStreamDefaultController(this, controller, startAlgorithm, pullAlgorithm, cancelAlgorithm, highWaterMark, sizeAlgorithm);
    }
  }

  /**
   * Whether or not the readable stream is locked to a {@link ReadableStreamDefaultReader | reader}.
   */
  get locked() {
    if(!IsReadableStream(this)) throw streamBrandCheckException2('locked');
    return IsReadableStreamLocked(this);
  }

  /**
   * Cancels the stream, signaling a loss of interest in the stream by a consumer.
   *
   * The supplied `reason` argument will be given to the underlying source's {@link UnderlyingSource.cancel | cancel()}
   * method, which might or might not use it.
   */
  cancel(reason = undefined) {
    if(!IsReadableStream(this)) return promiseRejectedWith(streamBrandCheckException2('cancel'));
    if(IsReadableStreamLocked(this)) return promiseRejectedWith(new TypeError('Cannot cancel a stream that already has a reader'));
    return ReadableStreamCancel(this, reason);
  }

  getReader(rawOptions = undefined) {
    if(!IsReadableStream(this)) throw streamBrandCheckException2('getReader');
    const options = ((options, context) => {
      assertDictionary(options, context);
      const mode = options == null ? undefined : options.mode;
      return {
        mode: mode === undefined ? undefined : convertReadableStreamReaderMode(mode, `${context} has member 'mode' that`),
      };
    })(rawOptions, 'First parameter');
    if(options.mode === undefined) return AcquireReadableStreamDefaultReader(this);
    assert(options.mode == 'byob');
    return AcquireReadableStreamBYOBReader(this);
  }

  pipeThrough(rawTransform, rawOptions = {}) {
    if(!IsReadableStream(this)) throw streamBrandCheckException2('pipeThrough');
    assertRequiredArgument(rawTransform, 1, 'pipeThrough');
    const transform = ((pair, context) => {
      assertDictionary(pair, context);
      const readable = pair == null ? undefined : pair.readable;
      assertRequiredField(readable, 'readable', 'ReadableWritablePair');
      assertReadableStream(readable, `${context} has member 'readable' that`);
      const writable = pair == null ? undefined : pair.writable;
      assertRequiredField(writable, 'writable', 'ReadableWritablePair');
      assertWritableStream(writable, `${context} has member 'writable' that`);
      return { readable, writable };
    })(rawTransform, 'First parameter');
    const options = convertPipeOptions(rawOptions, 'Second parameter');
    if(IsReadableStreamLocked(this)) throw new TypeError('ReadableStream.prototype.pipeThrough cannot be used on a locked ReadableStream');
    if(IsWritableStreamLocked(transform.writable)) throw new TypeError('ReadableStream.prototype.pipeThrough cannot be used on a locked WritableStream');
    const promise = ReadableStreamPipeTo(this, transform.writable, options.preventClose, options.preventAbort, options.preventCancel, options.signal);
    setPromiseIsHandledToTrue(promise);
    return transform.readable;
  }

  pipeTo(destination, rawOptions = {}) {
    if(!IsReadableStream(this)) return promiseRejectedWith(streamBrandCheckException2('pipeTo'));
    if(destination === undefined) return promiseRejectedWith(`Parameter 1 is required in 'pipeTo'.`);
    if(!IsWritableStream(destination)) return promiseRejectedWith(new TypeError(`ReadableStream.prototype.pipeTo's first argument must be a WritableStream`));
    let options;
    try {
      options = convertPipeOptions(rawOptions, 'Second parameter');
    } catch(e) {
      return promiseRejectedWith(e);
    }
    if(IsReadableStreamLocked(this)) return promiseRejectedWith(new TypeError('ReadableStream.prototype.pipeTo cannot be used on a locked ReadableStream'));
    if(IsWritableStreamLocked(destination)) return promiseRejectedWith(new TypeError('ReadableStream.prototype.pipeTo cannot be used on a locked WritableStream'));
    return ReadableStreamPipeTo(this, destination, options.preventClose, options.preventAbort, options.preventCancel, options.signal);
  }

  /**
   * Tees this readable stream, returning a two-element array containing the two resulting branches as
   * new {@link ReadableStream} instances.
   *
   * Teeing a stream will lock it, preventing any other consumer from acquiring a reader.
   * To cancel the stream, cancel both of the resulting branches; a composite cancellation reason will then be
   * propagated to the stream's underlying source.
   *
   * Note that the chunks seen in each branch will be the same object. If the chunks are not immutable,
   * this could allow interference between the two branches.
   */
  tee() {
    if(!IsReadableStream(this)) throw streamBrandCheckException2('tee');

    let branches;

    if(IsReadableByteStreamController(this._readableStreamController)) {
      branches = ReadableByteStreamTee(this);
    } else {
      branches = ReadableStreamDefaultTee(this, false);
    }

    return CreateArrayFromList(branches);
  }

  values(rawOptions = undefined) {
    if(!IsReadableStream(this)) throw streamBrandCheckException2('values');
    const options = ((options, context) => {
      assertDictionary(options, context);
      const preventCancel = options == null ? undefined : options.preventCancel;
      return { preventCancel: Boolean(preventCancel) };
    })(rawOptions, 'First parameter');

    const reader = AcquireReadableStreamDefaultReader(this);
    const impl = new ReadableStreamAsyncIteratorImpl(reader, options.preventCancel);
    const iterator = Object.create(ReadableStreamAsyncIteratorPrototype);
    iterator._asyncIteratorImpl = impl;
    return iterator;
  }

  [Symbol.asyncIterator](options) {
    return this.values(options);
  }

  /**
   * Creates a new ReadableStream wrapping the provided iterable or async iterable.
   *
   * This can be used to adapt various kinds of objects into a readable stream,
   * such as an array, an async generator, or a Node.js readable stream.
   */
  static from(asyncIterable) {
    if(typeIsObject(asyncIterable) && typeof asyncIterable.getReader != 'undefined') {
      const reader = asyncIterable.getReader();

      let stream;
      const startAlgorithm = noop;
      function pullAlgorithm() {
        let readPromise;
        try {
          readPromise = reader.read();
        } catch(e) {
          return promiseRejectedWith(e);
        }
        return transformPromiseWith(readPromise, readResult => {
          if(!typeIsObject(readResult)) throw new TypeError('The promise returned by the reader.read() method must fulfill with an object');
          if(readResult.done) {
            ReadableStreamDefaultControllerClose(stream._readableStreamController);
          } else {
            const value = readResult.value;
            ReadableStreamDefaultControllerEnqueue(stream._readableStreamController, value);
          }
        });
      }

      function cancelAlgorithm(reason) {
        try {
          return promiseResolvedWith(reader.cancel(reason));
        } catch(e) {
          return promiseRejectedWith(e);
        }
      }

      stream = CreateReadableStream(startAlgorithm, pullAlgorithm, cancelAlgorithm, 0);
      return stream;
    }

    let stream;
    const iteratorRecord = GetIterator(asyncIterable, 'async');
    const startAlgorithm = noop;

    function pullAlgorithm() {
      let nextResult;
      try {
        nextResult = IteratorNext(iteratorRecord);
      } catch(e) {
        return promiseRejectedWith(e);
      }
      const nextPromise = promiseResolvedWith(nextResult);
      return transformPromiseWith(nextPromise, iterResult => {
        if(!typeIsObject(iterResult)) throw new TypeError('The promise returned by the iterator.next() method must fulfill with an object');
        const done = iterResult.done;
        if(done) {
          ReadableStreamDefaultControllerClose(stream._readableStreamController);
        } else {
          const value = iterResult.value;
          ReadableStreamDefaultControllerEnqueue(stream._readableStreamController, value);
        }
      });
    }

    function cancelAlgorithm(reason) {
      const iterator = iteratorRecord.iterator;
      let returnMethod;
      try {
        returnMethod = GetMethod(iterator, 'return');
      } catch(e) {
        return promiseRejectedWith(e);
      }
      if(returnMethod === undefined) return promiseResolvedWith(undefined);
      const returnPromise = promiseCall(returnMethod, iterator, [reason]);
      return transformPromiseWith(returnPromise, iterResult => {
        if(!typeIsObject(iterResult)) throw new TypeError('The promise returned by the iterator.return() method must fulfill with an object');
        return undefined;
      });
    }

    stream = CreateReadableStream(startAlgorithm, pullAlgorithm, cancelAlgorithm, 0);
    return stream;
  }
}

/*Object.defineProperties(ReadableStream, {
  from: { enumerable: true },
});*/

/*Object.defineProperties(ReadableStream.prototype, {
  cancel: { enumerable: true },
  getReader: { enumerable: true },
  pipeThrough: { enumerable: true },
  pipeTo: { enumerable: true },
  tee: { enumerable: true },
  values: { enumerable: true },
  locked: { enumerable: true },
});*/

setFunctionName(ReadableStream.from, 'from');
setFunctionName(ReadableStream.prototype.cancel, 'cancel');
setFunctionName(ReadableStream.prototype.getReader, 'getReader');
setFunctionName(ReadableStream.prototype.pipeThrough, 'pipeThrough');
setFunctionName(ReadableStream.prototype.pipeTo, 'pipeTo');
setFunctionName(ReadableStream.prototype.tee, 'tee');
setFunctionName(ReadableStream.prototype.values, 'values');

declare(ReadableStream.prototype, { [Symbol.toStringTag]: 'ReadableStream' });

Object.defineProperty(ReadableStream.prototype, Symbol.asyncIterator, {
  value: ReadableStream.prototype.values,
  writable: true,
  configurable: true,
});

function CreateReadableStream(startAlgorithm, pullAlgorithm, cancelAlgorithm, highWaterMark = 1, sizeAlgorithm = () => 1) {
  assert(IsNonNegativeNumber(highWaterMark));
  const stream = Object.create(ReadableStream.prototype);
  InitializeReadableStream(stream);
  const controller = Object.create(ReadableStreamDefaultController.prototype);
  SetUpReadableStreamDefaultController(stream, controller, startAlgorithm, pullAlgorithm, cancelAlgorithm, highWaterMark, sizeAlgorithm);
  return stream;
}

function CreateReadableByteStream(startAlgorithm, pullAlgorithm, cancelAlgorithm) {
  const stream = Object.create(ReadableStream.prototype);
  InitializeReadableStream(stream);
  const controller = Object.create(ReadableByteStreamController.prototype);
  SetUpReadableByteStreamController(stream, controller, startAlgorithm, pullAlgorithm, cancelAlgorithm, 0, undefined);
  return stream;
}

function InitializeReadableStream(stream) {
  declare(stream, { _state: 'readable', _reader: undefined, _storedError: undefined, _disturbed: false });
}

function IsReadableStream(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_readableStreamController')) return false;
  return x instanceof ReadableStream;
}

function IsReadableStreamLocked(stream) {
  assert(IsReadableStream(stream));
  if(stream._reader === undefined) return false;
  return true;
}

function ReadableStreamCancel(stream, reason) {
  stream._disturbed = true;
  if(stream._state == 'closed') return promiseResolvedWith(undefined);
  if(stream._state == 'errored') return promiseRejectedWith(stream._storedError);
  ReadableStreamClose(stream);
  const reader = stream._reader;
  if(reader !== undefined && IsReadableStreamBYOBReader(reader)) {
    const readIntoRequests = reader._readIntoRequests;
    reader._readIntoRequests = new SimpleQueue();
    readIntoRequests.forEach(readIntoRequest => readIntoRequest._closeSteps(undefined));
  }

  const sourceCancelPromise = stream._readableStreamController[CancelSteps](reason);
  return transformPromiseWith(sourceCancelPromise, noop);
}

function ReadableStreamClose(stream) {
  assert(stream._state == 'readable');
  stream._state = 'closed';
  const reader = stream._reader;
  if(reader === undefined) return;
  defaultReaderClosedPromiseResolve(reader);
  if(IsReadableStreamDefaultReader(reader)) {
    const readRequests = reader._readRequests;
    reader._readRequests = new SimpleQueue();
    readRequests.forEach(readRequest => readRequest._closeSteps());
  }
}

function ReadableStreamError(stream, e) {
  assert(IsReadableStream(stream));
  assert(stream._state == 'readable');
  stream._state = 'errored';
  stream._storedError = e;
  const reader = stream._reader;
  if(reader === undefined) return;
  defaultReaderClosedPromiseReject(reader, e);
  if(IsReadableStreamDefaultReader(reader)) {
    ReadableStreamDefaultReaderErrorReadRequests(reader, e);
  } else {
    assert(IsReadableStreamBYOBReader(reader));
    ReadableStreamBYOBReaderErrorReadIntoRequests(reader, e);
  }
}

function streamBrandCheckException2(name) {
  return new TypeError(`ReadableStream.prototype.${name} can only be used on a ReadableStream`);
}

// src/lib/validators/queuing-strategy-init.ts
function convertQueuingStrategyInit(init, context) {
  assertDictionary(init, context);
  const highWaterMark = init == null ? undefined : init.highWaterMark;
  assertRequiredField(highWaterMark, 'highWaterMark', 'QueuingStrategyInit');
  return {
    highWaterMark: convertUnrestrictedDouble(highWaterMark),
  };
}

// src/lib/byte-length-queuing-strategy.ts
const byteLengthSizeFunction = chunk => {
  return chunk.byteLength;
};

setFunctionName(byteLengthSizeFunction, 'size');
export class ByteLengthQueuingStrategy {
  constructor(options) {
    assertRequiredArgument(options, 1, 'ByteLengthQueuingStrategy');
    options = convertQueuingStrategyInit(options, 'First parameter');
    declare(this, { _byteLengthQueuingStrategyHighWaterMark: options.highWaterMark });
  }

  /**
   * Returns the high water mark provided to the constructor.
   */
  get highWaterMark() {
    if(!IsByteLengthQueuingStrategy(this)) throw byteLengthBrandCheckException('highWaterMark');
    return this._byteLengthQueuingStrategyHighWaterMark;
  }

  /**
   * Measures the size of `chunk` by returning the value of its `byteLength` property.
   */
  get size() {
    if(!IsByteLengthQueuingStrategy(this)) throw byteLengthBrandCheckException('size');
    return byteLengthSizeFunction;
  }
}

/*Object.defineProperties(ByteLengthQueuingStrategy.prototype, {
  highWaterMark: { enumerable: true },
  size: { enumerable: true },
});*/

declare(ByteLengthQueuingStrategy.prototype, { [Symbol.toStringTag]: 'ByteLengthQueuingStrategy' });

function byteLengthBrandCheckException(name) {
  return new TypeError(`ByteLengthQueuingStrategy.prototype.${name} can only be used on a ByteLengthQueuingStrategy`);
}

function IsByteLengthQueuingStrategy(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_byteLengthQueuingStrategyHighWaterMark')) return false;
  return x instanceof ByteLengthQueuingStrategy;
}

// src/lib/count-queuing-strategy.ts
const countSizeFunction = () => 1;
setFunctionName(countSizeFunction, 'size');

export class CountQueuingStrategy {
  constructor(options) {
    assertRequiredArgument(options, 1, 'CountQueuingStrategy');
    options = convertQueuingStrategyInit(options, 'First parameter');
    declare(this, { _countQueuingStrategyHighWaterMark: options.highWaterMark });
  }

  /**
   * Returns the high water mark provided to the constructor.
   */
  get highWaterMark() {
    if(!IsCountQueuingStrategy(this)) throw countBrandCheckException('highWaterMark');
    return this._countQueuingStrategyHighWaterMark;
  }

  /**
   * Measures the size of `chunk` by always returning 1.
   * This ensures that the total queue size is a count of the number of chunks in the queue.
   */
  get size() {
    if(!IsCountQueuingStrategy(this)) throw countBrandCheckException('size');
    return countSizeFunction;
  }
}

/*Object.defineProperties(CountQueuingStrategy.prototype, {
  highWaterMark: { enumerable: true },
  size: { enumerable: true },
});*/

declare(CountQueuingStrategy.prototype, { [Symbol.toStringTag]: 'CountQueuingStrategy' });

function countBrandCheckException(name) {
  return new TypeError(`CountQueuingStrategy.prototype.${name} can only be used on a CountQueuingStrategy`);
}

function IsCountQueuingStrategy(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_countQueuingStrategyHighWaterMark')) return false;
  return x instanceof CountQueuingStrategy;
}

// src/lib/transform-stream.ts
export class TransformStream {
  constructor(rawTransformer = {}, rawWritableStrategy = {}, rawReadableStrategy = {}) {
    if(rawTransformer === undefined) rawTransformer = null;
    const writableStrategy = convertQueuingStrategy(rawWritableStrategy, 'Second parameter');
    const readableStrategy = convertQueuingStrategy(rawReadableStrategy, 'Third parameter');
    const transformer = ((original, context) => {
      assertDictionary(original, context);
      const cancel = original == null ? undefined : original.cancel;
      const flush = original == null ? undefined : original.flush;
      const readableType = original == null ? undefined : original.readableType;
      const start = original == null ? undefined : original.start;
      const transform = original == null ? undefined : original.transform;
      const writableType = original == null ? undefined : original.writableType;
      return {
        cancel: cancel === undefined ? undefined : (assertFunction(cancel, `${context} has member 'cancel' that`), reason => promiseCall(cancel, original, [reason])),
        flush: flush === undefined ? undefined : (assertFunction(flush, `${context} has member 'flush' that`), controller => promiseCall(flush, original, [controller])),
        readableType,
        start: start === undefined ? undefined : (assertFunction(start, `${context} has member 'start' that`), controller => reflectCall(start, original, [controller])),
        transform:
          transform === undefined ? undefined : (assertFunction(transform, `${context} has member 'transform' that`), (chunk, controller) => promiseCall(transform, original, [chunk, controller])),
        writableType,
      };
    })(rawTransformer, 'First parameter');
    if(transformer.readableType !== undefined) throw new RangeError('Invalid readableType specified');
    if(transformer.writableType !== undefined) throw new RangeError('Invalid writableType specified');
    const readableHighWaterMark = ExtractHighWaterMark(readableStrategy, 0);
    const readableSizeAlgorithm = ExtractSizeAlgorithm(readableStrategy);
    const writableHighWaterMark = ExtractHighWaterMark(writableStrategy, 1);
    const writableSizeAlgorithm = ExtractSizeAlgorithm(writableStrategy);
    let startPromise_resolve;
    const startPromise = newPromise(resolve => (startPromise_resolve = resolve));

    {
      const stream = this;

      function startAlgorithm() {
        return startPromise;
      }

      function writeAlgorithm(chunk) {
        assert(stream._writable._state == 'writable');
        const controller = stream._transformStreamController;
        if(stream._backpressure) {
          const backpressureChangePromise = stream._backpressureChangePromise;
          assert(backpressureChangePromise !== undefined);
          return transformPromiseWith(backpressureChangePromise, () => {
            const writable = stream._writable;
            const state = writable._state;
            if(state == 'erroring') throw writable._storedError;
            assert(state == 'writable');
            return TransformStreamDefaultControllerPerformTransform(controller, chunk);
          });
        }
        return TransformStreamDefaultControllerPerformTransform(controller, chunk);
      }

      function abortAlgorithm(reason) {
        const controller = stream._transformStreamController;
        if(controller._finishPromise !== undefined) return controller._finishPromise;
        const readable = stream._readable;
        controller._finishPromise = newPromise((resolve, reject) => {
          controller._finishPromise_resolve = resolve;
          controller._finishPromise_reject = reject;
        });
        const cancelPromise = controller._cancelAlgorithm(reason);
        TransformStreamDefaultControllerClearAlgorithms(controller);
        uponPromise(
          cancelPromise,
          () => {
            if(readable._state == 'errored') {
              defaultControllerFinishPromiseReject(controller, readable._storedError);
            } else {
              ReadableStreamDefaultControllerError(readable._readableStreamController, reason);
              defaultControllerFinishPromiseResolve(controller);
            }
            return null;
          },
          r => {
            ReadableStreamDefaultControllerError(readable._readableStreamController, r);
            defaultControllerFinishPromiseReject(controller, r);
            return null;
          },
        );
        return controller._finishPromise;
      }

      function closeAlgorithm() {
        const controller = stream._transformStreamController;
        if(controller._finishPromise !== undefined) return controller._finishPromise;
        const readable = stream._readable;
        controller._finishPromise = newPromise((resolve, reject) => {
          controller._finishPromise_resolve = resolve;
          controller._finishPromise_reject = reject;
        });
        const flushPromise = controller._flushAlgorithm();
        TransformStreamDefaultControllerClearAlgorithms(controller);
        uponPromise(
          flushPromise,
          () => {
            if(readable._state == 'errored') {
              defaultControllerFinishPromiseReject(controller, readable._storedError);
            } else {
              ReadableStreamDefaultControllerClose(readable._readableStreamController);
              defaultControllerFinishPromiseResolve(controller);
            }
            return null;
          },
          r => {
            ReadableStreamDefaultControllerError(readable._readableStreamController, r);
            defaultControllerFinishPromiseReject(controller, r);
            return null;
          },
        );
        return controller._finishPromise;
      }

      this._writable = CreateWritableStream(startAlgorithm, writeAlgorithm, closeAlgorithm, abortAlgorithm, writableHighWaterMark, writableSizeAlgorithm);

      function pullAlgorithm() {
        assert(stream._backpressure);
        assert(stream._backpressureChangePromise !== undefined);
        TransformStreamSetBackpressure(stream, false);
        return stream._backpressureChangePromise;
      }

      function cancelAlgorithm(reason) {
        const controller = stream._transformStreamController;
        if(controller._finishPromise !== undefined) return controller._finishPromise;
        const writable = stream._writable;
        controller._finishPromise = newPromise((resolve, reject) => {
          controller._finishPromise_resolve = resolve;
          controller._finishPromise_reject = reject;
        });
        const cancelPromise = controller._cancelAlgorithm(reason);
        TransformStreamDefaultControllerClearAlgorithms(controller);
        uponPromise(
          cancelPromise,
          () => {
            if(writable._state == 'errored') {
              defaultControllerFinishPromiseReject(controller, writable._storedError);
            } else {
              WritableStreamDefaultControllerErrorIfNeeded(writable._writableStreamController, reason);
              TransformStreamUnblockWrite(stream);
              defaultControllerFinishPromiseResolve(controller);
            }
            return null;
          },
          r => {
            WritableStreamDefaultControllerErrorIfNeeded(writable._writableStreamController, r);
            TransformStreamUnblockWrite(stream);
            defaultControllerFinishPromiseReject(controller, r);
            return null;
          },
        );
        return controller._finishPromise;
      }
      declare(this, {
        _readable: CreateReadableStream(startAlgorithm, pullAlgorithm, cancelAlgorithm, readableHighWaterMark, readableSizeAlgorithm),
        _backpressure: undefined,
        _backpressureChangePromise: undefined,
        _backpressureChangePromise_resolve: undefined,
      });
      TransformStreamSetBackpressure(this, true);
      declare(this, { _transformStreamController: undefined });
    }

    const controller = Object.create(TransformStreamDefaultController.prototype);

    const transformAlgorithm =
      transformer.transform !== undefined
        ? chunk => transformer.transform(chunk, controller)
        : chunk => {
            try {
              TransformStreamDefaultControllerEnqueue(controller, chunk);
              return promiseResolvedWith(undefined);
            } catch(transformResultE) {
              return promiseRejectedWith(transformResultE);
            }
          };

    const flushAlgorithm = transformer.flush !== undefined ? () => transformer.flush(controller) : () => promiseResolvedWith(undefined);
    const cancelAlgorithm = transformer.cancel !== undefined ? reason => transformer.cancel(reason) : () => promiseResolvedWith(undefined);

    SetUpTransformStreamDefaultController(this, controller, transformAlgorithm, flushAlgorithm, cancelAlgorithm);

    startPromise_resolve(transformer.start !== undefined ? transformer.start(this._transformStreamController) : undefined);
  }
  /**
   * The readable side of the transform stream.
   */
  get readable() {
    if(!IsTransformStream(this)) throw streamBrandCheckException3('readable');
    return this._readable;
  }

  /**
   * The writable side of the transform stream.
   */
  get writable() {
    if(!IsTransformStream(this)) throw streamBrandCheckException3('writable');
    return this._writable;
  }
}

/*Object.defineProperties(TransformStream.prototype, {
  readable: { enumerable: true },
  writable: { enumerable: true },
});*/

declare(TransformStream.prototype, { [Symbol.toStringTag]: 'TransformStream' });

function IsTransformStream(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_transformStreamController')) return false;
  return x instanceof TransformStream;
}

function TransformStreamError(stream, e) {
  ReadableStreamDefaultControllerError(stream._readable._readableStreamController, e);
  TransformStreamErrorWritableAndUnblockWrite(stream, e);
}

function TransformStreamErrorWritableAndUnblockWrite(stream, e) {
  TransformStreamDefaultControllerClearAlgorithms(stream._transformStreamController);
  WritableStreamDefaultControllerErrorIfNeeded(stream._writable._writableStreamController, e);
  TransformStreamUnblockWrite(stream);
}

function TransformStreamUnblockWrite(stream) {
  if(stream._backpressure) TransformStreamSetBackpressure(stream, false);
}

function TransformStreamSetBackpressure(stream, backpressure) {
  assert(stream._backpressure !== backpressure);
  if(stream._backpressureChangePromise !== undefined) stream._backpressureChangePromise_resolve();
  stream._backpressureChangePromise = newPromise(resolve => (stream._backpressureChangePromise_resolve = resolve));
  stream._backpressure = backpressure;
}

export class TransformStreamDefaultController {
  constructor() {
    throw new TypeError('Illegal constructor');
  }

  /**
   * Returns the desired size to fill the readable side’s internal queue. It can be negative, if the queue is over-full.
   */
  get desiredSize() {
    if(!IsTransformStreamDefaultController(this)) throw defaultControllerBrandCheckException3('desiredSize');
    const readableController = this._controlledTransformStream._readable._readableStreamController;
    return ReadableStreamDefaultControllerGetDesiredSize(readableController);
  }

  enqueue(chunk = undefined) {
    if(!IsTransformStreamDefaultController(this)) throw defaultControllerBrandCheckException3('enqueue');
    TransformStreamDefaultControllerEnqueue(this, chunk);
  }

  /**
   * Errors both the readable side and the writable side of the controlled transform stream, making all future
   * interactions with it fail with the given error `e`. Any chunks queued for transformation will be discarded.
   */
  error(reason = undefined) {
    if(!IsTransformStreamDefaultController(this)) throw defaultControllerBrandCheckException3('error');

    TransformStreamError(this._controlledTransformStream, reason);
  }

  /**
   * Closes the readable side and errors the writable side of the controlled transform stream. This is useful when the
   * transformer only needs to consume a portion of the chunks written to the writable side.
   */
  terminate() {
    if(!IsTransformStreamDefaultController(this)) throw defaultControllerBrandCheckException3('terminate');

    const stream = this._controlledTransformStream;
    const readableController = stream._readable._readableStreamController;
    ReadableStreamDefaultControllerClose(readableController);
    const error = new TypeError('TransformStream terminated');
    TransformStreamErrorWritableAndUnblockWrite(stream, error);
  }
}

/*Object.defineProperties(TransformStreamDefaultController.prototype, {
  enqueue: { enumerable: true },
  error: { enumerable: true },
  terminate: { enumerable: true },
  desiredSize: { enumerable: true },
});*/

setFunctionName(TransformStreamDefaultController.prototype.enqueue, 'enqueue');
setFunctionName(TransformStreamDefaultController.prototype.error, 'error');
setFunctionName(TransformStreamDefaultController.prototype.terminate, 'terminate');

declare(TransformStreamDefaultController.prototype, { [Symbol.toStringTag]: 'TransformStreamDefaultController' });

function IsTransformStreamDefaultController(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_controlledTransformStream')) return false;
  return x instanceof TransformStreamDefaultController;
}

function TransformStreamDefaultControllerClearAlgorithms(controller) {
  controller._transformAlgorithm = undefined;
  controller._flushAlgorithm = undefined;
  controller._cancelAlgorithm = undefined;
}

function TransformStreamDefaultControllerEnqueue(controller, chunk) {
  const stream = controller._controlledTransformStream;
  const readableController = stream._readable._readableStreamController;
  if(!ReadableStreamDefaultControllerCanCloseOrEnqueue(readableController)) throw new TypeError('Readable side is not in a state that permits enqueue');
  try {
    ReadableStreamDefaultControllerEnqueue(readableController, chunk);
  } catch(e) {
    TransformStreamErrorWritableAndUnblockWrite(stream, e);
    throw stream._readable._storedError;
  }

  const backpressure = ReadableStreamDefaultControllerHasBackpressure(readableController);
  if(backpressure !== stream._backpressure) {
    assert(backpressure);
    TransformStreamSetBackpressure(stream, true);
  }
}

function TransformStreamDefaultControllerPerformTransform(controller, chunk) {
  const transformPromise = controller._transformAlgorithm(chunk);
  return transformPromiseWith(transformPromise, undefined, r => {
    TransformStreamError(controller._controlledTransformStream, r);
    throw r;
  });
}

function defaultControllerBrandCheckException3(name) {
  return new TypeError(`TransformStreamDefaultController.prototype.${name} can only be used on a TransformStreamDefaultController`);
}

function defaultControllerFinishPromiseResolve(controller) {
  if(controller._finishPromise_resolve === undefined) return;
  controller._finishPromise_resolve();
  controller._finishPromise_resolve = undefined;
  controller._finishPromise_reject = undefined;
}

function defaultControllerFinishPromiseReject(controller, reason) {
  if(controller._finishPromise_reject === undefined) return;
  setPromiseIsHandledToTrue(controller._finishPromise);
  controller._finishPromise_reject(reason);
  controller._finishPromise_resolve = undefined;
  controller._finishPromise_reject = undefined;
}

function streamBrandCheckException3(name) {
  return new TypeError(`TransformStream.prototype.${name} can only be used on a TransformStream`);
}
