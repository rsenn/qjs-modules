export function quote(str, q = '"') {
  return q + str.replace(new RegExp(q, 'g'), '\\' + q) + q;
}

export function escape(str, pred = codePoint => codePoint < 32 || codePoint > 0xff) {
  let s = '';
  for(let i = 0; i < str.length; i++) {
    let code = str.codePointAt(i);
    if(!pred(code)) {
      s += str[i];
      continue;
    }

    if(code == 0) s += `\\0`;
    else if(code == 10) s += `\\n`;
    else if(code == 13) s += `\\r`;
    else if(code == 9) s += `\\t`;
    else if(code <= 0xff) s += `\\x${('0' + code.toString(16)).slice(-2)}`;
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
      if((r = get(n))) return r;
      r = fn.call(this, n, ...rest);
      set(n, r);
      return r;
    },
    { cache, get, set }
  );
}

export function inherits(ctor, superCtor) {
  if(superCtor) {
    ctor.super_ = superCtor;
    delete ctor.prototype;
    ctor.prototype = Object.create(superCtor.prototype, {
      constructor: {
        value: ctor,
        enumerable: false,
        writable: true,
        configurable: true
      }
    });
  }
}

export function getset(target, ...args) {
  let ret = [];
  if(typeof target == 'function') {
    ret = [target, typeof args[0] == 'function' ? args[0] : target];
  } else if(hasGetSet(target)) {
    if(target.get === target.set) {
      const GetSet = (...args) => target.set(...args);
      ret = [GetSet, GetSet];
    } else ret = [key => target.get(key), (key, value) => target.set(key, value)];
  } else if(isObject(target)) {
    ret = [key => target[key], (key, value) => (target[key] = value)];
  } else {
    throw new TypeError(`getset unknown argument type '${typeof target}'`);
  }
  if(args.length) {
    let [get, set] = ret;
    ret = [() => get(...args), value => set(...args, value)];
  }
  return ret;
}

export function modifier(...args) {
  let gs = gettersetter(...args);
  return fn => {
    let value = gs();
    return fn(value, newValue => gs(newValue));
  };
}

export function getter(target, ...args) {
  if(isObject(target) && typeof target.get == 'function') return () => target.get(...args);
  let ret;
  if(typeof target == 'function') {
    ret = target;
  } else if(hasGetSet(target)) {
    ret = key => target.get(key);
  } else if(isObject(target)) {
    ret = key => target[key];
  } else {
    throw new TypeError(`getter unknown argument type '${typeof target}'`);
  }
  if(args.length) {
    let get = ret;
    ret = () => get(...args);
  }
  return ret;
}

export function setter(target, ...args) {
  if(isObject(target) && typeof target.set == 'function') return value => target.set(...args, value);
  let ret;
  if(typeof target == 'function') {
    ret = target;
  } else if(hasGetSet(target)) {
    ret = (key, value) => target.set(key, value);
  } else if(isObject(target)) {
    ret = (key, value) => (target[key] = value);
  } else {
    throw new TypeError(`setter unknown argument type '${typeof target}'`);
  }
  if(args.length) {
    let set = ret;
    ret = value => set(...args, value);
  }
  return ret;
}

export function gettersetter(target, ...args) {
  let fn;
  if(isObject(target) && typeof target.receiver == 'function') return (...args2) => target.receiver(...args, ...args2);
  if(typeof target == 'function') {
    if(typeof args[0] == 'function' && args[0] !== target) {
      let setter = args.shift();
      fn = (...args) => (args.length == 0 ? target() : setter(...args));
    } else fn = target;
  } else if(hasGetSet(target)) {
    if(target.get === target.set) fn = (...args) => target.set(...args);
    else fn = (...args) => (args.length < 2 ? target.get(...args) : target.set(...args));
  } else if(isObject(target)) {
    fn = (...args) => {
      const [key, value] = args;
      if(args.length == 1) return target[key];
      target[key] = value;
    };
  } else {
    throw new TypeError(`gettersetter unknown argument type '${typeof target}'`);
  }
  if(fn !== target) define(fn, { receiver: target });
  if(args.length) return (...args2) => fn(...args, ...args2);
  return fn;
}

export function hasGetSet(obj) {
  return isObject(obj) && ['get', 'set'].every(m => typeof obj[m] == 'function');
}

