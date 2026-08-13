import { AbortController, AbortSignal } from './abort.js';
import { assert_default as assert, noop } from './assert.js';
import { SimpleQueue } from './simple-queue.js';
import { assign, define, isPrototypeOf, setFunctionName, typeIsObject, weakMapper } from './util.js';

const CTRL = weakMapper(key => ({}));
const STRM = weakMapper(key => ({}));
const READ = weakMapper(key => ({}));
const WRITE = weakMapper(key => ({}));
const DEBUG = false;

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
  assign(READ(reader), { ownerReadableStream: stream });
  const S = STRM(stream);
  assign(S, { reader });

  if(S.state == 'readable') {
    defaultReaderClosedPromiseInitialize(reader);
  } else if(S.state == 'closed') {
    defaultReaderClosedPromiseInitialize(reader);
    defaultReaderClosedPromiseResolve(reader);
  } else {
    assert(S.state == 'errored');
    defaultReaderClosedPromiseInitializeAsRejected(reader, S.storedError);
  }
}

function ReadableStreamReaderGenericCancel(reader, reason) {
  const stream = READ(reader).ownerReadableStream;
  assert(stream !== undefined);
  return ReadableStreamCancel(stream, reason);
}

function ReadableStreamReaderGenericRelease(reader) {
  const R = READ(reader);
  const stream = R.ownerReadableStream;
  assert(stream !== undefined);
  const S = STRM(stream);
  assert(S.reader === reader);
  if(S.state == 'readable') {
    defaultReaderClosedPromiseReject(reader, new TypeError(`Reader was released and can no longer be used to monitor the stream's closedness`));
  } else {
    assert(R.closedPromise_resolve === undefined);
    assert(R.closedPromise_reject === undefined);
    defaultReaderClosedPromiseInitializeAsRejected(reader, new TypeError(`Reader was released and can no longer be used to monitor the stream's closedness`));
  }
  S.readableStreamController[ReleaseSteps]();
  S.reader = undefined;
  R.ownerReadableStream = undefined;
}

function readerLockException(name) {
  return new TypeError('Cannot ' + name + ' a stream using a released reader');
}

function defaultReaderClosedPromiseInitialize(reader) {
  assign(READ(reader), {
    closedPromise: newPromise((resolve, reject) => {
      assign(READ(reader), { closedPromise_resolve: resolve });
      assign(READ(reader), { closedPromise_reject: reject });
    }),
  });
}

function defaultReaderClosedPromiseInitializeAsRejected(reader, reason) {
  defaultReaderClosedPromiseInitialize(reader);
  defaultReaderClosedPromiseReject(reader, reason);
}

function defaultReaderClosedPromiseReject(reader, reason) {
  const R = READ(reader);

  if(R.closedPromise_reject === undefined) return;
  setPromiseIsHandledToTrue(R.closedPromise);
  R.closedPromise_reject(reason);
  R.closedPromise_resolve = undefined;
  R.closedPromise_reject = undefined;
}

function defaultReaderClosedPromiseResolve(reader) {
  const R = READ(reader);
  if(R.closedPromise_resolve === undefined) return;
  R.closedPromise_resolve(undefined);
  R.closedPromise_resolve = undefined;
  R.closedPromise_reject = undefined;
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
  const S = STRM(stream);
  assert(IsReadableStreamDefaultReader(S.reader));
  assert(S.state == 'readable');
  const { reader } = S;
  READ(reader).readRequests.push(readRequest);
}

function ReadableStreamFulfillReadRequest(stream, chunk, done) {
  const reader = STRM(stream).reader;
  const R = READ(reader);
  assert(R.readRequests.length > 0);
  const readRequest = R.readRequests.shift();
  if(done) readRequest._closeSteps();
  else readRequest._chunkSteps(chunk);
}

function ReadableStreamGetNumReadRequests(stream) {
  const { reader } = STRM(stream);
  return READ(reader).readRequests.length;
}

function ReadableStreamHasDefaultReader(stream) {
  const reader = STRM(stream).reader;
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
    assign(READ(this), { readRequests: new SimpleQueue() });
  }

  /**
   * Returns a promise that will be fulfilled when the stream becomes closed,
   * or rejected if the stream ever errors or the reader's lock is released before the stream finishes closing.
   */
  get closed() {
    if(!IsReadableStreamDefaultReader(this)) return promiseRejectedWith(defaultReaderBrandCheckException('closed'));
    return READ(this).closedPromise;
  }

  /**
   * If the reader is active, behaves the same as {@link ReadableStream.cancel | stream.cancel(reason)}.
   */
  cancel(reason = undefined) {
    if(!IsReadableStreamDefaultReader(this)) return promiseRejectedWith(defaultReaderBrandCheckException('cancel'));
    if(READ(this).ownerReadableStream === undefined) return promiseRejectedWith(readerLockException('cancel'));
    return ReadableStreamReaderGenericCancel(this, reason);
  }

  /**
   * Returns a promise that allows access to the next chunk from the stream's internal queue, if available.
   *
   * If reading a chunk causes the queue to become empty, more data will be pulled from the underlying source.
   */
  read() {
    if(!IsReadableStreamDefaultReader(this)) return promiseRejectedWith(defaultReaderBrandCheckException('read'));
    if(READ(this).ownerReadableStream === undefined) return promiseRejectedWith(readerLockException('read from'));
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
    if(READ(this).ownerReadableStream === undefined) return;
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

define(ReadableStreamDefaultReader.prototype, { [Symbol.toStringTag]: 'ReadableStreamDefaultReader' });

function IsReadableStreamDefaultReader(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(READ(x), 'readRequests')) return false;
  return x instanceof ReadableStreamDefaultReader;
}

function ReadableStreamDefaultReaderRead(reader, readRequest) {
  const stream = READ(reader).ownerReadableStream;
  assert(stream !== undefined);
  const S = STRM(stream);

  S.disturbed = true;
  if(S.state == 'closed') {
    readRequest._closeSteps();
  } else if(S.state == 'errored') {
    readRequest._errorSteps(S.storedError);
  } else {
    assert(S.state == 'readable');
    S.readableStreamController[PullSteps](readRequest);
  }
}

function ReadableStreamDefaultReaderErrorReadRequests(reader, e) {
  const R = READ(reader);
  const readRequests = R.readRequests;
  R.readRequests = new SimpleQueue();
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

let TransferArrayBuffer = O => {
  if(typeof O.transfer == 'function') TransferArrayBuffer = buffer => buffer.transfer();
  else if(typeof structuredClone == 'function') TransferArrayBuffer = buffer => structuredClone(buffer, { transfer: [buffer] });
  else TransferArrayBuffer = buffer => buffer;
  return TransferArrayBuffer(O);
};

function CanTransferArrayBuffer(O) {
  return !IsDetachedBuffer(O);
}

let IsDetachedBuffer = O => {
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
    assign(this, { _ongoingPromise: undefined, _isFinished: false, _reader: reader, _preventCancel: preventCancel });
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
    assert(READ(reader).ownerReadableStream !== undefined);
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
    const reader = STRM(this).reader;
    const R = READ(reader);
    assert(R.ownerReadableStream !== undefined);
    assert(R.readRequests.length === 0);
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
  const P = CTRL(container);
  assert('queue' in P && 'queueTotalSize' in P);
  assert(P.queue.length > 0);
  const pair = P.queue.shift();
  P.queueTotalSize -= pair.size;
  if(P.queueTotalSize < 0) P.queueTotalSize = 0;
  return pair.value;
}

function EnqueueValueWithSize(container, value, size) {
  const P = CTRL(container);
  assert('queue' in P && 'queueTotalSize' in P);
  if(!IsNonNegativeNumber(size) || size === Infinity) throw new RangeError('Size must be a finite, non-NaN, non-negative number.');
  P.queue.push({ value, size });
  P.queueTotalSize += size;
}

function PeekQueueValue(container) {
  const P = CTRL(container);
  assert('queue' in P && 'queueTotalSize' in P);
  assert(P.queue.length > 0);
  const pair = P.queue.peek();
  return pair.value;
}

function ResetQueue(container) {
  const P = CTRL(container);
  assert('queue' in P && 'queueTotalSize' in P);
  P.queue = new SimpleQueue();
  P.queueTotalSize = 0;
}

// src/stub/number-isinteger.ts
const NumberIsInteger = Number.isInteger ?? (value => typeof value == 'number' && isFinite(value) && Math.floor(value) === value);

// src/lib/helpers/array-buffer-view.ts
function isDataView(view) {
  if(isPrototypeOf(DataView.prototype, view)) return true;
  return view.constructor == DataView;
}

function isDataViewConstructor(ctor) {
  return ctor === DataView;
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

define(ReadableStreamBYOBRequest.prototype, { [Symbol.toStringTag]: 'ReadableStreamBYOBRequest' });

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
    const P = CTRL(this);
    if(!IsReadableByteStreamController(this)) throw byteStreamControllerBrandCheckException('close');
    if(P.closeRequested) throw new TypeError('The stream has already been closed; do not close it again!');
    const state = STRM(P.controlledReadableByteStream).state;
    if(state != 'readable') throw new TypeError(`The stream (in ${state} state) is not in the readable state and cannot be closed`);
    ReadableByteStreamControllerClose(this);
  }

  enqueue(chunk) {
    const P = CTRL(this);
    if(!IsReadableByteStreamController(this)) throw byteStreamControllerBrandCheckException('enqueue');
    assertRequiredArgument(chunk, 1, 'enqueue');
    if(!ArrayBuffer.isView(chunk)) throw new TypeError('chunk must be an array buffer view');
    if(chunk.byteLength === 0) throw new TypeError('chunk must have non-zero byteLength');
    if(chunk.buffer.byteLength === 0) throw new TypeError(`chunk's buffer must have non-zero byteLength`);
    if(P.closeRequested) throw new TypeError('stream is closed or draining');
    const state = STRM(P.controlledReadableByteStream).state;
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
    const result = CTRL(this).cancelAlgorithm(reason);
    ReadableByteStreamControllerClearAlgorithms(this);
    return result;
  }

  /** @internal */
  [PullSteps](readRequest) {
    const P = CTRL(this);
    const stream = P.controlledReadableByteStream;
    assert(ReadableStreamHasDefaultReader(stream));
    if(P.queueTotalSize > 0) {
      assert(ReadableStreamGetNumReadRequests(stream) === 0);
      ReadableByteStreamControllerFillReadRequestFromQueue(this, readRequest);
      return;
    }
    const autoAllocateChunkSize = P.autoAllocateChunkSize;
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
      P.pendingPullIntos.push(pullIntoDescriptor);
    }
    ReadableStreamAddReadRequest(stream, readRequest);
    ReadableByteStreamControllerCallPullIfNeeded(this);
  }

  /** @internal */
  [ReleaseSteps]() {
    const P = CTRL(this);
    if(P.pendingPullIntos.length > 0) {
      const firstPullInto = P.pendingPullIntos.peek();
      firstPullInto.readerType = 'none';
      P.pendingPullIntos = new SimpleQueue();
      P.pendingPullIntos.push(firstPullInto);
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

define(ReadableByteStreamController.prototype, { [Symbol.toStringTag]: 'ReadableByteStreamController' });

function IsReadableByteStreamController(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(CTRL(x), 'controlledReadableByteStream')) return false;
  return x instanceof ReadableByteStreamController;
}

function IsReadableStreamBYOBRequest(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(x, '_associatedReadableByteStreamController')) return false;
  return x instanceof ReadableStreamBYOBRequest;
}

