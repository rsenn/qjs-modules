import inspect from 'inspect';
import {
  SyscallError,
  arrayToBitfield,
  atob,
  atomToString,
  atomToValue,
  bitfieldToArray,
  btoa,
  compileFile,
  concatArrayBuffer,
  dupArrayBuffer,
  evalBinary,
  getByteCode,
  getClassAtom,
  getClassConstructor,
  getClassCount,
  getClassID,
  getClassName,
  getClassProto,
  getCommandLine,
  getCurrentWorkingDirectory,
  getExecutable,
  getFileDescriptor,
  getOpCodes,
  getPerformanceCounter,
  getProcMaps,
  getProcMounts,
  getProcStat,
  getPrototypeChain,
  getRootDirectory,
  getegid,
  geteuid,
  getgid,
  getpid,
  getppid,
  getsid,
  getuid,
  hrtime,
  readObject,
  resizeArrayBuffer,
  setegid,
  seteuid,
  setgid,
  setuid,
  toArrayBuffer,
  toPointer,
  toString,
  uname,
  valuePtr,
  valueTag,
  valueToAtom,
  valueType,
  writeObject
} from 'misc';
import { extendArray, ArrayExtensions } from './extendArray.js';
import * as os from 'os';

const slice = (x, s, e) => String.prototype.slice.call(x, s, e);
const stringify = v => `${v}`;
const protoOf = Object.getPrototypeOf;
const formatNumber = n => (n === -0 ? '-0' : `${n}`);
const isNative = fn => /\[native\scode\]/.test(stringify(fn));

export default function util() {
  return util;
}

util.prototype.constructor = util;

const AsyncFunction = async function x() {}.constructor;
const GeneratorFunction = function* () {}.constructor;
const AsyncGeneratorFunction = async function* () {}.constructor;
const TypedArray = protoOf(protoOf(new Uint16Array(10))).constructor;

const SetIteratorPrototype = protoOf(new Set().values());
const MapIteratorPrototype = protoOf(new Map().entries());
const GeneratorPrototype = protoOf((function* () {})());

export const errors = [
  null,
  'EPERM',
  'ENOENT',
  'ESRCH',
  'EINTR',
  'EIO',
  'ENXIO',
  'E2BIG',
  'ENOEXEC',
  'EBADF',
  'ECHILD',
  'EAGAIN',
  'ENOMEM',
  'EACCES',
  'EFAULT',
  'ENOTBLK',
  'EBUSY',
  'EEXIST',
  'EXDEV',
  'ENODEV',
  'ENOTDIR',
  'EISDIR',
  'EINVAL',
  'ENFILE',
  'EMFILE',
  'ENOTTY',
  'ETXTBSY',
  'EFBIG',
  'ENOSPC',
  'ESPIPE',
  'EROFS',
  'EMLINK',
  'EPIPE',
  'EDOM',
  'ERANGE',
  'EDEADLK',
  'ENAMETOOLONG',
  'ENOLCK',
  'ENOSYS',
  'ENOTEMPTY',
  null,
  null,
  'ENOMSG',
  'EIDRM',
  'ECHRNG',
  'EL2NSYNC',
  'EL3HLT',
  'EL3RST',
  'ELNRNG',
  'EUNATCH',
  'ENOCSI',
  'EL2HLT',
  'EBADE',
  'EBADR',
  'EXFULL',
  'ENOANO',
  'EBADRQC',
  null,
  '',
  'EBFONT',
  'ENOSTR',
  'ENODATA',
  'ETIME',
  'ENOSR',
  'ENONET',
  'ENOPKG',
  'EREMOTE',
  'ENOLINK',
  'EADV',
  'ESRMNT',
  'ECOMM',
  'EPROTO',
  'EMULTIHOP',
  'EDOTDOT',
  'EBADMSG',
  'EOVERFLOW',
  'ENOTUNIQ',
  'EBADFD',
  'EREMCHG',
  'ELIBACC',
  'ELIBBAD',
  'ELIBSCN',
  'ELIBMAX',
  'ELIBEXEC',
  'EILSEQ',
  'ERESTART',
  'ESTRPIPE',
  'EUSERS',
  'ENOTSOCK',
  'EDESTADDRREQ',
  'EMSGSIZE',
  'EPROTOTYPE',
  'ENOPROTOOPT',
  'EPROTONOSUPPORT',
  'ESOCKTNOSUPPORT',
  'EOPNOTSUPP',
  'EPFNOSUPPORT',
  'EAFNOSUPPORT',
  'EADDRINUSE',
  'EADDRNOTAVAIL',
  'ENETDOWN',
  'ENETUNREACH',
  'ENETRESET',
  'ECONNABORTED',
  'ECONNRESET',
  'ENOBUFS',
  'EISCONN',
  'ENOTCONN',
  'ESHUTDOWN',
  'ETOOMANYREFS',
  'ETIMEDOUT',
  'ECONNREFUSED',
  'EHOSTDOWN',
  'EHOSTUNREACH',
  'EALREADY',
  'EINPROGRESS',
  'ESTALE',
  'EUCLEAN',
  'ENOTNAM',
  'ENAVAIL',
  'EISNAM',
  'EREMOTEIO',
  'EDQUOT',
  'ENOMEDIUM',
  'EMEDIUMTYPE',
  'ECANCELED',
  'ENOKEY',
  'EKEYEXPIRED',
  'EKEYREVOKED',
  'EKEYREJECTED',
  'EOWNERDEAD',
  'ENOTRECOVERABLE',
  'ERFKILL'
];

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
  }
};

