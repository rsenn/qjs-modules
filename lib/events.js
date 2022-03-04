const indexOf = (haystack, needle) => Array.prototype.indexOf.call(haystack, needle);

export class EventEmitter {
  #events = {};

  constructor() {}

  on(event, listener) {
    if(!Array.isArray(this.#events[event])) this.#events[event] = [];
    this.#events[event].push(listener);
  }

  removeListener(event, listener) {
    const handlers = this.#events[event];
    if(Array.isArray(handlers)) {
      const idx = indexOf(handlers, listener);
      if(idx > -1) {
        handlers.splice(idx, 1);
        if(handlers.length == 0) delete this.#events[event];
      }
    }
  }

  removeAllListeners(event) {
    if(event) {
      if(Array.isArray(this.#events[event])) delete this.#events[event];
    } else {
      this.#events = {};
    }
  }

  rawListeners(event) {
    if(Array.isArray(this.#events[event])) return [...this.#events[event]];
  }

  emit(event, ...args) {
    const handlers = this.#events[event];
    if(Array.isArray(handlers)) for(let handler of handlers) handler.apply(this, args);
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
        listeners: new Map()
      }
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
    for(let i = typedListeners.length; i >= 0; i--)
      if(typedListeners[i] === listener) typedListeners.splice(i, 1);
  }

  dispatchEvent(type, event) {
    const typedListeners = this.#typedListeners(type);
    if('target' in event || 'detail' in event) event.target = this;
    const queue = [];
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
    ...methods
  });
};

export default { EventEmitter, EventTarget, eventify };