function ReadableByteStreamControllerCallPullIfNeeded(controller) {
  const P = CTRL(controller);
  const stream = P.controlledReadableByteStream;
  if(STRM(stream).state != 'readable') return false;
  if(P.closeRequested) return false;
  if(!P.started) return false;
  
  // Check if there are pending read requests - if so, we should pull regardless of desiredSize
  const hasPendingReads = (ReadableStreamHasDefaultReader(stream) && ReadableStreamGetNumReadRequests(stream) > 0) ||
                          (ReadableStreamHasBYOBReader(stream) && ReadableStreamGetNumReadIntoRequests(stream) > 0);
  
  const desiredSize = ReadableByteStreamControllerGetDesiredSize(controller);
  assert(desiredSize !== null);
  const shouldPull = hasPendingReads || desiredSize > 0;

  if(!shouldPull) return;
  if(P.pulling) {
    P.pullAgain = true;
    return;
  }

  assert(!P.pullAgain);
  P.pulling = true;
  const pullPromise = P.pullAlgorithm();
  uponPromise(
    pullPromise,
    () => {
      P.pulling = false;
      if(P.pullAgain) {
        P.pullAgain = false;
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
  CTRL(controller).pendingPullIntos = new SimpleQueue();
}

function ReadableByteStreamControllerCommitPullIntoDescriptor(stream, pullIntoDescriptor) {
  assert(STRM(stream).state != 'errored');
  assert(pullIntoDescriptor.readerType != 'none');
  let done = false;
  if(STRM(stream).state == 'closed') {
    assert(pullIntoDescriptor.bytesFilled % pullIntoDescriptor.elementSize === 0);
    done = true;
  }

  const filledView = ReadableByteStreamControllerConvertPullIntoDescriptor(pullIntoDescriptor);
  if(pullIntoDescriptor.readerType == 'default') {
    ReadableStreamFulfillReadRequest(stream, filledView, done);
  } else {
    assert(pullIntoDescriptor.readerType == 'byob');

    const reader = STRM(stream).reader;
    const R = READ(reader);
    assert(R.readIntoRequests.length > 0);
    const readIntoRequest = R.readIntoRequests.shift();
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
  const P = CTRL(controller);
  P.queue.push({ buffer, byteOffset, byteLength });
  P.queueTotalSize += byteLength;
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
  const maxBytesToCopy = Math.min(CTRL(controller).queueTotalSize, pullIntoDescriptor.byteLength - pullIntoDescriptor.bytesFilled);
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

  const queue = CTRL(controller).queue;
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
    CTRL(controller).queueTotalSize -= bytesToCopy;
    ReadableByteStreamControllerFillHeadPullIntoDescriptor(controller, bytesToCopy, pullIntoDescriptor);
    totalBytesToCopyRemaining -= bytesToCopy;
  }

  if(!ready) {
    assert(CTRL(controller).queueTotalSize === 0);
    assert(pullIntoDescriptor.bytesFilled > 0);
    assert(pullIntoDescriptor.bytesFilled < pullIntoDescriptor.minimumFill);
  }

  return ready;
}

function ReadableByteStreamControllerFillHeadPullIntoDescriptor(controller, size, pullIntoDescriptor) {
  const P = CTRL(controller);
  assert(P.pendingPullIntos.length === 0 || P.pendingPullIntos.peek() === pullIntoDescriptor);
  assert(P.byobRequest === null);
  pullIntoDescriptor.bytesFilled += size;
}

function ReadableByteStreamControllerHandleQueueDrain(controller) {
  const P = CTRL(controller);
  assert(STRM(P.controlledReadableByteStream).state == 'readable');
  if(P.queueTotalSize === 0 && P.closeRequested) {
    ReadableByteStreamControllerClearAlgorithms(controller);
    ReadableStreamClose(P.controlledReadableByteStream);
  } else {
    ReadableByteStreamControllerCallPullIfNeeded(controller);
  }
}

function ReadableByteStreamControllerInvalidateBYOBRequest(controller) {
  const P = CTRL(controller);
  if(P.byobRequest === null) return;
  P.byobRequest._associatedReadableByteStreamController = undefined;
  P.byobRequest._view = null;
  P.byobRequest = null;
}

function ReadableByteStreamControllerProcessPullIntoDescriptorsUsingQueue(controller) {
  const P = CTRL(controller);
  assert(!P.closeRequested);
  const filledPullIntos = [];
  while(P.pendingPullIntos.length > 0) {
    if(P.queueTotalSize === 0) break;
    const pullIntoDescriptor = P.pendingPullIntos.peek();
    assert(pullIntoDescriptor.readerType != 'none');
    if(ReadableByteStreamControllerFillPullIntoDescriptorFromQueue(controller, pullIntoDescriptor)) {
      ReadableByteStreamControllerShiftPendingPullInto(controller);
      filledPullIntos.push(pullIntoDescriptor);
    }
  }

  return filledPullIntos;
}

function ReadableByteStreamControllerRespondInternal(controller, bytesWritten) {
  const P = CTRL(controller);
  const firstDescriptor = P.pendingPullIntos.peek();
  assert(CanTransferArrayBuffer(firstDescriptor.buffer));
  ReadableByteStreamControllerInvalidateBYOBRequest(controller);
  const state = STRM(P.controlledReadableByteStream).state;
  if(state == 'closed') {
    assert(bytesWritten === 0);

    assert(firstDescriptor.bytesFilled % firstDescriptor.elementSize === 0);
    if(firstDescriptor.readerType == 'none') ReadableByteStreamControllerShiftPendingPullInto(controller);
    const stream = P.controlledReadableByteStream;
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
      ReadableByteStreamControllerCommitPullIntoDescriptors(P.controlledReadableByteStream, filledPullIntos2);
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
    ReadableByteStreamControllerCommitPullIntoDescriptor(P.controlledReadableByteStream, firstDescriptor);
    ReadableByteStreamControllerCommitPullIntoDescriptors(P.controlledReadableByteStream, filledPullIntos);
  }

  ReadableByteStreamControllerCallPullIfNeeded(controller);
}

function ReadableByteStreamControllerShiftPendingPullInto(controller) {
  const P = CTRL(controller);
  assert(P.byobRequest === null);
  const descriptor = P.pendingPullIntos.shift();
  return descriptor;
}

function ReadableByteStreamControllerClearAlgorithms(controller) {
  Object.assign(CTRL(controller), { pullAlgorithm: undefined, cancelAlgorithm: undefined });
}

function ReadableByteStreamControllerClose(controller) {
  const P = CTRL(controller);
  const stream = P.controlledReadableByteStream;
  if(P.closeRequested || STRM(stream).state != 'readable') return;
  if(P.queueTotalSize > 0) {
    P.closeRequested = true;
    return;
  }

  if(P.pendingPullIntos.length > 0) {
    const firstPendingPullInto = P.pendingPullIntos.peek();
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
  const P = CTRL(controller);
  const stream = P.controlledReadableByteStream;
  if(P.closeRequested || STRM(stream).state != 'readable') return;
  const { buffer, byteOffset, byteLength } = chunk;
  if(IsDetachedBuffer(buffer)) throw new TypeError("chunk's buffer is detached and so cannot be enqueued");
  const transferredBuffer = TransferArrayBuffer(buffer);
  if(P.pendingPullIntos.length > 0) {
    const firstPendingPullInto = P.pendingPullIntos.peek();
    if(IsDetachedBuffer(firstPendingPullInto.buffer)) throw new TypeError("The BYOB request's buffer has been detached and so cannot be filled with an enqueued chunk");
    ReadableByteStreamControllerInvalidateBYOBRequest(controller);
    firstPendingPullInto.buffer = TransferArrayBuffer(firstPendingPullInto.buffer);
    if(firstPendingPullInto.readerType == 'none') ReadableByteStreamControllerEnqueueDetachedPullIntoToQueue(controller, firstPendingPullInto);
  }

  if(ReadableStreamHasDefaultReader(stream)) {
    const reader = STRM(P.controlledReadableByteStream).reader;
    assert(IsReadableStreamDefaultReader(reader));
    const R = READ(reader);
    while(R.readRequests.length > 0) {
      if(P.queueTotalSize === 0) return;
      const readRequest = R.readRequests.shift();
      ReadableByteStreamControllerFillReadRequestFromQueue(controller, readRequest);
    }

    if(ReadableStreamGetNumReadRequests(stream) === 0) {
      assert(P.pendingPullIntos.length === 0);
      ReadableByteStreamControllerEnqueueChunkToQueue(controller, transferredBuffer, byteOffset, byteLength);
    } else {
      assert(P.queue.length === 0);
      if(P.pendingPullIntos.length > 0) {
        assert(P.pendingPullIntos.peek().readerType == 'default');
        ReadableByteStreamControllerShiftPendingPullInto(controller);
      }
      const transferredView = new Uint8Array(transferredBuffer, byteOffset, byteLength);
      ReadableStreamFulfillReadRequest(stream, transferredView, false);
    }
  } else if(ReadableStreamHasBYOBReader(stream)) {
    ReadableByteStreamControllerEnqueueChunkToQueue(controller, transferredBuffer, byteOffset, byteLength);
    const filledPullIntos = ReadableByteStreamControllerProcessPullIntoDescriptorsUsingQueue(controller);
    ReadableByteStreamControllerCommitPullIntoDescriptors(P.controlledReadableByteStream, filledPullIntos);
  } else {
    assert(!IsReadableStreamLocked(stream));
    ReadableByteStreamControllerEnqueueChunkToQueue(controller, transferredBuffer, byteOffset, byteLength);
  }

  ReadableByteStreamControllerCallPullIfNeeded(controller);
}

function ReadableByteStreamControllerError(controller, e) {
  const stream = CTRL(controller).controlledReadableByteStream;
  if(STRM(stream).state != 'readable') return;
  ReadableByteStreamControllerClearPendingPullIntos(controller);
  ResetQueue(controller);
  ReadableByteStreamControllerClearAlgorithms(controller);
  ReadableStreamError(stream, e);
}

function ReadableByteStreamControllerFillReadRequestFromQueue(controller, readRequest) {
  const P = CTRL(controller);
  assert(P.queueTotalSize > 0);
  const entry = P.queue.shift();
  P.queueTotalSize -= entry.byteLength;
  ReadableByteStreamControllerHandleQueueDrain(controller);
  const view = new Uint8Array(entry.buffer, entry.byteOffset, entry.byteLength);
  readRequest._chunkSteps(view);
}

function ReadableByteStreamControllerGetBYOBRequest(controller) {
  const P = CTRL(controller);
  if(P.byobRequest === null && P.pendingPullIntos.length > 0) {
    const firstDescriptor = P.pendingPullIntos.peek();
    const view = new Uint8Array(firstDescriptor.buffer, firstDescriptor.byteOffset + firstDescriptor.bytesFilled, firstDescriptor.byteLength - firstDescriptor.bytesFilled);
    const byobRequest = Object.create(ReadableStreamBYOBRequest.prototype);

    assert(IsReadableByteStreamController(controller));
    assert(typeof view == 'object');
    assert(ArrayBuffer.isView(view));
    assert(!IsDetachedBuffer(view.buffer));
    byobRequest._associatedReadableByteStreamController = controller;
    byobRequest._view = view;

    P.byobRequest = byobRequest;
  }

  return P.byobRequest;
}

function ReadableByteStreamControllerGetDesiredSize(controller) {
  const P = CTRL(controller);
  const state = STRM(P.controlledReadableByteStream).state;
  if(state == 'errored') return null;
  if(state == 'closed') return 0;
  return P.strategyHWM - P.queueTotalSize;
}

function ReadableByteStreamControllerRespond(controller, bytesWritten) {
  const P = CTRL(controller);
  assert(P.pendingPullIntos.length > 0);
  const firstDescriptor = P.pendingPullIntos.peek();
  const state = STRM(P.controlledReadableByteStream).state;
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
  const P = CTRL(controller);
  assert(P.pendingPullIntos.length > 0);
  assert(!IsDetachedBuffer(view.buffer));
  const firstDescriptor = P.pendingPullIntos.peek();
  const state = STRM(P.controlledReadableByteStream).state;
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
  const P = CTRL(controller);
  assert(STRM(stream).readableStreamController === undefined);
  if(autoAllocateChunkSize !== undefined) {
    assert(NumberIsInteger(autoAllocateChunkSize));
    assert(autoAllocateChunkSize > 0);
  }

  P.controlledReadableByteStream = stream;
  P.pullAgain = false;
  P.pulling = false;
  P.byobRequest = null;
  P.queue = P.queueTotalSize = undefined;
  ResetQueue(controller);
  P.closeRequested = false;
  P.started = false;
  P.strategyHWM = highWaterMark;
  P.pullAlgorithm = pullAlgorithm;
  P.cancelAlgorithm = cancelAlgorithm;
  P.autoAllocateChunkSize = autoAllocateChunkSize;
  P.pendingPullIntos = new SimpleQueue();
  assign(STRM(stream), { readableStreamController: controller });
  const startResult = startAlgorithm();
  uponPromise(
    promiseResolvedWith(startResult),
    () => {
      P.started = true;
      assert(!P.pulling);
      assert(!P.pullAgain);
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
  assert(IsReadableStreamBYOBReader(STRM(stream).reader));
  assert(STRM(stream).state == 'readable' || STRM(stream).state == 'closed');
  READ(STRM(stream).reader).readIntoRequests.push(readIntoRequest);
}

function ReadableStreamGetNumReadIntoRequests(stream) {
  return READ(STRM(stream).reader).readIntoRequests.length;
}

function ReadableStreamHasBYOBReader(stream) {
  const reader = STRM(stream).reader;
  if(reader === undefined) return false;
  if(!IsReadableStreamBYOBReader(reader)) return false;
  return true;
}

export class ReadableStreamBYOBReader {
  constructor(stream) {
    assertRequiredArgument(stream, 1, 'ReadableStreamBYOBReader');
    assertReadableStream(stream, 'First parameter');
    if(IsReadableStreamLocked(stream)) throw new TypeError('This stream has already been locked for exclusive reading by another reader');
    if(!IsReadableByteStreamController(STRM(stream).readableStreamController)) throw new TypeError('Cannot construct a ReadableStreamBYOBReader for a stream not constructed with a byte source');
    ReadableStreamReaderGenericInitialize(this, stream);
    assign(READ(this), { readIntoRequests: new SimpleQueue() });
  }

  /**
   * Returns a promise that will be fulfilled when the stream becomes closed, or rejected if the stream ever errors or
   * the reader's lock is released before the stream finishes closing.
   */
  get closed() {
    if(!IsReadableStreamBYOBReader(this)) return promiseRejectedWith(byobReaderBrandCheckException('closed'));
    return READ(this).closedPromise;
  }

  /**
   * If the reader is active, behaves the same as {@link ReadableStream.cancel | stream.cancel(reason)}.
   */
  cancel(reason = undefined) {
    if(!IsReadableStreamBYOBReader(this)) return promiseRejectedWith(byobReaderBrandCheckException('cancel'));
    if(READ(this).ownerReadableStream === undefined) return promiseRejectedWith(readerLockException('cancel'));
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
    if(READ(this).ownerReadableStream === undefined) return promiseRejectedWith(readerLockException('read from'));
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
    if(READ(this).ownerReadableStream === undefined) return;

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

define(ReadableStreamBYOBReader.prototype, { [Symbol.toStringTag]: 'ReadableStreamBYOBReader' });

function IsReadableStreamBYOBReader(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(READ(x), 'readIntoRequests')) return false;
  return x instanceof ReadableStreamBYOBReader;
}

function ReadableStreamBYOBReaderRead(reader, view, min, readIntoRequest) {
  const stream = READ(reader).ownerReadableStream;
  assert(stream !== undefined);
  STRM(stream).disturbed = true;
  if(STRM(stream).state == 'errored') readIntoRequest._errorSteps(STRM(stream).storedError);
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
    if(CTRL(STRM(stream).readableStreamController).pendingPullIntos.length > 0) {
      CTRL(STRM(stream).readableStreamController).pendingPullIntos.push(pullIntoDescriptor);
      ReadableStreamAddReadIntoRequest(stream, readIntoRequest);
      return;
    }

    if(STRM(stream).state == 'closed') {
      const emptyView = new ctor(pullIntoDescriptor.buffer, pullIntoDescriptor.byteOffset, 0);
      readIntoRequest._closeSteps(emptyView);
      return;
    }

    if(CTRL(STRM(stream).readableStreamController).queueTotalSize > 0) {
      if(ReadableByteStreamControllerFillPullIntoDescriptorFromQueue(STRM(stream).readableStreamController, pullIntoDescriptor)) {
        const filledView = ReadableByteStreamControllerConvertPullIntoDescriptor(pullIntoDescriptor);
        ReadableByteStreamControllerHandleQueueDrain(STRM(stream).readableStreamController);
        readIntoRequest._chunkSteps(filledView);
        return;
      }
      if(CTRL(STRM(stream).readableStreamController).closeRequested) {
        const e = new TypeError('Insufficient bytes to fill elements in the given buffer');
        ReadableByteStreamControllerError(STRM(stream).readableStreamController, e);
        readIntoRequest._errorSteps(e);
        return;
      }
    }

    CTRL(STRM(stream).readableStreamController).pendingPullIntos.push(pullIntoDescriptor);
    ReadableStreamAddReadIntoRequest(stream, readIntoRequest);
    ReadableByteStreamControllerCallPullIfNeeded(STRM(stream).readableStreamController);
  }
}

function ReadableStreamBYOBReaderErrorReadIntoRequests(reader, e) {
  const R = READ(reader);
  const readIntoRequests = R.readIntoRequests;
  R.readIntoRequests = new SimpleQueue();
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

define(WritableStream.prototype, { [Symbol.toStringTag]: 'WritableStream' });

function AcquireWritableStreamDefaultWriter(stream) {
  return new WritableStreamDefaultWriter(stream);
}

function InitializeWritableStream(stream) {
  assign(STRM(stream), {
    state: 'writable',
    storedError: undefined,
    writer: undefined,
    writableStreamController: undefined,
    writeRequests: new SimpleQueue(),
    inFlightWriteRequest: undefined,
    closeRequest: undefined,
    inFlightCloseRequest: undefined,
    pendingAbortRequest: undefined,
    backpressure: false,
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
  if(!Object.prototype.hasOwnProperty.call(STRM(x), 'writableStreamController')) return false;
  return x instanceof WritableStream;
}

function IsWritableStreamLocked(stream) {
  assert(IsWritableStream(stream));
  if(STRM(stream).writer === undefined) return false;
  return true;
}

function WritableStreamAbort(stream, reason) {
  var _a2;
  if(STRM(stream).state == 'closed' || STRM(stream).state == 'errored') return promiseResolvedWith(undefined);
  CTRL(STRM(stream).writableStreamController).abortReason = reason;
  (_a2 = CTRL(STRM(stream).writableStreamController).abortController) == null ? undefined : _a2.abort(reason);
  const state = STRM(stream).state;
  if(state == 'closed' || state == 'errored') return promiseResolvedWith(undefined);
  if(STRM(stream).pendingAbortRequest !== undefined) return STRM(stream).pendingAbortRequest._promise;
  assert(state == 'writable' || state == 'erroring');
  let wasAlreadyErroring = false;
  if(state == 'erroring') {
    wasAlreadyErroring = true;
    reason = undefined;
  }

  const promise = newPromise((resolve, reject) => {
    STRM(stream).pendingAbortRequest = {
      _promise: undefined,
      _resolve: resolve,
      _reject: reject,
      _reason: reason,
      _wasAlreadyErroring: wasAlreadyErroring,
    };
  });
  STRM(stream).pendingAbortRequest._promise = promise;
  if(!wasAlreadyErroring) WritableStreamStartErroring(stream, reason);
  return promise;
}

function WritableStreamClose(stream) {
  const state = STRM(stream).state;
  if(state == 'closed' || state == 'errored') return promiseRejectedWith(new TypeError(`The stream (in ${state} state) is not in the writable state and cannot be closed`));
  assert(state == 'writable' || state == 'erroring');
  assert(!WritableStreamCloseQueuedOrInFlight(stream));
  const promise = newPromise((resolve, reject) => {
    const closeRequest = {
      _resolve: resolve,
      _reject: reject,
    };
    STRM(stream).closeRequest = closeRequest;
  });
  const writer = STRM(stream).writer;
  if(writer !== undefined && STRM(stream).backpressure && state == 'writable') defaultWriterReadyPromiseResolve(writer);

  EnqueueValueWithSize(STRM(stream).writableStreamController, closeSentinel, 0);
  WritableStreamDefaultControllerAdvanceQueueIfNeeded(STRM(stream).writableStreamController);

  return promise;
}

function WritableStreamDealWithRejection(stream, error) {
  const state = STRM(stream).state;
  if(state == 'writable') {
    WritableStreamStartErroring(stream, error);
    return;
  }

  assert(state == 'erroring');
  WritableStreamFinishErroring(stream);
}

function WritableStreamStartErroring(stream, reason) {
  assert(STRM(stream).storedError === undefined);
  assert(STRM(stream).state == 'writable');
  const controller = STRM(stream).writableStreamController;
  assert(controller !== undefined);
  STRM(stream).state = 'erroring';
  STRM(stream).storedError = reason;
  const writer = STRM(stream).writer;
  if(writer !== undefined) WritableStreamDefaultWriterEnsureReadyPromiseRejected(writer, reason);
  if(!WritableStreamHasOperationMarkedInFlight(stream) && CTRL(controller).started) WritableStreamFinishErroring(stream);
}

function WritableStreamFinishErroring(stream) {
  assert(STRM(stream).state == 'erroring');
  assert(!WritableStreamHasOperationMarkedInFlight(stream));
  STRM(stream).state = 'errored';
  STRM(stream).writableStreamController[ErrorSteps]();
  const storedError = STRM(stream).storedError;
  STRM(stream).writeRequests.forEach(writeRequest => writeRequest._reject(storedError));
  STRM(stream).writeRequests = new SimpleQueue();
  if(STRM(stream).pendingAbortRequest === undefined) {
    WritableStreamRejectCloseAndClosedPromiseIfNeeded(stream);
    return;
  }

  const abortRequest = STRM(stream).pendingAbortRequest;
  STRM(stream).pendingAbortRequest = undefined;
  if(abortRequest._wasAlreadyErroring) {
    abortRequest._reject(storedError);
    WritableStreamRejectCloseAndClosedPromiseIfNeeded(stream);
    return;
  }

  const promise = STRM(stream).writableStreamController[AbortSteps](abortRequest._reason);
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
  if(STRM(stream).closeRequest === undefined && STRM(stream).inFlightCloseRequest === undefined) return false;
  return true;
}

function WritableStreamHasOperationMarkedInFlight(stream) {
  if(STRM(stream).inFlightWriteRequest === undefined && STRM(stream).inFlightCloseRequest === undefined) return false;
  return true;
}

function WritableStreamRejectCloseAndClosedPromiseIfNeeded(stream) {
  assert(STRM(stream).state == 'errored');
  if(STRM(stream).closeRequest !== undefined) {
    assert(STRM(stream).inFlightCloseRequest === undefined);
    STRM(stream).closeRequest._reject(STRM(stream).storedError);
    STRM(stream).closeRequest = undefined;
  }

  const writer = STRM(stream).writer;
  if(writer !== undefined) defaultWriterClosedPromiseReject(writer, STRM(stream).storedError);
}

function WritableStreamUpdateBackpressure(stream, backpressure) {
  assert(STRM(stream).state == 'writable');
  assert(!WritableStreamCloseQueuedOrInFlight(stream));
  const writer = STRM(stream).writer;
  if(writer !== undefined && backpressure !== STRM(stream).backpressure) {
    if(backpressure) {
      assert(WRITE(writer).readyPromise_resolve === undefined);
      assert(WRITE(writer).readyPromise_reject === undefined);
      defaultWriterReadyPromiseInitialize(writer);
    } else {
      assert(!backpressure);
      defaultWriterReadyPromiseResolve(writer);
    }
  }

  STRM(stream).backpressure = backpressure;
}

export class WritableStreamDefaultWriter {
  constructor(stream) {
    assertRequiredArgument(stream, 1, 'WritableStreamDefaultWriter');
    assertWritableStream(stream, 'First parameter');
    if(IsWritableStreamLocked(stream)) throw new TypeError('This stream has already been locked for exclusive writing by another writer');
    assign(WRITE(this), { ownerWritableStream: stream });
    assign(STRM(stream), { writer: this });
    const state = STRM(stream).state;
    if(state == 'writable') {
      if(!WritableStreamCloseQueuedOrInFlight(stream) && STRM(stream).backpressure) defaultWriterReadyPromiseInitialize(this);
      else defaultWriterReadyPromiseInitializeAsResolved(this);
      defaultWriterClosedPromiseInitialize(this);
    } else if(state == 'erroring') {
      defaultWriterReadyPromiseInitializeAsRejected(this, STRM(stream).storedError);
      defaultWriterClosedPromiseInitialize(this);
    } else if(state == 'closed') {
      defaultWriterReadyPromiseInitializeAsResolved(this);

      defaultWriterClosedPromiseInitialize(this);
      defaultWriterClosedPromiseResolve(this);
    } else {
      assert(state == 'errored');
      const storedError = STRM(stream).storedError;
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
    return WRITE(this).closedPromise;
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
    if(WRITE(this).ownerWritableStream === undefined) throw defaultWriterLockException('desiredSize');

    const stream = WRITE(this).ownerWritableStream;
    const state = STRM(stream).state;
    if(state == 'errored' || state == 'erroring') return null;
    if(state == 'closed') return 0;
    return WritableStreamDefaultControllerGetDesiredSize(STRM(stream).writableStreamController);
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
    return WRITE(this).readyPromise;
  }

  /**
   * If the reader is active, behaves the same as {@link WritableStream.abort | stream.abort(reason)}.
   */
  abort(reason = undefined) {
    if(!IsWritableStreamDefaultWriter(this)) return promiseRejectedWith(defaultWriterBrandCheckException('abort'));
    if(WRITE(this).ownerWritableStream === undefined) return promiseRejectedWith(defaultWriterLockException('abort'));

    const stream = WRITE(this).ownerWritableStream;
    assert(stream !== undefined);
    return WritableStreamAbort(stream, reason);
  }

  /**
   * If the reader is active, behaves the same as {@link WritableStream.close | stream.close()}.
   */
  close() {
    if(!IsWritableStreamDefaultWriter(this)) return promiseRejectedWith(defaultWriterBrandCheckException('close'));
    const stream = WRITE(this).ownerWritableStream;
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
    const stream = WRITE(this).ownerWritableStream;
    if(stream === undefined) return;
    assert(STRM(stream).writer !== undefined);
    WritableStreamDefaultWriterRelease(this);
  }

  write(chunk = undefined) {
    if(!IsWritableStreamDefaultWriter(this)) return promiseRejectedWith(defaultWriterBrandCheckException('write'));
    if(WRITE(this).ownerWritableStream === undefined) return promiseRejectedWith(defaultWriterLockException('write to'));
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

define(WritableStreamDefaultWriter.prototype, { [Symbol.toStringTag]: 'WritableStreamDefaultWriter' });

function IsWritableStreamDefaultWriter(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(WRITE(x), 'ownerWritableStream')) return false;
  return x instanceof WritableStreamDefaultWriter;
}

function WritableStreamDefaultWriterClose(writer) {
  const stream = WRITE(writer).ownerWritableStream;
  assert(stream !== undefined);
  return WritableStreamClose(stream);
}

function WritableStreamDefaultWriterEnsureReadyPromiseRejected(writer, error) {
  if(WRITE(writer).readyPromiseState == 'pending') {
    defaultWriterReadyPromiseReject(writer, error);
  } else {
    assert(WRITE(writer).readyPromise_resolve === undefined);
    assert(WRITE(writer).readyPromise_reject === undefined);
    defaultWriterReadyPromiseInitializeAsRejected(writer, error);
  }
}

function WritableStreamDefaultWriterRelease(writer) {
  const stream = WRITE(writer).ownerWritableStream;
  assert(stream !== undefined);
  assert(STRM(stream).writer === writer);
  const releasedError = new TypeError(`Writer was released and can no longer be used to monitor the stream's closedness`);
  WritableStreamDefaultWriterEnsureReadyPromiseRejected(writer, releasedError);

  if(WRITE(writer).closedPromiseState == 'pending') defaultWriterClosedPromiseReject(writer, releasedError);
  else {
    assert(WRITE(writer).closedPromise_resolve === undefined);
    assert(WRITE(writer).closedPromise_reject === undefined);
    assert(WRITE(writer).closedPromiseState != 'pending');
    defaultWriterClosedPromiseInitializeAsRejected(writer, releasedError);
  }

  STRM(stream).writer = undefined;
  WRITE(writer).ownerWritableStream = undefined;
}

function WritableStreamDefaultWriterWrite(writer, chunk) {
  const stream = WRITE(writer).ownerWritableStream;
  assert(stream !== undefined);
  const controller = STRM(stream).writableStreamController;
  const P = CTRL(controller);

  let chunkSize;

  if(P.strategySizeAlgorithm === undefined) {
    assert(STRM(P.controlledWritableStream).state == 'erroring' || STRM(P.controlledWritableStream).state == 'errored');
    return 1;
  }
  try {
    chunkSize = P.strategySizeAlgorithm(chunk);
  } catch(chunkSizeE) {
    WritableStreamDefaultControllerErrorIfNeeded(controller, chunkSizeE);
    chunkSize = 1;
  }

  if(stream !== WRITE(writer).ownerWritableStream) return promiseRejectedWith(defaultWriterLockException('write to'));
  const state = STRM(stream).state;
  if(state == 'errored') return promiseRejectedWith(STRM(stream).storedError);
  if(WritableStreamCloseQueuedOrInFlight(stream) || state == 'closed') return promiseRejectedWith(new TypeError('The stream is closing or closed and cannot be written to'));
  if(state == 'erroring') return promiseRejectedWith(STRM(stream).storedError);
  assert(state == 'writable');

  assert(IsWritableStreamLocked(stream));
  assert(STRM(stream).state == 'writable');
  const promise = newPromise((resolve, reject) =>
    STRM(stream).writeRequests.push({
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

  //const stream = P.controlledWritableStream;
  if(!WritableStreamCloseQueuedOrInFlight(stream) && STRM(stream).state == 'writable') {
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
    return CTRL(this).abortReason;
  }

  /**
   * An `AbortSignal` that can be used to abort the pending write or close operation when the stream is aborted.
   */
  get signal() {
    if(!IsWritableStreamDefaultController(this)) throw defaultControllerBrandCheckException('signal');
    const P = CTRL(this);
    if(P.abortController === undefined) throw new TypeError('WritableStreamDefaultController.prototype.signal is not supported');
    return P.abortController.signal;
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
    const state = STRM(CTRL(this).controlledWritableStream).state;
    if(state != 'writable') return;
    WritableStreamDefaultControllerError(this, e);
  }

  /** @internal */
  [AbortSteps](reason) {
    const result = CTRL(this).abortAlgorithm(reason);
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

define(WritableStreamDefaultController.prototype, { [Symbol.toStringTag]: 'WritableStreamDefaultController' });

function IsWritableStreamDefaultController(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(CTRL(x), 'controlledWritableStream')) return false;
  return x instanceof WritableStreamDefaultController;
}

function SetUpWritableStreamDefaultController(stream, controller, startAlgorithm, writeAlgorithm, closeAlgorithm, abortAlgorithm, highWaterMark, sizeAlgorithm) {
  assert(IsWritableStream(stream));
  assert(STRM(stream).writableStreamController === undefined);
  const P = CTRL(controller);
  P.controlledWritableStream = stream;
  assign(STRM(stream), { writableStreamController: controller });
  P.queue = undefined;
  P.queueTotalSize = undefined;
  ResetQueue(controller);
  P.abortReason = undefined;
  P.abortController = new AbortController();
  P.started = false;
  P.strategySizeAlgorithm = sizeAlgorithm;
  P.strategyHWM = highWaterMark;
  P.writeAlgorithm = writeAlgorithm;
  P.closeAlgorithm = closeAlgorithm;
  P.abortAlgorithm = abortAlgorithm;
  const backpressure = WritableStreamDefaultControllerGetBackpressure(controller);
  WritableStreamUpdateBackpressure(stream, backpressure);
  const startResult = startAlgorithm();
  const startPromise = promiseResolvedWith(startResult);
  uponPromise(
    startPromise,
    () => {
      assert(STRM(stream).state == 'writable' || STRM(stream).state == 'erroring');
      P.started = true;
      WritableStreamDefaultControllerAdvanceQueueIfNeeded(controller);
      return null;
    },
    r => {
      assert(STRM(stream).state == 'writable' || STRM(stream).state == 'erroring');
      P.started = true;
      WritableStreamDealWithRejection(stream, r);
      return null;
    },
  );
}

function WritableStreamDefaultControllerClearAlgorithms(controller) {
  Object.assign(CTRL(controller), { writeAlgorithm: undefined, closeAlgorithm: undefined, abortAlgorithm: undefined, strategySizeAlgorithm: undefined });
}

function WritableStreamDefaultControllerClose(controller) {
  EnqueueValueWithSize(controller, closeSentinel, 0);
  WritableStreamDefaultControllerAdvanceQueueIfNeeded(controller);
}

function WritableStreamDefaultControllerGetDesiredSize(controller) {
  return CTRL(controller).strategyHWM - CTRL(controller).queueTotalSize;
}

function WritableStreamDefaultControllerAdvanceQueueIfNeeded(controller) {
  const P = CTRL(controller);
  const stream = P.controlledWritableStream;
  if(!P.started) return;
  if(STRM(stream).inFlightWriteRequest !== undefined) return;
  const state = STRM(stream).state;
  assert(state != 'closed' && state != 'errored');
  if(state == 'erroring') {
    WritableStreamFinishErroring(stream);
    return;
  }

  if(P.queue.length === 0) return;
  const value = PeekQueueValue(controller);
  if(value === closeSentinel) {
    assert(STRM(stream).inFlightCloseRequest === undefined);
    assert(STRM(stream).closeRequest !== undefined);
    STRM(stream).inFlightCloseRequest = STRM(stream).closeRequest;
    STRM(stream).closeRequest = undefined;

    DequeueValue(controller);
    assert(P.queue.length === 0);
    const sinkClosePromise = P.closeAlgorithm();
    WritableStreamDefaultControllerClearAlgorithms(controller);
    uponPromise(
      sinkClosePromise,
      () => {
        assert(STRM(stream).inFlightCloseRequest !== undefined);
        STRM(stream).inFlightCloseRequest._resolve(undefined);
        STRM(stream).inFlightCloseRequest = undefined;
        const state = STRM(stream).state;
        assert(state == 'writable' || state == 'erroring');
        if(state == 'erroring') {
          STRM(stream).storedError = undefined;
          if(STRM(stream).pendingAbortRequest !== undefined) {
            STRM(stream).pendingAbortRequest._resolve();
            STRM(stream).pendingAbortRequest = undefined;
          }
        }

        STRM(stream).state = 'closed';
        const writer = STRM(stream).writer;
        if(writer !== undefined) defaultWriterClosedPromiseResolve(writer);
        assert(STRM(stream).pendingAbortRequest === undefined);
        assert(STRM(stream).storedError === undefined);

        return null;
      },
      reason => {
        assert(STRM(stream).inFlightCloseRequest !== undefined);
        STRM(stream).inFlightCloseRequest._reject(reason);
        STRM(stream).inFlightCloseRequest = undefined;
        assert(STRM(stream).state == 'writable' || STRM(stream).state == 'erroring');
        if(STRM(stream).pendingAbortRequest !== undefined) {
          STRM(stream).pendingAbortRequest._reject(reason);
          STRM(stream).pendingAbortRequest = undefined;
        }

        WritableStreamDealWithRejection(stream, reason);
        return null;
      },
    );
  } else {
    assert(STRM(stream).inFlightWriteRequest === undefined);
    assert(STRM(stream).writeRequests.length !== 0);
    STRM(stream).inFlightWriteRequest = STRM(stream).writeRequests.shift();

    const sinkWritePromise = P.writeAlgorithm(value);
    uponPromise(
      sinkWritePromise,
      () => {
        assert(STRM(stream).inFlightWriteRequest !== undefined);
        STRM(stream).inFlightWriteRequest._resolve(undefined);
        STRM(stream).inFlightWriteRequest = undefined;

        const state = STRM(stream).state;
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
        if(STRM(stream).state == 'writable') WritableStreamDefaultControllerClearAlgorithms(controller);

        assert(STRM(stream).inFlightWriteRequest !== undefined);
        STRM(stream).inFlightWriteRequest._reject(error);
        STRM(stream).inFlightWriteRequest = undefined;
        assert(STRM(stream).state == 'writable' || STRM(stream).state == 'erroring');
        WritableStreamDealWithRejection(stream, error);

        return null;
      },
    );
  }
}

function WritableStreamDefaultControllerErrorIfNeeded(controller, error) {
  if(STRM(CTRL(controller).controlledWritableStream).state == 'writable') WritableStreamDefaultControllerError(controller, error);
}

function WritableStreamDefaultControllerGetBackpressure(controller) {
  const desiredSize = WritableStreamDefaultControllerGetDesiredSize(controller);
  return desiredSize <= 0;
}

function WritableStreamDefaultControllerError(controller, error) {
  const stream = CTRL(controller).controlledWritableStream;
  assert(STRM(stream).state == 'writable');
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
  assign(WRITE(writer), {
    closedPromise: newPromise((resolve, reject) => {
      assign(WRITE(writer), { closedPromise_resolve: resolve });
      assign(WRITE(writer), { closedPromise_reject: reject });
      assign(WRITE(writer), { closedPromiseState: 'pending' });
    }),
  });
}

function defaultWriterClosedPromiseInitializeAsRejected(writer, reason) {
  defaultWriterClosedPromiseInitialize(writer);
  defaultWriterClosedPromiseReject(writer, reason);
}

function defaultWriterClosedPromiseReject(writer, reason) {
  if(WRITE(writer).closedPromise_reject === undefined) return;
  assert(WRITE(writer).closedPromiseState == 'pending');
  setPromiseIsHandledToTrue(WRITE(writer).closedPromise);
  WRITE(writer).closedPromise_reject(reason);
  WRITE(writer).closedPromise_resolve = undefined;
  WRITE(writer).closedPromise_reject = undefined;
  WRITE(writer).closedPromiseState = 'rejected';
}

function defaultWriterClosedPromiseResolve(writer) {
  if(WRITE(writer).closedPromise_resolve === undefined) return;
  assert(WRITE(writer).closedPromiseState == 'pending');
  WRITE(writer).closedPromise_resolve(undefined);
  WRITE(writer).closedPromise_resolve = undefined;
  WRITE(writer).closedPromise_reject = undefined;
  WRITE(writer).closedPromiseState = 'resolved';
}

function defaultWriterReadyPromiseInitialize(writer) {
  assign(WRITE(writer), {
    readyPromise: newPromise((resolve, reject) => assign(WRITE(writer), { readyPromise_resolve: resolve, readyPromise_reject: reject })),
    readyPromiseState: 'pending',
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
  if(WRITE(writer).readyPromise_reject === undefined) return;
  setPromiseIsHandledToTrue(WRITE(writer).readyPromise);
  WRITE(writer).readyPromise_reject(reason);
  WRITE(writer).readyPromise_resolve = undefined;
  WRITE(writer).readyPromise_reject = undefined;
  WRITE(writer).readyPromiseState = 'rejected';
}

function defaultWriterReadyPromiseResolve(writer) {
  if(WRITE(writer).readyPromise_resolve === undefined) return;
  WRITE(writer).readyPromise_resolve(undefined);
  WRITE(writer).readyPromise_resolve = undefined;
  WRITE(writer).readyPromise_reject = undefined;
  WRITE(writer).readyPromiseState = 'fulfilled';
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
  STRM(source).disturbed = true;
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
            if(STRM(dest).state == 'writable') return WritableStreamAbort(dest, error);
            return promiseResolvedWith(undefined);
          });
        }
        if(!preventCancel) {
          actions.push(() => {
            if(STRM(source).state == 'readable') return ReadableStreamCancel(source, error);
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
      return PerformPromiseThen(WRITE(writer).readyPromise, () => {
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
    isOrBecomesErrored(source, READ(reader).closedPromise, storedError => {
      if(!preventAbort) shutdownWithAction(() => WritableStreamAbort(dest, storedError), true, storedError);
      else shutdown(true, storedError);
      return null;
    });
    isOrBecomesErrored(dest, WRITE(writer).closedPromise, storedError => {
      if(!preventCancel) shutdownWithAction(() => ReadableStreamCancel(source, storedError), true, storedError);
      else shutdown(true, storedError);
      return null;
    });
    isOrBecomesClosed(source, READ(reader).closedPromise, () => {
      if(!preventClose)
        shutdownWithAction(() => {
          const stream = WRITE(writer).ownerWritableStream;
          assert(stream !== undefined);
          const state = STRM(stream).state;
          if(WritableStreamCloseQueuedOrInFlight(stream) || state == 'closed') return promiseResolvedWith(undefined);
          if(state == 'errored') return promiseRejectedWith(STRM(stream).storedError);
          assert(state == 'writable' || state == 'erroring');
          return WritableStreamDefaultWriterClose(writer);
        });
      else shutdown();
      return null;
    });
    if(WritableStreamCloseQueuedOrInFlight(dest) || STRM(dest).state == 'closed') {
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
      if(STRM(stream).state == 'errored') action(STRM(stream).storedError);
      else uponRejection(promise, action);
    }
    function isOrBecomesClosed(stream, promise, action) {
      if(STRM(stream).state == 'closed') action();
      else uponFulfillment(promise, action);
    }
    function shutdownWithAction(action, originalIsError, originalError) {
      if(shuttingDown) return;
      shuttingDown = true;
      if(STRM(dest).state == 'writable' && !WritableStreamCloseQueuedOrInFlight(dest)) uponFulfillment(waitForWritesToFinish(), doTheRest);
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
      if(STRM(dest).state == 'writable' && !WritableStreamCloseQueuedOrInFlight(dest)) uponFulfillment(waitForWritesToFinish(), () => finalize(isError, error));
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
    const result = CTRL(this).cancelAlgorithm(reason);
    ReadableStreamDefaultControllerClearAlgorithms(this);
    return result;
  }

  /** @internal */
  [PullSteps](readRequest) {
    const P = CTRL(this);
    const stream = P.controlledReadableStream;
    if(P.queue.length > 0) {
      const chunk = DequeueValue(this);
      if(P.closeRequested && P.queue.length === 0) {
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

define(ReadableStreamDefaultController.prototype, { [Symbol.toStringTag]: 'ReadableStreamDefaultController' });

function IsReadableStreamDefaultController(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(CTRL(x), 'controlledReadableStream')) return false;
  return x instanceof ReadableStreamDefaultController;
}

function ReadableStreamDefaultControllerCallPullIfNeeded(controller) {
  const P = CTRL(controller);
  const shouldPull = ReadableStreamDefaultControllerShouldCallPull(controller);
  if(!shouldPull) return;
  if(P.pulling) {
    P.pullAgain = true;
    return;
  }

  assert(!P.pullAgain);
  P.pulling = true;
  const pullPromise = P.pullAlgorithm();
  uponPromise(
    pullPromise,
    () => {
      P.pulling = false;
      if(P.pullAgain) {
        P.pullAgain = false;
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
  const P = CTRL(controller);
  const stream = P.controlledReadableStream;
  if(!ReadableStreamDefaultControllerCanCloseOrEnqueue(controller)) return false;
  if(!P.started) return false;
  if(IsReadableStreamLocked(stream) && ReadableStreamGetNumReadRequests(stream) > 0) return true;
  const desiredSize = ReadableStreamDefaultControllerGetDesiredSize(controller);
  assert(desiredSize !== null);
  return desiredSize > 0;
}

function ReadableStreamDefaultControllerClearAlgorithms(controller) {
  Object.assign(CTRL(controller), { pullAlgorithm: undefined, cancelAlgorithm: undefined, strategySizeAlgorithm: undefined });
}

function ReadableStreamDefaultControllerClose(controller) {
  const P = CTRL(controller);
  if(!ReadableStreamDefaultControllerCanCloseOrEnqueue(controller)) return;
  const stream = P.controlledReadableStream;
  P.closeRequested = true;
  if(P.queue.length === 0) {
    ReadableStreamDefaultControllerClearAlgorithms(controller);
    ReadableStreamClose(stream);
  }
}

function ReadableStreamDefaultControllerEnqueue(controller, chunk) {
  if(!ReadableStreamDefaultControllerCanCloseOrEnqueue(controller)) return;
  const P = CTRL(controller);
  const stream = P.controlledReadableStream;
  if(IsReadableStreamLocked(stream) && ReadableStreamGetNumReadRequests(stream) > 0) {
    ReadableStreamFulfillReadRequest(stream, chunk, false);
  } else {
    let chunkSize;
    try {
      chunkSize = P.strategySizeAlgorithm(chunk);
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
  const stream = CTRL(controller).controlledReadableStream;
  if(STRM(stream).state != 'readable') return;
  ResetQueue(controller);
  ReadableStreamDefaultControllerClearAlgorithms(controller);
  ReadableStreamError(stream, e);
}

function ReadableStreamDefaultControllerGetDesiredSize(controller) {
  const P = CTRL(controller);
  const state = STRM(P.controlledReadableStream).state;
  if(state == 'errored') return null;
  if(state == 'closed') return 0;
  return P.strategyHWM - P.queueTotalSize;
}

function ReadableStreamDefaultControllerHasBackpressure(controller) {
  return !ReadableStreamDefaultControllerShouldCallPull(controller);
}

function ReadableStreamDefaultControllerCanCloseOrEnqueue(controller) {
  const P = CTRL(controller);
  const state = STRM(P.controlledReadableStream).state;
  if(!P.closeRequested && state == 'readable') return true;
  return false;
}

function SetUpReadableStreamDefaultController(stream, controller, startAlgorithm, pullAlgorithm, cancelAlgorithm, highWaterMark, sizeAlgorithm) {
  const P = CTRL(controller);
  assert(STRM(stream).readableStreamController === undefined);
  P.controlledReadableStream = stream;
  P.queue = undefined;
  P.queueTotalSize = undefined;
  ResetQueue(controller);
  P.started = false;
  P.closeRequested = false;
  P.pullAgain = false;
  P.pulling = false;
  P.strategySizeAlgorithm = sizeAlgorithm;
  P.strategyHWM = highWaterMark;
  P.pullAlgorithm = pullAlgorithm;
  P.cancelAlgorithm = cancelAlgorithm;
  assign(STRM(stream), { readableStreamController: controller });
  const startResult = startAlgorithm();
  uponPromise(
    promiseResolvedWith(startResult),
    () => {
      P.started = true;
      assert(!P.pulling);
      assert(!P.pullAgain);
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
          if(!canceled1) ReadableStreamDefaultControllerEnqueue(STRM(branch1).readableStreamController, chunk1);
          if(!canceled2) ReadableStreamDefaultControllerEnqueue(STRM(branch2).readableStreamController, chunk2);
          reading = false;
          if(readAgain) pullAlgorithm();
        });
      },
      _closeSteps: () => {
        reading = false;
        if(!canceled1) ReadableStreamDefaultControllerClose(STRM(branch1).readableStreamController);
        if(!canceled2) ReadableStreamDefaultControllerClose(STRM(branch2).readableStreamController);
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
  uponRejection(READ(reader).closedPromise, r => {
    ReadableStreamDefaultControllerError(STRM(branch1).readableStreamController, r);
    ReadableStreamDefaultControllerError(STRM(branch2).readableStreamController, r);
    if(!canceled1 || !canceled2) resolveCancelPromise(undefined);
    return null;
  });
  return [branch1, branch2];
}

function ReadableByteStreamTee(stream) {
  assert(IsReadableStream(stream));
  assert(IsReadableByteStreamController(STRM(stream).readableStreamController));
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
    uponRejection(READ(thisReader).closedPromise, r => {
      if(thisReader !== reader) return null;
      ReadableByteStreamControllerError(STRM(branch1).readableStreamController, r);
      ReadableByteStreamControllerError(STRM(branch2).readableStreamController, r);
      if(!canceled1 || !canceled2) resolveCancelPromise(undefined);
      return null;
    });
  }

  function pullWithDefaultReader() {
    if(IsReadableStreamBYOBReader(reader)) {
      assert(READ(reader).readIntoRequests.length === 0);
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
              ReadableByteStreamControllerError(STRM(branch1).readableStreamController, cloneE);
              ReadableByteStreamControllerError(STRM(branch2).readableStreamController, cloneE);
              resolveCancelPromise(ReadableStreamCancel(stream, cloneE));
              return;
            }
          }
          if(!canceled1) ReadableByteStreamControllerEnqueue(STRM(branch1).readableStreamController, chunk1);
          if(!canceled2) ReadableByteStreamControllerEnqueue(STRM(branch2).readableStreamController, chunk2);
          reading = false;
          if(readAgainForBranch1) pull1Algorithm();
          else if(readAgainForBranch2) pull2Algorithm();
        });
      },
      _closeSteps: () => {
        reading = false;
        if(!canceled1) ReadableByteStreamControllerClose(STRM(branch1).readableStreamController);
        if(!canceled2) ReadableByteStreamControllerClose(STRM(branch2).readableStreamController);
        if(CTRL(STRM(branch1).readableStreamController).pendingPullIntos.length > 0) ReadableByteStreamControllerRespond(STRM(branch1).readableStreamController, 0);
        if(CTRL(STRM(branch2).readableStreamController).pendingPullIntos.length > 0) ReadableByteStreamControllerRespond(STRM(branch2).readableStreamController, 0);
        if(!canceled1 || !canceled2) resolveCancelPromise(undefined);
      },
      _errorSteps: () => (reading = false),
    };
    ReadableStreamDefaultReaderRead(reader, readRequest);
  }

  function pullWithBYOBReader(view, forBranch2) {
    if(IsReadableStreamDefaultReader(reader)) {
      assert(READ(reader).readRequests.length === 0);
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
              ReadableByteStreamControllerError(STRM(byobBranch).readableStreamController, cloneE);
              ReadableByteStreamControllerError(STRM(otherBranch).readableStreamController, cloneE);
              resolveCancelPromise(ReadableStreamCancel(stream, cloneE));
              return;
            }
            if(!byobCanceled) ReadableByteStreamControllerRespondWithNewView(STRM(byobBranch).readableStreamController, chunk);
            ReadableByteStreamControllerEnqueue(STRM(otherBranch).readableStreamController, clonedChunk);
          } else if(!byobCanceled) {
            ReadableByteStreamControllerRespondWithNewView(STRM(byobBranch).readableStreamController, chunk);
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
        if(!byobCanceled) ReadableByteStreamControllerClose(STRM(byobBranch).readableStreamController);
        if(!otherCanceled) ReadableByteStreamControllerClose(STRM(otherBranch).readableStreamController);
        if(chunk !== undefined) {
          assert(chunk.byteLength === 0);
          if(!byobCanceled) ReadableByteStreamControllerRespondWithNewView(STRM(byobBranch).readableStreamController, chunk);
          if(!otherCanceled && CTRL(STRM(otherBranch).readableStreamController).pendingPullIntos.length > 0) ReadableByteStreamControllerRespond(STRM(otherBranch).readableStreamController, 0);
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
    const byobRequest = ReadableByteStreamControllerGetBYOBRequest(STRM(branch1).readableStreamController);
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
    const byobRequest = ReadableByteStreamControllerGetBYOBRequest(STRM(branch2).readableStreamController);
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

    if(IsReadableByteStreamController(STRM(this).readableStreamController)) {
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
            ReadableStreamDefaultControllerClose(STRM(stream).readableStreamController);
          } else {
            const value = readResult.value;
            ReadableStreamDefaultControllerEnqueue(STRM(stream).readableStreamController, value);
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
          ReadableStreamDefaultControllerClose(STRM(stream).readableStreamController);
        } else {
          const value = iterResult.value;
          ReadableStreamDefaultControllerEnqueue(STRM(stream).readableStreamController, value);
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

define(ReadableStream.prototype, { [Symbol.toStringTag]: 'ReadableStream' });

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
  assign(STRM(stream), { state: 'readable', reader: undefined, storedError: undefined, disturbed: false });
}

function IsReadableStream(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(STRM(x), 'readableStreamController')) return false;
  return x instanceof ReadableStream;
}

function IsReadableStreamLocked(stream) {
  assert(IsReadableStream(stream));
  if(STRM(stream).reader === undefined) return false;
  return true;
}

function ReadableStreamCancel(stream, reason) {
  STRM(stream).disturbed = true;
  if(STRM(stream).state == 'closed') return promiseResolvedWith(undefined);
  if(STRM(stream).state == 'errored') return promiseRejectedWith(STRM(stream).storedError);
  ReadableStreamClose(stream);
  const reader = STRM(stream).reader;
  if(reader !== undefined && IsReadableStreamBYOBReader(reader)) {
    const R = READ(reader);
    const readIntoRequests = R.readIntoRequests;
    R.readIntoRequests = new SimpleQueue();
    readIntoRequests.forEach(readIntoRequest => readIntoRequest._closeSteps(undefined));
  }

  const sourceCancelPromise = STRM(stream).readableStreamController[CancelSteps](reason);
  return transformPromiseWith(sourceCancelPromise, noop);
}

function ReadableStreamClose(stream) {
  assert(STRM(stream).state == 'readable');
  STRM(stream).state = 'closed';
  const reader = STRM(stream).reader;
  if(reader === undefined) return;
  const R = READ(reader);
  defaultReaderClosedPromiseResolve(reader);
  if(IsReadableStreamDefaultReader(reader)) {
    const readRequests = R.readRequests;
    R.readRequests = new SimpleQueue();
    readRequests.forEach(readRequest => readRequest._closeSteps());
  }
}

function ReadableStreamError(stream, e) {
  assert(IsReadableStream(stream));
  assert(STRM(stream).state == 'readable');
  STRM(stream).state = 'errored';
  STRM(stream).storedError = e;
  const reader = STRM(stream).reader;
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
    assign(this, { _byteLengthQueuingStrategyHighWaterMark: options.highWaterMark });
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

define(ByteLengthQueuingStrategy.prototype, { [Symbol.toStringTag]: 'ByteLengthQueuingStrategy' });

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
    assign(this, { _countQueuingStrategyHighWaterMark: options.highWaterMark });
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

define(CountQueuingStrategy.prototype, { [Symbol.toStringTag]: 'CountQueuingStrategy' });

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
        assert(STRM(STRM(stream).writable).state == 'writable');
        const controller = STRM(stream).transformStreamController;
        if(STRM(stream).backpressure) {
          const backpressureChangePromise = STRM(stream).backpressureChangePromise;
          assert(backpressureChangePromise !== undefined);
          return transformPromiseWith(backpressureChangePromise, () => {
            const writable = STRM(stream).writable;
            const state = STRM(writable).state;
            if(state == 'erroring') throw STRM(writable).storedError;
            assert(state == 'writable');
            return TransformStreamDefaultControllerPerformTransform(controller, chunk);
          });
        }
        return TransformStreamDefaultControllerPerformTransform(controller, chunk);
      }

      function abortAlgorithm(reason) {
        const controller = STRM(stream).transformStreamController;
        const P = CTRL(controller);
        if(P.finishPromise !== undefined) return P.finishPromise;
        const readable = STRM(stream).readable;
        P.finishPromise = newPromise((resolve, reject) => {
          P.finishPromise_resolve = resolve;
          P.finishPromise_reject = reject;
        });
        const cancelPromise = P.cancelAlgorithm(reason);
        TransformStreamDefaultControllerClearAlgorithms(controller);
        uponPromise(
          cancelPromise,
          () => {
            if(STRM(readable).state == 'errored') {
              defaultControllerFinishPromiseReject(controller, STRM(readable).storedError);
            } else {
              ReadableStreamDefaultControllerError(STRM(readable).readableStreamController, reason);
              defaultControllerFinishPromiseResolve(controller);
            }
            return null;
          },
          r => {
            ReadableStreamDefaultControllerError(STRM(readable).readableStreamController, r);
            defaultControllerFinishPromiseReject(controller, r);
            return null;
          },
        );
        return P.finishPromise;
      }

      function closeAlgorithm() {
        const controller = STRM(stream).transformStreamController;
        const P = CTRL(controller);
        if(P.finishPromise !== undefined) return P.finishPromise;
        const readable = STRM(stream).readable;
        P.finishPromise = newPromise((resolve, reject) => {
          P.finishPromise_resolve = resolve;
          P.finishPromise_reject = reject;
        });
        const flushPromise = P.flushAlgorithm();
        TransformStreamDefaultControllerClearAlgorithms(controller);
        uponPromise(
          flushPromise,
          () => {
            if(STRM(readable).state == 'errored') {
              defaultControllerFinishPromiseReject(controller, STRM(readable).storedError);
            } else {
              ReadableStreamDefaultControllerClose(STRM(readable).readableStreamController);
              defaultControllerFinishPromiseResolve(controller);
            }
            return null;
          },
          r => {
            ReadableStreamDefaultControllerError(STRM(readable).readableStreamController, r);
            defaultControllerFinishPromiseReject(controller, r);
            return null;
          },
        );
        return P.finishPromise;
      }

      STRM(this).writable = CreateWritableStream(startAlgorithm, writeAlgorithm, closeAlgorithm, abortAlgorithm, writableHighWaterMark, writableSizeAlgorithm);

      function pullAlgorithm() {
        assert(STRM(stream).backpressure);
        assert(STRM(stream).backpressureChangePromise !== undefined);
        TransformStreamSetBackpressure(stream, false);
        return STRM(stream).backpressureChangePromise;
      }

      function cancelAlgorithm(reason) {
        const controller = STRM(stream).transformStreamController;
        const P = CTRL(controller);
        if(P.finishPromise !== undefined) return P.finishPromise;
        const writable = STRM(stream).writable;
        P.finishPromise = newPromise((resolve, reject) => {
          P.finishPromise_resolve = resolve;
          P.finishPromise_reject = reject;
        });
        const cancelPromise = P.cancelAlgorithm(reason);
        TransformStreamDefaultControllerClearAlgorithms(controller);
        uponPromise(
          cancelPromise,
          () => {
            if(STRM(writable).state == 'errored') {
              defaultControllerFinishPromiseReject(controller, STRM(writable).storedError);
            } else {
              WritableStreamDefaultControllerErrorIfNeeded(STRM(writable).writableStreamController, reason);
              TransformStreamUnblockWrite(stream);
              defaultControllerFinishPromiseResolve(controller);
            }
            return null;
          },
          r => {
            WritableStreamDefaultControllerErrorIfNeeded(STRM(writable).writableStreamController, r);
            TransformStreamUnblockWrite(stream);
            defaultControllerFinishPromiseReject(controller, r);
            return null;
          },
        );
        return P.finishPromise;
      }
      assign(STRM(this), {
        readable: CreateReadableStream(startAlgorithm, pullAlgorithm, cancelAlgorithm, readableHighWaterMark, readableSizeAlgorithm),
        backpressure: undefined,
        backpressureChangePromise: undefined,
        backpressureChangePromise_resolve: undefined,
      });
      TransformStreamSetBackpressure(this, true);
      assign(STRM(this), { transformStreamController: undefined });
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

    startPromise_resolve(transformer.start !== undefined ? transformer.start(STRM(this).transformStreamController) : undefined);
  }
  /**
   * The readable side of the transform stream.
   */
  get readable() {
    if(!IsTransformStream(this)) throw streamBrandCheckException3('readable');
    return STRM(this).readable;
  }

  /**
   * The writable side of the transform stream.
   */
  get writable() {
    if(!IsTransformStream(this)) throw streamBrandCheckException3('writable');
    return STRM(this).writable;
  }
}

/*Object.defineProperties(TransformStream.prototype, {
  readable: { enumerable: true },
  writable: { enumerable: true },
});*/

define(TransformStream.prototype, { [Symbol.toStringTag]: 'TransformStream' });

function IsTransformStream(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(STRM(x), 'transformStreamController')) return false;
  return x instanceof TransformStream;
}

function TransformStreamError(stream, e) {
  ReadableStreamDefaultControllerError(STRM(STRM(stream).readable).readableStreamController, e);
  TransformStreamErrorWritableAndUnblockWrite(stream, e);
}

function TransformStreamErrorWritableAndUnblockWrite(stream, e) {
  TransformStreamDefaultControllerClearAlgorithms(STRM(stream).transformStreamController);
  WritableStreamDefaultControllerErrorIfNeeded(STRM(STRM(stream).writable).writableStreamController, e);
  TransformStreamUnblockWrite(stream);
}

function TransformStreamUnblockWrite(stream) {
  if(STRM(stream).backpressure) TransformStreamSetBackpressure(stream, false);
}

function TransformStreamSetBackpressure(stream, backpressure) {
  assert(STRM(stream).backpressure !== backpressure);
  if(STRM(stream).backpressureChangePromise !== undefined) STRM(stream).backpressureChangePromise_resolve();
  STRM(stream).backpressureChangePromise = newPromise(resolve => (STRM(stream).backpressureChangePromise_resolve = resolve));
  STRM(stream).backpressure = backpressure;
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
    const readableController = STRM(STRM(CTRL(this).controlledTransformStream).readable).readableStreamController;
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

    TransformStreamError(CTRL(this).controlledTransformStream, reason);
  }

  /**
   * Closes the readable side and errors the writable side of the controlled transform stream. This is useful when the
   * transformer only needs to consume a portion of the chunks written to the writable side.
   */
  terminate() {
    if(!IsTransformStreamDefaultController(this)) throw defaultControllerBrandCheckException3('terminate');

    const stream = CTRL(this).controlledTransformStream;
    const readableController = STRM(STRM(stream).readable).readableStreamController;
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

define(TransformStreamDefaultController.prototype, { [Symbol.toStringTag]: 'TransformStreamDefaultController' });

function IsTransformStreamDefaultController(x) {
  if(!typeIsObject(x)) return false;
  if(!Object.prototype.hasOwnProperty.call(CTRL(x), 'controlledTransformStream')) return false;
  return x instanceof TransformStreamDefaultController;
}

function TransformStreamDefaultControllerClearAlgorithms(controller) {
  Object.assign(CTRL(controller), { transformAlgorithm: undefined, flushAlgorithm: undefined, cancelAlgorithm: undefined });
}

function SetUpTransformStreamDefaultController(stream, controller, transformAlgorithm, flushAlgorithm, cancelAlgorithm) {
  assert(IsTransformStream(stream));
  assert(STRM(stream).transformStreamController === undefined);
  const P = CTRL(controller);
  P.controlledTransformStream = stream;
  STRM(stream).transformStreamController = controller;
  P.transformAlgorithm = transformAlgorithm;
  P.flushAlgorithm = flushAlgorithm;
  P.cancelAlgorithm = cancelAlgorithm;
}

function TransformStreamDefaultControllerEnqueue(controller, chunk) {
  const stream = CTRL(controller).controlledTransformStream;
  const readableController = STRM(STRM(stream).readable).readableStreamController;
  if(!ReadableStreamDefaultControllerCanCloseOrEnqueue(readableController)) throw new TypeError('Readable side is not in a state that permits enqueue');
  try {
    ReadableStreamDefaultControllerEnqueue(readableController, chunk);
  } catch(e) {
    TransformStreamErrorWritableAndUnblockWrite(stream, e);
    throw STRM(STRM(stream).readable).storedError;
  }

  const backpressure = ReadableStreamDefaultControllerHasBackpressure(readableController);
  if(backpressure !== STRM(stream).backpressure) {
    assert(backpressure);
    TransformStreamSetBackpressure(stream, true);
  }
}

function TransformStreamDefaultControllerPerformTransform(controller, chunk) {
  const P = CTRL(controller);

  const transformPromise = P.transformAlgorithm(chunk);
  return transformPromiseWith(transformPromise, undefined, r => {
    TransformStreamError(P.controlledTransformStream, r);
    throw r;
  });
}

function defaultControllerBrandCheckException3(name) {
  return new TypeError(`TransformStreamDefaultController.prototype.${name} can only be used on a TransformStreamDefaultController`);
}

function defaultControllerFinishPromiseResolve(controller) {
  const P = CTRL(controller);
  if(P.finishPromise_resolve === undefined) return;
  P.finishPromise_resolve();
  P.finishPromise_resolve = undefined;
  P.finishPromise_reject = undefined;
}

function defaultControllerFinishPromiseReject(controller, reason) {
  const P = CTRL(controller);
  if(P.finishPromise_reject === undefined) return;
  setPromiseIsHandledToTrue(P.finishPromise);
  P.finishPromise_reject(reason);
  P.finishPromise_resolve = undefined;
  P.finishPromise_reject = undefined;
}

function streamBrandCheckException3(name) {
  return new TypeError(`TransformStream.prototype.${name} can only be used on a TransformStream`);
}