export function isObject(v) {
  return v != null && { function: true, object: true }[typeof v];
}

export function hasBuiltIn(o, m) {
  return isNative(protoOf(o)[m]);
}

export function format(...args) {
  return formatWithOptionsInternal(undefined, args);
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
        const c = String.prototype.charCodeAt.call(x, ++i);
        if(a + 1 !== v.length) {
          switch (c) {
            case 115:
              const y = v[++a];
              if(typeof y === 'number') t = formatNumber(y);
              else if(typeof y === 'bigint') t = `${y}n`;
              else if(typeof y !== 'object' || y === null || !hasBuiltIn(y, 'toString'))
                t = String(y);
              else t = inspect(y, { ...o, compact: 3, colors: false, depth: 0 });
              break;
            case 106:
              t = stringify(v[++a]);
              break;
            case 100:
              const n = v[++a];
              if(typeof n === 'bigint') t = `$numn`;
              else if(typeof n === 'symbol') t = 'NaN';
              else t = formatNumber(Number(n));
              break;
            case 79:
              t = inspect(v[++a], o);
              break;
            case 111:
              t = inspect(v[++a], {
                ...o,
                showHidden: true,
                showProxy: true,
                depth: 4
              });
              break;
            case 105:
              const k = v[++a];
              if(typeof k === 'bigint') t = `${k}`;
              else if(typeof k === 'symbol') t = 'NaN';
              else t = formatNumber(parseInt(k));
              break;
            case 102:
              const d = v[++a];
              if(typeof d === 'symbol') t = 'NaN';
              else t = formatNumber(parseFloat(d));
              break;
            case 99:
              a += 1;
              t = '';
              break;
            case 37:
              s += slice(x, p, i);
              p = i + 1;
              continue;
            default:
              continue;
          }
          if(p !== i - 1) s += slice(x, p, i - 1);
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
    'assertion failed: got |' +
      actual +
      '|' +
      ', expected |' +
      expected +
      '|' +
      (message ? ' (' + message + ')' : '')
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

export function memoize(fn) {
  let cache = {};
  return (n, ...rest) => {
    if(n in cache) return cache[n];
    return (cache[n] = fn(n, ...rest));
  };
}

export function define(obj, ...args) {
  for(let props of args) {
    let desc = Object.getOwnPropertyDescriptors(props);
    for(let prop in desc) {
      const { value } = desc[prop];
      if(typeof value == 'function') desc[prop].writable = false;
    }
    Object.defineProperties(obj, desc);
  }
  return obj;
}

export function getConstructorChain(obj) {
  let ret = [];
  let chain = getPrototypeChain(obj);
  if(obj.constructor && obj.constructor != chain[0].constructor) chain.unshift(obj);

  for(let proto of chain) ret.push(proto.constructor);
  return ret;
}

const inspectMethod = Symbol.for('nodejs.util.inspect.custom');

util.inspect = inspect;
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
util.define = define;
util.ArrayExtensions = ArrayExtensions;
util.extendArray = (proto = Array.prototype, def = util.define) => def(proto, ArrayExtensions);
util.errors = errors;
util.getPrototypeChain = getPrototypeChain;
util.getConstructorChain = getConstructorChain;

Object.assign(util, {
  [inspectMethod]() {
    let obj = { ...util };
    delete obj[inspectMethod];
    return inspect(obj, { customInspect: false });
  }
});

export { extendArray, ArrayExtensions } from './extendArray.js';
export { toString, toArrayBuffer, btoa, atob, getPrototypeChain } from 'misc';
export { inspect } from 'inspect';
