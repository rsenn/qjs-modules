//import { SyscallError } from 'syscallerror';
import { searchArrayBuffer, dupArrayBuffer, escape, getPrototypeChain, isArray, isBigDecimal, isBigFloat, isBigInt, isBool, isCFunction, isConstructor, isEmptyString, isError, isException, isExtensible, isFunction, isHTMLDDA, isInstanceOf, isInteger, isJobPending, isLiveObject, isNull, isNumber, isObject, isRegisteredClass, isString, isSymbol, isUncatchableError, isUndefined, isUninitialized, isArrayBuffer, rand, toArrayBuffer, toString, bits, getTypeName, atexit, getpid } from 'misc';
import { extendArray, ArrayExtensions } from './extendArray.js';
import { extendGenerator, GeneratorExtensions, GeneratorPrototype } from './extendGenerator.js';
import { AsyncGeneratorExtensions, AsyncGeneratorPrototype, AsyncGeneratorConstructor } from './extendAsyncGenerator.js';
//import { MathExtensions, extendMath } from './extendMath.js';
import * as os from 'os';
import { basename } from 'path';

const sliceFn = x =>
  ({
    Array: Array.prototype.slice,
    String: String.prototype.slice,
    ArrayBuffer(start, end) {
      start ??= 0;
      end ??= this.byteLength;
      return dupArrayBuffer(this, start, end - start);
    },
    SharedArrayBuffer: SharedArrayBuffer.prototype.slice,
    Uint8ClampedArray: Uint8ClampedArray.prototype.slice,
    Int8Array: Int8Array.prototype.slice,
    Uint8Array: Uint8Array.prototype.slice,
    Int16Array: Int16Array.prototype.slice,
    Uint16Array: Uint16Array.prototype.slice,
    Int32Array: Int32Array.prototype.slice,
    Uint32Array: Uint32Array.prototype.slice,
    BigInt64Array: BigInt64Array.prototype.slice,
    BigUint64Array: BigUint64Array.prototype.slice,
    Float32Array: Float32Array.prototype.slice,
    Float64Array: Float64Array.prototype.slice
  }[getTypeName(x)]);

export const slice = (x, s, e) => (sliceFn(x) ?? x.slice).call(x, s, e);

/*typeof x == 'object'
    ? isArrayBuffer(x)
      ? dupArrayBuffer(x, s, e)
      : Array.isArray(x)
      ? Array.prototype.slice.call(x, s, e)
      : x.slice(s, e)
    : String.prototype.slice.call(x, s, e);*/
const stringify = v => `${v}`;
const protoOf = Object.getPrototypeOf;
const formatNumber = n => (n === -0 ? '-0' : `${n}`);
const isNative = fn => /\[native\scode\]/.test(stringify(fn));

/*function util() {
  return util;
}

util.prototype.constructor = util;*/

const AsyncFunction = async function x() {}.constructor;
const GeneratorFunction = function* () {}.constructor;
const AsyncGeneratorFunction = async function* () {}.constructor;
const TypedArray = protoOf(protoOf(new Uint16Array(10))).constructor;

const SetIteratorPrototype = protoOf(new Set().values());
const MapIteratorPrototype = protoOf(new Map().entries());
//const GeneratorPrototype = protoOf((function* () {})());

// prettier-ignore
export const errors = [null, 'EPERM', 'ENOENT', 'ESRCH', 'EINTR', 'EIO', 'ENXIO', 'E2BIG', 'ENOEXEC', 'EBADF', 'ECHILD', 'EAGAIN', 'ENOMEM', 'EACCES', 'EFAULT', 'ENOTBLK', 'EBUSY', 'EEXIST', 'EXDEV', 'ENODEV', 'ENOTDIR', 'EISDIR', 'EINVAL', 'ENFILE', 'EMFILE', 'ENOTTY', 'ETXTBSY', 'EFBIG', 'ENOSPC', 'ESPIPE', 'EROFS', 'EMLINK', 'EPIPE', 'EDOM', 'ERANGE', 'EDEADLK', 'ENAMETOOLONG', 'ENOLCK', 'ENOSYS', 'ENOTEMPTY', null, null, 'ENOMSG', 'EIDRM', 'ECHRNG', 'EL2NSYNC', 'EL3HLT', 'EL3RST', 'ELNRNG', 'EUNATCH', 'ENOCSI', 'EL2HLT', 'EBADE', 'EBADR', 'EXFULL', 'ENOANO', 'EBADRQC', null, '', 'EBFONT', 'ENOSTR', 'ENODATA', 'ETIME', 'ENOSR', 'ENONET', 'ENOPKG', 'EREMOTE', 'ENOLINK', 'EADV', 'ESRMNT', 'ECOMM', 'EPROTO', 'EMULTIHOP', 'EDOTDOT', 'EBADMSG', 'EOVERFLOW', 'ENOTUNIQ', 'EBADFD', 'EREMCHG', 'ELIBACC', 'ELIBBAD', 'ELIBSCN', 'ELIBMAX', 'ELIBEXEC', 'EILSEQ', 'ERESTART', 'ESTRPIPE', 'EUSERS', 'ENOTSOCK', 'EDESTADDRREQ', 'EMSGSIZE', 'EPROTOTYPE', 'ENOPROTOOPT', 'EPROTONOSUPPORT', 'ESOCKTNOSUPPORT', 'EOPNOTSUPP', 'EPFNOSUPPORT', 'EAFNOSUPPORT', 'EADDRINUSE', 'EADDRNOTAVAIL', 'ENETDOWN', 'ENETUNREACH', 'ENETRESET', 'ECONNABORTED', 'ECONNRESET', 'ENOBUFS', 'EISCONN', 'ENOTCONN', 'ESHUTDOWN', 'ETOOMANYREFS', 'ETIMEDOUT', 'ECONNREFUSED', 'EHOSTDOWN', 'EHOSTUNREACH', 'EALREADY', 'EINPROGRESS', 'ESTALE', 'EUCLEAN', 'ENOTNAM', 'ENAVAIL', 'EISNAM', 'EREMOTEIO', 'EDQUOT', 'ENOMEDIUM', 'EMEDIUMTYPE', 'ECANCELED', 'ENOKEY', 'EKEYEXPIRED', 'EKEYREVOKED', 'EKEYREJECTED', 'EOWNERDEAD', 'ENOTRECOVERABLE', 'ERFKILL'];

