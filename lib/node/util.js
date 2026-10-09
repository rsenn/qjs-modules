/* node/util.js: Node's `util` (https://nodejs.org/api/util.html).
 * loaded for `node:util`; the bare `util` stays qjs-modules' own helper
 * module, which this one re-exports except where Node's shapes differ.
 *
 * ```js
 * import { inspect, format, promisify, parseArgs, styleText } from 'node:util';
 * ```
 *
 * limits:
 *   inspect()   no Proxy target/handler, Map/Set iterator contents and
 *               WeakMap/WeakSet entries (the engine cannot read them)
 *   types       isProxy/isExternal/isKeyObject/isCryptoKey are false
 *   getCallSites()  parsed from Error().stack; scriptId is '0'
 */
import { signum, promiseState, promiseResult } from 'misc';
import { isDeepEqual as deepEqualImpl, partialDeepEqual as partialDeepEqualImpl } from 'assert';

const { getPrototypeOf, getOwnPropertyDescriptor, getOwnPropertyNames, getOwnPropertySymbols, keys: objectKeys, defineProperty, setPrototypeOf, prototype: ObjectPrototype } = Object;
const hasOwn = (o, k) => Object.prototype.hasOwnProperty.call(o, k);
const proc = () => globalThis.process;

/* ---- errors ---- */

const nodeError = (Type, code, message) => Object.assign(new Type(message), { code });
const describe = v => (v === null ? 'null' : v === undefined ? 'undefined' : typeof v == 'function' ? `function ${v.name}` : typeof v == 'object' ? (v.constructor?.name ? `an instance of ${v.constructor.name}` : inspect(v, { depth: -1 })) : `type ${typeof v} (${inspect(v)})`);
const argTypeError = (name, expected, value) => nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "${name}" argument must be ${expected}. Received ${describe(value)}`);
const argValueError = (name, value, reason = 'is invalid') => nodeError(TypeError, 'ERR_INVALID_ARG_VALUE', `The argument '${name}' ${reason}. Received ${inspect(value)}`);
const rangeError = (name, range, value) => nodeError(RangeError, 'ERR_OUT_OF_RANGE', `The value of "${name}" is out of range. It must be ${range}. Received ${typeof value == 'number' && Number.isInteger(value) && Math.abs(value) > 2 ** 32 ? addNumericSeparator(String(value)) : inspect(value)}`);

class AbortError extends Error {
  constructor(message = 'The operation was aborted', options) {
    super(message, options);
    this.code = 'ABORT_ERR';
    this.name = 'AbortError';
  }
}

/* ---- system errors (Linux errno -> [name, message]) ---- */

const errnoTable = `1:EPERM:operation not permitted\n2:ENOENT:no such file or directory\n3:ESRCH:no such process\n4:EINTR:interrupted system call\n5:EIO:i/o error\n6:ENXIO:no such device or address\n7:E2BIG:argument list too long\n8:ENOEXEC:exec format error\n9:EBADF:bad file descriptor\n11:EAGAIN:resource temporarily unavailable\n12:ENOMEM:not enough memory\n13:EACCES:permission denied\n14:EFAULT:bad address in system call argument\n16:EBUSY:resource busy or locked\n17:EEXIST:file already exists\n18:EXDEV:cross-device link not permitted\n19:ENODEV:no such device\n20:ENOTDIR:not a directory\n21:EISDIR:illegal operation on a directory\n22:EINVAL:invalid argument\n23:ENFILE:file table overflow\n24:EMFILE:too many open files\n25:ENOTTY:inappropriate ioctl for device\n26:ETXTBSY:text file is busy\n27:EFBIG:file too large\n28:ENOSPC:no space left on device\n29:ESPIPE:invalid seek\n30:EROFS:read-only file system\n31:EMLINK:too many links\n32:EPIPE:broken pipe\n34:ERANGE:result too large\n36:ENAMETOOLONG:name too long\n38:ENOSYS:function not implemented\n39:ENOTEMPTY:directory not empty\n40:ELOOP:too many symbolic links encountered\n49:EUNATCH:protocol driver not attached\n61:ENODATA:no data available\n64:ENONET:machine is not on the network\n71:EPROTO:protocol error\n75:EOVERFLOW:value too large for defined data type\n84:EILSEQ:illegal byte sequence\n88:ENOTSOCK:socket operation on non-socket\n89:EDESTADDRREQ:destination address required\n90:EMSGSIZE:message too long\n91:EPROTOTYPE:protocol wrong type for socket\n92:ENOPROTOOPT:protocol not available\n93:EPROTONOSUPPORT:protocol not supported\n94:ESOCKTNOSUPPORT:socket type not supported\n95:ENOTSUP:operation not supported on socket\n97:EAFNOSUPPORT:address family not supported\n98:EADDRINUSE:address already in use\n99:EADDRNOTAVAIL:address not available\n100:ENETDOWN:network is down\n101:ENETUNREACH:network is unreachable\n103:ECONNABORTED:software caused connection abort\n104:ECONNRESET:connection reset by peer\n105:ENOBUFS:no buffer space available\n106:EISCONN:socket is already connected\n107:ENOTCONN:socket is not connected\n108:ESHUTDOWN:cannot send after transport endpoint shutdown\n110:ETIMEDOUT:connection timed out\n111:ECONNREFUSED:connection refused\n112:EHOSTDOWN:host is down\n113:EHOSTUNREACH:host is unreachable\n114:EALREADY:connection already in progress\n121:EREMOTEIO:remote I/O error\n125:ECANCELED:operation canceled\n3000:EAI_ADDRFAMILY:address family not supported\n3001:EAI_AGAIN:temporary failure\n3002:EAI_BADFLAGS:bad ai_flags value\n3003:EAI_CANCELED:request canceled\n3004:EAI_FAIL:permanent failure\n3005:EAI_FAMILY:ai_family not supported\n3006:EAI_MEMORY:out of memory\n3007:EAI_NODATA:no address\n3008:EAI_NONAME:unknown node or service\n3009:EAI_OVERFLOW:argument buffer overflow\n3010:EAI_SERVICE:service not available for socket type\n3011:EAI_SOCKTYPE:socket type not supported\n3013:EAI_BADHINTS:invalid value for hints\n3014:EAI_PROTOCOL:resolved protocol is unknown\n4028:EFTYPE:inappropriate file type or format\n4080:ECHARSET:invalid Unicode character\n4094:UNKNOWN:unknown error\n4095:EOF:end of file`;

let errorMap;
export function getSystemErrorMap() {
  if(!errorMap) {
    errorMap = new Map();

    for(const line of errnoTable.split('\n')) {
      const [n, name, ...msg] = line.split(':');
      errorMap.set(-Number(n), [name, msg.join(':')]);
    }
  }

  return errorMap;
}

const checkErrno = err => {
  if(typeof err != 'number') throw argTypeError('err', 'of type number', err);
  if(err >= 0 || !Number.isSafeInteger(err)) throw rangeError('err', 'a negative integer', err);
};

export function getSystemErrorName(err) {
  checkErrno(err);
  return getSystemErrorMap().get(err)?.[0] ?? `Unknown system error ${err}`;
}

export function getSystemErrorMessage(err) {
  checkErrno(err);
  return getSystemErrorMap().get(err)?.[1] ?? `Unknown system error ${err}`;
}

export function _errnoException(err, syscall, original) {
  const code = getSystemErrorName(err);
  let message = `${syscall} ${code}`;

  if(original) message += ` ${original}`;

  return Object.assign(new Error(message), { errno: err, code, syscall });
}

export function _exceptionWithHostPort(err, syscall, address, port, additional) {
  const code = getSystemErrorName(err);
  let details = port && port > 0 ? `${address}:${port}` : address;

  if(additional) details += ` - Local (${additional})`;

  return Object.assign(new Error(`${syscall} ${code} ${details}`), { errno: err, code, syscall, address, ...(port && port > 0 && { port }) });
}

/* ---- signals ---- */

export function convertProcessSignalToExitCode(signal) {
  if(typeof signal != 'string') throw argTypeError('signal', 'of type string', signal);
  let n;

  try {
    n = /^SIG/.test(signal) ? signum(signal) : undefined;
  } catch(e) {}

  if(!Number.isInteger(n)) throw argValueError('signal', signal, 'is not a valid signal name');
  return 128 + n;
}

export function setTraceSigInt(enable) {
  if(typeof enable != 'boolean') throw argTypeError('enable', 'of type boolean', enable);
}

/* ---- inspect: port of Node's util.inspect ---- */

export const inspectColors = {
  reset: [0, 0], bold: [1, 22], dim: [2, 22], italic: [3, 23], underline: [4, 24], blink: [5, 25], inverse: [7, 27], hidden: [8, 28], strikethrough: [9, 29], doubleunderline: [21, 24],
  black: [30, 39], red: [31, 39], green: [32, 39], yellow: [33, 39], blue: [34, 39], magenta: [35, 39], cyan: [36, 39], white: [37, 39],
  bgBlack: [40, 49], bgRed: [41, 49], bgGreen: [42, 49], bgYellow: [43, 49], bgBlue: [44, 49], bgMagenta: [45, 49], bgCyan: [46, 49], bgWhite: [47, 49],
  framed: [51, 54], overlined: [53, 55],
  gray: [90, 39], redBright: [91, 39], greenBright: [92, 39], yellowBright: [93, 39], blueBright: [94, 39], magentaBright: [95, 39], cyanBright: [96, 39], whiteBright: [97, 39],
  bgGray: [100, 49], bgRedBright: [101, 49], bgGreenBright: [102, 49], bgYellowBright: [103, 49], bgBlueBright: [104, 49], bgMagentaBright: [105, 49], bgCyanBright: [106, 49], bgWhiteBright: [107, 49],
};

for(const [target, alias] of [['gray', 'grey'], ['gray', 'blackBright'], ['bgGray', 'bgGrey'], ['bgGray', 'bgBlackBright'], ['dim', 'faint'], ['strikethrough', 'crossedout'], ['strikethrough', 'strikeThrough'], ['strikethrough', 'crossedOut'], ['hidden', 'conceal'], ['inverse', 'swapColors'], ['inverse', 'swapcolors'], ['doubleunderline', 'doubleUnderline']])
  defineProperty(inspectColors, alias, { get: () => inspectColors[target], set: v => (inspectColors[target] = v), configurable: true, enumerable: false });

const inspectStyles = { special: 'cyan', number: 'yellow', bigint: 'yellow', boolean: 'yellow', undefined: 'grey', null: 'bold', string: 'green', symbol: 'green', date: 'magenta', regexp: 'red', module: 'underline' };

const inspectDefaultOptions = { showHidden: false, depth: 2, colors: false, customInspect: true, showProxy: false, maxArrayLength: 100, maxStringLength: 10000, breakLength: 80, compact: 3, sorted: false, getters: false, numericSeparator: false };

export const inspectCustom = Symbol.for('nodejs.util.inspect.custom');

const kObjectType = 0,
  kArrayType = 1,
  kArrayExtrasType = 2;

const keyStrRegExp = /^[a-zA-Z_][a-zA-Z_0-9]*$/;
const numberRegExp = /^(0|[1-9][0-9]*)$/;
const strEscapeSequencesRegExp = /[\x00-\x1f\x27\x5c\x7f-\x9f]|[\ud800-\udbff](?![\udc00-\udfff])|(?<![\ud800-\udbff])[\udc00-\udfff]/;
const strEscapeSequencesReplacer = /[\x00-\x1f\x27\x5c\x7f-\x9f]|[\ud800-\udbff](?![\udc00-\udfff])|(?<![\ud800-\udbff])[\udc00-\udfff]/g;
const strEscapeSequencesRegExpSingle = /[\x00-\x1f\x5c\x7f-\x9f]|[\ud800-\udbff](?![\udc00-\udfff])|(?<![\ud800-\udbff])[\udc00-\udfff]/;
const strEscapeSequencesReplacerSingle = /[\x00-\x1f\x5c\x7f-\x9f]|[\ud800-\udbff](?![\udc00-\udfff])|(?<![\ud800-\udbff])[\udc00-\udfff]/g;
const classRegExp = /^(\s+[^(]*?)\s*{/;
const stripCommentsRegExp = /(\/\/.*?\n)|(\/\*(.|\n)*?\*\/)/g;
const ansiRegExp = /\u001b\[\d\d?m/g;

const meta = [
  '\\x00', '\\x01', '\\x02', '\\x03', '\\x04', '\\x05', '\\x06', '\\x07', '\\b', '\\t', '\\n', '\\x0B', '\\f', '\\r', '\\x0E', '\\x0F',
  '\\x10', '\\x11', '\\x12', '\\x13', '\\x14', '\\x15', '\\x16', '\\x17', '\\x18', '\\x19', '\\x1A', '\\x1B', '\\x1C', '\\x1D', '\\x1E', '\\x1F',
];

const escapeLone = str => {
  const c = str.charCodeAt(0);
  if(c < 32) return meta[c];
  if(c == 39) return "\\'";
  if(c == 92) return '\\\\';
  if(c >= 0x7f && c <= 0x9f) return `\\x${c.toString(16).toUpperCase()}`;
  return `\\u${c.toString(16)}`;
};

/* quoteString: ' by default, " or ` when the string contains ' */
function quoteString(str) {
  const hasSingle = str.includes("'");
  const q = !hasSingle ? "'" : !str.includes('"') ? '"' : !str.includes('`') && !str.includes('${') ? '`' : "'";
  const re = q == "'" ? strEscapeSequencesReplacer : strEscapeSequencesReplacerSingle;
  const body = str.replace(re, escapeLone);
  return q + body + q;
}

