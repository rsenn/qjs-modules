export function quote(str, q = '"') {
  return q + str.replace(new RegExp(q, 'g'), '\\' + q) + q;
}

export function escape(str, pred = (codePoint) => codePoint < 32 || codePoint > 0xff) {
  let s = '';
  for (let i = 0; i < str.length; i++) {
    let code = str.codePointAt(i);
    if (!pred(code)) {
      s += str[i];
      continue;
    }

    if (code == 0) s += `\\0`;
    else if (code == 10) s += `\\n`;
    else if (code == 13) s += `\\r`;
    else if (code == 9) s += `\\t`;
    else if (code <= 0xff) s += `\\x${('0' + code.toString(16)).slice(-2)}`;
    else s += `\\u${('0000' + code.toString(16)).slice(-4)}`;
  }
  return s;
}

export function isObject(obj) {
  return !(obj === null) && { object: obj, function: obj }[typeof obj];
}

export function memoize(fn, cache = {}) {
  let [get, set] = getset(cache);
  return define(
    function Memoize(n, ...rest) {
      let r;
      if ((r = get(n))) return r;
      r = fn.call(this, n, ...rest);
      set(n, r);
      return r;
    },
    { cache, get, set }
  );
}

export function inherits(ctor, superCtor) {
  if (superCtor) {
    ctor.super_ = superCtor;
    delete ctor.prototype;
    ctor.prototype = Object.create(superCtor.prototype, {
      constructor: {
        value: ctor,
        enumerable: false,
        writable: true,
        configurable: true,
      },
    });
  }
}

export function getset(target, ...args) {
  let ret = [];
  if (typeof target == 'function') {
    ret = [target, typeof args[0] == 'function' ? args[0] : target];
  } else if (hasGetSet(target)) {
    if (target.get === target.set) {
      const GetSet = (...args) => target.set(...args);
      ret = [GetSet, GetSet];
    } else ret = [(key) => target.get(key), (key, value) => target.set(key, value)];
  } else if (isObject(target)) {
    ret = [(key) => target[key], (key, value) => (target[key] = value)];
  } else {
    throw new TypeError(`getset unknown argument type '${typeof target}'`);
  }
  if (args.length) {
    let [get, set] = ret;
    ret = [() => get(...args), (value) => set(...args, value)];
  }
  return ret;
}

export function modifier(...args) {
  let gs = gettersetter(...args);
  return (fn) => {
    let value = gs();
    return fn(value, (newValue) => gs(newValue));
  };
}

export function getter(target, ...args) {
  if (isObject(target) && typeof target.get == 'function') return () => target.get(...args);
  let ret;
  if (typeof target == 'function') {
    ret = target;
  } else if (hasGetSet(target)) {
    ret = (key) => target.get(key);
  } else if (isObject(target)) {
    ret = (key) => target[key];
  } else {
    throw new TypeError(`getter unknown argument type '${typeof target}'`);
  }
  if (args.length) {
    let get = ret;
    ret = () => get(...args);
  }
  return ret;
}

export function setter(target, ...args) {
  if (isObject(target) && typeof target.set == 'function') return (value) => target.set(...args, value);
  let ret;
  if (typeof target == 'function') {
    ret = target;
  } else if (hasGetSet(target)) {
    ret = (key, value) => target.set(key, value);
  } else if (isObject(target)) {
    ret = (key, value) => (target[key] = value);
  } else {
    throw new TypeError(`setter unknown argument type '${typeof target}'`);
  }
  if (args.length) {
    let set = ret;
    ret = (value) => set(...args, value);
  }
  return ret;
}

export function gettersetter(target, ...args) {
  let fn;
  if (isObject(target) && typeof target.receiver == 'function') return (...args2) => target.receiver(...args, ...args2);
  if (typeof target == 'function') {
    if (typeof args[0] == 'function' && args[0] !== target) {
      let setter = args.shift();
      fn = (...args) => (args.length == 0 ? target() : setter(...args));
    } else fn = target;
  } else if (hasGetSet(target)) {
    if (target.get === target.set) fn = (...args) => target.set(...args);
    else fn = (...args) => (args.length < 2 ? target.get(...args) : target.set(...args));
  } else if (isObject(target)) {
    fn = (...args) => {
      const [key, value] = args;
      if (args.length == 1) return target[key];
      target[key] = value;
    };
  } else {
    throw new TypeError(`gettersetter unknown argument type '${typeof target}'`);
  }
  if (fn !== target) define(fn, { receiver: target });
  if (args.length) return (...args2) => fn(...args, ...args2);
  return fn;
}

