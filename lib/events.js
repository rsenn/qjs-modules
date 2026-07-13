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

export class EventEmitter {
  #listeners = {};

  on(type, listener) {
    checkType(type);
    checkListener(listener);

    (this.#listeners[type] ??= []).push(listener);
  }

  removeListener(type, listener) {
    checkType(type);
    checkListener(listener);

    if(!(type in this.#listeners)) return;

    const handlers = this.#listeners[type];

    removeAll(handlers, listener);

    if(handlers.length == 0) delete this.#listeners[type];
  }

  removeAllListeners(type) {
    if(!type) {
      for(let key in events) delete events[key];
      return;
    }

    checkType(type);

    if(!(type in this.#listeners)) return;

    delete this.#listeners[type];
  }

  rawListeners(type) {
    checkType(type);

    if(!(type in this.#listeners)) return;

    return [...this.#listeners[type]];
  }

  emit(type, ...args) {
    if(!(type in this.#listeners)) return;

    for(let handler of this.#listeners[type]) handler.call(this, type, ...args);
  }

  once(type, listener) {
    const callback = (...args) => {
      this.removeListener(type, callback);
      listener.apply(this, args);
    };

    callback.listener = listener;

    this.on(type, callback);
  }
}

EventEmitter.prototype[Symbol.toStringTag] = 'EventEmitter';

/** Checks event type argument */
function checkType(type) {
  if(typeof type != 'string') throw new TypeError('`type` must be a string');
}

/** Checks event listener argument */
function checkListener(listener) {
  if(typeof listener != 'function') throw new TypeError('`listener` must be a function');
}

/** Removes all elements that match @param elem from an array */
function removeAll(arr, elem) {
  for(let i = arr.length; i >= 0; i--) if(arr[i] === elem) arr.splice(i, 1);
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
  return waitOne(emitter, events);
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