function addNumericSeparator(integerString) {
  let result = '';
  let i = integerString.length;
  const start = integerString.startsWith('-') ? 1 : 0;
  for(; i >= start + 4; i -= 3) result = `_${integerString.slice(i - 3, i)}${result}`;
  return i === integerString.length ? integerString : `${integerString.slice(0, i)}${result}`;
}

function addNumericSeparatorEnd(integerString) {
  let result = '';
  let i = 0;
  for(; i < integerString.length - 3; i += 3) result += `${integerString.slice(i, i + 3)}_`;
  return i === 0 ? integerString : `${result}${integerString.slice(i)}`;
}

function formatNumber(fn, number, numericSeparator) {
  if(!numericSeparator) {
    if(Object.is(number, -0)) return fn('-0', 'number');
    return fn(`${number}`, 'number');
  }

  const integer = Math.trunc(number);
  const string = String(integer);
  if(integer === number) {
    if(!Number.isFinite(number) || string.includes('e')) return fn(string, 'number');
    return fn(`${addNumericSeparator(string)}`, 'number');
  }

  if(Number.isNaN(number)) return fn(string, 'number');
  return fn(`${addNumericSeparator(string)}.${addNumericSeparatorEnd(String(number).slice(string.length + 1))}`, 'number');
}

function formatBigInt(fn, bigint, numericSeparator) {
  const string = String(bigint);
  if(!numericSeparator) return fn(`${string}n`, 'bigint');
  return fn(`${addNumericSeparator(string)}n`, 'bigint');
}

const stylizeNoColor = str => str;

function getStringWidth(str) {
  return str.replace(ansiRegExp, '').length;
}

function getUserOptions(ctx, isCrossContext) {
  const ret = { stylize: ctx.stylize, showHidden: ctx.showHidden, depth: ctx.depth, colors: ctx.colors, customInspect: ctx.customInspect, showProxy: ctx.showProxy, maxArrayLength: ctx.maxArrayLength, maxStringLength: ctx.maxStringLength, breakLength: ctx.breakLength, compact: ctx.compact, sorted: ctx.sorted, getters: ctx.getters, numericSeparator: ctx.numericSeparator, ...ctx.userOptions };
  return ret;
}

export function inspect(value, opts) {
  const ctx = {
    budget: {},
    indentationLvl: 0,
    seen: [],
    currentDepth: 0,
    stylize: stylizeNoColor,
    showHidden: inspectDefaultOptions.showHidden,
    depth: inspectDefaultOptions.depth,
    colors: inspectDefaultOptions.colors,
    customInspect: inspectDefaultOptions.customInspect,
    showProxy: inspectDefaultOptions.showProxy,
    maxArrayLength: inspectDefaultOptions.maxArrayLength,
    maxStringLength: inspectDefaultOptions.maxStringLength,
    breakLength: inspectDefaultOptions.breakLength,
    compact: inspectDefaultOptions.compact,
    sorted: inspectDefaultOptions.sorted,
    getters: inspectDefaultOptions.getters,
    numericSeparator: inspectDefaultOptions.numericSeparator,
  };

  if(arguments.length > 1) {
    if(arguments.length > 2) {
      if(arguments[2] !== undefined) ctx.depth = arguments[2];
      if(arguments.length > 3 && arguments[3] !== undefined) ctx.colors = arguments[3];
    }

    if(typeof opts === 'boolean') ctx.showHidden = opts;
    else if(opts) {
      for(const key of objectKeys(opts)) {
        if(hasOwn(inspectDefaultOptions, key) || key === 'stylize') ctx[key] = opts[key];
        else if(ctx.userOptions === undefined) ctx.userOptions = opts;
      }
    }
  }

  if(ctx.colors) ctx.stylize = stylizeWithColor;
  if(ctx.maxArrayLength === null) ctx.maxArrayLength = Infinity;
  if(ctx.maxStringLength === null) ctx.maxStringLength = Infinity;
  return formatValue(ctx, value, 0);
}

inspect.custom = inspectCustom;
defineProperty(inspect, 'defaultOptions', {
  get() {
    return inspectDefaultOptions;
  },
  set(options) {
    if(options === null || typeof options !== 'object') throw argTypeError('options', 'of type object', options);
    Object.assign(inspectDefaultOptions, options);
  },
});
inspect.colors = inspectColors;
inspect.styles = Object.assign(Object.create(null), inspectStyles);

function stylizeWithColor(str, styleType) {
  const style = inspect.styles[styleType];

  if(style !== undefined) {
    const color = inspect.colors[style];
    if(color !== undefined) return `\u001b[${color[0]}m${str}\u001b[${color[1]}m`;
  }

  return str;
}

const builtInObjects = new Set(getOwnPropertyNames(globalThis).filter(e => /^[A-Z][a-zA-Z0-9]+$/.test(e)));

function isInstanceof(object, proto) {
  try {
    return object instanceof proto;
  } catch {
    return false;
  }
}

function getConstructorName(obj, ctx, recurseTimes, protoProps) {
  let firstProto;
  const tmp = obj;

  while(obj || isUndetectable(obj)) {
    const descriptor = getOwnPropertyDescriptor(obj, 'constructor');

    if(descriptor !== undefined && typeof descriptor.value === 'function' && descriptor.value.name !== '' && isInstanceof(tmp, descriptor.value)) {
      if(protoProps !== undefined && (firstProto !== obj || !builtInObjects.has(descriptor.value.name))) addPrototypeProperties(ctx, tmp, firstProto || tmp, recurseTimes, protoProps);
      return String(descriptor.value.name);
    }

    obj = getPrototypeOf(obj);
    if(firstProto === undefined) firstProto = obj;
  }

  if(firstProto === null) return null;
  const res = tmpName(tmp);
  if(recurseTimes > ctx.depth && ctx.depth !== null) return `${res} <Complex prototype>`;
  const protoConstr = getConstructorName(firstProto, ctx, recurseTimes + 1, protoProps);
  if(protoConstr === null) return `${res} <${inspect(firstProto, { ...ctx, customInspect: false, depth: -1 })}>`;
  return `${res} <${protoConstr}>`;
}

const isUndetectable = () => false;
const tmpName = obj => Object.prototype.toString.call(obj).slice(8, -1);

function addPrototypeProperties(ctx, main, obj, recurseTimes, output) {
  let depth = 0;
  let keys;
  let keySet;

  do {
    if(depth !== 0 || main === obj) {
      obj = getPrototypeOf(obj);
      if(obj === null) return;
      const descriptor = getOwnPropertyDescriptor(obj, 'constructor');
      if(descriptor !== undefined && typeof descriptor.value === 'function' && builtInObjects.has(descriptor.value.name)) return;
    }

    if(depth === 0) keySet = new Set();
    else keys.forEach(key => keySet.add(key));
    keys = Reflect.ownKeys(obj);
    ctx.seen.push(main);

    for(const key of keys) {
      if(key === 'constructor' || hasOwn(main, key) || (depth !== 0 && keySet.has(key))) continue;
      const desc = getOwnPropertyDescriptor(obj, key);
      if(typeof desc.value === 'function') continue;
      const value = formatProperty(ctx, obj, recurseTimes, key, kObjectType, desc, main);
      if(ctx.colors) output.push(`\u001b[2m${value}\u001b[22m`);
      else output.push(value);
    }

    ctx.seen.pop();
  } while(++depth !== 3);
}

function getPrefix(constructor, tag, fallback, size = '') {
  if(constructor === null) {
    if(tag !== '' && fallback !== tag) return `[${fallback}${size}: null prototype] [${tag}] `;
    return `[${fallback}${size}: null prototype] `;
  }

  if(tag !== '' && constructor !== tag) return `${constructor}${size} [${tag}] `;
  return `${constructor}${size} `;
}

function getKeys(value, showHidden) {
  let keys;
  const symbols = getOwnPropertySymbols(value);

  if(showHidden) {
    keys = getOwnPropertyNames(value);
    if(symbols.length !== 0) keys.push(...symbols);
  } else {
    try {
      keys = objectKeys(value);
    } catch(err) {
      keys = getOwnPropertyNames(value);
    }

    if(symbols.length !== 0) {
      const filter = key => Object.prototype.propertyIsEnumerable.call(value, key);
      keys.push(...symbols.filter(filter));
    }
  }

  return keys;
}

function formatValue(ctx, value, recurseTimes, typedArray) {
  if(typeof value !== 'object' && typeof value !== 'function') return formatPrimitive(ctx.stylize, value, ctx);
  if(value === null) return ctx.stylize('null', 'null');

  const context = value;
  if(ctx.customInspect) {
    const maybeCustom = value[inspectCustom];
    if(typeof maybeCustom === 'function' && maybeCustom !== inspect && !(value.constructor && value.constructor.prototype === value)) {
      const depth = ctx.depth === null ? null : ctx.depth - recurseTimes;
      const isCrossContext = !(context instanceof Object);
      const ret = Function.prototype.call.call(maybeCustom, context, depth, getUserOptions(ctx, isCrossContext), inspect);
      if(ret !== context) {
        if(typeof ret !== 'string') return formatValue(ctx, ret, recurseTimes);
        return ret.replaceAll('\n', `\n${' '.repeat(ctx.indentationLvl)}`);
      }
    }
  }

  if(ctx.seen.includes(value)) {
    let index = 1;

    if(ctx.circular === undefined) {
      ctx.circular = new Map();
      ctx.circular.set(value, index);
    } else {
      index = ctx.circular.get(value);

      if(index === undefined) {
        index = ctx.circular.size + 1;
        ctx.circular.set(value, index);
      }
    }

    return ctx.stylize(`[Circular *${index}]`, 'special');
  }

  return formatRaw(ctx, value, recurseTimes, typedArray);
}

const isErrorObject = e => types.isNativeError(e) || e instanceof Error;

