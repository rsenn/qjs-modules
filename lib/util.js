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
  writeObject,
  rand,
  randi,
  randf,
  srand
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
        let f = '';
        while('sjxdOoifc%'.indexOf(x[i + 1]) == -1) {
          f += x[i + 1];
          ++i;
        }
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
              if(typeof n === 'bigint') t = `$numn`;
              else if(typeof n === 'symbol') t = 'NaN';
              else t = formatNumber(c == 120 ? Number(n).toString(16) : Number(n));
              break;
            case 79: // %O
              t = inspect(v[++a], o);
              break;
            case 111: // %o
              t = inspect(v[++a], {
                ...o,
                showHidden: true,
                showProxy: true,
                depth: 4
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
          console.log('pad', { pad, f });

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

  if(actual !== null && expected !== null && typeof actual == 'object' && typeof expected == 'object' && actual.toString() === expected.toString()) return;

  throw Error('assertion failed: got |' + actual + '|' + ', expected |' + expected + '|' + (message ? ' (' + message + ')' : ''));
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

export function waitFor(ms) {
  return new Promise(resolve => os.setTimeout(resolve, ms));
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

export function weakAssign(obj, ...args) {
  let desc={};
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

const ANSI_BACKGROUND_OFFSET = 10;

const wrapAnsi16 =
  (offset = 0) =>
  code =>
    `\u001B[${code + offset}m`;

const wrapAnsi256 =
  (offset = 0) =>
  code =>
    `\u001B[${38 + offset};5;${code}m`;

const wrapAnsi16m =
  (offset = 0) =>
  (red, green, blue) =>
    `\u001B[${38 + offset};2;${red};${green};${blue}m`;

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

        return 16 + 36 * Math.round((red / 255) * 5) + 6 * Math.round((green / 255) * 5) + Math.round((blue / 255) * 5);
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

          red = Math.floor(code / 36) / 5;
          green = Math.floor(remainder / 6) / 5;
          blue = (remainder % 6) / 5;
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
  let rnd = args.shift();
  if(range.length < 2) range.unshift(0);
  return Math.round(misc.rand(range[1] - range[0] + 1) + range[0]);
}

export const ansiStyles = getAnsiStyles();

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
util.weakAssign = weakAssign;
util.ArrayExtensions = ArrayExtensions;
util.extendArray = (proto = Array.prototype, def = util.define) => def(proto, ArrayExtensions);
util.errors = errors;
util.getPrototypeChain = getPrototypeChain;
util.getConstructorChain = getConstructorChain;
util.ansiStyles = ansiStyles;
util.randInt = randInt;

Object.assign(util, {
  [inspectMethod]() {
    let obj = { ...util };
    delete obj[inspectMethod];
    return inspect(obj, { customInspect: false });
  }
});

export { extendArray, ArrayExtensions } from './extendArray.js';
export {
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
export { inspect } from 'inspect';
