/* globals.js: installs the web/Node globals QuickJS lacks; opt-in, qjsm only.
 * rule: a global that already exists is never replaced.
 *
 * ```js
 * import 'globals';   // URL, TextEncoder, AbortController, Blob, ReadableStream,
 *                     // atob/btoa, queueMicrotask, structuredClone, setImmediate, crypto, ...
 * ```
 *
 * | global                                   | from                         |
 * | ---------------------------------------- | ---------------------------- |
 * | URL, URLSearchParams                     | url                          |
 * | TextEncoder, TextDecoder                 | textcode                     |
 * | AbortController, AbortSignal             | abort                        |
 * | EventTarget                              | events                       |
 * | Blob                                     | blob                         |
 * | ReadableStream, WritableStream, ...      | streams                      |
 * | atob, btoa                               | defined here                 |
 * | setTimeout, setInterval, setImmediate    | timers                       |
 * | queueMicrotask, structuredClone, crypto  | defined here                 |
 */
import { clearImmediate, clearInterval, clearTimeout, setImmediate, setInterval, setTimeout } from 'timers';
import { closeSync, openSync, readSync, constants } from 'fs';
import { Blob } from 'blob';
import { EventTarget } from 'events';
import { TextDecoder, TextEncoder } from 'textcode';
import { URL, URLSearchParams } from 'url';
import { AbortController, AbortSignal } from 'abort';
import * as streams from 'streams';

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
const invalidCharacter = message => Object.assign(new Error(message), { name: 'InvalidCharacterError', code: 5 });

/* btoa(binary): base64 of a Latin-1 string; atob(b64) is the inverse, padding optional
 *
 * ```js
 * btoa('hello')       // 'aGVsbG8='
 * atob('aGVsbG8')     // 'hello'
 * ```
 *
 * throws InvalidCharacterError for a char above 0xFF (btoa) or a non-base64 char (atob).
 */
function btoa(data) {
  const str = String(data);
  let out = '';

  for(let i = 0; i < str.length; i += 3) {
    const [a, b, c] = [0, 1, 2].map(k => (i + k < str.length ? str.charCodeAt(i + k) : -1));

    if(a > 255 || b > 255 || c > 255) throw invalidCharacter('The string to be encoded contains characters outside of the Latin1 range.');

    const n = (a << 16) | ((b < 0 ? 0 : b) << 8) | (c < 0 ? 0 : c);

    out += B64[(n >> 18) & 63] + B64[(n >> 12) & 63] + (b < 0 ? '=' : B64[(n >> 6) & 63]) + (c < 0 ? '=' : B64[n & 63]);
  }

  return out;
}

function atob(data) {
  let str = String(data).replace(/[\t\n\f\r ]/g, '');

  if(str.length % 4 == 0) str = str.replace(/==?$/, '');
  if(str.length % 4 == 1 || /[^A-Za-z0-9+/]/.test(str)) throw invalidCharacter('The string to be decoded is not correctly encoded.');

  let out = '', bits = 0, acc = 0;

  for(const ch of str) {
    acc = (acc << 6) | B64.indexOf(ch);
    bits += 6;

    if(bits >= 8) {
      bits -= 8;
      out += String.fromCharCode((acc >> bits) & 255);
    }
  }

  return out;
}

function dataCloneError(message) {
  return Object.assign(new Error(message), { name: 'DataCloneError', code: 25 });
}

/* structuredClone(value): deep copy by the HTML structured clone algorithm (no transfer)
 *
 * ```js
 * structuredClone({ d: new Date(0), m: new Map([[1, [2]]]) })   // an independent copy
 * ```
 *
 * throws DataCloneError for functions and symbols.
 */