function formatRaw(ctx, value, recurseTimes, typedArray) {
  let keys;
  let protoProps;

  if(ctx.showHidden && (recurseTimes <= ctx.depth || ctx.depth === null)) protoProps = [];

  const constructor = getConstructorName(value, ctx, recurseTimes, protoProps);
  if(protoProps !== undefined && protoProps.length === 0) protoProps = undefined;

  let tag = value[Symbol.toStringTag];
  if(typeof tag !== 'string' || (tag !== '' && (ctx.showHidden ? hasOwn : Object.prototype.propertyIsEnumerable).call(value, Symbol.toStringTag))) tag = '';
  let base = '';
  let formatter = getEmptyFormatArray;
  let braces;
  let noIterator = true;
  let i = 0;
  let extrasType = kObjectType;

  if(Symbol.iterator in value || constructor === null) {
    noIterator = false;

    if(Array.isArray(value)) {
      const prefix = constructor !== 'Array' || tag !== '' ? getPrefix(constructor, tag, 'Array', `(${value.length})`) : '';
      keys = getOwnNonIndexProperties(value, ctx.showHidden);
      braces = [`${prefix}[`, ']'];
      if(value.length === 0 && keys.length === 0 && protoProps === undefined) return `${braces[0]}]`;
      extrasType = kArrayExtrasType;
      formatter = formatArray;
    } else if(types.isSet(value)) {
      const size = getter(Set.prototype, 'size').call(value);
      const prefix = getPrefix(constructor, tag, 'Set', `(${size})`);
      keys = getKeys(value, ctx.showHidden);
      formatter = formatSet.bind(null, value);
      if(size === 0 && keys.length === 0 && protoProps === undefined) return `${prefix}{}`;
      braces = [`${prefix}{`, '}'];
    } else if(types.isMap(value)) {
      const size = getter(Map.prototype, 'size').call(value);
      const prefix = getPrefix(constructor, tag, 'Map', `(${size})`);
      keys = getKeys(value, ctx.showHidden);
      formatter = formatMap.bind(null, value);
      if(size === 0 && keys.length === 0 && protoProps === undefined) return `${prefix}{}`;
      braces = [`${prefix}{`, '}'];
    } else if(types.isTypedArray(value)) {
      keys = getOwnNonIndexProperties(value, ctx.showHidden);
      const bound = value;
      const fallback = typedArrayName(value);
      const size = value.length;
      const prefix = getPrefix(constructor, tag, fallback, `(${size})`);
      braces = [`${prefix}[`, ']'];
      if(value.length === 0 && keys.length === 0 && !ctx.showHidden) return `${braces[0]}]`;
      formatter = formatTypedArray.bind(null, bound, size);
      extrasType = kArrayExtrasType;
    } else if(types.isMapIterator(value)) {
      keys = getKeys(value, ctx.showHidden);
      braces = [`[${tag}] {`, '}'];
      if(keys.length === 0) return `${braces[0]}}`;
      formatter = () => [];
    } else if(types.isSetIterator(value)) {
      keys = getKeys(value, ctx.showHidden);
      braces = [`[${tag}] {`, '}'];
      if(keys.length === 0) return `${braces[0]}}`;
      formatter = () => [];
    } else {
      noIterator = true;
    }
  }

  if(noIterator) {
    keys = getKeys(value, ctx.showHidden);
    braces = ['{', '}'];

    if(typeof value === 'function') {
      base = getFunctionBase(value, constructor, tag);
      if(keys.length === 0 && protoProps === undefined) return ctx.stylize(base, 'special');
    } else if(constructor === 'Object') {
      if(types.isArgumentsObject(value)) braces[0] = '[Arguments] {';
      else if(tag !== '') braces[0] = `${getPrefix(constructor, tag, 'Object')}{`;
      if(keys.length === 0 && protoProps === undefined) return `${braces[0]}}`;
    } else if(types.isRegExp(value)) {
      base = RegExp.prototype.toString.call(value);
      const prefix = getPrefix(constructor, tag, 'RegExp');
      if(prefix !== 'RegExp ') base = `${prefix}${base}`;
      if((keys.length === 0 && protoProps === undefined) || (recurseTimes > ctx.depth && ctx.depth !== null)) return ctx.stylize(base, 'regexp');
    } else if(types.isDate(value)) {
      if(Number.isNaN(Date.prototype.getTime.call(value))) base = Date.prototype.toString.call(value);
      else base = Date.prototype.toISOString.call(value);
      const prefix = getPrefix(constructor, tag, 'Date');
      if(prefix !== 'Date ') base = `${prefix}${base}`;
      if(keys.length === 0 && protoProps === undefined) return ctx.stylize(base, 'date');
    } else if(isErrorObject(value)) {
      base = formatError(value, constructor, tag, ctx, keys);
      if(keys.length === 0 && protoProps === undefined) return base;
    } else if(types.isAnyArrayBuffer(value)) {
      const arrayType = types.isArrayBuffer(value) ? 'ArrayBuffer' : 'SharedArrayBuffer';
      const prefix = getPrefix(constructor, tag, arrayType);
      if(typedArray === undefined) formatter = formatArrayBuffer;
      else if(keys.length === 0 && protoProps === undefined) return prefix + `{ [byteLength]: ${formatNumber(ctx.stylize, value.byteLength, false)} }`;
      braces[0] = `${prefix}{`;
      keys.unshift('byteLength');
    } else if(types.isDataView(value)) {
      braces[0] = `${getPrefix(constructor, tag, 'DataView')}{`;
      keys.unshift('byteLength', 'byteOffset', 'buffer');
    } else if(types.isPromise(value)) {
      braces[0] = `${getPrefix(constructor, tag, 'Promise')}{`;
      formatter = formatPromise;
    } else if(types.isWeakSet(value)) {
      braces[0] = `${getPrefix(constructor, tag, 'WeakSet')}{`;
      formatter = ctx.showHidden ? formatWeakCollection : formatWeakCollection;
    } else if(types.isWeakMap(value)) {
      braces[0] = `${getPrefix(constructor, tag, 'WeakMap')}{`;
      formatter = formatWeakCollection;
    } else if(types.isModuleNamespaceObject(value)) {
      braces[0] = `${getPrefix(constructor, tag, 'Module')}{`;
      formatter = formatNamespaceObject.bind(null, keys);
    } else if(types.isBoxedPrimitive(value)) {
      base = getBoxedBase(value, ctx, keys, constructor, tag);
      if(keys.length === 0 && protoProps === undefined) return base;
    } else {
      if(keys.length === 0 && protoProps === undefined) return `${getCtxName(constructor, tag, 'Object')}{}`;
      braces[0] = `${getCtxName(constructor, tag, 'Object')}{`;
    }
  }

  if(recurseTimes > ctx.depth && ctx.depth !== null) {
    let constructorName = getCtxName(constructor, tag, 'Object').slice(0, -1);
    if(constructor !== null) constructorName = `[${constructorName}]`;
    return ctx.stylize(constructorName, 'special');
  }

  recurseTimes += 1;

  ctx.seen.push(value);
  ctx.currentDepth = recurseTimes;
  let output;
  let i2 = 0;

  try {
    output = formatter(ctx, value, recurseTimes);
    for(i2 = 0; i2 < keys.length; i2++) output.push(formatProperty(ctx, value, recurseTimes, keys[i2], extrasType));
    if(protoProps !== undefined) output.push(...protoProps);
  } catch(err) {
    const constructorName = getCtxName(constructor, tag, 'Object').slice(0, -1);
    return handleMaxCallStackSize(ctx, err, constructorName, indentationLvl0(ctx));
  }

  if(ctx.circular !== undefined) {
    const index = ctx.circular.get(value);
    if(index !== undefined) {
      const reference = ctx.stylize(`<ref *${index}>`, 'special');
      if(ctx.compact !== true) base = base === '' ? reference : `${reference} ${base}`;
      else braces[0] = `${reference} ${braces[0]}`;
    }
  }

  ctx.seen.pop();

  if(ctx.sorted) {
    const comparator = ctx.sorted === true ? undefined : ctx.sorted;
    if(extrasType === kObjectType) output = output.sort(comparator);
    else if(keys.length > 1) {
      const sorted = output.slice(output.length - keys.length).sort(comparator);
      output.splice(output.length - keys.length, keys.length, ...sorted);
    }
  }

  const res = reduceToSingleString(ctx, output, base, braces, extrasType, recurseTimes, value);
  const budget = ctx.budget[ctx.indentationLvl] || 0;
  const newLength = budget + res.length;
  ctx.budget[ctx.indentationLvl] = newLength;
  if(newLength > 2 ** 27) ctx.depth = -1;
  return res;
}

const indentationLvl0 = ctx => ctx.indentationLvl;

function handleMaxCallStackSize(ctx, err, constructorName, indentationLvl) {
  if(err instanceof RangeError || /stack|recursion/i.test(String(err?.message))) {
    ctx.seen.pop();
    ctx.indentationLvl = indentationLvl;
    return ctx.stylize(`[${constructorName}: Inspection interrupted prematurely. Maximum call stack size exceeded.]`, 'special');
  }

  throw err;
}

function getCtxName(constructor, tag, fallback) {
  return getPrefix(constructor, tag, fallback);
}

function getOwnNonIndexProperties(value, showHidden) {
  const all = showHidden ? [...getOwnPropertyNames(value), ...getOwnPropertySymbols(value)] : [...objectKeys(value), ...getOwnPropertySymbols(value).filter(k => Object.prototype.propertyIsEnumerable.call(value, k))];
  return all.filter(k => typeof k === 'symbol' || !(numberRegExp.test(k) && Number(k) < 2 ** 32 - 1));
}

function typedArrayName(v) {
  return typedArrayTag.call(v);
}

function getClassBase(value, constructor, tag) {
  const hasName = hasOwn(value, 'name');
  const name = hasName && value.name;
  let base = `[class`;

  if(name) base += ` ${name}`;
  else base += ' (anonymous)';
  if(constructor !== 'Function' && constructor !== null) base += ` [${constructor}]`;
  if(tag !== '' && constructor !== tag) base += ` [${tag}]`;
  if(constructor !== null) {
    const superName = getPrototypeOf(value).name;
    if(superName) base += ` extends ${superName}`;
  } else base += ' extends [null prototype]';
  return `${base}]`;
}

function getFunctionBase(value, constructor, tag) {
  const stringified = Function.prototype.toString.call(value);

  if(stringified.startsWith('class') && stringified.endsWith('}')) {
    const slice = stringified.slice(5, -1);
    const bracketIndex = slice.indexOf('{');
    if(bracketIndex !== -1 && (!slice.slice(0, bracketIndex).includes('(') || classRegExp.exec(slice.replace(stripCommentsRegExp, '')) !== null)) return getClassBase(value, constructor, tag);
  }

  let type = 'Function';
  if(types.isGeneratorFunction(value)) type = `Generator${type}`;
  if(types.isAsyncFunction(value)) type = `Async${type}`;
  let base = `[${type}`;
  if(constructor === null) base += ' (null prototype)';
  if(value.name === '') base += ' (anonymous)';
  else base += `: ${value.name}`;
  base += ']';
  if(constructor !== type && constructor !== null) base += ` ${constructor}`;
  if(tag !== '' && constructor !== tag) base += ` [${tag}]`;
  return base;
}

function getBoxedBase(value, ctx, keys, constructor, tag) {
  let fn, type;

  if(types.isNumberObject(value)) {
    fn = Number.prototype.valueOf;
    type = 'Number';
  } else if(types.isStringObject(value)) {
    fn = String.prototype.valueOf;
    type = 'String';
    keys.splice(0, value.length);
  } else if(types.isBooleanObject(value)) {
    fn = Boolean.prototype.valueOf;
    type = 'Boolean';
  } else if(types.isBigIntObject(value)) {
    fn = BigInt.prototype.valueOf;
    type = 'BigInt';
  } else {
    fn = Symbol.prototype.valueOf;
    type = 'Symbol';
  }

  let base = `[${type}`;
  if(type !== constructor) {
    if(constructor === null) base += ' (null prototype)';
    else base += ` (${constructor})`;
  }

  base += `: ${formatPrimitive(stylizeNoColor, fn.call(value), ctx)}]`;
  if(tag !== '' && tag !== constructor) base += ` [${tag}]`;
  if(keys.length !== 0 || ctx.stylize === stylizeNoColor) return base;
  return ctx.stylize(base, type.toLowerCase());
}

function getStackString(error) {
  return error.stack ? String(error.stack) : Error.prototype.toString.call(error);
}

function getStackFrames(ctx, err, stack) {
  const frames = stack.split('\n');
  let cause;

  try {
    ({ cause } = err);
  } catch {}

  if(cause != null && isErrorObject(cause)) {
    const causeStack = getStackString(cause);
    const causeStackStart = causeStack.indexOf('\n    at');
    if(causeStackStart !== -1) {
      const causeFrames = causeStack.slice(causeStackStart + 1).split('\n');
      const { len, offset } = identicalSequenceRange(frames, causeFrames);
      if(len > 0) {
        const skipped = len - 2;
        const msg = `    ... ${skipped} lines matching cause stack trace ...`;
        frames.splice(offset + 1, skipped, ctx.stylize(msg, 'undefined'));
      }
    }
  }

  return frames;
}

function identicalSequenceRange(a, b) {
  for(let i = 0; i < a.length - 3; i++) {
    const pos = b.indexOf(a[i]);

    if(pos !== -1) {
      const rest = b.length - pos;

      if(rest > 3) {
        let len = 1;
        const maxLen = Math.min(a.length - i, rest);
        while(maxLen > len && a[i + len] === b[pos + len]) len++;
        if(len > 3) return { len, offset: i };
      }
    }
  }

  return { len: 0, offset: 0 };
}

function improveStack(stack, constructor, name, tag) {
  let len = name.length;

  if(typeof name !== 'string') stack = stack.replace(/^([A-Z][a-z_ A-Z0-9[\]()-]+)(?::|\n\s+at)/, '');
  if(constructor === null || (name.endsWith('Error') && stack.startsWith(name) && (stack.length === len || stack[len] === ':' || stack[len] === '\n'))) {
    let fallback = 'Error';

    if(constructor === null) {
      const start = /^([A-Z][a-z_ A-Z0-9[\]()-]+)(?::|\n\s+at)/.exec(stack) || /^([a-z_A-Z0-9-]*Error)$/.exec(stack);
      fallback = (start && start[1]) || '';
      len = fallback.length;
      fallback = fallback || 'Error';
    }

    const prefix = getPrefix(constructor, tag, fallback).slice(0, -1);
    if(name !== prefix) {
      if(prefix.includes(name)) {
        if(len === 0) stack = `${prefix}: ${stack}`;
        else stack = `${prefix}${stack.slice(len)}`;
      } else {
        stack = `${prefix} [${name}]${stack.slice(len)}`;
      }
    }
  }

  return stack;
}

function removeDuplicateErrorKeys(ctx, keys, err, stack) {
  if(!ctx.showHidden && keys.length !== 0) {
    for(const name of ['name', 'message', 'stack']) {
      const index = keys.indexOf(name);
      if(index !== -1 && (typeof err[name] !== 'string' || stack.includes(err[name]))) keys.splice(index, 1);
    }
  }
}

function markNodeModules(ctx, line) {
  return line;
}

