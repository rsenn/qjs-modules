const indexOf = (haystack, needle) => Array.prototype.indexOf.call(haystack, needle);

const getHandlers = (
  (map = new WeakMap()) =>
  obj => (map.has(obj) || map.set(obj, {}), map.get(obj))
)();

export class EventEmitter {
  on(event, listener) {
    const events = getHandlers(this);

    if(!Array.isArray(events[event])) events[event] = [];
    events[event].push(listener);
  }

  removeListener(event, listener) {
    const events = getHandlers(this);

    if(Array.isArray(events[event])) {
      const idx = indexOf(events[event], listener);

      if(idx > -1) {
        events[event].splice(idx, 1);
        if(events[event].length == 0) delete events[event];
      }
    }
  }

  removeAllListeners(event) {
    const events = getHandlers(this);

    if(event) {
      if(Array.isArray(events[event])) delete events[event];
    } else {
      for(let key in events) delete events[key];
    }
  }

  rawListeners(event) {
    const events = getHandlers(this);

    if(Array.isArray(events[event])) return [...events[event]];
  }

  emit(event, ...args) {
    const events = getHandlers(this);

    if(Array.isArray(events[event])) for(let handler of events[event]) handler.apply(this, args);
  }

  once(event, listener) {
    const callback = (...args) => {
      this.removeListener(event, callback);
      listener.apply(this, args);
    };

    callback.listener = listener;
    this.on(event, callback);
  }
}

EventEmitter.prototype[Symbol.toStringTag] = 'EventEmitter';

const PRIVATE = Symbol('EventTarget');

export class EventTarget {
  constructor() {
    Object.defineProperty(this, PRIVATE, {
      value: {
        listeners: new Map(),
      },
    });
  }

  #typedListeners(type) {
    const { listeners } = this[PRIVATE];

    if(!listeners.has(type)) listeners.set(type, []);
    return listeners.get(type);
  }

  addEventListener(type, listener) {
    if(typeof type !== 'string') throw new TypeError('`type` must be a string');
    if(typeof listener !== 'function') throw new TypeError('`listener` must be a function');
    this.#typedListeners(type).push(listener);
  }

  removeEventListener(type, listener) {
    if(typeof type !== 'string') throw new TypeError('`type` must be a string');
    if(typeof listener !== 'function') throw new TypeError('`listener` must be a function');

    const typedListeners = this.#typedListeners(type);

    for(let i = typedListeners.length; i >= 0; i--) if(typedListeners[i] === listener) typedListeners.splice(i, 1);
  }

  dispatchEvent(type, event) {
    const queue = [],
      typedListeners = this.#typedListeners(type);

    if('target' in event || 'detail' in event) event.target = this;

    for(let i = 0; i < typedListeners.length; i++) queue[i] = typedListeners[i];

    for(let listener of queue) listener(event);

    // Also fire if this EventTarget has an `on${EVENT_TYPE}` property that's a function
    if(typeof this[`on${type}`] === 'function') this[`on${type}`](event);
  }
}

EventTarget.prototype[Symbol.toStringTag] = 'EventTarget';

const getMethods = obj =>
  Object.getOwnPropertyNames(obj)
    .filter(n => n != 'constructor')
    .reduce((acc, n) => ({ ...acc, [n]: obj[n] }), {});

export const eventify = self => {
  let methods = getMethods(EventEmitter.prototype);

  console.log(methods);

  return Object.assign(self, {
    events: {},
    ...methods,
  });
};

export default EventEmitter;