export function hasGetSet(obj) {
  return isObject(obj) && ['get', 'set'].every((m) => typeof obj[m] == 'function');
}

export function mapObject(target) {
  let obj;
  if (hasGetSet(target.receiver)) return target.receiver;
  if (hasGetSet(target)) obj = target;
  else if (typeof target == 'function') obj = { get: target, set: target };
  else if (isObject(target))
    obj = {
      set: (key, value) => (target[key] = value),
      get: (key) => target[key],
    };
  if (obj !== target) define(obj, { receiver: target });
  return obj;
}

export function once(fn, thisArg, memoFn) {
  let ret,
    ran = false;

  return function (...args) {
    if (!ran) {
      ran = true;
      ret = fn.apply(thisArg || this, args);
    } else if (typeof memoFn == 'function') {
      ret = memoFn(ret);
    }
    return ret;
  };
}

export function waitFor(ms) {
  return new Promise((resolve) => timers.setTimeout(resolve, ms));
}

export function define(obj, ...args) {
  for (let props of args) {
    let desc = {};
    let syms = Object.getOwnPropertySymbols(props).concat(Object.getOwnPropertyNames(props));
    for (let prop of syms) {
      if (prop == '__proto__') {
        Object.setPrototypeOf(obj, props[prop]);
        continue;
      }
      const { value, ...rest } = (desc[prop] = Object.getOwnPropertyDescriptor(props, prop));
      desc[prop].enumerable = false;
      if (typeof value == 'function') desc[prop].writable = false;
    }
    Object.defineProperties(obj, desc);
  }
  return obj;
}

export function weakAssign(obj, ...args) {
  let desc = {};
  for (let other of args) {
    let otherDesc = Object.getOwnPropertyDescriptors(other);
    for (let key in otherDesc) if (!(key in obj) && desc[key] === undefined && otherDesc[key] !== undefined) desc[key] = otherDesc[key];
  }
  return Object.defineProperties(obj, desc);
}

export function getPrototypeChain(obj, limit = -1, start = 0) {
  let i = -1,
    ret = [];
  do {
    if (i >= start && (limit == -1 || i < start + limit)) ret.push(obj);
    if (obj === Object.prototype || obj.constructor === Object) break;
    ++i;
  } while ((obj = obj.__proto__ || Object.getPrototypeOf(obj)));
  return ret;
}

export function getConstructorChain(obj, ...range) {
  let ret = [];
  pushUnique(ret, obj.constructor);
  for (let proto of misc.getPrototypeChain(obj, ...range)) pushUnique(ret, proto.constructor);
  return ret;
}

export function hasPrototype(obj, proto) {
  return misc.getPrototypeChain(obj).indexOf(proto) != -1;
}

export function filter(seq, pred, thisArg) {
  if (isObject(pred) && pred instanceof RegExp) {
    let re = pred;
    pred = (el, i) => re.test(el);
  }
  let r = [],
    i = 0;
  for (let el of seq) {
    if (pred.call(thisArg, el, i++, seq)) r.push(el);
  }
  return r;
}

export const curry =
  (f, arr = [], length = f.length) =>
  (...args) =>
    ((a) => (a.length === length ? f(...a) : curry(f, a)))([...arr, ...args]);

export function* split(buf, ...points) {
  points.sort();
  const splitAt = (b, pos, len) => {
    let r = pos < b.byteLength ? [slice(b, 0, pos), slice(b, pos)] : [null, b];
    return r;
  };
  let prev,
    len = 0;
  for (let offset of points) {
    let at = offset - len;
    [prev, buf] = splitAt(buf, at, len);
    if (prev) {
      yield prev;
      len = offset;
    }
  }
  if (buf) yield buf;
}

export const unique = (arr, cmp) => arr.filter(typeof cmp == 'function' ? (el, i, arr) => arr.findIndex((item) => cmp(el, item)) == i : (el, i, arr) => arr.indexOf(el) == i);