function formatError(err, constructor, tag, ctx, keys) {
  const name = err.name != null ? String(err.name) : 'Error';
  const stack = getStackString(err);
  removeDuplicateErrorKeys(ctx, keys, err, stack);

  if('cause' in err && (keys.length === 0 || !keys.includes('cause'))) keys.push('cause');
  if(Array.isArray(err.errors) && (keys.length === 0 || !keys.includes('errors'))) keys.push('errors');
  let stackStr = improveStack(stack, constructor, name, tag);
  let pos = (err.message && stackStr.indexOf(err.message)) || -1;
  if(pos !== -1) pos += err.message.length;
  const stackStart = stackStr.indexOf('\n    at', pos);
  if(stackStart === -1) {
    stackStr = `[${stackStr}]`;
  } else {
    let newStack = stackStr.slice(0, stackStart);
    const stackFramePart = stackStr.slice(stackStart + 1);
    const lines = getStackFrames(ctx, err, stackFramePart);
    if(ctx.colors) {
      for(const line of lines) newStack += `\n${markNodeModules(ctx, line)}`;
    } else {
      newStack += `\n${lines.join('\n')}`;
    }

    stackStr = newStack;
  }

  if(ctx.indentationLvl !== 0) {
    const indentation = ' '.repeat(ctx.indentationLvl);
    stackStr = stackStr.replaceAll('\n', `\n${indentation}`);
  }

  return stackStr;
}

function groupArrayElements(ctx, output, value) {
  let totalLength = 0;
  let maxLength = 0;
  let i = 0;
  let outputLength = output.length;

  if(ctx.maxArrayLength < output.length) outputLength--;
  const separatorSpace = 2;
  const dataLen = new Array(outputLength);

  for(; i < outputLength; i++) {
    const len = ctx.colors ? getStringWidth(output[i]) : output[i].length;
    dataLen[i] = len;
    totalLength += len + separatorSpace;
    if(maxLength < len) maxLength = len;
  }

  const actualMax = maxLength + separatorSpace;
  if(actualMax * 3 + ctx.indentationLvl < ctx.breakLength && (totalLength / actualMax > 5 || maxLength <= 6)) {
    const approxCharHeights = 2.5;
    const averageBias = Math.sqrt(actualMax - totalLength / output.length);
    const biasedMax = Math.max(actualMax - 3 - averageBias, 1);
    const columns = Math.min(Math.round(Math.sqrt(approxCharHeights * biasedMax * outputLength) / biasedMax), Math.floor((ctx.breakLength - ctx.indentationLvl) / actualMax), ctx.compact * 4, 15);
    if(columns <= 1) return output;
    const tmp = [];
    const maxLineLength = [];

    for(let i = 0; i < columns; i++) {
      let lineLength = 0;
      for(let j = i; j < output.length; j += columns) if(dataLen[j] > lineLength) lineLength = dataLen[j];
      maxLineLength.push(lineLength + separatorSpace);
    }

    let order = String.prototype.padStart;
    if(value !== undefined) {
      for(let i = 0; i < output.length; i++) {
        if(typeof value[i] !== 'number' && typeof value[i] !== 'bigint') {
          order = String.prototype.padEnd;
          break;
        }
      }
    }

    for(let i = 0; i < outputLength; i += columns) {
      const max = Math.min(i + columns, outputLength);
      let str = '';
      let j = i;

      for(; j < max - 1; j++) {
        const padding = maxLineLength[j - i] + output[j].length - dataLen[j];
        str += order.call(`${output[j]}, `, padding, ' ');
      }

      if(order === String.prototype.padStart) {
        const padding = maxLineLength[j - i] + output[j].length - dataLen[j] - separatorSpace;
        str += output[j].padStart(padding, ' ');
      } else {
        str += output[j];
      }

      tmp.push(str);
    }

    if(ctx.maxArrayLength < output.length) tmp.push(output[outputLength]);
    output = tmp;
  }

  return output;
}

function isBelowBreakLength(ctx, output, start, base) {
  let totalLength = output.length + start;
  if(totalLength + output.length > ctx.breakLength) return false;

  for(let i = 0; i < output.length; i++) {
    if(ctx.colors) totalLength += output[i].replace(ansiRegExp, '').length;
    else totalLength += output[i].length;
    if(totalLength > ctx.breakLength) return false;
  }

  return base === '' || !base.includes('\n');
}

function reduceToSingleString(ctx, output, base, braces, extrasType, recurseTimes, value) {
  if(ctx.compact !== true) {
    if(typeof ctx.compact === 'number' && ctx.compact >= 1) {
      const entries = output.length;

      if(extrasType === kArrayExtrasType && entries > 6) output = groupArrayElements(ctx, output, value);
      if(ctx.currentDepth - recurseTimes < ctx.compact && entries === output.length) {
        const start = output.length + ctx.indentationLvl + braces[0].length + base.length + 10;

        if(isBelowBreakLength(ctx, output, start, base)) {
          const joinedOutput = output.join(', ');
          if(!joinedOutput.includes('\n')) return `${base ? `${base} ` : ''}${braces[0]} ${joinedOutput}` + ` ${braces[1]}`;
        }
      }
    }

    const indentation = `\n${' '.repeat(ctx.indentationLvl)}`;
    return `${base ? `${base} ` : ''}${braces[0]}${indentation}  ${output.join(`,${indentation}  `)}${indentation}${braces[1]}`;
  }

  if(isBelowBreakLength(ctx, output, 0, base)) return `${braces[0]}${base ? ` ${base}` : ''} ${output.join(', ')} ` + braces[1];
  const indentation = ' '.repeat(ctx.indentationLvl);
  const ln = base === '' && braces[0].length === 1 ? ' ' : `${base ? ` ${base}` : ''}\n${indentation}  `;
  return `${braces[0]}${ln}${output.join(`,\n${indentation}  `)} ${braces[1]}`;
}

function formatPrimitive(fn, value, ctx) {
  if(typeof value === 'string') {
    let trailer = '';

    if(value.length > ctx.maxStringLength) {
      const remaining = value.length - ctx.maxStringLength;
      value = value.slice(0, ctx.maxStringLength);
      trailer = `... ${remaining} more character${remaining > 1 ? 's' : ''}`;
    }

    if(ctx.compact !== true && value.length > 16 && value.length > ctx.breakLength - ctx.indentationLvl - 4) {
      return (
        value
          .split(/(?<=\n)/)
          .map(line => fn(quoteString(line), 'string'))
          .join(` +\n${' '.repeat(ctx.indentationLvl + 2)}`) + trailer
      );
    }

    return fn(quoteString(value), 'string') + trailer;
  }

  if(typeof value === 'number') return formatNumber(fn, value, ctx.numericSeparator);
  if(typeof value === 'bigint') return formatBigInt(fn, value, ctx.numericSeparator);
  if(typeof value === 'boolean') return fn(`${value}`, 'boolean');
  if(typeof value === 'undefined') return fn('undefined', 'undefined');
  return fn(maybeQuoteSymbol(value, ctx), 'symbol');
}

function maybeQuoteSymbol(symbol, ctx) {
  return Symbol.prototype.toString.call(symbol);
}

function getEmptyFormatArray() {
  return [];
}

function formatArray(ctx, value, recurseTimes) {
  const valLen = value.length;
  const len = Math.min(Math.max(0, ctx.maxArrayLength), valLen);
  const remaining = valLen - len;
  const output = [];

  for(let i = 0; i < len; i++) {
    if(!hasOwn(value, i)) return formatArrayBuffer_holes(ctx, value, recurseTimes, len, output, i);
    output.push(formatProperty(ctx, value, recurseTimes, i, kArrayType));
  }

  if(remaining > 0) output.push(remainingText(remaining));
  return output;
}

function formatArrayBuffer_holes(ctx, value, recurseTimes, len, output, i) {
  const keys = objectKeys(value);
  let index = i;

  for(; i < keys.length && output.length < len; i++) {
    const key = keys[i];
    const tmp = +key;

    if(tmp > 2 ** 32 - 2) break;
    if(`${index}` !== key) {
      if(!numberRegExp.test(key)) break;
      const emptyItems = tmp - index;
      const ending = emptyItems > 1 ? 's' : '';
      const message = `<${emptyItems} empty item${ending}>`;
      output.push(ctx.stylize(message, 'undefined'));
      index = tmp;
      if(output.length === len) break;
    }

    output.push(formatProperty(ctx, value, recurseTimes, key, kArrayType));
    index++;
  }

  const remaining = value.length - index;
  if(output.length !== len) {
    if(remaining > 0) {
      const ending = remaining > 1 ? 's' : '';
      const message = `<${remaining} empty item${ending}>`;
      output.push(ctx.stylize(message, 'undefined'));
    }
  } else if(remaining > 0) {
    output.push(remainingText(remaining));
  }

  return output;
}

function remainingText(remaining) {
  return `... ${remaining} more item${remaining > 1 ? 's' : ''}`;
}

function formatArguments(ctx, value, recurseTimes) {
  const out = [];
  for(let i = 0; i < value.length; i++) out.push(formatProperty(ctx, value, recurseTimes, i, kArrayType));
  return out;
}

function formatArrayBuffer(ctx, value) {
  let buffer;
  try {
    buffer = new Uint8Array(value);
  } catch {
    return [ctx.stylize('(detached)', 'special')];
  }

  let str = [...buffer.subarray(0, Math.min(ctx.maxArrayLength, buffer.length))].map(b => b.toString(16).padStart(2, '0')).join(' ');
  const remaining = buffer.length - ctx.maxArrayLength;
  if(remaining > 0) str += ` ... ${remaining} more byte${remaining > 1 ? 's' : ''}`;
  return [`${ctx.stylize('[Uint8Contents]', 'special')}: <${str}>`];
}

function formatTypedArray(value, length, ctx, ignored, recurseTimes) {
  const maxLength = Math.min(Math.max(0, ctx.maxArrayLength), length);
  const remaining = value.length - maxLength;
  const output = new Array(maxLength);
  const elementFormatter = value.length > 0 && typeof value[0] === 'number' ? formatNumber : formatBigInt;

  for(let i = 0; i < maxLength; ++i) output[i] = elementFormatter(ctx.stylize, value[i], ctx.numericSeparator);
  if(remaining > 0) output[maxLength] = remainingText(remaining);
  if(ctx.showHidden) {
    ctx.indentationLvl += 2;
    for(const key of ['BYTES_PER_ELEMENT', 'length', 'byteLength', 'byteOffset', 'buffer']) {
      const str = formatValue(ctx, value[key], recurseTimes, true);
      output.push(`[${key}]: ${str}`);
    }

    ctx.indentationLvl -= 2;
  }

  return output;
}

function formatSet(value, ctx, ignored, recurseTimes) {
  const length = getter(Set.prototype, 'size').call(value);
  const maxLength = Math.min(Math.max(0, ctx.maxArrayLength), length);
  const remaining = length - maxLength;
  const output = [];
  ctx.indentationLvl += 2;
  let i = 0;

  for(const v of Set.prototype.values.call(value)) {
    if(i >= maxLength) break;
    output.push(formatValue(ctx, v, recurseTimes));
    i++;
  }

  if(remaining > 0) output.push(remainingText(remaining));
  ctx.indentationLvl -= 2;
  return output;
}

function formatMap(value, ctx, ignored, recurseTimes) {
  const length = getter(Map.prototype, 'size').call(value);
  const maxLength = Math.min(Math.max(0, ctx.maxArrayLength), length);
  const remaining = length - maxLength;
  const output = [];
  ctx.indentationLvl += 2;
  let i = 0;

  for(const { 0: k, 1: v } of Map.prototype.entries.call(value)) {
    if(i >= maxLength) break;
    output.push(`${formatValue(ctx, k, recurseTimes)} => ${formatValue(ctx, v, recurseTimes)}`);
    i++;
  }

  if(remaining > 0) output.push(remainingText(remaining));
  ctx.indentationLvl -= 2;
  return output;
}

function formatWeakCollection(ctx) {
  return [ctx.stylize('<items unknown>', 'special')];
}

function formatPromise(ctx, value, recurseTimes) {
  let output;
  const state = promiseState(value);

  if(state === 0) {
    output = [ctx.stylize('<pending>', 'special')];
  } else {
    ctx.indentationLvl += 2;
    const str = formatValue(ctx, promiseResult(value), recurseTimes);
    ctx.indentationLvl -= 2;
    output = [state === 2 ? `${ctx.stylize('<rejected>', 'special')} ${str}` : str];
  }

  return output;
}

function formatNamespaceObject(keys, ctx, value, recurseTimes) {
  const output = new Array(keys.length);

  for(let i = 0; i < keys.length; i++) {
    try {
      output[i] = formatProperty(ctx, value, recurseTimes, keys[i], kObjectType);
    } catch(err) {
      const tmp = { [keys[i]]: '' };
      output[i] = formatProperty(ctx, tmp, recurseTimes, keys[i], kObjectType);
      const pos = output[i].lastIndexOf(' ');
      output[i] = output[i].slice(0, pos + 1) + ctx.stylize('<uninitialized>', 'special');
    }
  }

  keys.length = 0;
  return output;
}