export function mapObject(target) {
  let obj;
  if(hasGetSet(target.receiver)) return target.receiver;
  if(hasGetSet(target)) obj = target;
  else if(typeof target == 'function') obj = { get: target, set: target };
  else if(isObject(target))
    obj = {
      set: (key, value) => (target[key] = value),
      get: key => target[key]
    };
  if(obj !== target) define(obj, { receiver: target });
  return obj;
}

export function once(fn, thisArg, memoFn) {
  let ret,
    ran = false;

  return function(...args) {
    if(!ran) {
      ran = true;
      ret = fn.apply(thisArg || this, args);
    } else if(typeof memoFn == 'function') {
      ret = memoFn(ret);
    }
    return ret;
  };
}

export function waitFor(ms) {
  return new Promise(resolve => timers.setTimeout(resolve, ms));
}

export function define(obj, ...args) {
  for(let props of args) {
    let desc = {};
    let syms = Object.getOwnPropertySymbols(props).concat(Object.getOwnPropertyNames(props));
    for(let prop of syms) {
      if(prop == '__proto__') {
        Object.setPrototypeOf(obj, props[prop]);
        continue;
      }
      const { value, ...rest } = (desc[prop] = Object.getOwnPropertyDescriptor(props, prop));
      desc[prop].enumerable = false;
      if(typeof value == 'function') desc[prop].writable = false;
    }
    Object.defineProperties(obj, desc);
  }
  return obj;
}

export function weakAssign(obj, ...args) {
  let desc = {};
  for(let other of args) {
    let otherDesc = Object.getOwnPropertyDescriptors(other);
    for(let key in otherDesc) if(!(key in obj) && desc[key] === undefined && otherDesc[key] !== undefined) desc[key] = otherDesc[key];
  }
  return Object.defineProperties(obj, desc);
}

export function getPrototypeChain(obj, limit = -1, start = 0) {
  let i = -1,
    ret = [];
  do {
    if(i >= start && (limit == -1 || i < start + limit)) ret.push(obj);
    if(obj === Object.prototype || obj.constructor === Object) break;
    ++i;
  } while((obj = obj.__proto__ || Object.getPrototypeOf(obj)));
  return ret;
}

export function getConstructorChain(obj, ...range) {
  let ret = [];
  pushUnique(ret, obj.constructor);
  for(let proto of misc.getPrototypeChain(obj, ...range)) pushUnique(ret, proto.constructor);
  return ret;
}

export function hasPrototype(obj, proto) {
  return misc.getPrototypeChain(obj).indexOf(proto) != -1;
}

export function filter(seq, pred, thisArg) {
  if(isObject(pred) && pred instanceof RegExp) {
    let re = pred;
    pred = (el, i) => re.test(el);
  }
  let r = [],
    i = 0;
  for(let el of seq) {
    if(pred.call(thisArg, el, i++, seq)) r.push(el);
  }
  return r;
}

export const curry =
  (f, arr = [], length = f.length) =>
  (...args) =>
    (a => (a.length === length ? f(...a) : curry(f, a)))([...arr, ...args]);

export function* split(buf, ...points) {
  points.sort();
  const splitAt = (b, pos, len) => {
    let r = pos < b.byteLength ? [slice(b, 0, pos), slice(b, pos)] : [null, b];
    return r;
  };
  let prev,
    len = 0;
  for(let offset of points) {
    let at = offset - len;
    [prev, buf] = splitAt(buf, at, len);
    if(prev) {
      yield prev;
      len = offset;
    }
  }
  if(buf) yield buf;
}

export const unique = (arr, cmp) => arr.filter(typeof cmp == 'function' ? (el, i, arr) => arr.findIndex(item => cmp(el, item)) == i : (el, i, arr) => arr.indexOf(el) == i);

export const getFunctionArguments = fn =>
  (fn + '')
    .replace(/\n.*/g, '')
    .replace(/(=>|{|\n).*/g, '')
    .replace(/^function\s*/, '')
    .replace(/^\((.*)\)\s*$/g, '$1')
    .split(/,\s*/g);

export const getClassName = obj => (isObject(obj) ? ('constructor' in obj && obj.constructor.name) || obj[Symbol.toStringTag] : undefined);