export const types = {
  isAnyArrayBuffer(v) {
    return isObject(v) && (v instanceof ArrayBuffer || v instanceof SharedArrayBuffer);
  },
  isArrayBuffer(v) {
    return isObject(v) && v instanceof ArrayBuffer;
  },
  isBigInt64Array(v) {
    return isObject(v) && v instanceof BigInt64Array;
  },
  isBigUint64Array(v) {
    return isObject(v) && v instanceof BigUint64Array;
  },
  isDate(v) {
    return isObject(v) && v instanceof Date;
  },
  isFloat32Array(v) {
    return isObject(v) && v instanceof Float32Array;
  },
  isFloat64Array(v) {
    return isObject(v) && v instanceof Float64Array;
  },
  isInt8Array(v) {
    return isObject(v) && v instanceof Int8Array;
  },
  isInt16Array(v) {
    return isObject(v) && v instanceof Int16Array;
  },
  isInt32Array(v) {
    return isObject(v) && v instanceof Int32Array;
  },
  isMap(v) {
    return isObject(v) && v instanceof Map;
  },
  isPromise(v) {
    return isObject(v) && v instanceof Promise;
  },
  isProxy(v) {
    return isObject(v) && v instanceof Proxy;
  },
  isRegExp(v) {
    return isObject(v) && v instanceof RegExp;
  },
  isSet(v) {
    return isObject(v) && v instanceof Set;
  },
  isSharedArrayBuffer(v) {
    return isObject(v) && v instanceof SharedArrayBuffer;
  },
  isUint8Array(v) {
    return isObject(v) && v instanceof Uint8Array;
  },
  isUint8ClampedArray(v) {
    return isObject(v) && v instanceof Uint8ClampedArray;
  },
  isUint16Array(v) {
    return isObject(v) && v instanceof Uint16Array;
  },
  isUint32Array(v) {
    return isObject(v) && v instanceof Uint32Array;
  },
  isWeakMap(v) {
    return isObject(v) && v instanceof WeakMap;
  },
  isWeakSet(v) {
    return isObject(v) && v instanceof WeakSet;
  },
  isDataView(v) {
    return isObject(v) && v instanceof DataView;
  },
  isBooleanObject(v) {
    return isObject(v) && v instanceof Boolean;
  },
  isAsyncFunction(v) {
    return isObject(v) && v instanceof AsyncFunction;
  },
  isGeneratorFunction(v) {
    return isObject(v) && v instanceof GeneratorFunction;
  },
  isAsyncGeneratorFunction(v) {
    return isObject(v) && v instanceof AsyncGeneratorFunction;
  },
  isNumberObject(v) {
    return isObject(v) && v instanceof Number;
  },
  isBigIntObject(v) {
    return isObject(v) && v instanceof BigInt;
  },
  isSymbolObject(v) {
    return v && v instanceof Symbol;
  },
  isNativeError(v) {
    return isObject(v) && v instanceof Error && isNative(v.constructor);
  },
  isMapIterator(v) {
    return isObject(v) && protoOf(v) == MapIteratorPrototype;
  },
  isSetIterator(v) {
    return isObject(v) && protoOf(v) == SetIteratorPrototype;
  },
  isStringObject(v) {
    return isObject(v) && v instanceof String;
  },
  isArrayBufferView(v) {
    return isObject(v) && ArrayBuffer.isView(v);
  },
  isArgumentsObject(v) {
    return Object.prototype.toString.call(v) == '[object Arguments]';
  },

  /* isExternal(v) {
    return isObject(v) && v instanceof External;
  },*/

  isBoxedPrimitive(v) {
    return isObject(v) && [Number, String, Boolean, BigInt, Symbol].some(ctor => v instanceof ctor);
  },

  isGeneratorObject(v) {
    return isObject(v) && protoOf(v) == GeneratorPrototype;
  },
  isTypedArray(v) {
    return isObject(v) && v instanceof TypedArray;
  },
  isModuleNamespaceObject(v) {
    return isObject(v) && v[Symbol.toStringTag] == 'Module';
  },
  isConstructor(v) {
    return isFunction(v) && 'prototype' in v;
  },
  isIterable(v) {
    return isObject(v) && isFunction(v[Symbol.iterator]);
  },
  isAsyncIterable(v) {
    return isObject(v) && isFunction(v[Symbol.asyncIterator]);
  },
  isIterator(v) {
    return isObject(v) && isFunction(v.next);
  },
  isArrayLike(v) {
    return isObject(v) && typeof v.length == 'number' && Number.isInteger(v.length);
  }
};

export function hasBuiltIn(o, m) {
  return isNative(protoOf(o)[m]);
}

export function isAsync(fn) {
  if(types.isAsyncFunction(fn) || types.isAsyncGeneratorFunction(fn)) return true;

  if(isFunction(fn)) return /^async\s+function/.test(fn + '');
}

export function format(...args) {
  return formatWithOptionsInternal({ hideKeys: ['constructor'] }, args);
}

export function formatWithOptions(opts, ...args) {
  if(!isObject(opts)) throw new TypeError(`options argument is not an object`);
  return formatWithOptionsInternal(opts, args);
}

function formatWithOptionsInternal(o, v) {
  const x = v[0];
  let a = 0;
  let s = '';
  let j = '';
  if(typeof x === 'string') {
    if(v.length === 1) return x;
    let t;
    let p = 0;
    for(let i = 0; i < x.length - 1; i++) {
      if(x[i] == '%') {
        let f = '';
        while('sjxdOoifc%'.indexOf(x[i + 1]) == -1) {
          f += x[i + 1];
          ++i;
        }
        if(p < i) s += slice(x, p, i);
        p = i + 1;

        const c = String.prototype.charCodeAt.call(x, ++i);
        if(a + 1 !== v.length) {
          switch (c) {
            case 115: // %s
              const y = v[++a];
              if(typeof y === 'number') t = formatNumber(y);
              else if(typeof y === 'bigint') t = `${y}n`;
              else if(typeof y !== 'object' || y === null || !hasBuiltIn(y, 'toString')) t = String(y);
              else t = inspect(y, { ...o, compact: 3, colors: false, depth: 0 });
              break;
            case 106: // %j
              t = stringify(v[++a]);
              break;
            case 120: // %x
            case 100: // %d
              const n = v[++a];
              if(typeof n === 'bigint') t = `${n}n`;
              else if(typeof n === 'symbol') t = 'NaN';
              else t = formatNumber(c == 120 ? Number(n).toString(16) : Number(n));
              break;
            case 79: // %O
              t = inspect(v[++a], o);
              break;
            case 111: // %o
              t = /*v[++a]+'' ?? */ inspect(v[++a], {
                ...o,
                showHidden: true,
                showProxy: true,
                depth: 1,
                protoChain: false
              });
              break;
            case 105: // %i
              const k = v[++a];
              if(typeof k === 'bigint') t = `${k}`;
              else if(typeof k === 'symbol') t = 'NaN';
              else t = formatNumber(parseInt(k));
              break;
            case 102: // %f
              const d = v[++a];
              if(typeof d === 'symbol') t = 'NaN';
              else t = formatNumber(parseFloat(d));
              break;
            case 99: // %c
              a += 1;
              t = '';
              break;
            case 37: // %%
              s += slice(x, p, i);
              p = i + 1;
              continue;
            default:
              continue;
          }
          if(p !== i - 1) s += slice(x, p, i - 1);
          let pad = parseInt(f);
          if(Math.abs(pad) > 0) t = t['pad' + (pad < 0 ? 'End' : 'Start')](Math.abs(pad), /^-?0/.test(f) ? '0' : ' ');
          s += t;
          p = i + 1;
        } else if(c === 37) {
          s += slice(x, p, i);
          p = i + 1;
        }
      }
    }
    if(p !== 0) {
      a++;
      j = ' ';
      if(p < x.length) s += slice(x, p);
    }
  }
  while(a < v.length) {
    const y = v[a];
    s += j;
    s += !isString(y) ? inspect(y, o) : y;
    j = ' ';
    a++;
  }
  return s;
}

export function assert(actual, expected, message) {
  if(arguments.length == 1) expected = true;

  if(actual === expected) return;

  if(
    actual !== null &&
    expected !== null &&
    isObject(actual) &&
    isObject(expected) &&
    actual.toString() === expected.toString()
  )
    return;

  throw Error(
    'assertion failed: got |' + actual + '|' + ', expected |' + expected + '|' + (message ? ' (' + message + ')' : '')
  );
}

export function setInterval(callback, ms) {
  let map = (setInterval.map ??= new Map());
  let id = (setInterval.id = (setInterval.id ?? 0) + 1);
  let obj = { callback, ms };
  map.set(id, obj);

  function start() {
    obj.id = os.setTimeout(() => {
      start();
      callback();
    }, obj.ms);
  }

  start();
  return id;
}

