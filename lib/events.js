export class EventTarget {
  #listeners = {};

  /**
   * Sets up a function that will be called whenever the specified event is delivered to the target.
   *
   * @param {string}   type     Event type
   * @param {Function} listener Callback
   */
  addEventListener(type, listener) {
    checkType(type);
    checkListener(listener);

    (this.#listeners[type] ??= []).push(listener);
  }

  /**
   * Removes an event listener previously registered with EventTarget.addEventListener()
   * from the target. The event listener to be removed is identified using a combination
   * of the event type, the event listener function itself, and various optional options
   * that may affect the matching process; see Matching event
   *
   * @param {string}   type     Event type
   * @param {Function} listener Callback
   */
  removeEventListener(type, listener) {
    checkType(type);
    checkListener(listener);

    if(!(type in this.#listeners)) return;

    removeAll(this.#listeners[type], listener);

    if(this.#listeners[type].length == 0) delete this.#listeners[type];
  }

  /**
   * Sends an Event to the object, (synchronously) invoking the affected event listeners in
   * the appropriate order. The normal event processing rules (including the capturing and
   * optional bubbling phase) also apply to events dispatched manually with dispatchEvent().
   *
   * @param  {Event} event  Event object
   */
  dispatchEvent(event) {
    const { type } = event;

    if(event != null && typeof event == 'object' && ('target' in event || 'detail' in event)) event.target = this;

    if(type in this.#listeners) {
      const queue = [...this.#listeners[type]];

      for(let listener of queue) listener(event);
    }

    /**
     * Also fire if this EventTarget has an `on${EVENT_TYPE}` property that's a function -
     * checked unconditionally (not just when a listener was also registered for `type`),
     * since this is the only dispatch path for an EventTarget subclass with no
     * addEventListener() call for `type`.
     */
    if(typeof this['on' + type] == 'function') this['on' + type](event);
  }
}

EventTarget.prototype[Symbol.toStringTag] = 'EventTarget';

/* EventEmitter: Node's `events.EventEmitter` (also Bun's, Deno's).
 *
 * ```js
 * const e = new EventEmitter();
 * e.on('x', (a, b) => {});    // returns e; handlers get the emit() arguments only
 * e.emit('x', 1, 2);          // true if a handler ran
 * ```
 *
 * `emit('error')` without a listener throws the argument (or an Error).
 * state lives in lazily created `_events` / `_maxListeners`, so an object that
 * merely inherits from EventEmitter.prototype (such as `process`) works.
 */
const kMax = 10;

function state(self) {
  if(!Object.prototype.hasOwnProperty.call(self, '_events'))
    Object.defineProperty(self, '_events', { value: Object.create(null), writable: true, enumerable: false, configurable: true });

  return self._events;
}

function add(self, type, listener, prepend, once) {
  checkListener(listener);

  const events = state(self);
  let fn = listener;

  if(once) {
    fn = function(...args) {
      self.removeListener(type, fn);
      return listener.apply(this, args);
    };
    fn.listener = listener;
  }

  if(events.newListener !== undefined) self.emit('newListener', type, listener);

  const list = (events[type] ??= []);

  prepend ? list.unshift(fn) : list.push(fn);

  const max = self.getMaxListeners();

  if(max > 0 && list.length > max && !list.warned) {
    list.warned = true;
    console.error(`MaxListenersExceededWarning: ${list.length} ${String(type)} listeners added. MaxListeners is ${max}`);
  }

  return self;
}

export class EventEmitter {
  static defaultMaxListeners = kMax;

  static listenerCount(emitter, type) {
    return emitter.listenerCount(type);
  }

  on(type, listener) {
    return add(this, type, listener, false, false);
  }

  addListener(type, listener) {
    return add(this, type, listener, false, false);
  }

  prependListener(type, listener) {
    return add(this, type, listener, true, false);
  }

  once(type, listener) {
    return add(this, type, listener, false, true);
  }

  prependOnceListener(type, listener) {
    return add(this, type, listener, true, true);
  }

  removeListener(type, listener) {
    checkListener(listener);

    const events = state(this);
    const list = events[type];

    if(!list) return this;

    for(let i = list.length - 1; i >= 0; i--) {
      if(list[i] === listener || list[i].listener === listener) {
        list.splice(i, 1);
        if(!list.length) delete events[type];
        if(events.removeListener !== undefined) this.emit('removeListener', type, listener);
        break;
      }
    }

    return this;
  }

  off(type, listener) {
    return this.removeListener(type, listener);
  }

  removeAllListeners(type) {
    const events = state(this);

    if(type === undefined) {
      for(const key of Reflect.ownKeys(events)) if(key !== 'removeListener') this.removeAllListeners(key);
      this.removeAllListeners('removeListener');
    } else if(events[type]) {
      for(const fn of [...events[type]].reverse()) this.removeListener(type, fn.listener ?? fn);
      delete events[type];
    }

    return this;
  }

  listeners(type) {
    return (state(this)[type] ?? []).map(fn => fn.listener ?? fn);
  }

  rawListeners(type) {
    return [...(state(this)[type] ?? [])];
  }

  listenerCount(type) {
    return (state(this)[type] ?? []).length;
  }

  eventNames() {
    return Reflect.ownKeys(state(this));
  }

  setMaxListeners(n) {
    if(typeof n != 'number' || n < 0 || Number.isNaN(n)) throw new RangeError('The value of "n" is out of range. It must be a non-negative number.');
    Object.defineProperty(this, '_maxListeners', { value: n, writable: true, enumerable: false, configurable: true });
    return this;
  }

  getMaxListeners() {
    return this._maxListeners ?? EventEmitter.defaultMaxListeners;
  }

  emit(type, ...args) {
    const list = state(this)[type];

    if(!list) {
      if(type === 'error') {
        const err = args[0];

        if(err instanceof Error) throw err;

        const e = new Error(`Unhandled error. (${typeof err == 'string' ? `'${err}'` : String(err)})`);

        e.context = err;
        throw e;
      }

      return false;
    }

    for(const fn of [...list]) fn.apply(this, args);

    return true;
  }
}

EventEmitter.prototype[Symbol.toStringTag] = 'EventEmitter';

/** Checks event type argument */
function checkType(type) {
  if(typeof type != 'string') throw new TypeError('`type` must be a string');
}

/** Removes all elements that match @param elem from an array */
function removeAll(arr, elem) {
  for(let i = arr.length; i >= 0; i--) if(arr[i] === elem) arr.splice(i, 1);
}

/** Checks event listener argument */
function checkListener(listener) {
  if(typeof listener != 'function') throw new TypeError('The "listener" argument must be of type function');
}

/**
 * Wait for one of @param events to happen
 * 
 * @param  {EventTarget} emitter  Object that receives the event
 * @param  {...string}   events   Event types

 * @return {Promise<Event>} Promise that resolves with the event that happened
 */
export function once(emitter, ...events) {
  if(events.length == 1 && Array.isArray(events[0])) events = events[0];

  if(typeof emitter.addEventListener == 'function') return waitOne(emitter, events);

  /* EventEmitter: Node resolves with the array of emit() arguments, rejects on 'error' */
  return new Promise((resolve, reject) => {
    const [type] = events;
    const onError = err => {
      emitter.removeListener(type, onEvent);
      reject(err);
    };
    const onEvent = (...args) => {
      if(type !== 'error') emitter.removeListener('error', onError);
      resolve(args);
    };

    emitter.once(type, onEvent);
    if(type !== 'error') emitter.once('error', onError);
  });
}

/**
 * Wait for one of @param events to happen
 *
 * @param  {EventTarget} emitter  Object that receives the event
 * @param  {Array}       events   Event types
 * @param  {Object}      options  Options passed to addEventListener
 *
 * @return {Promise<Event>} Promise that resolves with the event that happened
 */
export function waitOne(emitter, events, options = { passive: true, capture: false }) {
  return new Promise(resolve => {
    events.forEach(type => emitter.addEventListener(type, handler, options));

    function handler(event) {
      events.forEach(type => emitter.removeEventListener(type, handler, options));
      resolve(event);
    }
  });
}

export function EventTargetProperties(properties = []) {
  const map = new WeakMap();
  const eventHandlers = key => ((value = map.get(key)) || map.set(key, (value = {})), value);

  const ctor = class extends EventTarget {
    constructor() {
      super();
    }
  };

  for(const type of properties) {
    Object.defineProperty(ctor.prototype, 'on' + type, {
      get() {
        return eventHandlers(this)[type];
      },
      set(value) {
        /* Just store it - dispatchEvent()'s own `this['on' + type]` fallback
           is what actually invokes it. Also registering it via
           addEventListener() here would fire it a second time per event. */
        eventHandlers(this)[type] = value;
      },
    });
  }

  return ctor;
}

export const defaultMaxListeners = EventEmitter.defaultMaxListeners;

/* getEventListeners(emitter, type): listeners of an EventEmitter (EventTarget has no way to list them) */
export function getEventListeners(emitter, type) {
  return typeof emitter.listeners == 'function' ? emitter.listeners(type) : [];
}

export const listenerCount = (emitter, type) => emitter.listenerCount(type);

export function getMaxListeners(emitter) {
  return emitter.getMaxListeners();
}

export function setMaxListeners(n = EventEmitter.defaultMaxListeners, ...targets) {
  if(!targets.length) EventEmitter.defaultMaxListeners = n;
  for(const t of targets) t.setMaxListeners?.(n);
}

/* on(emitter, type): async iterator over the emit() argument arrays of `type` */
export function on(emitter, type, { signal } = {}) {
  const queue = [], waiting = [];
  let error = null, done = false;
  const handler = (...args) => (waiting.length ? waiting.shift().resolve({ value: args, done: false }) : queue.push(args));
  const onError = err => {
    error = err;
    waiting.length ? waiting.shift().reject(err) : (done = true);
  };
  const finish = () => {
    emitter.removeListener(type, handler);
    emitter.removeListener('error', onError);
    done = true;
    for(const w of waiting.splice(0)) w.resolve({ value: undefined, done: true });
  };

  emitter.on(type, handler);
  if(type !== 'error') emitter.on('error', onError);
  signal?.addEventListener('abort', () => onError(Object.assign(new Error('The operation was aborted'), { name: 'AbortError', code: 'ABORT_ERR' })), { once: true });

  return {
    next() {
      if(queue.length) return Promise.resolve({ value: queue.shift(), done: false });
      if(error) {
        const e = error;

        error = null;
        finish();
        return Promise.reject(e);
      }
      if(done) return Promise.resolve({ value: undefined, done: true });
      return new Promise((resolve, reject) => waiting.push({ resolve, reject }));
    },
    return() {
      finish();
      return Promise.resolve({ value: undefined, done: true });
    },
    throw(err) {
      finish();
      return Promise.reject(err);
    },
    [Symbol.asyncIterator]() {
      return this;
    },
  };
}

export default EventEmitter;
