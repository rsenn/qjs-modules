//import { SyscallError } from 'syscallerror';
import { JS_EVAL_FLAG_BACKTRACE_BARRIER, Location, dupArrayBuffer, escape, getPrototypeChain, isArray, isBigDecimal, isBigFloat, isBigInt, isBool, isCFunction, isConstructor, isEmptyString, isError, isException, isExtensible, isFunction, isHTMLDDA, isInstanceOf, isInteger, isJobPending, isLiveObject, isNull, isNumber, isObject, isRegisteredClass, isString, isSymbol, isUncatchableError, isUndefined, isUninitialized, isArrayBuffer, rand, toArrayBuffer, toString, watch, bits } from 'misc';
import { extendArray, ArrayExtensions } from './extendArray.js';
import { extendGenerator, GeneratorExtensions, GeneratorPrototype } from './extendGenerator.js';
import * as os from 'os';

const slice = (x, s, e) =>
  typeof x == 'object'
    ? isArrayBuffer(x)
      ? dupArrayBuffer(x, s, e)
      : Array.isArray(x)
      ? Array.prototype.slice.call(x, s, e)
      : x.slice(s, e)
    : String.prototype.slice.call(x, s, e);
const stringify = v => `${v}`;
const protoOf = Object.getPrototypeOf;
const formatNumber = n => (n === -0 ? '-0' : `${n}`);
const isNative = fn => /\[native\scode\]/.test(stringify(fn));

function util() {
  return util;
}

util.prototype.constructor = util;

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
    return isObject(v) && typeof v[Symbol.iterator] == 'function';
  },
  isAsyncIterable(v) {
    return isObject(v) && typeof v[Symbol.asyncIterator] == 'function';
  },
  isIterator(v) {
    return isObject(v) && typeof v.next == 'function';
  },
  isArrayLike(v) {
    return isObject(v) && typeof v.length == 'number' && Number.isInteger(v.length);
  }
};

export function hasBuiltIn(o, m) {
  return isNative(protoOf(o)[m]);
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
    s += typeof y !== 'string' ? inspect(y, o) : y;
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
    typeof actual == 'object' &&
    typeof expected == 'object' &&
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

export function getset(target, ...args) {
  let ret = [];
  if(isFunction(target)) {
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

export function gettersetter(target, ...args) {
  let fn;
  if(isObject(target) && isFunction(target.receiver)) return (...args2) => target.receiver(...args, ...args2);
  if(isFunction(target)) {
    if(isFunction(args[0]) && args[0] !== target) {
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
  return new Promise(resolve => os.setTimeout(resolve, ms));
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
    for(let key in otherDesc)
      if(!(key in obj) && desc[key] === undefined && otherDesc[key] !== undefined) desc[key] = otherDesc[key];
  }
  return Object.defineProperties(obj, desc);
}

export function getConstructorChain(obj) {
  let ret = [];
  let chain = getPrototypeChain(obj);
  if(obj.constructor && obj.constructor != chain[0].constructor) chain.unshift(obj);
  for(let proto of chain) ret.push(proto.constructor);
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

export const unique = (arr, cmp) =>
  arr.filter(
    typeof cmp == 'function'
      ? (el, i, arr) => arr.findIndex(item => cmp(el, item)) == i
      : (el, i, arr) => arr.indexOf(el) == i
  );

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
              console.log(`util.lazyProperty resolved `, obj[name]);
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
    if(/^-[^-]/.test(arg)) for(let ch of arg.slice(1)) acc.push('-' + ch);
    else acc.push(arg);
    return acc;
  }, []);
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
          if(arg.length > end) value = arg.substring(end + (arg[end] == '='));
          else value = args[++i];
        } else {
          value = true;
        }
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
        if(typeof handler == 'function') {
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

export function repeater(n, what) {
  if(typeof what == 'function')
    return (function* () {
      for(let i = 0; i < n; i++) yield what();
    })();
  return (function* () {
    for(let i = 0; i < n; i++) yield what;
  })();
}

export function repeat(n, what) {
  return [...repeater(n, what)];
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
  if(typeof str != 'string' || !Number.isFinite(max) || max < 0) return str;
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
  return typeof b == 'number' ? ((a % b) + b) % b : n => ((n % a) + a) % a;
}

export const ansiStyles = getAnsiStyles();

const inspectMethod = Symbol.for('nodejs.util.inspect.custom');

define(util, {
  errors,
  types,
  hasBuiltIn,
  format,
  formatWithOptions,
  assert,
  setInterval,
  clearInterval,
  inherits,
  memoize,
  getset,
  gettersetter,
  hasGetSet,
  mapObject,
  once,
  waitFor,
  define,
  weakAssign,
  getConstructorChain,
  hasPrototype,
  filter,
  curry,
  split,
  unique,
  getFunctionArguments,
  randInt,
  randFloat,
  randStr,
  toBigInt,
  lazyProperty,
  lazyProperties,
  getOpt,
  toUnixTime,
  unixTime,
  fromUnixTime,
  range,
  repeater,
  repeat,
  chunkArray,
  camelize,
  decamelize,
  shorten,
  arraysInCommon,
  mod,
  ansiStyles,
  extendArray
});

/*
util.format = format;
util.formatWithOptions = formatWithOptions;
util.types = types;
util.assert = assert;
util.hasBuiltIn = hasBuiltIn;
util.toString = toString;
util.toArrayBuffer = toArrayBuffer;
util.setInterval = setInterval;
util.clearInterval = clearInterval;
util.memoize = memoize;
util.once = once;
util.define = define;
util.weakAssign = weakAssign;
util.ArrayExtensions = ArrayExtensions;
util.extendArray = (proto = Array.prototype, def = util.define) => def(proto, ArrayExtensions);
util.extendGenerator = (proto = GeneratorPrototype, def = util.define) => def(proto, GeneratorExtensions);
util.errors = errors;
util.getPrototypeChain = getPrototypeChain;
util.getConstructorChain = getConstructorChain;
util.hasPrototype = hasPrototype;
util.ansiStyles = ansiStyles;
util.randInt = randInt;
util.curry = curry;
util.filter = filter;
util.split = split;
util.unique = unique;
util.escape = escape;

Object.assign(util, {
  [inspectMethod]() {
    let obj = { ...util };
    delete obj[inspectMethod];
    return inspect(obj, { customInspect: false });
  }
});*/

export { extendArray, ArrayExtensions } from './extendArray.js';
//export { SyscallError } from 'syscallerror';
export * from 'misc';
export { inspect } from 'inspect';
export default util;
export {
  JS_EVAL_FLAG_BACKTRACE_BARRIER,
  Location,
  dupArrayBuffer,
  escape,
  getPrototypeChain,
  isArray,
  isBigDecimal,
  isBigFloat,
  isBigInt,
  isBool,
  isCFunction,
  isConstructor,
  isEmptyString,
  isError,
  isException,
  isExtensible,
  isFunction,
  isHTMLDDA,
  isInstanceOf,
  isInteger,
  isJobPending,
  isLiveObject,
  isNull,
  isNumber,
  isObject,
  isRegisteredClass,
  isString,
  isSymbol,
  isUncatchableError,
  isUndefined,
  isUninitialized,
  isArrayBuffer,
  rand,
  toArrayBuffer,
  toString,
  watch
} from 'misc';