function formatProperty(ctx, value, recurseTimes, key, type, desc, original = value) {
  let name, str;
  let extra = ' ';
  desc = desc || getOwnPropertyDescriptor(value, key) || { value: value[key], enumerable: false };

  if(desc.value !== undefined) {
    const diff = ctx.compact !== true || type !== kObjectType ? 2 : 3;
    ctx.indentationLvl += diff;
    str = formatValue(ctx, desc.value, recurseTimes);
    if(diff === 3 && ctx.breakLength < getStringWidth(str)) extra = `\n${' '.repeat(ctx.indentationLvl)}`;
    ctx.indentationLvl -= diff;
  } else if(desc.get !== undefined) {
    const label = desc.set !== undefined ? 'Getter/Setter' : 'Getter';
    const s = ctx.stylize;
    const sp = 'special';

    if(ctx.getters && (ctx.getters === true || (ctx.getters === 'get' && desc.set === undefined) || (ctx.getters === 'set' && desc.set !== undefined))) {
      try {
        const tmp = desc.get.call(original);
        ctx.indentationLvl += 2;
        if(tmp === null) str = `${s(`[${label}:`, sp)} ${s('null', 'null')}${s(']', sp)}`;
        else if(typeof tmp === 'object') str = `${s(`[${label}]`, sp)} ${formatValue(ctx, tmp, recurseTimes)}`;
        else {
          const primitive = formatPrimitive(s, tmp, ctx);
          str = `${s(`[${label}:`, sp)} ${primitive}${s(']', sp)}`;
        }

        ctx.indentationLvl -= 2;
      } catch(err) {
        const message = `<Inspection threw (${err.message})>`;
        str = `${s(`[${label}:`, sp)} ${message}${s(']', sp)}`;
      }
    } else {
      str = ctx.stylize(`[${label}]`, sp);
    }
  } else if(desc.set !== undefined) {
    str = ctx.stylize('[Setter]', 'special');
  } else {
    str = ctx.stylize('undefined', 'undefined');
  }

  if(type === kArrayType) return str;
  if(typeof key === 'symbol') {
    name = `[${ctx.stylize(maybeQuoteSymbol(key, ctx), 'symbol')}]`;
  } else if(key === '__proto__') {
    name = "['__proto__']";
  } else if(desc.enumerable === false) {
    name = `[${key.replace(strEscapeSequencesReplacer, escapeLone)}]`;
  } else if(keyStrRegExp.test(key)) {
    name = ctx.stylize(key, 'name');
  } else {
    name = ctx.stylize(quoteString(key), 'string');
  }

  return `${name}:${extra}${str}`;
}

/* ---- format ---- */

function tryStringify(arg) {
  try {
    return JSON.stringify(arg);
  } catch(err) {
    if(/circular/i.test(String(err.message))) return '[Circular]';
    throw err;
  }
}

function hasBuiltInToString(value) {
  if(typeof value[Symbol.toPrimitive] === 'function') return false;
  if(typeof value.toString !== 'function') return true;
  if(hasOwn(value, 'toString')) return false;
  let pointer = value;
  do {
    pointer = getPrototypeOf(pointer);
  } while(!hasOwn(pointer, 'toString'));
  const descriptor = getOwnPropertyDescriptor(pointer, 'constructor');
  return descriptor !== undefined && typeof descriptor.value === 'function' && builtInObjects.has(descriptor.value.name);
}

function formatWithOptionsInternal(inspectOptions, args) {
  const first = args[0];
  let a = 0;
  let str = '';
  let join = '';

  if(typeof first === 'string') {
    if(args.length === 1) return first;
    let tempStr;
    let lastPos = 0;

    for(let i = 0; i < first.length - 1; i++) {
      if(first.charCodeAt(i) === 37) {
        const nextChar = first.charCodeAt(++i);

        if(a + 1 !== args.length) {
          switch (nextChar) {
            case 115: {
              const tempArg = args[++a];
              if(typeof tempArg === 'number') tempStr = formatNumber(stylizeNoColor, tempArg, false);
              else if(typeof tempArg === 'bigint') tempStr = formatBigInt(stylizeNoColor, tempArg, false);
              else if(typeof tempArg !== 'object' || tempArg === null || !hasBuiltInToString(tempArg)) tempStr = String(tempArg);
              else tempStr = inspect(tempArg, { ...inspectOptions, depth: 0, colors: false, compact: 3 });
              break;
            }

            case 106:
              tempStr = tryStringify(args[++a]);
              break;
            case 100: {
              const tempNum = args[++a];
              if(typeof tempNum === 'bigint') tempStr = formatBigInt(stylizeNoColor, tempNum, false);
              else if(typeof tempNum === 'symbol') tempStr = 'NaN';
              else tempStr = formatNumber(stylizeNoColor, Number(tempNum), false);
              break;
            }

            case 79:
              tempStr = inspect(args[++a], inspectOptions);
              break;
            case 111:
              tempStr = inspect(args[++a], { ...inspectOptions, showHidden: true, showProxy: true, depth: 4 });
              break;
            case 105: {
              const tempInteger = args[++a];
              if(typeof tempInteger === 'bigint') tempStr = formatBigInt(stylizeNoColor, tempInteger, false);
              else if(typeof tempInteger === 'symbol') tempStr = 'NaN';
              else tempStr = formatNumber(stylizeNoColor, parseInt(tempInteger), false);
              break;
            }

            case 102: {
              const tempFloat = args[++a];
              if(typeof tempFloat === 'symbol') tempStr = 'NaN';
              else tempStr = formatNumber(stylizeNoColor, parseFloat(tempFloat), false);
              break;
            }

            case 99:
              a += 1;
              tempStr = '';
              break;
            case 37:
              str += first.slice(lastPos, i);
              lastPos = i + 1;
              continue;
            default:
              continue;
          }

          if(lastPos !== i - 1) str += first.slice(lastPos, i - 1);
          str += tempStr;
          lastPos = i + 1;
        } else if(nextChar === 37) {
          str += first.slice(lastPos, i);
          lastPos = i + 1;
        }
      }
    }

    if(lastPos !== 0) {
      a++;
      join = ' ';
      if(lastPos < first.length) str += first.slice(lastPos);
    }
  }

  while(a < args.length) {
    const value = args[a];
    str += join;
    str += typeof value !== 'string' ? inspect(value, inspectOptions) : value;
    join = ' ';
    a++;
  }

  return str;
}

export function format(...args) {
  return formatWithOptionsInternal(undefined, args);
}

export function formatWithOptions(inspectOptions, ...args) {
  if(inspectOptions === null || typeof inspectOptions !== 'object') throw argTypeError('inspectOptions', 'of type object', inspectOptions);
  return formatWithOptionsInternal(inspectOptions, args);
}

/* ---- types ---- */

const brand = (fn, ...args) => v => {
  if(v === null || (typeof v !== 'object' && typeof v !== 'function')) return false;

  try {
    fn.call(v, ...args);
    return true;
  } catch {
    return false;
  }
};

const TypedArray = getPrototypeOf(Uint8Array);
const typedArrayTag = getOwnPropertyDescriptor(TypedArray.prototype, Symbol.toStringTag).get;
const protoTag = v => Object.prototype.toString.call(v).slice(8, -1);
const isObjectLike = v => v !== null && (typeof v === 'object' || typeof v === 'function');
const typedArrayIs = name => v => isObjectLike(v) && typedArrayTag.call(v) === name;
const fnTag = v => (typeof v === 'function' ? getPrototypeOf(v)?.[Symbol.toStringTag] : undefined);
const getter = (proto, name) => getOwnPropertyDescriptor(proto, name).get;

const isArrayBufferBrand = brand(getter(ArrayBuffer.prototype, 'byteLength'));
const isSharedArrayBufferBrand = typeof SharedArrayBuffer == 'undefined' ? () => false : brand(getter(SharedArrayBuffer.prototype, 'byteLength'));
const isMapBrand = brand(Map.prototype.has, 0);
const isSetBrand = brand(Set.prototype.has, 0);
const isWeakMapBrand = brand(WeakMap.prototype.has, {});
const isWeakSetBrand = brand(WeakSet.prototype.has, {});
const isDateBrand = brand(Date.prototype.getTime);
const isRegExpBrand = brand(getter(RegExp.prototype, 'source'));
const isDataViewBrand = brand(getter(DataView.prototype, 'byteLength'));
const isNumberBrand = brand(Number.prototype.valueOf);
const isStringBrand = brand(String.prototype.valueOf);
const isBooleanBrand = brand(Boolean.prototype.valueOf);
const isSymbolBrand = brand(Symbol.prototype.valueOf);
const isBigIntBrand = brand(BigInt.prototype.valueOf);
const isPromiseBrand = v => {
  if(!isObjectLike(v)) return false;
  try {
    promiseState(v);
    return protoTag(v) === 'Promise' || v instanceof Promise;
  } catch {
    return false;
  }
};

export const types = {
  isExternal: () => false,
  isDate: v => isDateBrand(v),
  isArgumentsObject: v => isObjectLike(v) && protoTag(v) === 'Arguments' && Number.isInteger(v.length),
  isBigIntObject: v => isBigIntBrand(v) && typeof v === 'object',
  isBooleanObject: v => isBooleanBrand(v) && typeof v === 'object',
  isNumberObject: v => isNumberBrand(v) && typeof v === 'object',
  isStringObject: v => isStringBrand(v) && typeof v === 'object',
  isSymbolObject: v => isSymbolBrand(v) && typeof v === 'object',
  isBoxedPrimitive: v => typeof v === 'object' && (isNumberBrand(v) || isStringBrand(v) || isBooleanBrand(v) || isBigIntBrand(v) || isSymbolBrand(v)),
  isNativeError: v => isObjectLike(v) && v instanceof Error && protoTag(v) === 'Error',
  isRegExp: v => isRegExpBrand(v) && v !== RegExp.prototype,
  isAsyncFunction: v => typeof v === 'function' && /^Async(Generator)?Function$/.test(fnTag(v) ?? ''),
  isGeneratorFunction: v => typeof v === 'function' && /^(Async)?GeneratorFunction$/.test(fnTag(v) ?? ''),
  isGeneratorObject: v => isObjectLike(v) && protoTag(v) === 'Generator',
  isPromise: v => isPromiseBrand(v),
  isMap: v => isMapBrand(v),
  isSet: v => isSetBrand(v),
  isMapIterator: v => isObjectLike(v) && protoTag(v) === 'Map Iterator',
  isSetIterator: v => isObjectLike(v) && protoTag(v) === 'Set Iterator',
  isWeakMap: v => isWeakMapBrand(v),
  isWeakSet: v => isWeakSetBrand(v),
  isArrayBuffer: v => isArrayBufferBrand(v),
  isDataView: v => isDataViewBrand(v),
  isSharedArrayBuffer: v => isSharedArrayBufferBrand(v),
  isProxy: () => false,
  isModuleNamespaceObject: v => isObjectLike(v) && v[Symbol.toStringTag] === 'Module' && getPrototypeOf(v) === null,
  isAnyArrayBuffer: v => isArrayBufferBrand(v) || isSharedArrayBufferBrand(v),
  isCryptoKey: () => false,
  isKeyObject: () => false,
  isArrayBufferView: v => ArrayBuffer.isView(v),
  isTypedArray: v => isObjectLike(v) && typedArrayTag.call(v) !== undefined,
  isUint8Array: typedArrayIs('Uint8Array'),
  isUint8ClampedArray: typedArrayIs('Uint8ClampedArray'),
  isUint16Array: typedArrayIs('Uint16Array'),
  isUint32Array: typedArrayIs('Uint32Array'),
  isInt8Array: typedArrayIs('Int8Array'),
  isInt16Array: typedArrayIs('Int16Array'),
  isInt32Array: typedArrayIs('Int32Array'),
  isFloat16Array: typedArrayIs('Float16Array'),
  isFloat32Array: typedArrayIs('Float32Array'),
  isFloat64Array: typedArrayIs('Float64Array'),
  isBigInt64Array: typedArrayIs('BigInt64Array'),
  isBigUint64Array: typedArrayIs('BigUint64Array'),
};

/* ---- deprecated is*() ---- */

const warned = new Set();

/* deprecate(fn, msg, code, options): wraps fn to emit a DeprecationWarning once per code */
export function deprecate(fn, msg, code, options) {
  if(typeof fn !== 'function') throw argTypeError('fn', 'of type function', fn);
  if(code !== undefined && typeof code !== 'string') throw argTypeError('code', 'of type string', code);
  let warnedOnce = false;
  const p = proc();

  function deprecated(...args) {
    if(!warnedOnce && !p?.noDeprecation) {
      warnedOnce = true;
      if(code === undefined || !warned.has(code)) {
        if(code !== undefined) warned.add(code);
        if(p?.throwDeprecation) throw Object.assign(new Error(msg), { name: 'DeprecationWarning', code });
        p?.emitWarning?.(msg, 'DeprecationWarning', code);
      }
    }

    return new.target ? Reflect.construct(fn, args, new.target) : Reflect.apply(fn, this, args);
  }

  if(options?.modifyPrototype !== false) setPrototypeOf(deprecated, fn);
  if(fn.prototype) deprecated.prototype = fn.prototype;
  defineProperty(deprecated, 'length', { value: fn.length, configurable: true });
  return deprecated;
}

const dep = (name, fn, replacement, code) => deprecate(fn, `The \`util.${name}\` API is deprecated. ${replacement}`, code);

export const isArray = Array.isArray;
export const isBoolean = dep('isBoolean', arg => typeof arg === 'boolean', 'Please use `typeof arg === "boolean"` instead.', 'DEP0045');
export const isNull = dep('isNull', arg => arg === null, 'Please use `arg === null` instead.', 'DEP0050');
export const isNullOrUndefined = dep('isNullOrUndefined', arg => arg === null || arg === undefined, 'Please use `arg === null || arg === undefined` instead.', 'DEP0051');
export const isNumber = dep('isNumber', arg => typeof arg === 'number', 'Please use `typeof arg === "number"` instead.', 'DEP0052');
export const isString = dep('isString', arg => typeof arg === 'string', 'Please use `typeof arg === "string"` instead.', 'DEP0056');
export const isSymbol = dep('isSymbol', arg => typeof arg === 'symbol', 'Please use `typeof arg === "symbol"` instead.', 'DEP0057');
export const isUndefined = dep('isUndefined', arg => arg === undefined, 'Please use `arg === undefined` instead.', 'DEP0058');
export const isRegExp = dep('isRegExp', types.isRegExp, 'Please use `arg instanceof RegExp` instead.', 'DEP0055');
export const isObject = dep('isObject', arg => arg !== null && typeof arg === 'object', 'Please use `arg !== null && typeof arg === "object"` instead.', 'DEP0053');
export const isDate = dep('isDate', types.isDate, 'Please use `arg instanceof Date` instead.', 'DEP0047');
export const isError = dep('isError', e => protoTag(e) === 'Error' || e instanceof Error, 'Please use `ObjectPrototypeToString(e) === "[object Error]" || e instanceof Error` instead.', 'DEP0048');
export const isFunction = dep('isFunction', arg => typeof arg === 'function', 'Please use `typeof arg === "function"` instead.', 'DEP0049');
export const isPrimitive = dep('isPrimitive', arg => arg === null || (typeof arg !== 'object' && typeof arg !== 'function'), 'Please use `arg === null || (typeof arg !== "object" && typeof arg !== "function")` instead.', 'DEP0054');
export const isBuffer = dep('isBuffer', arg => globalThis.Buffer?.isBuffer?.(arg) ?? false, 'Please use `Buffer.isBuffer()` instead.', 'DEP0044');

export const _extend = dep(
  '_extend',
  (target, source) => {
    if(source === null || typeof source !== 'object') return target;
    for(const key of objectKeys(source)) target[key] = source[key];
    return target;
  },
  'Please use Object.assign() instead.',
  'DEP0060',
);

export const log = dep(
  'log',
  (...args) => {
    const d = new Date();
    const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    const pad = n => String(n).padStart(2, '0');
    console.log('%s - %s', `${d.getDate()} ${months[d.getMonth()]} ${[d.getHours(), d.getMinutes(), d.getSeconds()].map(pad).join(':')}`, format(...args));
  },
  'Please use a third party logging module instead.',
  'DEP0059',
);

/* ---- inherits / promisify / callbackify / debuglog ---- */

export function inherits(ctor, superCtor) {
  if(ctor === undefined || ctor === null) throw argTypeError('ctor', 'of type function', ctor);
  if(superCtor === undefined || superCtor === null) throw argTypeError('superCtor', 'of type function', superCtor);
  if(superCtor.prototype === undefined) throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "superCtor.prototype" property must be of type object. Received undefined`);
  defineProperty(ctor, 'super_', { value: superCtor, writable: true, configurable: true });
  setPrototypeOf(ctor.prototype, superCtor.prototype);
}

const kCustomPromisifiedSymbol = Symbol.for('nodejs.util.promisify.custom');
const kCustomPromisifyArgsSymbol = Symbol('customPromisifyArgs');

export function promisify(original) {
  if(typeof original !== 'function') throw argTypeError('original', 'of type function', original);

  if(original[kCustomPromisifiedSymbol]) {
    const fn = original[kCustomPromisifiedSymbol];
    if(typeof fn !== 'function') throw argTypeError('util.promisify.custom', 'of type function', fn);
    return defineProperty(fn, kCustomPromisifiedSymbol, { value: fn, enumerable: false, writable: false, configurable: true });
  }

  const argumentNames = original[kCustomPromisifyArgsSymbol];

  function fn(...args) {
    return new Promise((resolve, reject) => {
      Reflect.apply(original, this, [
        ...args,
        (err, ...values) => {
          if(err) return reject(err);

          if(argumentNames !== undefined && values.length > 1) {
            const obj = {};
            for(let i = 0; i < argumentNames.length; i++) obj[argumentNames[i]] = values[i];
            resolve(obj);
          } else {
            resolve(values[0]);
          }
        },
      ]);
    });
  }

  setPrototypeOf(fn, getPrototypeOf(original));
  defineProperty(fn, kCustomPromisifiedSymbol, { value: fn, enumerable: false, writable: false, configurable: true });

  const descriptors = Object.getOwnPropertyDescriptors(original);
  for(const key of Reflect.ownKeys(descriptors)) if(key !== 'length' || true) defineProperty(fn, key, descriptors[key]);
  return fn;
}

promisify.custom = kCustomPromisifiedSymbol;

export function callbackify(original) {
  if(typeof original !== 'function') throw argTypeError('original', 'of type function', original);

  function callbackified(...args) {
    const maybeCb = args.pop();
    if(typeof maybeCb !== 'function') throw argTypeError('last argument', 'of type function', maybeCb);
    const cb = maybeCb.bind(this);
    Reflect.apply(original, this, args).then(
      ret => proc().nextTick(cb, null, ret),
      rej => {
        if(!rej) rej = Object.assign(new Error('Promise was rejected with a falsy value'), { code: 'ERR_FALSY_VALUE_REJECTION', reason: rej });
        proc().nextTick(cb, rej);
      },
    );
  }

  const descriptors = Object.getOwnPropertyDescriptors(original);
  if(typeof descriptors.length.value === 'number') descriptors.length.value++;
  if(typeof descriptors.name.value === 'string') descriptors.name.value += 'Callbackified';
  Object.defineProperties(callbackified, descriptors);
  return callbackified;
}

const debugEnvRegex = {};
let debugEnv;

function initDebug() {
  if(debugEnv !== undefined) return;
  debugEnv = proc()?.env?.NODE_DEBUG ?? '';
  const re = debugEnv
    .replace(/[|\\{}()[\]^$+?.]/g, '\\$&')
    .replaceAll('*', '.*')
    .replaceAll(',', '$|^');
  debugEnvRegex.re = new RegExp(`^${re}$`, 'i');
}

const debugImpls = {};

export function debuglog(set, cb) {
  function init() {
    initDebug();
    set = set.toUpperCase();
    const enabled = debugEnv !== '' && debugEnvRegex.re.test(set);
    debugImpls[set] = enabled
      ? (...args) => {
          const p = proc();
          console.error('%s %d: %s', set, p?.pid, format(...args));
        }
      : () => {};
  }

  let debug = (...args) => {
    init();
    debug = debugImpls[set];
    if(typeof cb === 'function') cb(debug);
    return debug(...args);
  };
  let test = () => {
    init();
    test = () => debugImpls[set] !== undefined && debugEnv !== '' && debugEnvRegex.re.test(set);
    return test();
  };
  const logger = (...args) => debug(...args);
  defineProperty(logger, 'enabled', {
    get() {
      return test();
    },
    configurable: true,
    enumerable: true,
  });
  return logger;
}

export const debug = debuglog;

/* ---- deep equality ---- */

export function isDeepStrictEqual(val1, val2, options) {
  return deepEqualImpl(val1, val2, true, undefined, options === true || options?.skipPrototype === true);
}

export function isPartialDeepStrictEqual(val1, val2) {
  return partialDeepEqualImpl(val1, val2);
}

/* ---- strings ---- */

export function toUSVString(input) {
  return `${input}`.toWellFormed();
}

const ansiPattern = '[\\u001B\\u009B][[\\]()#;?]*' + '(?:(?:(?:(?:;[-a-zA-Z\\d\\/\\#&.:=?%@~_]+)*' + '|[a-zA-Z\\d]+(?:;[-a-zA-Z\\d\\/\\#&.:=?%@~_]*)*)?' + '(?:\\u0007|\\u001B\\u005C|\\u009C))' + '|(?:(?:\\d{1,4}(?:;\\d{0,4})*)?[\\dA-PR-TZcf-ntqry=><~]))';
const ansi = new RegExp(ansiPattern, 'g');

export function stripVTControlCharacters(str) {
  if(typeof str !== 'string') throw argTypeError('str', 'of type string', str);
  return str.replace(ansi, '');
}

export function styleText(format, text, { validateStream = true, stream = proc()?.stdout } = {}) {
  if(typeof text !== 'string') throw argTypeError('text', 'of type string', text);
  if(validateStream && !(stream && typeof stream.write == 'function' || typeof stream?.puts == 'function')) throw argTypeError('stream', 'an instance of ReadStream, WriteStream, or Stream', stream);
  const formats = Array.isArray(format) ? format : [format];
  let left = '';
  let right = '';

  for(const key of formats) {
    if(key === 'none') continue;
    const code = hasOwn(inspectColors, key) ? inspectColors[key] : undefined;
    if(code === undefined) throw argValueError('format', key, `must be one of: ${['none', ...objectKeys(inspectColors)].map(k => `'${k}'`).join(', ')}`);
    left += `\u001b[${code[0]}m`;
    right = `\u001b[${code[1]}m${right}`;
  }

  if(validateStream) {
    const fd = stream?.fd ?? (typeof stream?.fileno == 'function' ? stream.fileno() : undefined);
    const p = proc();
    if(!(p?.env && !('NO_COLOR' in p.env) && stream?.isTTY && (!('NODE_DISABLE_COLORS' in p.env)) && p.env.TERM !== 'dumb') && !('FORCE_COLOR' in (p?.env ?? {}))) return text;
  }

  return `${left}${text}${right}`;
}

/* ---- parseEnv ---- */

export function parseEnv(content) {
  if(typeof content !== 'string') throw argTypeError('content', 'of type string', content);
  const result = {};
  const lines = content.replace(/\r\n?/g, '\n');
  const re = /^[ \t]*(?:export[ \t]+)?([\w.-]+)[ \t]*=[ \t]*('(?:\\'|[^'])*'|"(?:\\"|[^"])*"|`(?:\\`|[^`])*`|[^#\n]*)?[ \t]*(?:#.*)?$/gm;
  let match;

  while((match = re.exec(lines)) != null) {
    const key = match[1];
    let value = (match[2] ?? '').trim();
    const quote = value[0];
    if(quote === '"' || quote === "'" || quote === '`') {
      value = value.slice(1, -1);
      if(quote === '"') value = value.replace(/\\n/g, '\n');
    }

    result[key] = value;
  }

  return result;
}

/* ---- call sites ---- */

export function getCallSites(frameCount = 10, options) {
  if(typeof frameCount === 'object') {
    options = frameCount;
    frameCount = 10;
  } else if(frameCount !== undefined && typeof frameCount !== 'number') throw argTypeError('frameCount', 'of type number', frameCount);
  if(frameCount < 1 || frameCount > 200) throw rangeError('frameCount', '>= 1 && <= 200', frameCount);
  const stack = String(new Error().stack ?? '')
    .split('\n')
    .filter(l => /^\s+at /.test(l))
    .slice(1, frameCount + 1);
  return stack.map(line => {
    const m = /^\s+at (?:(.*?) \()?(.*?)(?::(\d+))?(?::(\d+))?\)?$/.exec(line);
    const [, functionName = '', scriptName = '', lineNumber = '0', columnNumber = '0'] = m ?? [];
    const site = { functionName: functionName === '<anonymous>' ? '' : functionName, scriptId: '0', scriptName, lineNumber: +lineNumber, columnNumber: +columnNumber };
    return { ...site, column: site.columnNumber };
  });
}

export const getCallSite = deprecate(getCallSites, '`util.getCallSite` is deprecated. Use `util.getCallSites()` instead.', 'DEP0186');

/* ---- abort ---- */

export function aborted(signal, resource) {
  if(!signal || typeof signal.addEventListener !== 'function') throw argTypeError('signal', 'an instance of AbortSignal', signal);
  if(resource === null || (typeof resource !== 'object' && typeof resource !== 'function')) throw argTypeError('resource', 'an object', resource);
  if(signal.aborted) return Promise.resolve();
  return new Promise(resolve => signal.addEventListener('abort', () => resolve(), { once: true }));
}

export function transferableAbortController() {
  return new AbortController();
}

export function transferableAbortSignal(signal) {
  if(!signal || typeof signal.addEventListener !== 'function') throw argTypeError('signal', 'an instance of AbortSignal', signal);
  return signal;
}

export function markPromiseAsHandled(promise) {
  if(!types.isPromise(promise)) throw argTypeError('promise', 'an instance of Promise', promise);
  promise.then(undefined, () => {});
}

/* ---- debounce / throttle ---- */

const checkSignal = signal => {
  if(signal?.aborted) throw new AbortError(undefined, { cause: signal.reason });
};

export function debounce(fn, wait, options = {}) {
  if(typeof fn !== 'function') throw argTypeError('fn', 'of type function', fn);
  if(!Number.isInteger(wait)) throw argTypeError('wait', 'of type number', wait);
  if(options === null || typeof options !== 'object') throw argTypeError('options', 'of type object', options);
  const { leading = false, rejectOnCancel = false, signal } = options;
  checkSignal(signal);

  let timer;
  let waiters = [];
  let lastArgs;
  let lastThis;
  let windowOpen = false;

  const settle = async (list, run) => {
    try {
      const value = await run();
      for(const w of list) w.resolve(value);
    } catch(error) {
      for(const w of list) w.reject(error);
    }
  };

  const invoke = () => {
    clearTimeout(timer);
    timer = undefined;
    windowOpen = false;
    const list = waiters;
    waiters = [];
    pending = null;
    const args = lastArgs,
      self = lastThis;
    settle(list, () => fn.apply(self, args));
  };

  const cancel = reason => {
    clearTimeout(timer);
    timer = undefined;
    windowOpen = false;
    pending = null;
    const list = waiters;
    waiters = [];
    for(const w of list) w.reject(new AbortError(undefined, reason === undefined ? undefined : { cause: reason }));
  };

  let pending = null;

  function debounced(...args) {
    if(signal?.aborted) return Promise.reject(new AbortError(undefined, { cause: signal.reason }));
    lastArgs = args;
    lastThis = this;
    const p = new Promise((resolve, reject) => waiters.push({ resolve, reject }));
    pending = p;

    if(rejectOnCancel && waiters.length > 1) for(const w of waiters.splice(0, waiters.length - 1)) w.reject(new AbortError());
    if(leading && !windowOpen) {
      windowOpen = true;
      const w = waiters.splice(0);
      pending = null;
      settle(w, () => fn.apply(this, args));
      clearTimeout(timer);
      timer = setTimeout(() => {
        timer = undefined;
        windowOpen = false;
        if(waiters.length) invoke();
      }, wait);
      return p;
    }

    windowOpen = true;
    clearTimeout(timer);
    timer = setTimeout(invoke, wait);
    return p;
  }

  signal?.addEventListener('abort', () => cancel(signal.reason), { once: true });

  debounced.cancel = cancel;
  debounced.flush = () => {
    if(waiters.length) invoke();
  };
  defineProperty(debounced, 'pending', { get: () => pending, enumerable: true });
  defineProperty(debounced, 'pendingCount', { get: () => waiters.length, enumerable: true });
  debounced.ref = () => debounced;
  debounced.unref = () => debounced;
  defineProperty(debounced, 'name', { value: fn.name, configurable: true });
  defineProperty(debounced, 'length', { value: fn.length, configurable: true });
  return debounced;
}

export function throttle(fn, limit, interval, options = {}) {
  if(typeof fn !== 'function') throw argTypeError('fn', 'of type function', fn);
  if(!Number.isInteger(limit) || limit <= 0) throw rangeError('limit', '> 0', limit);
  if(!Number.isInteger(interval)) throw argTypeError('interval', 'of type number', interval);
  const { concurrency = Infinity, maxPending = Infinity, overflow = 'queue', signal, strict = false } = options;
  checkSignal(signal);

  const queue = [];
  const starts = [];
  let windowStart = 0;
  let windowCount = 0;
  let active = 0;
  let timer;
  let last = null;

  const throttled_ = () => nodeError(Error, 'ERR_THROTTLED', 'The throttled function call was rejected');
  const rate = () => {
    const now = Date.now();
    if(strict) {
      while(starts.length && now - starts[0] >= interval) starts.shift();
      return starts.length < limit;
    }

    if(windowCount > 0 && now - windowStart >= interval) windowCount = 0;
    return windowCount < limit;
  };
  const consume = () => {
    const now = Date.now();
    if(strict) starts.push(now);
    else {
      if(windowCount === 0) windowStart = now;
      windowCount++;
    }
  };
  const nextDelay = () => {
    const now = Date.now();
    return strict ? Math.max(0, starts[0] + interval - now) : Math.max(0, windowStart + interval - now);
  };
  const run = job => {
    consume();
    active++;
    Promise.resolve()
      .then(() => job.fn.apply(job.self, job.args))
      .then(
        v => job.resolve(v),
        e => job.reject(e),
      )
      .finally(() => {
        active--;
        pump();
      });
  };
  const pump = () => {
    while(queue.length && rate() && active < concurrency) run(queue.shift());
    last = queue.length ? last : null;
    if(queue.length && !timer && active < concurrency && !rate()) {
      timer = setTimeout(() => {
        timer = undefined;
        pump();
      }, nextDelay());
    }
  };

  function throttled(...args) {
    if(signal?.aborted) return Promise.reject(new AbortError(undefined, { cause: signal.reason }));
    const job = { fn, args, self: this };
    const p = new Promise((resolve, reject) => Object.assign(job, { resolve, reject }));
    const hasCapacity = !queue.length && rate() && active < concurrency;
    if(hasCapacity) {
      run(job);
      return p;
    }

    if(overflow === 'drop' || queue.length >= maxPending) {
      const rejected = Promise.reject(throttled_());
      rejected.catch(() => {});
      return rejected;
    }

    queue.push(job);
    last = p;
    pump();
    return p;
  }

  throttled.cancel = reason => {
    clearTimeout(timer);
    timer = undefined;
    windowCount = 0;
    starts.length = 0;
    for(const job of queue.splice(0)) job.reject(new AbortError(undefined, reason === undefined ? undefined : { cause: reason }));
    last = null;
  };
  throttled.hasImmediateCapacity = () => !queue.length && rate() && active < concurrency;
  defineProperty(throttled, 'pending', { get: () => last, enumerable: true });
  defineProperty(throttled, 'pendingCount', { get: () => queue.length, enumerable: true });
  defineProperty(throttled, 'activeCount', { get: () => active, enumerable: true });
  throttled.ref = () => throttled;
  throttled.unref = () => throttled;
  signal?.addEventListener('abort', () => throttled.cancel(signal.reason), { once: true });
  defineProperty(throttled, 'name', { value: fn.name, configurable: true });
  defineProperty(throttled, 'length', { value: fn.length, configurable: true });
  return throttled;
}

/* ---- diff (Myers) ---- */

export function diff(actual, expected) {
  const check = (name, v) => {
    if(Array.isArray(v)) {
      v.forEach((x, i) => {
        if(typeof x !== 'string') throw argTypeError(`${name}[${i}]`, 'of type string', x);
      });
    } else if(typeof v !== 'string') throw argTypeError(name, 'of type string', v);
  };
  check('actual', actual);
  check('expected', expected);
  const isStr = typeof actual === 'string' && typeof expected === 'string';
  const a = isStr ? [...actual] : actual,
    b = isStr ? [...expected] : expected;
  const n = a.length,
    m = b.length;
  if(n === m && a.every((x, i) => x === b[i])) return [];
  const max = n + m;
  const v = new Int32Array(2 * max + 2);
  const trace = [];
  let found = false;
  for(let d = 0; d <= max && !found; d++) {
    trace.push(v.slice());
    for(let k = -d; k <= d; k += 2) {
      let x;
      if(k === -d || (k !== d && v[k - 1 + max] < v[k + 1 + max])) x = v[k + 1 + max];
      else x = v[k - 1 + max] + 1;
      let y = x - k;
      while(x < n && y < m && a[x] === b[y]) {
        x++;
        y++;
      }

      v[k + max] = x;
      if(x >= n && y >= m) {
        found = true;
        break;
      }
    }
  }

  const result = [];
  let x = n,
    y = m;
  for(let d = trace.length - 1; d >= 0; d--) {
    const vv = trace[d];
    const k = x - y;
    const prevK = k === -d || (k !== d && vv[k - 1 + max] < vv[k + 1 + max]) ? k + 1 : k - 1;
    const prevX = vv[prevK + max];
    const prevY = prevX - prevK;
    while(x > prevX && y > prevY) {
      result.push([0, a[x - 1]]);
      x--;
      y--;
    }

    if(d > 0) {
      if(x === prevX) result.push([-1, b[prevY]]);
      else result.push([1, a[prevX]]);
    }

    x = prevX;
    y = prevY;
  }

  return result.reverse().map(([op, value]) => [op, value]);
}

/* ---- TextEncoder / TextDecoder ---- */

const encodingLabels = {
  'utf-8': ['unicode-1-1-utf-8', 'unicode11utf8', 'unicode20utf8', 'utf8', 'x-unicode20utf8'],
  'utf-16le': ['csunicode', 'iso-10646-ucs-2', 'ucs-2', 'unicode', 'unicodefeff', 'utf-16'],
  'windows-1252': ['ansi_x3.4-1968', 'ascii', 'cp1252', 'cp819', 'csisolatin1', 'ibm819', 'iso-8859-1', 'iso-ir-100', 'iso8859-1', 'iso88591', 'iso_8859-1', 'iso_8859-1:1987', 'l1', 'latin1', 'us-ascii', 'x-cp1252'],
};

const normalizeEncoding = label => {
  const l = String(label).trim().toLowerCase();
  for(const [name, aliases] of Object.entries(encodingLabels)) if(l === name || aliases.includes(l)) return name;
  return undefined;
};

const cp1252 = [0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160, 0x2039, 0x152, 0x8d, 0x17d, 0x8f, 0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0x9d, 0x17e, 0x178];

const toBytesView = input => {
  if(input === undefined) return new Uint8Array(0);
  if(ArrayBuffer.isView(input)) return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
  if(types.isAnyArrayBuffer(input)) return new Uint8Array(input);
  throw argTypeError('input', 'an instance of ArrayBuffer or ArrayBufferView', input);
};

/* decodeUtf8: WHATWG UTF-8 decode; returns [string, pending bytes] */
function decodeUtf8(bytes, fatal, stream, state) {
  let out = '';
  let { needed = 0, seen = 0, cp = 0, lower = 0x80, upper = 0xbf } = state;
  const fail = () => {
    if(fatal) throw nodeError(TypeError, 'ERR_ENCODING_INVALID_ENCODED_DATA', 'The encoded data was not valid for encoding utf-8');
    out += '\ufffd';
  };

  for(let i = 0; i < bytes.length; i++) {
    const b = bytes[i];

    if(needed === 0) {
      if(b <= 0x7f) out += String.fromCharCode(b);
      else if(b >= 0xc2 && b <= 0xdf) (needed = 1), (cp = b & 0x1f);
      else if(b >= 0xe0 && b <= 0xef) {
        if(b === 0xe0) lower = 0xa0;
        if(b === 0xed) upper = 0x9f;
        needed = 2;
        cp = b & 0xf;
      } else if(b >= 0xf0 && b <= 0xf4) {
        if(b === 0xf0) lower = 0x90;
        if(b === 0xf4) upper = 0x8f;
        needed = 3;
        cp = b & 0x7;
      } else fail();
      continue;
    }

    if(b < lower || b > upper) {
      cp = needed = seen = 0;
      lower = 0x80;
      upper = 0xbf;
      fail();
      i--;
      continue;
    }

    lower = 0x80;
    upper = 0xbf;
    cp = (cp << 6) | (b & 0x3f);
    if(++seen === needed) {
      out += String.fromCodePoint(cp);
      cp = needed = seen = 0;
    }
  }

  if(!stream && needed !== 0) {
    needed = seen = cp = 0;
    lower = 0x80;
    upper = 0xbf;
    fail();
  }

  Object.assign(state, { needed, seen, cp, lower, upper });
  return out;
}

export class TextDecoder {
  #encoding;
  #fatal;
  #ignoreBOM;
  #state = {};
  #bomSeen = false;
  #tail = null;

  constructor(encoding = 'utf-8', options = {}) {
    const enc = normalizeEncoding(encoding);
    if(enc === undefined) throw nodeError(RangeError, 'ERR_ENCODING_NOT_SUPPORTED', `The "${encoding}" encoding is not supported`);
    if(options === null || typeof options !== 'object') throw argTypeError('options', 'of type object', options);
    this.#encoding = enc;
    this.#fatal = !!options.fatal;
    this.#ignoreBOM = !!options.ignoreBOM;
  }

  get encoding() {
    return this.#encoding;
  }

  get fatal() {
    return this.#fatal;
  }

  get ignoreBOM() {
    return this.#ignoreBOM;
  }

  decode(input, options = {}) {
    let bytes = toBytesView(input);
    const stream = !!options?.stream;
    let str;

    if(this.#encoding === 'utf-8') str = decodeUtf8(bytes, this.#fatal, stream, this.#state);
    else if(this.#encoding === 'utf-16le') {
      if(this.#tail) {
        const merged = new Uint8Array(this.#tail.length + bytes.length);
        merged.set(this.#tail);
        merged.set(bytes, this.#tail.length);
        bytes = merged;
        this.#tail = null;
      }

      let end = bytes.length - (bytes.length % 2);
      if(end < bytes.length) {
        if(stream) this.#tail = bytes.slice(end);
        else if(this.#fatal) throw nodeError(TypeError, 'ERR_ENCODING_INVALID_ENCODED_DATA', 'The encoded data was not valid for encoding utf-16le');
      }

      str = '';
      for(let i = 0; i < end; i += 2) str += String.fromCharCode(bytes[i] | (bytes[i + 1] << 8));
      if(end < bytes.length && !stream) str += '\ufffd';
      if(!stream) str = str.toWellFormed();
    } else {
      str = '';
      for(const b of bytes) str += b >= 0x80 && b <= 0x9f ? String.fromCharCode(cp1252[b - 0x80]) : String.fromCharCode(b);
    }

    if(!this.#ignoreBOM && !this.#bomSeen && str.length) {
      if(str.charCodeAt(0) === 0xfeff) str = str.slice(1);
      this.#bomSeen = true;
    }

    if(!stream) {
      this.#bomSeen = false;
      this.#state = {};
    }

    return str;
  }

  get [Symbol.toStringTag]() {
    return 'TextDecoder';
  }
}

export class TextEncoder {
  get encoding() {
    return 'utf-8';
  }

  encode(input = '') {
    return Uint8Array.from(Buffer_from(String(input).toWellFormed()));
  }

  encodeInto(src, dest) {
    if(typeof src !== 'string') throw argTypeError('src', 'of type string', src);
    if(!(dest instanceof Uint8Array)) throw argTypeError('dest', 'an instance of Uint8Array', dest);
    const full = this.encode(src);
    let written = 0,
      read = 0;

    for(const ch of src.toWellFormed()) {
      const bytes = this.encode(ch);
      if(written + bytes.length > dest.length) break;
      dest.set(bytes, written);
      written += bytes.length;
      read += ch.length;
    }

    return { read, written };
  }

  get [Symbol.toStringTag]() {
    return 'TextEncoder';
  }
}

/* Buffer_from: UTF-8 bytes of a well-formed string */
function Buffer_from(s) {
  const out = [];

  for(const ch of s) {
    const c = ch.codePointAt(0);
    if(c < 0x80) out.push(c);
    else if(c < 0x800) out.push(0xc0 | (c >> 6), 0x80 | (c & 63));
    else if(c < 0x10000) out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
    else out.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
  }

  return out;
}

/* ---- MIMEType / MIMEParams ---- */

const tokenRe = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/;
const httpWs = /^[\t\n\r ]+|[\t\n\r ]+$/g;
const quotedRe = /^[\t\u0020-\u007E\u0080-\u00FF]*$/;

const invalidIndex = str => {
  for(let i = 0; i < str.length; i++) if(!tokenRe.test(str[i])) return i;
  return -1;
};

const mimeError = (what, input, str = input) => {
  const i = invalidIndex(str);
  return nodeError(TypeError, 'ERR_INVALID_MIME_SYNTAX', `The MIME syntax for a ${what} in "${input}" is invalid${i === -1 ? '' : ` at ${i}`}`);
};

const encodeValue = value => (value === '' ? '""' : tokenRe.test(value) ? value : `"${value.replace(/(["\\])/g, '\\$1')}"`);

export class MIMEParams {
  #map = new Map();

  delete(name) {
    this.#map.delete(String(name));
  }

  get(name) {
    return this.#map.get(String(name)) ?? null;
  }

  has(name) {
    return this.#map.has(String(name));
  }

  set(name, value) {
    name = String(name);
    value = String(value);
    if(!tokenRe.test(name)) throw mimeError('parameter name', name);
    if(!quotedRe.test(value)) throw nodeError(TypeError, 'ERR_INVALID_MIME_SYNTAX', `The MIME syntax for a parameter value in "${value}" is invalid`);
    this.#map.set(name, value);
  }

  *entries() {
    yield* this.#map.entries();
  }

  *keys() {
    yield* this.#map.keys();
  }

  *values() {
    yield* this.#map.values();
  }

  [Symbol.iterator]() {
    return this.entries();
  }

  toString() {
    let s = '';
    for(const [k, v] of this.#map) s += `;${k}=${encodeValue(v)}`;
    return s;
  }

  toJSON() {
    return this.toString();
  }

  static _parse(rest, params) {
    let i = 0;

    while(i < rest.length) {
      while(i < rest.length && (rest[i] === ';' || /[\t\n\r ]/.test(rest[i]))) i++;
      let name = '';

      while(i < rest.length && rest[i] !== ';' && rest[i] !== '=') name += rest[i++];
      name = name.toLowerCase();
      if(i >= rest.length) break;
      if(rest[i] === ';') continue;
      i++;
      let value = '';

      if(rest[i] === '"') {
        i++;

        while(i < rest.length) {
          if(rest[i] === '\\') {
            i++;
            if(i < rest.length) value += rest[i++];
          } else if(rest[i] === '"') {
            i++;
            break;
          } else value += rest[i++];
        }

        while(i < rest.length && rest[i] !== ';') i++;
      } else {
        while(i < rest.length && rest[i] !== ';') value += rest[i++];
        value = value.replace(httpWs, '');
        if(value === '') continue;
      }

      if(name !== '' && tokenRe.test(name) && quotedRe.test(value) && !params.has(name)) params.set(name, value);
    }
  }
}

export class MIMEType {
  #type;
  #subtype;
  #params = new MIMEParams();

  constructor(input) {
    input = `${input}`;
    const s = input.replace(httpWs, '');
    const slash = s.indexOf('/');
    if(slash === -1) throw mimeError('type', input);
    const type = s.slice(0, slash);
    if(!tokenRe.test(type)) throw mimeError('type', input, type);
    let semi = s.indexOf(';', slash);
    if(semi === -1) semi = s.length;
    const subtype = s.slice(slash + 1, semi).replace(httpWs, '');
    if(!tokenRe.test(subtype)) throw mimeError('subtype', input, subtype);
    this.#type = type.toLowerCase();
    this.#subtype = subtype.toLowerCase();
    MIMEParams._parse(s.slice(semi), this.#params);
  }

  get type() {
    return this.#type;
  }

  set type(v) {
    v = `${v}`;
    if(!tokenRe.test(v)) throw mimeError('type', v);
    this.#type = v.toLowerCase();
  }

  get subtype() {
    return this.#subtype;
  }

  set subtype(v) {
    v = `${v}`;
    if(!tokenRe.test(v)) throw mimeError('subtype', v);
    this.#subtype = v.toLowerCase();
  }

  get essence() {
    return `${this.#type}/${this.#subtype}`;
  }

  get params() {
    return this.#params;
  }

  toString() {
    return `${this.essence}${this.#params}`;
  }

  toJSON() {
    return this.toString();
  }

  static parse(string) {
    try {
      return new MIMEType(string);
    } catch {
      return null;
    }
  }

}

/* ---- parseArgs ---- */

const parseArgsError = (code, message) => nodeError(TypeError, code, message);

const isLoneShort = arg => /^-[^-]$/.test(arg);
const isLongOption = arg => /^--[^=]+$/.test(arg);
const isLongWithValue = arg => /^--[^=]+=/.test(arg);

export function parseArgs(config = {}) {
  if(config === null || typeof config !== 'object') throw argTypeError('config', 'of type object', config);
  const { args = proc()?.argv?.slice(2) ?? [], strict = true, options = {}, tokens: wantTokens = false } = config;
  const allowPositionals = config.allowPositionals ?? !strict;
  const allowNegative = config.allowNegative ?? false;

  if(!Array.isArray(args)) throw argTypeError('args', 'an instance of Array', args);
  if(typeof strict !== 'boolean') throw argTypeError('strict', 'of type boolean', strict);
  if(options === null || typeof options !== 'object') throw argTypeError('options', 'of type object', options);

  for(const [name, opt] of Object.entries(options)) {
    if(opt === null || typeof opt !== 'object') throw argTypeError(`options.${name}`, 'of type object', opt);
    if(opt.type !== 'string' && opt.type !== 'boolean') throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "options.${name}.type" property must be ('string|boolean'). Received ${typeof opt.type == 'string' ? `type string (${inspect(opt.type)})` : describe(opt.type)}`);
    if(opt.short !== undefined) {
      if(typeof opt.short !== 'string') throw argTypeError(`options.${name}.short`, 'of type string', opt.short);
      if(opt.short.length !== 1) throw argValueError(`options.${name}.short`, opt.short, 'must be a single character');
    }

    if(opt.multiple !== undefined && typeof opt.multiple !== 'boolean') throw argTypeError(`options.${name}.multiple`, 'of type boolean', opt.multiple);
    if(opt.default !== undefined) {
      const dv = opt.default;
      const ok = opt.multiple ? Array.isArray(dv) && dv.every(x => typeof x === opt.type) : typeof dv === opt.type;
      if(!ok) throw argTypeError(`options.${name}.default`, opt.multiple ? `an instance of Array` : `of type ${opt.type}`, dv);
    }
  }

  const longFor = short => {
    for(const [name, opt] of Object.entries(options)) if(opt.short === short) return name;
    return short;
  };
  const typeOf = name => (hasOwn(options, name) ? options[name].type : undefined);
  const tokens = [];
  const rest = [...args];
  let index = -1;
  let afterTerminator = false;

  while(rest.length) {
    const arg = rest.shift();
    index++;
    if(afterTerminator) {
      tokens.push({ kind: 'positional', index, value: arg });
      continue;
    }

    if(arg === '--') {
      tokens.push({ kind: 'option-terminator', index });
      afterTerminator = true;
      continue;
    }

    if(isLoneShort(arg)) {
      const shortOption = arg[1];
      const longOption = longFor(shortOption);
      let value;
      let inlineValue;
      if(typeOf(longOption) === 'string' && rest.length) {
        value = rest.shift();
        inlineValue = false;
      }

      tokens.push({ kind: 'option', name: longOption, rawName: arg, index, value, inlineValue });
      if(value !== undefined) index++;
      continue;
    }

    if(/^-[^-]./.test(arg) && !arg.startsWith('--')) {
      const firstShort = arg[1];
      const firstLong = longFor(firstShort);
      if(typeOf(firstLong) !== 'string') {
        rest.unshift(...[...arg.slice(1)].map(c => `-${c}`));
        index--;
        continue;
      }

      tokens.push({ kind: 'option', name: firstLong, rawName: `-${firstShort}`, index, value: arg.slice(2), inlineValue: true });
      continue;
    }

    if(isLongWithValue(arg)) {
      const eq = arg.indexOf('=');
      let name = arg.slice(2, eq);
      const value = arg.slice(eq + 1);
      tokens.push({ kind: 'option', name, rawName: `--${name}`, index, value, inlineValue: true });
      continue;
    }

    if(isLongOption(arg)) {
      let name = arg.slice(2);
      let negated = false;
      if(allowNegative && name.startsWith('no-') && typeOf(name.slice(3)) === 'boolean' && typeOf(name) === undefined) {
        negated = true;
      }

      let value;
      let inlineValue;
      if(!negated && typeOf(name) === 'string' && rest.length) {
        value = rest.shift();
        inlineValue = false;
      }

      tokens.push({ kind: 'option', name: negated ? name.slice(3) : name, rawName: arg, index, value: negated ? undefined : value, inlineValue: negated ? undefined : inlineValue, ...(negated && { negated: true }) });
      if(value !== undefined) index++;
      continue;
    }

    tokens.push({ kind: 'positional', index, value: arg });
  }

  const values = Object.create(null);
  const positionals = [];
  const display = token => {
    const t = typeOf(token.name);
    const opt = options[token.name];
    return opt?.short ? `-${opt.short}, --${token.name}` : `--${token.name}`;
  };

  for(const token of tokens) {
    if(token.kind === 'positional') {
      if(strict && !allowPositionals) throw parseArgsError('ERR_PARSE_ARGS_UNEXPECTED_POSITIONAL', `Unexpected argument '${token.value}'. This command does not take positional arguments`);
      positionals.push(token.value);
    } else if(token.kind === 'option') {
      const known = hasOwn(options, token.name);
      if(strict) {
        if(!known) {
          const flag = token.rawName.startsWith('--') ? token.rawName : token.rawName;
          throw parseArgsError('ERR_PARSE_ARGS_UNKNOWN_OPTION', `Unknown option '${token.rawName}'` + (allowPositionals ? `. To specify a positional argument starting with a '-', place it at the end of the command after '--', as in '-- "${token.rawName}"` : ''));
        }

        const type = options[token.name].type;
        if(type === 'string' && typeof token.value !== 'string') throw parseArgsError('ERR_PARSE_ARGS_INVALID_OPTION_VALUE', `Option '${display(token)} <value>' argument missing`);
        if(type === 'string' && token.inlineValue === false && token.value.length > 1 && token.value[0] === '-') {
          const short = options[token.name].short;
          throw parseArgsError('ERR_PARSE_ARGS_INVALID_OPTION_VALUE', `Option '--${token.name}' argument is ambiguous.\nDid you forget to specify the option argument for '--${token.name}'?\nTo specify an option argument starting with a dash use '--${token.name}=-XYZ'${short ? ` or '-${short}-XYZ'` : ''}.`);
        }

        if(type === 'boolean' && token.value !== undefined) throw parseArgsError('ERR_PARSE_ARGS_INVALID_OPTION_VALUE', `Option '${display(token)}' does not take an argument`);
      }

      const value = token.negated ? false : token.value ?? true;
      if(known && options[token.name].multiple) (values[token.name] ??= []).push(value);
      else values[token.name] = value;
    }
  }

  for(const [name, opt] of Object.entries(options)) if(opt.default !== undefined && values[name] === undefined) values[name] = opt.default;

  const result = { values, positionals };
  if(wantTokens) result.tokens = tokens;
  return result;
}