function structuredClone(value) {
  const seen = new Map();

  const clone = v => {
    if(typeof v == 'function' || typeof v == 'symbol') throw dataCloneError(`${typeof v == 'function' ? v.toString().slice(0, 40) : String(v)} could not be cloned.`);
    if(typeof v != 'object' || v === null) return v;
    if(seen.has(v)) return seen.get(v);

    let out;

    if(v instanceof Date) out = new Date(v.getTime());
    else if(v instanceof RegExp) out = new RegExp(v.source, v.flags);
    else if(v instanceof Boolean || v instanceof Number || v instanceof String) out = Object(v.valueOf());
    else if(v instanceof ArrayBuffer) out = v.slice(0);
    else if(ArrayBuffer.isView(v)) {
      const buffer = clone(v.buffer);

      out = v instanceof DataView ? new DataView(buffer, v.byteOffset, v.byteLength) : new v.constructor(buffer, v.byteOffset, v.length);
    } else if(v instanceof Map) {
      out = new Map();
      seen.set(v, out);
      for(const [k, x] of v) out.set(clone(k), clone(x));
      return out;
    } else if(v instanceof Set) {
      out = new Set();
      seen.set(v, out);
      for(const x of v) out.add(clone(x));
      return out;
    } else if(v instanceof Error) {
      const ctor = { EvalError, RangeError, ReferenceError, SyntaxError, TypeError, URIError }[v.name] ?? Error;

      out = new ctor(v.message);
      seen.set(v, out);
      if(v.stack !== undefined) out.stack = v.stack;
      if('cause' in v) out.cause = clone(v.cause);
      return out;
    } else if(Array.isArray(v)) out = new Array(v.length);
    else out = {};

    seen.set(v, out);

    if(!(v instanceof Date || v instanceof RegExp || ArrayBuffer.isView(v) || v instanceof ArrayBuffer || v instanceof Boolean || v instanceof Number || v instanceof String))
      for(const k of Object.keys(v)) out[k] = clone(v[k]);

    return out;
  };

  return clone(value);
}

/* crypto.getRandomValues / randomUUID from /dev/urandom */
function randomBytes(u8) {
  const fd = openSync('/dev/urandom', constants.O_RDONLY);

  try {
    for(let at = 0; at < u8.length; ) at += readSync(fd, u8, at, u8.length - at);
  } finally {
    closeSync(fd);
  }

  return u8;
}

const crypto = {
  getRandomValues(array) {
    if(!ArrayBuffer.isView(array) || array instanceof Float32Array || array instanceof Float64Array || array instanceof DataView)
      throw Object.assign(new TypeError('The data argument must be an integer-type TypedArray'), { name: 'TypeMismatchError' });
    if(array.byteLength > 65536) throw Object.assign(new RangeError('The ArrayBufferView\'s byte length exceeds the number of bytes of entropy available via this API (65536)'), { name: 'QuotaExceededError' });

    randomBytes(new Uint8Array(array.buffer, array.byteOffset, array.byteLength));
    return array;
  },
  randomUUID() {
    const b = randomBytes(new Uint8Array(16));

    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;

    const h = [...b].map(x => x.toString(16).padStart(2, '0')).join('');

    return `${h.slice(0, 8)}-${h.slice(8, 12)}-${h.slice(12, 16)}-${h.slice(16, 20)}-${h.slice(20)}`;
  },
};

const queueMicrotask = fn => {
  if(typeof fn != 'function') throw new TypeError('The "callback" argument must be of type function');
  Promise.resolve().then(fn);
};

const globals = {
  URL, URLSearchParams, TextEncoder, TextDecoder, AbortController, AbortSignal, EventTarget, Blob, atob, btoa,
  setTimeout, clearTimeout, setInterval, clearInterval, setImmediate, clearImmediate, queueMicrotask, structuredClone, crypto,
  ...Object.fromEntries(['ReadableStream', 'WritableStream', 'TransformStream', 'ByteLengthQueuingStrategy', 'CountQueuingStrategy', 'TextEncoderStream', 'TextDecoderStream'].map(n => [n, streams[n]])),
};

for(const [name, value] of Object.entries(globals))
  if(value !== undefined && !(name in globalThis)) Object.defineProperty(globalThis, name, { value, writable: true, enumerable: false, configurable: true });

export { structuredClone };