export function clearInterval(id) {
  let map = (setInterval.map ??= new Map());

  let obj = map.get(id);

  if(obj) os.clearTimeout(obj.id);
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

export function getConstructor(obj) {
  return obj.constructor ?? Object.getPrototypeOf(obj).constructor;
}

export function memoize(fn, cache = {}) {
  let [get, set] = getset(cache);
  //console.log('memoize', cache, get == WeakMap.prototype.get, set + '');
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

export function getset(target, ...args) {
  let ret = [];
  if(isFunction(target)) {
    ret = [target, isFunction(args[0]) ? args[0] : target];
  } else if(hasGetSet(target)) {
    if(target.get === target.set) {
      const GetSet = (...args) => target.set(...args);
      ret = [GetSet, GetSet];
    } else {
      ret = [key => target.get(key), (key, value) => target.set(key, value)];
      //console.log('getset', ret[1] + '', target.get === target.set);
    }
  } else if(Array.isArray(target)) {
    ret = [
      key => target.find(([k, v]) => key === k),
      (key, value) => {
        let i = target.findIndex(([k, v]) => k === key);
        if(i != -1) {
          if(value !== undefined) target[i][1] = value;
          else delete target[i];
        } else {
          target.push([key, value]);
        }
      }
    ];
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
  if(isObject(target) && isFunction(target.get)) return () => target.get(...args);
  let ret;
  if(isFunction(target)) {
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
  if(isObject(target) && isFunction(target.set)) return value => target.set(...args, value);
  let ret;
  if(isFunction(target)) {
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

/**
 * Returns a function that either sets or gets (abstract) properties on the target,
 * depending on number of arguments given
 *
 * @param  {Map|Object}    target          Target object
 * @return {Function}           The getter/setter function
 */
export function gettersetter(obj, ...optional) {
  let fn;
  if(isObject(obj) && isFunction(obj.receiver)) return (...v) => obj.receiver(...optional, ...v);
  if(isFunction(obj)) {
    if(isFunction(optional[0]) && optional[0] !== obj) {
      let setter = optional.shift();
      fn = (...optional) => (optional.length == 0 ? obj() : setter(...optional));
    } else fn = obj;
  } else if(hasGetSet(obj)) {
    if(obj.get === obj.set) fn = (...optional) => obj.set(...optional);
    else fn = (...optional) => (optional.length < 2 ? obj.get(...optional) : obj.set(...optional));
  } else if(isObject(obj)) {
    fn = (...optional) => {
      const [k, v] = optional;
      if(optional.length == 1) return obj[k];
      obj[k] = v;
    };
  } else {
    throw new TypeError(`gettersetter unknown argument type '${typeof obj}'`);
  }
  if(fn !== obj) define(fn, { receiver: obj });
  if(optional.length) return (...v) => fn(...optional, ...v);
  return fn;
}

export function hasFn(target) {
  if(isObject(target)) return isFunction(target.has) ? key => target.has(key) : key => key in target;
}

export function remover(target) {
  if(isObject(target)) return isFunction(target.delete) ? key => target.delete(key) : key => delete target[key];
}

export function getOrCreate(target, create = () => ({}), set) {
  const get = getter(target),
    has = hasFn(target);
  set ??= setter(target);
  let value;
  return key =>
    (value = has.call(target, key)
      ? get.call(target, key)
      : ((value = create(key, target)), set.call(target, key, value), value));
}

export function hasGetSet(obj) {
  return isObject(obj) && ['get', 'set'].every(m => isFunction(obj[m]));
}

export function getSetArgument(get, set) {
  return (...args) => (args.length > 1 ? set(...args) : get(args[0]));
}

export function wrapGetSet(getter, setter, ...args) {
  let [get, set] = getset(...args);

  return getSetArgument(
    prop => getter(get(prop)),
    (prop, value) => set(prop, setter(value))
  );
}

export function weakGetSet(...args) {
  return wrapGetSet(
    wref => wref.deref(),
    value => new WeakRef(value),
    ...args
  );
}

export function addremovehas(target, ...args) {
  let fn;
  if(isObject(target)) {
    if(target instanceof Set || ['add', 'delete', 'has'].every(n => isFunction(target[n])))
      return [el => (target.add(el), target), el => (target.delete(el), target), el => (target.has(el), target)];

    if(target instanceof Array) {
      let idx;
      return [
        el => (target.push(el), target),
        el => {
          while(has(el)) target.splice(idx, idx + 1);
          return target;
        },
        has
      ];
      const has = el => {
        return (idx = target.indexOf(el)) != -1;
      };
    }
  }
}

export function lookupObject(getset, instance = {}, handlers = {}) {
  return new Proxy(
    instance,
    weakAssign(handlers, {
      get(target, prop) {
        return getset(numericIndex(prop));
      },
      set(target, prop, value) {
        getset(numericIndex(prop), value);
      }
    })
  );
}

export function mapObject(target) {
  let obj;
  if(hasGetSet(target.receiver)) return target.receiver;
  if(hasGetSet(target)) obj = target;
  else if(isFunction(target)) obj = { get: target, set: target };
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
    } else if(isFunction(memoFn)) {
      ret = memoFn(ret);
    }
    return ret;
  };
}

export function waitFor(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

export function waitCancellable(ms) {
  let rsfun, tmrid;
  return define(
    new Promise(resolve => {
      rsfun = resolve;
      tmrid = setTimeout(() => ((rsfun = tmrid = null), resolve()), ms);
    }),
    { cancel: () => tmrid && (clearTimeout(tmrid), (tmrid = null), rsfun()) }
  );
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
      if(isFunction(value)) desc[prop].writable = false;
    }
    Object.defineProperties(obj, desc);
  }
  return obj;
}

export function defineGetter(obj, key, fn, enumerable = false) {
  if(!obj.hasOwnProperty(key))
    Object.defineProperty(obj, key, {
      enumerable,
      configurable: true,
      get: fn
    });
  return obj;
}

export function defineGetterSetter(obj, key, g, s, enumerable = false) {
  if(!obj.hasOwnProperty(key))
    Object.defineProperty(obj, key, {
      get: g,
      set: s,
      enumerable
    });
  return obj;
}

export function defineGettersSetters(obj, gettersSetters) {
  for(let name in gettersSetters) defineGetterSetter(obj, name, gettersSetters[name], gettersSetters[name]);
  return obj;
}

/*export function defineGettersSetters(obj, gettersSetters, enumerable = false) {
  let props = {};
  try {
    for(let name in gettersSetters) props[name] = { get: gettersSetters[name], set: gettersSetters[name], enumerable };
    return Object.defineProperties(obj, props);
  } catch(e) {
    for(let name in gettersSetters)
      try {
        defineGetterSetter(obj, name, gettersSetters[name], gettersSetters[name]);
      } catch(e) {
        console.log(`Failed setting property '${name}'`);
      }
    return obj;
  }
}*/

export function* prototypeIterator(obj, pred = (obj, depth) => true) {
  let depth = 0;

  while(obj) {
    if(pred(obj, depth)) yield obj;
    let tmp = Object.getPrototypeOf(obj);
    if(tmp === obj) break;
    obj = tmp;
    ++depth;
  }
}

export function keys(obj, start = 0, end = obj => obj === Object.prototype) {
  let pred,
    a = [],
    depth = 0;

  if(!isFunction(end)) {
    let n = end;
    pred = (obj, depth) => depth >= start && depth < n;
    end = () => false;
  } else {
    pred = (obj, depth) => depth >= start;
  }

  for(let proto of prototypeIterator(obj, pred)) {
    if(end(proto, depth++)) break;
    a.push(...Object.getOwnPropertySymbols(proto).concat(Object.getOwnPropertyNames(proto)));
  }

  return [...new Set(a)];
}

export function entries(obj, start = 0, end = obj => obj === Object.prototype) {
  let a = [];
  for(let key of keys(obj, start, end)) a.push([key, obj[key]]);
  return a;
}

export function values(obj, start = 0, end = obj => obj === Object.prototype) {
  let a = [];
  for(let key of keys(obj, start, end)) a.push(obj[key]);
  return a;
}

export function getMethodNames(obj, depth = 1, start = 0) {
  let names = [];
  for(let n of keys(obj, start, start + depth)) {
    try {
      if(isFunction(obj[n])) names.push(n);
    } catch(e) {}
  }
  return names;
}

export function getMethods(obj, depth = 1, start = 0) {
  let methods = {};

  for(let n of getMethodNames(obj, depth, start)) methods[n] = obj[n];

  return methods;
}

export function properties(obj, options = { enumerable: true }) {
  let desc = {};
  const { memoize: memo = false, ...opts } = options;
  const mfn = memo ? fn => memoize(fn) : fn => fn;
  for(let prop of keys(obj)) {
    if(Array.isArray(obj[prop])) {
      const [get, set] = obj[prop];
      desc[prop] = { ...opts, get, set };
    } else if(isFunction(obj[prop])) {
      desc[prop] = { ...opts, get: mfn(obj[prop]) };
    }
  }
  return Object.defineProperties({}, desc);
}

export function weakAssign(obj, ...args) {
  let desc = {};
  for(let other of args) {
    let otherDesc = Object.getOwnPropertyDescriptors(other);
    for(let key in otherDesc)
      if(!(key in obj) && desc[key] === undefined && otherDesc[key] !== undefined) desc[key] = otherDesc[key];
  }
  return Object.defineProperties(obj, desc);
}

export function getConstructorChain(obj, ...range) {
  let ret = [];
  pushUnique(ret, obj.constructor);
  for(let proto of getPrototypeChain(obj, ...range)) pushUnique(ret, proto.constructor);
  return ret;
}

export function hasPrototype(obj, proto) {
  return getPrototypeChain(obj).indexOf(proto) != -1;
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

export function filterKeys(r, needles, keep = true) {
  let pred;
  if(isFunction(needles)) {
    pred = needles;
  } else {
    if(!Array.isArray(needles)) needles = [...needles];
    pred = key => (needles.indexOf(key) != -1) === keep;
  }
  return Object.keys(r)
    .filter(pred)
    .reduce((obj, key) => {
      obj[key] = r[key];
      return obj;
    }, {});
}

export const curry =
  (f, arr = [], length = f.length) =>
  (...args) =>
    (a => (a.length === length ? f(...a) : curry(f, a)))([...arr, ...args]);

export const clamp = curry((min, max, value) => Math.max(min, Math.min(max, value)));

export const generate = (fn, add) => {
  let gen;

  if(isObject(add) && add instanceof Set) {
    let s = add;
    add = (item, gen) => {
      s.add(item);
      return s;
    };
  }

  if(isFunction(add))
    return function(...args) {
      let acc;
      gen = fn.call(this, ...args);

      for(let item of gen) acc = add(item, gen, acc);
      return acc;
    };

  return function(...args) {
    gen = fn.call(this, ...args);
    return [...gen];
  };
};

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

export function uniquePred(cmp = null) {
  return isFunction(cmp)
    ? (el, i, arr) => arr.findIndex(item => cmp(el, item)) == i
    : (el, i, arr) => arr.indexOf(el) == i;
}

export const unique = (...args) => {
  if(Array.isArray(args[0])) return ((arr, cmp) => arr.filter(uniquePred(cmp)))(...args);

  return (function* unique(seq) {
    let items = new Set();
    for(let el of seq) {
      if(!items.has(el)) {
        yield el;
        items.add(el);
      }
    }
  })(...args);
};

export const getFunctionArguments = fn =>
  (fn + '')
    .replace(/\n.*/g, '')
    .replace(/(=>|{|\n).*/g, '')
    .replace(/^function\s*/, '')
    .replace(/^\((.*)\)\s*$/g, '$1')
    .split(/,\s*/g);

const ANSI_BACKGROUND_OFFSET = 10;

const wrapAnsi16 =
  (offset = 0) =>
  code =>
    `\x1b[${code + offset}m`;

const wrapAnsi256 =
  (offset = 0) =>
  code =>
    `\x1b[${38 + offset};5;${code}m`;

const wrapAnsi16m =
  (offset = 0) =>
  (red, green, blue) =>
    `\x1b[${38 + offset};2;${red};${green};${blue}m`;

function getAnsiStyles() {
  const codes = new Map();
  const styles = {
    modifier: {
      reset: [0, 0],
      // 21 isn't widely supported and 22 does the same thing
      bold: [1, 22],
      dim: [2, 22],
      italic: [3, 23],
      underline: [4, 24],
      overline: [53, 55],
      inverse: [7, 27],
      hidden: [8, 28],
      strikethrough: [9, 29]
    },
    color: {
      black: [30, 39],
      red: [31, 39],
      green: [32, 39],
      yellow: [33, 39],
      blue: [34, 39],
      magenta: [35, 39],
      cyan: [36, 39],
      white: [37, 39],

      // Bright color
      blackBright: [90, 39],
      redBright: [91, 39],
      greenBright: [92, 39],
      yellowBright: [93, 39],
      blueBright: [94, 39],
      magentaBright: [95, 39],
      cyanBright: [96, 39],
      whiteBright: [97, 39]
    },
    bgColor: {
      bgBlack: [40, 49],
      bgRed: [41, 49],
      bgGreen: [42, 49],
      bgYellow: [43, 49],
      bgBlue: [44, 49],
      bgMagenta: [45, 49],
      bgCyan: [46, 49],
      bgWhite: [47, 49],

      // Bright color
      bgBlackBright: [100, 49],
      bgRedBright: [101, 49],
      bgGreenBright: [102, 49],
      bgYellowBright: [103, 49],
      bgBlueBright: [104, 49],
      bgMagentaBright: [105, 49],
      bgCyanBright: [106, 49],
      bgWhiteBright: [107, 49]
    }
  };

  // Alias bright black as gray (and grey)
  styles.color.gray = styles.color.blackBright;
  styles.bgColor.bgGray = styles.bgColor.bgBlackBright;
  styles.color.grey = styles.color.blackBright;
  styles.bgColor.bgGrey = styles.bgColor.bgBlackBright;

  for(const [groupName, group] of Object.entries(styles)) {
    for(const [styleName, style] of Object.entries(group)) {
      styles[styleName] = {
        open: `\u001B[${style[0]}m`,
        close: `\u001B[${style[1]}m`
      };

      group[styleName] = styles[styleName];

      codes.set(style[0], style[1]);
    }

    Object.defineProperty(styles, groupName, {
      value: group,
      enumerable: false
    });
  }

  Object.defineProperty(styles, 'codes', {
    value: codes,
    enumerable: false
  });

  styles.color.close = '\u001B[39m';
  styles.bgColor.close = '\u001B[49m';

  styles.color.ansi = wrapAnsi16();
  styles.color.ansi256 = wrapAnsi256();
  styles.color.ansi16m = wrapAnsi16m();
  styles.bgColor.ansi = wrapAnsi16(ANSI_BACKGROUND_OFFSET);
  styles.bgColor.ansi256 = wrapAnsi256(ANSI_BACKGROUND_OFFSET);
  styles.bgColor.ansi16m = wrapAnsi16m(ANSI_BACKGROUND_OFFSET);

  // From https://github.com/Qix-/color-convert/blob/3f0e0d4e92e235796ccb17f6e85c72094a651f49/conversions.js
  Object.defineProperties(styles, {
    rgbToAnsi256: {
      value: (red, green, blue) => {
        // We use the extended greyscale palette here, with the exception of
        // black and white. normal palette only has 4 greyscale shades.
        if(red === green && green === blue) {
          if(red < 8) {
            return 16;
          }

          if(red > 248) {
            return 231;
          }

          return Math.round(((red - 8) / 247) * 24) + 232;
        }
        const c = [red, green, blue].map(c => (c / 255) * 5);
        return 16 + 36 * c[0] + 6 * c[1] + c[2];
      },
      enumerable: false
    },
    hexToRgb: {
      value: hex => {
        const matches = /(?<colorString>[a-f\d]{6}|[a-f\d]{3})/i.exec(hex.toString(16));
        if(!matches) {
          return [0, 0, 0];
        }

        let { colorString } = matches.groups;

        if(colorString.length === 3) {
          colorString = colorString
            .split('')
            .map(character => character + character)
            .join('');
        }

        const integer = Number.parseInt(colorString, 16);

        return [(integer >> 16) & 0xff, (integer >> 8) & 0xff, integer & 0xff];
      },
      enumerable: false
    },
    hexToAnsi256: {
      value: hex => styles.rgbToAnsi256(...styles.hexToRgb(hex)),
      enumerable: false
    },
    ansi256ToAnsi: {
      value: code => {
        if(code < 8) {
          return 30 + code;
        }

        if(code < 16) {
          return 90 + (code - 8);
        }

        let red;
        let green;
        let blue;

        if(code >= 232) {
          red = ((code - 232) * 10 + 8) / 255;
          green = red;
          blue = red;
        } else {
          code -= 16;

          const remainder = code % 36;

          red = Math.floor(code / 36) * 0.2;
          green = Math.floor(remainder / 6) * 0.2;
          blue = (remainder % 6) * 0.2;
        }

        const value = Math.max(red, green, blue) * 2;

        if(value === 0) {
          return 30;
        }

        let result = 30 + ((Math.round(blue) << 2) | (Math.round(green) << 1) | Math.round(red));

        if(value === 2) {
          result += 60;
        }

        return result;
      },
      enumerable: false
    },
    rgbToAnsi: {
      value: (red, green, blue) => styles.ansi256ToAnsi(styles.rgbToAnsi256(red, green, blue)),
      enumerable: false
    },
    hexToAnsi: {
      value: hex => styles.ansi256ToAnsi(styles.hexToAnsi256(hex)),
      enumerable: false
    }
  });

  return styles;
}

export function stripAnsi(str) {
  return (str + '').replace(new RegExp('\x1b[[(?);]{0,2}(;?[0-9])*.', 'g'), '');
}

export function padAnsi(str, n, s = ' ') {
  let { length } = stripAnsi(str);
  let pad = '';
  for(let i = length; i < n; i++) pad += s;
  return pad;
}

export function padStartAnsi(str, n, s = ' ') {
  return padAnsi(str, n, s) + str;
}

export function padEndAnsi(str, n, s = ' ') {
  return str + padAnsi(str, n, s);
}

export function mapFunctional(fn) {
  return function* (arg) {
    for(let item of arg) yield fn(item);
  };
}

export function map(...args) {
  let [obj, fn] = args;
  let ret = a => a;
  if(types.isIterator(obj)) {
    return ret(function* () {
      let i = 0;
      for(let item of obj) yield fn(item, i++, obj);
    })();
  }
  if(isFunction(obj)) return mapFunctional(...args);
  if(isFunction(obj.map)) return obj.map(fn);

  if(isFunction(obj.entries)) {
    const ctor = obj.constructor;
    obj = obj.entries();
    ret = a => new ctor([...a]);
  }

  if(types.isIterable(obj))
    return ret(
      (function* () {
        let i = 0;
        for(let item of obj) yield fn(item, i++, obj);
      })()
    );
  ret = {};
  for(let key in obj) {
    if(obj.hasOwnProperty(key)) {
      let item = fn(key, obj[key], obj);
      if(item) ret[item[0]] = item[1];
    }
  }
  return ret;
}

export function randInt(...args) {
  let range = args.splice(0, 2);
  let rng = args.shift() ?? Math.random;
  if(range.length < 2) range.unshift(0);
  return Math.round(rand(range[1] - range[0] + 1) + range[0]);
}

export function randFloat(min, max, rng = Math.random) {
  return rng() * (max - min) + min;
}

export function randStr(n, set = '_0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz', rng = Math.random) {
  let o = '';

  while(--n >= 0) o += set[Math.round(rng() * (set.length - 1))];
  return o;
}

export function toBigInt(arg) {
  if(types.isArrayBuffer(arg)) {
    const bits = bits(arg).join('');
    return eval(`0b${bits}n`);
  }
  return BigInt(arg);
}

export function roundDigits(precision) {
  if(typeof precision == 'number') return -Math.log10(precision);

  precision = precision + '';
  let index = precision.indexOf('.');
  let frac = index == -1 ? '' : precision.slice(index + 1);
  return frac.length;

  return -clamp(-Infinity, 0, Math.floor(Math.log10(precision - Number.EPSILON)));
}

export function roundTo(value, prec, digits, type = 'round') {
  if(!Number.isFinite(value)) return value;
  const fn = Math[type];
  if(prec == 1) return fn(value);
  if(prec < 1 && prec > 0 && !isNumber(digits)) digits = -Math.log10(prec);
  let ret = prec >= Number.EPSILON ? fn(value / prec) * prec : value;
  if(isNumber(digits) && digits >= 1 && digits <= 100) ret = +ret.toFixed(digits);
  return ret;
}

export function lazyProperty(obj, name, getter, opts = {}) {
  return Object.defineProperty(obj, name, {
    get: types.isAsyncFunction(getter)
      ? async function() {
          return replaceProperty(await getter.call(obj, name));
        }
      : function() {
          const value = getter.call(obj, name);
          if(types.isPromise(value)) {
            value.then(v => {
              replaceProperty(v);
              console.log(`lazyProperty resolved `, obj[name]);
              return v;
            });
            return value;
          }
          return replaceProperty(value);
        },
    configurable: true,
    ...opts
  });

  function replaceProperty(value) {
    delete obj[name];
    Object.defineProperty(obj, name, { value, ...opts });
    return value;
  }
}

export function lazyProperties(obj, gettersObj, opts = {}) {
  opts = { enumerable: false, ...opts };
  for(let prop of Object.getOwnPropertyNames(gettersObj)) lazyProperty(obj, prop, gettersObj[prop], opts);
  return obj;
}

export function observeProperties(target = {}, obj, fn = (prop, value) => {}, opts = {}) {
  opts = { enumerable: false, ...opts };
  for(let prop of Object.getOwnPropertyNames(obj)) {
    Object.defineProperty(target, prop, {
      ...opts,
      get: () => obj[prop],
      set: value => {
        let r = fn(prop, value, obj);
        if(r !== false) obj[prop] = value;
        if(isFunction(r)) r(prop, value, obj);
      }
    });
  }
  return target;
}

export function decorate(decorators, obj, ...args) {
  if(!Array.isArray(decorators)) decorators = [decorators];
  for(let decorator of decorators)
    for(let prop of Object.getOwnPropertyNames(obj))
      if(typeof obj[prop] == 'function') obj[prop] = decorator(obj[prop], obj, ...args);
  return obj;
}

export function getOpt(options = {}, args) {
  let short, long;
  let result = {};
  let positional = (result['@'] = []);
  if(!(options instanceof Array)) options = Object.entries(options);
  const findOpt = arg =>
    options.find(([optname, option]) => (Array.isArray(option) ? option.indexOf(arg) != -1 : false) || arg == optname);
  let [, params] = options.find(opt => opt[0] == '@') || [];
  if(typeof params == 'string') params = params.split(',');
  args = args.reduce((acc, arg) => {
    if(/^-[^-]/.test(arg)) {
      let opt = findOpt(arg[1]);
      if(!opt || !opt[1][0]) {
        for(let ch of arg.slice(1)) acc.push('-' + ch);
        return acc;
      }
    }
    acc.push(arg);
    return acc;
  }, []);
  //console.log('getOpt', { args });
  for(let i = 0; i < args.length; i++) {
    const arg = args[i];
    let opt;

    if(arg[0] == '-') {
      let name, value, start, end;
      if(arg[1] == '-') long = true;
      else short = true;
      start = short ? 1 : 2;
      if(short) end = 2;
      else if((end = arg.indexOf('=')) == -1) end = arg.length;
      name = arg.substring(start, end);
      if((opt = findOpt(name))) {
        const [has_arg, handler] = opt[1];
        if(has_arg) {
          //console.log('getOpt', { name, value, arg, end });
          if(arg.length > end) value = arg.substring(end + (arg[end] == '='));
          else value = args[++i];
        } else {
          value = true;
        }
        if(isFunction(handler))
          try {
            value = handler(value, result[opt[0]], options, result);
          } catch(e) {}
        result[opt[0]] = value;
        continue;
      }
    }
    if(params.length) {
      const param = params.shift();
      if((opt = findOpt(param))) {
        const [, [, handler]] = opt;
        let value = arg;
        if(isFunction(handler)) {
          try {
            value = handler(value, result[opt[0]], options, result);
          } catch(e) {}
        }
        const name = opt[0];
        result[opt[0]] = value;
        continue;
      }
    }
    result['@'] = [...(result['@'] ?? []), arg];
  }
  return result;
}

export function showHelp(opts, exitCode = 0) {
  let entries = Array.isArray(opts) ? opts : Object.entries(opts);
  let maxlen = entries.reduce((acc, [name]) => (acc > name.length ? acc : name.length), 0);

  let s = entries.reduce(
    (acc, [name, [hasArg, fn, shortOpt]]) =>
      acc +
      (
        `    ${(shortOpt ? '-' + shortOpt + ',' : '').padStart(4, ' ')} --${name.padEnd(maxlen, ' ')} ` +
        (hasArg ? (typeof hasArg == 'boolean' ? 'ARG' : hasArg) : '')
      ).padEnd(40, ' ') +
      '\n',
    `Usage: ${basename(scriptArgs[0])} [OPTIONS] <FILES...>\n\n`
  );

  std.puts(s + '\n');
  std.exit(exitCode);
}

export function toUnixTime(dateObj, utc = false) {
  if(!(dateObj instanceof Date)) dateObj = new Date(dateObj);
  let epoch = Math.floor(dateObj.getTime() / 1000);
  if(utc) epoch += dateObj.getTimezoneOffset() * 60;
  return epoch;
}

export function unixTime(utc = false) {
  return toUnixTime(new Date(), utc);
}

export function fromUnixTime(epoch, utc = false) {
  let t = parseInt(epoch);
  let d = new Date(0);
  utc ? d.setUTCSeconds(t) : d.setSeconds(t);
  return d;
}

export function range(...args) {
  let [start, end, step = 1] = args;
  let ret;
  start /= step;
  end /= step;
  if(start > end) {
    ret = [];
    while(start >= end) ret.push(start--);
  } else {
    ret = Array.from({ length: end - start + 1 }, (v, k) => k + start);
  }
  if(step != 1) {
    ret = ret.map(n => n * step);
  }
  return ret;
}

export function chunkArray(arr, size) {
  const fn = (a, v, i) => {
    const j = i % size;
    if(j == 0) a.push([]);
    a[a.length - 1].push(v);
    return a;
  };

  return arr.reduce(fn, []);
}

export function decodeHTMLEntities(text) {
  const entities = {
    amp: '&',
    apos: "'",
    '#x27': "'",
    '#x2F': '/',
    '#39': "'",
    '#47': '/',
    lt: '<',
    gt: '>',
    nbsp: ' ',
    quot: '"'
  };
  return (text + '').replace(new RegExp('&([^;]+);', 'gm'), (match, entity) => entities[entity] || match);
}

export function ucfirst(str) {
  if(!isString(str)) str = str + '';
  return str.substring(0, 1).toUpperCase() + str.substring(1);
}

export function lcfirst(str) {
  if(!isString(str)) str = str + '';
  return str.substring(0, 1).toLowerCase() + str.substring(1);
}

export function camelize(str, delim = '') {
  return str.replace(/^([A-Z])|[\s-_]+(\w)/g, (match, p1, p2, offset) => {
    if(p2) return delim + p2.toUpperCase();
    return p1.toLowerCase();
  });
}

export function decamelize(str, delim = '-') {
  return /.[A-Z]/.test(str)
    ? str
        .replace(/([a-z\d])([A-Z])/g, '$1' + delim + '$2')
        .replace(/([A-Z]+)([A-Z][a-z\d]+)/g, '$1' + delim + '$2')
        .toLowerCase()
    : str;
}

export function shorten(str, max = 40, suffix = '...') {
  max = +max;
  if(isNaN(max)) max = Infinity;
  if(Array.isArray(str)) return Array.prototype.slice.call(str, 0, Math.min(str.length, max)).concat([suffix]);
  if(!isString(str) || !Number.isFinite(max) || max < 0) return str;
  str = '' + str;

  if(str.length > max) {
    let n = Math.floor((max - (2 + suffix.length)) / 2);
    let tail = str.length - n;
    let len = Math.min(n, tail);
    let insert = ' ' + suffix + ' ' + (str.length - (len + n)) + ' bytes ' + suffix + ' ';

    return str.substring(0, len) + insert + str.substring(tail);
  }
  return str;
}

export function* bytesToUTF8(bytes) {
  if(bytes instanceof ArrayBuffer) bytes = new Uint8Array(bytes);
  let state = 0,
    val = 0;
  for(const c of bytes) {
    if(state !== 0 && c >= 0x80 && c < 0xc0) {
      val = (val << 6) | (c & 0x3f);
      state--;
      if(state === 0) yield val;
    } else if(c >= 0xc0 && c < 0xf8) {
      state = 1 + (c >= 0xe0) + (c >= 0xf0);
      val = c & ((1 << (6 - state)) - 1);
    } else {
      state = 0;
      yield c;
    }
  }
}

export function codePointsToString(codePoints) {
  let s = '';
  for(let c of codePoints) s += String.fromCodePoint(c);
  return s;
}

export function bufferToString(b) {
  return codePointsToString(bytesToUTF8(b));
}

export function arraysInCommon(a) {
  let i,
    c,
    n = a.length,
    min = Infinity;
  while(n) {
    if(a[--n].length < min) {
      min = a[n].length;
      i = n;
    }
  }
  c = Array.from(a.splice(i, 1)[0]);
  return c.filter((itm, indx) => {
    if(c.indexOf(itm) == indx) return a.every(arr => arr.indexOf(itm) != -1);
  });
}

export function arrayFacade(proto, itemFn = (container, i) => container.at(i)) {
  return define(proto, {
    *[Symbol.iterator]() {
      const { length } = this;
      for(let i = 0; i < length; i++) yield itemFn(this, i);
    },
    *keys() {
      const { length } = this;
      for(let i = 0; i < length; i++) yield i;
    },
    *entries() {
      const { length } = this;
      for(let i = 0; i < length; i++) yield [i, itemFn(this, i)];
    },
    *values() {
      const { length } = this;
      for(let i = 0; i < length; i++) yield itemFn(this, i);
    },
    forEach(callback, thisArg) {
      const { length } = this;
      for(let i = 0; i < length; i++) callback.call(thisArg, itemFn(this, i), i, this);
    },
    reduce(callback, accu, thisArg) {
      const { length } = this;
      for(let i = 0; i < length; i++) accu = callback.call(thisArg, accu, itemFn(this, i), i, this);
      return accu;
    }
  });
}

export function mod(a, b) {
  return isNumber(b) ? ((a % b) + b) % b : n => ((n % a) + a) % a;
}

export function pushUnique(arr, ...args) {
  let reject = [];
  for(let arg of args)
    if(arr.indexOf(arg) == -1) arr.push(arg);
    else reject.push(arg);
  return reject;
}

export function inserter(dest, next = (k, v) => {}) {
  const insert =
    isFunction(dest.set) && dest.set.length >= 2
      ? (k, v) => dest.set(k, v)
      : Array.isArray(dest)
      ? (k, v) => dest.push([k, v])
      : (k, v) => (dest[k] = v);
  let fn;
  fn = function(key, value) {
    insert(key, value);
    next(key, value);
    return fn;
  };
  fn.dest = dest;
  fn.insert = insert;
  return fn;
}

export function intersect(a, b) {
  if(!Array.isArray(a)) a = [...a];
  return a.filter(Set.prototype.has, new Set(b));
}

export function symmetricDifference(a, b) {
  return [].concat(...difference(a, b));
}

export function* partitionArray(a, size) {
  for(let i = 0; i < a.length; i += size) yield a.slice(i, i + size);
}

export function difference(a, b, includes) {
  if(!Array.isArray(a)) a = [...a];
  if(!Array.isArray(b)) b = [...b];

  if(!isFunction(includes)) return [a.filter(x => !b.includes(x)), b.filter(x => !a.includes(x))];

  return [a.filter(x => !includes(b, x)), b.filter(x => !includes(a, x))];
}

export function intersection(a, b) {
  if(!(a instanceof Set)) a = new Set(a);
  if(!(b instanceof Set)) b = new Set(b);
  let intersection = new Set([...a].filter(x => b.has(x)));
  return Array.from(intersection);
}

export function union(a, b, equality) {
  if(equality === undefined) return [...new Set([...a, ...b])];

  return unique([...a, ...b], equality);
}

/**
 * accepts array and function returning `true` or `false` for each element
 *
 * @param  {[type]}   array    [description]
 * @param  {Function} callback [description]
 * @return {[type]}            [description]
 */
export function partition(array, callback) {
  const matches = [],
    nonMatches = [];

  // push each element into array depending on return value of `callback`
  for(let element of array) (callback(element) ? matches : nonMatches).push(element);

  return [matches, nonMatches];
}

export function push(obj, ...values) {
  assert(isObject(obj));
  if(isFunction(obj.push)) {
    obj.push(...values);
  } else if(isFunction(obj.add)) {
    values.forEach(v => obj.add(v));
  } else if(typeof obj.length == 'number' && (obj.length === 0 || obj[obj.length - 1] !== undefined)) {
    for(let item of values) obj[obj.length++] = item;
  } else {
    throw new Error(`Don't know how to push value to ${className(obj)}`);
  }
  return obj;
}

export function repeater(n, what) {
  if(isNumber(n)) {
    let max = n;
    n = () => --max >= 0;
  }
  if(isFunction(what))
    return (function* () {
      for(let i = 0; ; i++) {
        let value = what();
        if(!n(value, i)) break;
        yield value;
      }
    })();
  return (function* () {
    for(let i = 0; ; i++) {
      let value = what;
      if(!n(value, i)) break;
      yield value;
    }
  })();
}

export function repeat(n, what) {
  return repeater(n, what);
}

export function functionName(fn) {
  if(isFunction(fn) && isString(fn.name)) return fn.name;
  try {
    const matches = /function\s*([^(]*)\(.*/g.exec(fn + '');
    if(matches && matches[1]) return matches[1];
  } catch {}
  return null;
}

export { functionName as fnName };

export function className(obj) {
  if(isObject(obj)) {
    if('constructor' in obj) return functionName(obj.constructor);
    if(Symbol.toStringTag in obj) return obj[Symbol.toStringTag];
  }
  return null;
}

export const isArrowFunction = fn =>
  (isFunction(fn) && !('prototype' in fn)) || /\ =>\ /.test(('' + fn).replace(/\n.*/g, ''));

// time a given function
export function instrument(
  fn,
  log = (duration, name, args, ret) =>
    console.log(
      `function '${name}'` +
        (ret !== undefined ? ` {= ${escape(ret + '').substring(0, 100) + '...'}}` : '') +
        ` timing: ${duration.toFixed(3)}ms`
    ),
  logInterval = 0 //1000
) {
  // const { now, hrtime, functionName } = Util;
  let last = Date.now();
  let duration = 0,
    times = 0;
  const name = functionName(fn) || '<anonymous>';
  const asynchronous = isAsync(fn) || isAsync(now);
  const doLog = asynchronous
    ? async (args, ret) => {
        let t = Date.now();
        if(t - (await last) >= logInterval) {
          log(duration / times, name, args, ret);
          duration = times = 0;
          last = t;
        }
      }
    : (args, ret) => {
        let t = Date.now();
        //console.log('doLog', { passed: t - last, logInterval });
        if(t - last >= logInterval) {
          log(duration / times, name, args, ret);
          duration = times = 0;
          last = t;
        }
      };

  return asynchronous
    ? async function(...args) {
        const start = Date.now();
        let ret = await fn.apply(this, args);
        duration += Date.now() - start;
        times++;
        await doLog(args, ret);
        return ret;
      }
    : function(...args) {
        const start = Date.now();
        let ret = fn.apply(this, args);
        duration += Date.now() - start;
        times++;
        doLog(args, ret);
        return ret;
      };
}

export const hash = (newMap = () => new Map()) => {
  let map = newMap();
  let cache = memoize((...args) => gettersetter(newMap(...args)), new Map());

  // let [get, set] = getset(cache);

  return {
    get(path) {
      let i = 0,
        obj = map;
      for(let part of path) {
        let cachefn = cache(obj) ?? getter(obj);
        console.log('cache', { i, cache });
        obj = cachefn(part);
        console.log('cachefn', { i, cachefn });
      }
      return obj;
    },
    set(path, value) {
      let i = 0,
        obj = map;
      let key = path.pop();

      for(let part of path) {
        console.log('cache', { part, obj });
        let cachefn = cache(obj.receiver ?? obj);
        console.log('cachefn', { i, cachefn });
        obj = cachefn(part) ?? (cachefn(part, gettersetter(newMap())), cachefn(part));
        console.log('cachefn', { obj });
      }
      return obj(key, value);
    }
  };
};

export const catchable = function Catchable(self) {
  assert(isFunction(self));

  if(!(self instanceof catchable)) Object.setPrototypeOf(self, catchable.prototype);
  if('constructor' in self) self.constructor = catchable;

  return self;
};

Object.assign(catchable, {
  [Symbol.species]: catchable,
  prototype: Object.assign(function () {}, {
    then(fn) {
      return this.constructor[Symbol.species]((...args) => {
        let result;
        try {
          result = this(...args);
        } catch(e) {
          throw e;
          return;
        }
        return fn(result);
      });
    },
    catch(fn) {
      return this.constructor[Symbol.species]((...args) => {
        let result;
        try {
          result = this(...args);
        } catch(e) {
          return fn(e);
        }
        return result;
      });
    }
  })
});

export function isNumeric(value) {
  for(let f of [v => +v, parseInt, parseFloat]) if(!isNaN(f(value))) return true;
  return false;
}

export function isIndex(value) {
  return !isNaN(+value) && Math.floor(+value) + '' == value + '';
}

export function numericIndex(value) {
  return isIndex(value) ? +value : value;
}

export function histogram(arr, out = new Map()) {
  let [get, set] = getset(out);

  const incr = key => set(key, (get(key) ?? 0) + 1);
  for(let item of arr) {
    incr(item);
  }
  return out;
}

export function propertyLookupHandlers(handler = key => null) {
  let handlers = {
    get(target, key, receiver) {
      return handler(key);
    }
  };
  let tmp = handler();

  if(!isString(tmp))
    try {
      let a = Array.isArray(tmp) ? tmp : [...tmp];
      if(a)
        handlers.ownKeys = function(target) {
          return handler();
        };
    } catch(e) {}

  return handlers;
}

export function propertyLookup(...args) {
  let [obj = {}, handler = key => null] = args.length == 1 ? [{}, ...args] : args;

  return new Proxy(
    obj,
    propertyLookupHandlers(function (...args) {
      if(args.length >= 1 && args[0] !== undefined) {
        let [key] = args;
        if(key in obj) return obj[key];

        return handler(key);
      }
      return handler();
    })
  );
}

export function abbreviate(str, max = 40, suffix = '...') {
  max = +max;
  if(isNaN(max)) max = Infinity;
  if(Array.isArray(str)) {
    return Array.prototype.slice.call(str, 0, Math.min(str.length, max)).concat([suffix]);
  }
  if(!isString(str) || !Number.isFinite(max) || max < 0) return str;
  str = '' + str;
  if(str.length > max) {
    return str.substring(0, max - suffix.length) + suffix;
  }
  return str;
}

export function tryFunction(fn, resolve = a => a, reject = () => null) {
  if(!isFunction(resolve)) {
    let rval = resolve;
    resolve = () => rval;
  }
  if(!isFunction(reject)) {
    let cval = reject;
    reject = () => cval;
  }
  return isAsync(fn)
    ? async function(...args) {
        let ret;
        try {
          ret = await fn(...args);
        } catch(err) {
          return reject(err, ...args);
        }
        return resolve(ret, ...args);
      }
    : function(...args) {
        let ret;
        try {
          ret = fn(...args);
        } catch(err) {
          return reject(err, ...args);
        }
        return resolve(ret, ...args);
      };
}

export function tryCatch(fn, resolve = a => a, reject = () => null, ...args) {
  if(isAsync(fn))
    return fn(...args)
      .then(resolve)
      .catch(reject);

  return tryFunction(fn, resolve, reject)(...args);
}

export function mapAdapter(fn) {
  let r = {
    get(key) {
      return fn(key);
    },
    set(key, value) {
      fn(key, value);
      return this;
    }
  };
  let tmp = fn();
  if(types.isIterable(tmp) || types.isPromise(tmp)) r.keys = () => fn();

  if(fn[Symbol.iterator]) r.entries = fn[Symbol.iterator];
  else {
    let g = fn();
    if(types.isIterable(g) || types.isGeneratorFunction(g)) r.entries = () => fn();
  }

  return mapFunction(r);
}

/**
 * @param Array   forward
 * @param Array   backward
 *
 * component2path,  path2eagle  => component2eagle
 *  eagle2path, path2component =>
 */
export function mapFunction(map) {
  let fn;
  fn = function(...args) {
    const [key, value] = args;
    switch (args.length) {
      case 0:
        return fn.keys();
      case 1:
        return fn.get(key);
      case 2:
        return fn.set(key, value);
    }
  };

  fn.map = (m => {
    while(isFunction(m) && m.map !== undefined) m = m.map;
    return m;
  })(map);

  if(map instanceof Map || (isObject(map) && isFunction(map.get) && isFunction(map.set))) {
    fn.set = (key, value) => (map.set(key, value), (k, v) => fn(k, v));
    fn.get = key => map.get(key);
  } else if(map instanceof Cache || (isObject(map) && isFunction(map.match) && isFunction(map.put))) {
    fn.set = (key, value) => (map.put(key, value), (k, v) => fn(k, v));
    fn.get = key => map.match(key);
  } else if(isObject(map) && isFunction(map.getItem) && isFunction(map.setItem)) {
    fn.set = (key, value) => (map.setItem(key, value), (k, v) => fn(k, v));
    fn.get = key => map.getItem(key);
  } else {
    fn.set = (key, value) => ((map[key] = value), (k, v) => fn(k, v));
    fn.get = key => map[key];
  }

  fn.update = function(key, fn = (k, v) => v) {
    let oldValue = this.get(key);
    let newValue = fn(oldValue, key);
    if(oldValue != newValue) {
      if(newValue === undefined && isFunction(map.delete)) map.delete(key);
      else this.set(key, newValue);
    }
    return newValue;
  };

  if(isFunction(map.entries)) {
    fn.entries = function* () {
      for(let [key, value] of map.entries()) yield [key, value];
    };
    fn.values = function* () {
      for(let [key, value] of map.entries()) yield value;
    };
    fn.keys = function* () {
      for(let [key, value] of map.entries()) yield key;
    };
    fn[Symbol.iterator] = fn.entries;
    fn[inspectSymbol] = function() {
      return new Map(this.map(([key, value]) => [Array.isArray(key) ? key.join('.') : key, value]));
    };
  } else if(isFunction(map.keys)) {
    if(isAsync(map.keys) || types.isPromise(map.keys())) {
      fn.keys = async () => [...(await map.keys())];

      fn.entries = async () => {
        let r = [];
        for(let key of await fn.keys()) r.push([key, await fn.get(key)]);
        return r;
      };
      fn.values = async () => {
        let r = [];
        for(let key of await fn.keys()) r.push(await fn.get(key));
        return r;
      };
    } else {
      fn.keys = function* () {
        for(let key of map.keys()) yield key;
      };

      fn.entries = function* () {
        for(let key of fn.keys()) yield [key, fn(key)];
      };
      fn.values = function* () {
        for(let key of fn.keys()) yield fn(key);
      };
    }
  }

  if(isFunction(fn.entries)) {
    fn.filter = function(pred) {
      return mapFunction(
        new Map(
          (function* () {
            let i = 0;
            for(let [key, value] of fn.entries()) if(pred([key, value], i++)) yield [key, value];
          })()
        )
      );
    };
    fn.map = function(t) {
      return mapFunction(
        new Map(
          (function* () {
            let i = 0;

            for(let [key, value] of fn.entries()) yield t([key, value], i++);
          })()
        )
      );
    };
    fn.forEach = function(fn) {
      let i = 0;

      for(let [key, value] of this.entries()) fn([key, value], i++);
    };
  }
  if(isFunction(map.delete)) fn.delete = key => map.delete(key);

  if(isFunction(map.has)) fn.has = key => map.has(key);
  return fn;
}

export function mapWrapper(map, toKey = key => key, fromKey = key => key) {
  let fn = mapFunction(map);
  fn.set = (key, value) => (map.set(toKey(key), value), (k, v) => fn(k, v));
  fn.get = key => map.get(toKey(key));
  if(isFunction(map.keys)) fn.keys = () => [...map.keys()].map(fromKey);
  if(isFunction(map.entries))
    fn.entries = function* () {
      for(let [key, value] of map.entries()) yield [fromKey(key), value];
    };
  if(isFunction(map.values))
    fn.values = function* () {
      for(let value of map.values()) yield value;
    };
  if(isFunction(map.has)) fn.has = key => map.has(toKey(key));
  if(isFunction(map.delete)) fn.delete = key => map.delete(toKey(key));

  fn.map = (m => {
    while(isFunction(m) && m.map !== undefined) m = m.map;
    return m;
  })(map);

  return fn;
}

export function weakMapper(createFn, map = new WeakMap(), hitFn) {
  let self = function(obj, ...args) {
    let ret;
    if(map.has(obj)) {
      ret = map.get(obj);
      if(isFunction(hitFn)) hitFn(obj, ret);
    } else {
      ret = createFn(obj, ...args);
      map.set(obj, ret);
    }
    return ret;
  };
  self.set = (k, v) => map.set(k, v);
  self.get = k => map.get(k);
  self.map = map;
  return self;
}

export function wrapGenerator(fn) {
  return types.isGeneratorFunction(fn)
    ? function(...args) {
        return [...fn.call(this, ...args)];
      }
    : fn;
}

export function wrapGeneratorMethods(obj) {
  for(let name of keys(obj, 1, 0)) if(isFunction(obj[name])) obj[name] = wrapGenerator(obj[name]);

  return obj;
}

export function isBrowser() {
  let ret = false;
  tryCatch(
    () => window,
    w => (isObject(w) ? (ret = true) : undefined),
    () => {}
  );
  tryCatch(
    () => document,
    d => (d == window.document && isObject(d) ? (ret = true) : undefined),
    () => {}
  );
  return ret;
}

export function startInteractive() {
  os.kill(getpid(), os.SIGUSR1);
}

export const matchAll = curry(function* (re, str) {
  let match;
  re = re instanceof RegExp ? re : new RegExp(Array.isArray(re) ? '(' + re.join('|') + ')' : re, 'g');
  do {
    if((match = re.exec(str))) yield match;
  } while(match != null);
});

export function indexOf(...args) {
  let [obj, what, offset] = args;

  const hasMethod = typeof obj == 'string' || (isObject(obj) && 'indexOf' in obj);
  const isAB = types.isArrayBuffer(obj) || types.isTypedArray(obj);

  if(args.length == 1) {
    if(hasMethod) return obj.indexOf.bind(obj);
    if(isAB)
      return (what, offset = 0) => {
        let r = searchArrayBuffer(obj, what, offset);
        return r === null ? -1 : r;
      };
    if(types.isArrayLike(obj))
      return (what, offset = 0) => {
        let { length } = obj;
        for(let i = offset >= 0 ? offset : 0; i < length; i++) if(obj[i] === what) return i;
        return -1;
      };
  } else {
    if(hasMethod) return obj.indexOf(what, offset);
    if(isAB) {
      let r = searchArrayBuffer(obj, what, offset);
      return r === null ? -1 : r;
    }
    if(types.isArrayLike(obj)) {
      let { length } = obj;
      for(let i = offset >= 0 ? offset : 0; i < length; i++) if(obj[i] === what) return i;
      return -1;
    }
  }
}

export function* searchAll(haystack, needle, offset = 0) {
  let r,
    i = offset;
  const a = i.constructor(1),
    fn = indexOf(haystack);
  while((r = fn(needle, i)) != -1) {
    yield r;
    i = r + a;
  }
}

export function bindProperties(obj, target, props, gen) {
  if(props instanceof Array) props = Object.fromEntries(props.map(name => [name, name]));
  const [propMap, propNames] = Array.isArray(props)
    ? [props.reduce((acc, name) => ({ ...acc, [name]: name }), {}), props]
    : [props, Object.keys(props)];
  gen ??= p => v => v === undefined ? target[propMap[p]] : (target[propMap[p]] = v);
  const propGetSet = propNames
    .map(k => [k, propMap[k]])
    .reduce(
      (a, [k, v]) => ({
        ...a,
        [k]: isFunction(v)
          ? (...args) => v.call(target, k, ...args)
          : (gen && gen(k)) || ((...args) => (args.length > 0 ? (target[k] = args[0]) : target[k]))
      }),
      {}
    );
  Object.defineProperties(
    obj,
    propNames.reduce(
      (a, k) => {
        const prop = props[k];
        const get_set = propGetSet[k];
        return {
          ...a,
          [k]: {
            get: get_set,
            set: get_set,
            enumerable: true
          }
        };
      },
      {
        __getter_setter__: { value: gen, enumerable: false },
        __bound_target__: { value: target, enumerable: false }
      }
    )
  );
  return obj;
}

export function predicate(fn_or_regex, pred) {
  let fn = fn_or_regex;
  if(typeof fn_or_regex == 'string') fn_or_regex = new RegExp('^' + fn_or_regex + '$');
  if(isObject(fn_or_regex) && fn_or_regex instanceof RegExp) {
    fn = arg => fn_or_regex.test(arg + '');
    fn.valueOf = function() {
      return fn_or_regex;
    };
  }
  if(isFunction(pred)) return arg => pred(arg, fn);
  return fn;
}

export function transformer(a, ...l) {
  return l.reduce(
    (acc, fn) =>
      function(...v) {
        return fn.call(this, acc.call(this, ...v), ...v);
      },
    a
  );
}

export const ansiStyles = getAnsiStyles();

export const inspectSymbol = Symbol.for('quickjs.inspect.custom');

export { extendArray, ArrayExtensions } from './extendArray.js';
export { extendGenerator, GeneratorExtensions } from './extendGenerator.js';
export { extendAsyncGenerator, AsyncGeneratorExtensions } from './extendAsyncGenerator.js';

export * from 'misc';
