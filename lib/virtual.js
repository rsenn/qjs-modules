const STORE = Symbol('store');
const TYPE = Symbol('type');

/**
 * Pure JavaScript implementation of the VirtualProperties class.
 * Compatible with the quickjs-virtual.c native module specification.
 */
export class VirtualProperties {
  constructor(target) {
    if(target === undefined || target === null) {
      this[STORE] = {};
      this[TYPE] = 'object';
    } else if(Array.isArray(target)) {
      this[STORE] = target;
      this[TYPE] = 'array';
    } else if(target instanceof Map) {
      this[STORE] = target;
      this[TYPE] = 'map';
    } else if(typeof target === 'object') {
      this[STORE] = target;
      this[TYPE] = 'object';
    } else throw new TypeError('Invalid backing store');
  }

  has(key) {
    const store = this[STORE];
    const type = this[TYPE];
    if(type === 'map') return store.has(key);
    if(type === 'array') {
      const idx = typeof key === 'string' ? Number(key) : key;
      return Number.isInteger(idx) && idx >= 0 && idx < store.length && idx in store;
    }
    return key in store;
  }

  get(key) {
    const store = this[STORE];
    if(this[TYPE] === 'map') return store.get(key);
    return store[key];
  }

  set(key, value) {
    const store = this[STORE];
    if(this[TYPE] === 'map') store.set(key, value);
    else store[key] = value;
    return 1; /* C implementation returns int32 (1 for success) */
  }

  delete(key) {
    const store = this[STORE];
    if(this[TYPE] === 'map') return store.delete(key);
    return delete store[key];
  }

  keys(flags) {
    const store = this[STORE];
    if(this[TYPE] === 'map') return Array.from(store.keys());
    if(this[TYPE] === 'array') return Array.from(store.keys()).map(String);
    return Object.keys(store);
  }

  static array(target) {
    const vp = Object.create(VirtualProperties.prototype);
    vp[STORE] = target !== undefined ? target : [];
    vp[TYPE] = 'array';
    return vp;
  }

  static map(target) {
    const vp = Object.create(VirtualProperties.prototype);
    vp[STORE] = target !== undefined ? target : new Map();
    vp[TYPE] = 'map';
    return vp;
  }

  static object(target) {
    const vp = Object.create(VirtualProperties.prototype);
    vp[STORE] = target !== undefined ? target : {};
    vp[TYPE] = 'object';
    return vp;
  }

  static from(target) {
    if(Array.isArray(target)) return VirtualProperties.array(target);
    if(target instanceof Map) return VirtualProperties.map(target);
    if(target && typeof target === 'object') return VirtualProperties.object(target);
    return VirtualProperties.object({});
  }
}

Object.defineProperty(VirtualProperties.prototype, Symbol.toStringTag, {
  value: 'VirtualProperties',
  configurable: true,
});

export default VirtualProperties;
