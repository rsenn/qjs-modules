export class EventTarget {
  #listeners = {};
  #wrappers = new WeakMap(); /* listener -> [{ type, wrapper }], for once and signal */

  /**
   * Sets up a function that will be called whenever the specified event is delivered to the target.
   *
   * @param {string}   type       Event type
   * @param {Function} listener   Callback, called with this = the target
   * @param {Object}   [options]  { once, signal }, as in the DOM
   */
  addEventListener(type, listener, options) {
    checkType(type);
    checkListener(listener);

    const { once, signal } = typeof options == 'object' && options !== null ? options : {};
    let actual = listener;

    if(signal?.aborted) return;

    if(once || signal) {
      actual = function(event) {
        if(once) this.removeEventListener(type, listener);

        return listener.call(this, event);
      };

      (this.#wrappers.get(listener) ?? this.#wrappers.set(listener, []).get(listener)).push({ type, wrapper: actual });
    }

    (this.#listeners[type] ??= []).push(actual);

    if(signal) signal.addEventListener('abort', () => this.removeEventListener(type, listener), { once: true });
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

    const wrappers = this.#wrappers.get(listener);
    const i = wrappers ? wrappers.findIndex(w => w.type == type) : -1;

    if(i >= 0) {
      removeAll(this.#listeners[type], wrappers[i].wrapper);
      wrappers.splice(i, 1);

      if(wrappers.length == 0) this.#wrappers.delete(listener);
    } else removeAll(this.#listeners[type], listener);

    if(this.#listeners[type].length == 0) delete this.#listeners[type];
  }

  /**
   * Sends an Event to the object, (synchronously) invoking the affected event listeners in
   * the appropriate order. The normal event processing rules (including the capturing and
   * optional bubbling phase) also apply to events dispatched manually with dispatchEvent().
   *
   * @param  {Event} event  Event object
   * @return {boolean}      false when a listener called event.preventDefault()
   */
  dispatchEvent(event) {
    const { type } = event;
    /* an Event of lib/dom.js has a read-only target and these setters for it */
    const internal = typeof event?.setTarget == 'function';

    if(internal) {
      event.setTarget(this);
      event.setCurrentTarget(this);
      event.setEventPhase(2);
    } else if(event != null && typeof event == 'object' && ('target' in event || 'detail' in event)) event.target = this;

    if(type in this.#listeners) for(const listener of [...this.#listeners[type]]) listener.call(this, event);

    /* also fire an on<type> property that is a function, whether or not a
       listener was registered: the only dispatch path for a subclass that
       never calls addEventListener() for type */
    if(typeof this['on' + type] == 'function') this['on' + type](event);

    if(internal) {
      event.setCurrentTarget(null);
      event.setEventPhase(0);
    }

    return !event.defaultPrevented;
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