export const getFunctionArguments = (fn) =>
  (fn + '')
    .replace(/\n.*/g, '')
    .replace(/(=>|{|\n).*/g, '')
    .replace(/^function\s*/, '')
    .replace(/^\((.*)\)\s*$/g, '$1')
    .split(/,\s*/g);

export const getClassName = (obj) => (isObject(obj) ? ('constructor' in obj && obj.constructor.name) || obj[Symbol.toStringTag] : undefined);

export function bits(arrayBuffer, start, end) {
  let u8 = new Uint8Array(arrayBuffer);
  let a = [];
  end ??= u8.length << 3;
  for (let i = start ?? 0; i < end; i++) {
    let b = u8[i >>> 3];
    let s = 1 << (i & 7);
    a.push((b >>> (i & 7)) & 1);
  }
  return a;
}

export function dupArrayBuffer(arrayBuffer, start, end) {
  return ArrayBuffer.prototype.slice.call(arrayBuffer, start, end);
}

export function getTypeName(value) {
  return (
    {
      object() {
        if (value == null) return 'null';

        let name;
        if ('constructor' in value) name = value.constructor.name ?? (value.constructor + '').replace(/^\s*function\s*/g, '').replace(/\(.*/g, '');
        name ??= getTypeName(Object.getPrototypeOf(value));
        name ??= value[Symbol.toStringTag];
        return name;
      },
    }[typeof value] ?? (() => typeof value)
  )();
}

export function isArray(obj) {
  return Array.isArray(obj);
}

export function isArrayBuffer(obj) {
  return obj instanceof ArrayBuffer || obj[Symbol.toStringTag]=='ArrayBuffer';
}

export function isBigDecimal(num) {
  return typeof num == 'bigdecimal' || num[Symbol.toStringTag]=='BigDecimal';
}

export function isBigFloat(num) {
  return typeof num == 'bigfloat' || num[Symbol.toStringTag]=='BigFloat';
}

export function isBigInt(num) {
  return typeof num == 'bigint' || num[Symbol.toStringTag]=='isBigInt';
}

export function isBool(value) {
  return typeof value == 'boolean';
}

export function isCFunction(fn) {
  return false;
}

export function isConstructor(fn) {
  return typeof fn == 'function' && 'prototype' in fn;
}

export function isEmptyString(value) {
  return value==='';
}

export function isError(value) {
  return value instanceof Error || value[Symbol.toStringTag].endsWith('Error');
} 

export function isException(value) {
  return false;
}

export function isExtensible(value) {
  return typeof value == 'object' && value !== null && Object.isExtensible(value);
}

export function isFunction(value) {
  return typeof value == 'function';
}

export function isHTMLDDA(value) {
  return false;
}

export function isInstanceOf(value, ctor) {
  if(ctor[Symbol.hasInstance]) {
    return ctor[Symbol.hasInstance](value);
  }
    return typeof value == 'object' && value !== null && value instanceof ctor;
}

export function isInteger(value) {
  return Math.abs(value)%1 == 0;
}

export function isJobPending(id) {
  return false;
} 

export function isLiveObject(obj) {
  return true;
}

export function isNull(value) {
  return value === null;
}

export function isNumber(value) {
  return typeof value == 'number';
}
export function isUndefined(value) {
  return typeof value == 'undefined';
}

export function isString(value) {
  return typeof value == 'string';
}
export function isUninitialized(value) {
  return false;
}

export function isSymbol(value) {
  return typeof value == 'symbol' || value[Symbol.toStringTag]=='Symbol';
}

export function isUncatchableError(value) {
  return false;
}

export function isRegisteredClass(id) {
  return false;
}
export function rand() {
  return Math.random()* 2 ** 32;
}

export function randi() {
  return rand() - 2 ** 31;
}

export function randf() {
  return Math.random();
}

export function srand(seed) {
}

export function toArrayBuffer(value) {
  if(typeof value == 'object' && value !== null && 'buffer' in value && isArrayBuffer(value.buffer))
    return value.buffer;

  if(typeof value == 'string') {
    const encoder = new TextEncoder();
const view = encoder.encode(value);
return view.buffer;
  }
  return value;
}

export function toString(value) {
  return ''+value;
}
