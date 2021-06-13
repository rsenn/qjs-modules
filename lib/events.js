const indexOf = (haystack, needle) => Array.prototype.indexOf.call(haystack, needle);

export class EventEmitter {
  constructor() {
    this.events = {};
  }

  on(event, listener) {
    const { events } = this;
    if(!Array.isArray(events[event])) events[event] = [];
    events[event].push(listener);
  }

  removeListener(event, listener) {
    const { events } = this;
    const handlers = events[event];
    if(Array.isArray(handlers)) {
      let idx = indexOf(handlers, listener);
      if(idx > -1) handlers.splice(idx, 1);
    }
  }

  emit(event, ...args) {
    const { events } = this;
    const handlers = events[event];
    if(Array.isArray(handlers)) {
      let listeners = handlers.slice();
      let { length } = listeners;
      for(let i = 0; i < length; i++) listeners[i].apply(this, args);
    }
  }

  once(event, listener) {
    let callback;
    callback = (...args) => {
      this.removeListener(event, callback);
      listener.apply(this, args);
    };
    this.on(event, callback);
  }
}

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
    // Also fire if this EventTarget has an `on${EVENT_TYPE}` property
    // that's a function
    if(typeof this[`on${type}`] === 'function') this[`on${type}`](event);
  }
}

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
