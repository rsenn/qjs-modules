/* buffer.js: Node's `buffer` module (Buffer, constants, atob/btoa); Bun's and Deno's too.
 * depends on: misc (ArrayBuffer helpers), blob (Blob re-export).
 * rule: Buffer is a Uint8Array subclass; every byte operation works on the
 * underlying ArrayBuffer through misc.*ArrayBuffer with absolute offsets.
 *
 * ```js
 * import { Buffer } from 'buffer';
 *
 * const b = Buffer.from('héllo');           // <Buffer 68 c3 a9 6c 6c 6f>
 * b.toString('hex');                        // '68c3a96c6c6f'
 * Buffer.concat([b, Buffer.alloc(2)]).length // 8
 * ```
 *
 * encodings: utf8 (utf-8), utf16le (ucs2, ucs-2, utf-16le), latin1 (binary),
 * ascii, hex, base64, base64url. `new Buffer(x)` and `Buffer(x)` work as in Node.
 */
import { compareArrayBuffer, concatArrayBuffer, copyArrayBuffer, isArrayBuffer, searchArrayBuffer, sliceArrayBuffer, toArrayBuffer, toString as utf8Decode } from 'misc';
import { Blob } from 'blob';

export const kMaxLength = 2 ** 53 - 1;
export const kStringMaxLength = 2 ** 29 - 24;
export const constants = { MAX_LENGTH: kMaxLength, MAX_STRING_LENGTH: kStringMaxLength };
export const INSPECT_MAX_BYTES = 50;

const inspectCustom = Symbol.for('nodejs.util.inspect.custom');

/* ---- errors: Node's codes, `name [code]` style ---- */

function nodeError(Type, code, message) {
  const e = new Type(message);

  e.code = code;
  return e;
}

/* "Received ..." suffix of Node's argument errors */
function received(v) {
  if(v === undefined || v === null) return ` Received ${v}`;
  if(typeof v == 'function') return ` Received function ${v.name}`;
  if(typeof v == 'object') return v.constructor?.name ? ` Received an instance of ${v.constructor.name}` : ` Received ${typeof v}`;

  let s = String(typeof v == 'bigint' ? `${v}n` : v);

  if(typeof v == 'string') s = `'${s.length > 25 ? s.slice(0, 25) + '...' : s}'`;

  return ` Received type ${typeof v} (${s})`;
}

const argType = (name, expected, v) => nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "${name}" argument must be ${expected}.${received(v)}`);
const outOfRange = (name, range, v) => nodeError(RangeError, 'ERR_OUT_OF_RANGE', `The value of "${name}" is out of range. It must be ${range}. Received ${typeof v == 'number' && Math.abs(v) >= 1000 ? String(v).replace(/\B(?=(\d{3})+(?!\d))/g, '_') : String(v)}`);
const outOfBounds = () => nodeError(RangeError, 'ERR_BUFFER_OUT_OF_BOUNDS', 'Attempt to access memory outside buffer bounds');

/* ---- encodings ---- */

const ENCODINGS = {
  utf8: 'utf8',
  'utf-8': 'utf8',
  ucs2: 'utf16le',
  'ucs-2': 'utf16le',
  utf16le: 'utf16le',
  'utf-16le': 'utf16le',
  latin1: 'latin1',
  binary: 'latin1',
  ascii: 'ascii',
  hex: 'hex',
  base64: 'base64',
  base64url: 'base64url',
};

function normalizeEncoding(enc) {
  if(enc === undefined || enc === null || enc === '') return 'utf8';

  const n = ENCODINGS[String(enc).toLowerCase()];

  if(n === undefined) throw nodeError(TypeError, 'ERR_UNKNOWN_ENCODING', `Unknown encoding: ${enc}`);

  return n;
}

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
const B64URL = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
const B64_VALUE = new Int8Array(128).fill(-1);

for(let i = 0; i < 64; i++) {
  B64_VALUE[B64.charCodeAt(i)] = i;
  B64_VALUE[B64URL.charCodeAt(i)] = i;
}

const LONE_SURROGATE = /[\ud800-\udbff](?![\udc00-\udfff])|(?<![\ud800-\udbff])[\udc00-\udfff]/g;

/* a Uint8Array of `str` in `enc` (never shared with anything) */
function encode(str, enc) {
  switch (enc) {
    case 'utf8': {
      return new Uint8Array(toArrayBuffer(str.replace(LONE_SURROGATE, '�')));
    }
    case 'utf16le': {
      const out = new Uint8Array(str.length * 2);

      for(let i = 0; i < str.length; i++) {
        const c = str.charCodeAt(i);

        out[i * 2] = c & 255;
        out[i * 2 + 1] = c >> 8;
      }

      return out;
    }
    case 'latin1':
    case 'ascii': {
      const out = new Uint8Array(str.length);

      for(let i = 0; i < str.length; i++) out[i] = str.charCodeAt(i) & 255;

      return out;
    }
    case 'hex': {
      const out = new Uint8Array(str.length >> 1);
      let n = 0;

      for(; n < out.length; n++) {
        const hi = parseInt(str[n * 2], 16), lo = parseInt(str[n * 2 + 1], 16);

        if(Number.isNaN(hi) || Number.isNaN(lo)) break;

        out[n] = (hi << 4) | lo;
      }

      return n == out.length ? out : out.slice(0, n);
    }
    default: {
      /* base64, base64url: both alphabets accepted, anything else skipped, '=' ends */
      const out = new Uint8Array(((str.length * 3) >> 2) + 3);
      let n = 0, acc = 0, bits = 0;

      for(let i = 0; i < str.length; i++) {
        const c = str.charCodeAt(i);

        if(c == 61) break;

        const v = c < 128 ? B64_VALUE[c] : -1;

        if(v < 0) continue;

        acc = ((acc << 6) | v) & 0xffff;
        bits += 6;

        if(bits >= 8) {
          bits -= 8;
          out[n++] = (acc >> bits) & 255;
        }
      }

      return out.slice(0, n);
    }
  }
}

const CHUNK = 8192;

/* WHATWG utf-8 decode: one U+FFFD per maximal invalid subpart, as Node does */
function decodeUtf8(u8, start, end) {
  const out = [];

  for(let i = start; i < end; ) {
    const b = u8[i++];

    if(b < 0x80) {
      out.push(b);
      continue;
    }

    let need = 0, cp = 0, lo = 0x80, hi = 0xbf;

    if(b >= 0xc2 && b <= 0xdf) { need = 1; cp = b & 0x1f; }
    else if(b >= 0xe0 && b <= 0xef) { need = 2; cp = b & 0xf; if(b == 0xe0) lo = 0xa0; if(b == 0xed) hi = 0x9f; }
    else if(b >= 0xf0 && b <= 0xf4) { need = 3; cp = b & 7; if(b == 0xf0) lo = 0x90; if(b == 0xf4) hi = 0x8f; }
    else { out.push(0xfffd); continue; }

    for(; need > 0; need--, lo = 0x80, hi = 0xbf) {
      if(i >= end || u8[i] < lo || u8[i] > hi) { cp = 0xfffd; break; }
      cp = (cp << 6) | (u8[i++] & 0x3f);
    }

    out.push(cp);
  }

  let s = '';

  for(let i = 0; i < out.length; i += 4096) s += String.fromCodePoint(...out.slice(i, i + 4096));

  return s;
}

/* the string for bytes [start, end) of `u8` in `enc` */
function decode(u8, start, end, enc) {
  const len = end - start;

  if(len <= 0) return '';

  switch (enc) {
    case 'utf8': {
      const s = utf8Decode(sliceArrayBuffer(u8.buffer, u8.byteOffset + start, u8.byteOffset + end));

      return s.includes('\ufffd') ? decodeUtf8(u8, start, end) : s;
    }
    case 'hex': {
      let s = '';

      for(let i = start; i < end; i++) s += (u8[i] < 16 ? '0' : '') + u8[i].toString(16);

      return s;
    }
    case 'base64':
    case 'base64url': {
      const table = enc == 'base64' ? B64 : B64URL;
      let s = '';
      let i = start;

      for(; i + 2 < end; i += 3) {
        const n = (u8[i] << 16) | (u8[i + 1] << 8) | u8[i + 2];

        s += table[(n >> 18) & 63] + table[(n >> 12) & 63] + table[(n >> 6) & 63] + table[n & 63];
      }

      if(end - i == 1) {
        const n = u8[i] << 16;

        s += table[(n >> 18) & 63] + table[(n >> 12) & 63] + (enc == 'base64' ? '==' : '');
      } else if(end - i == 2) {
        const n = (u8[i] << 16) | (u8[i + 1] << 8);

        s += table[(n >> 18) & 63] + table[(n >> 12) & 63] + table[(n >> 6) & 63] + (enc == 'base64' ? '=' : '');
      }

      return s;
    }
    case 'utf16le': {
      let s = '';

      for(let i = start; i + 1 < end; i += 2) s += String.fromCharCode(u8[i] | (u8[i + 1] << 8));

      return s;
    }
    default: {
      const mask = enc == 'ascii' ? 0x7f : 0xff;
      let s = '';

      for(let i = start; i < end; i += CHUNK) {
        const codes = [];

        for(let j = i; j < Math.min(i + CHUNK, end); j++) codes.push(u8[j] & mask);

        s += String.fromCharCode.apply(null, codes);
      }

      return s;
    }
  }
}

/* the number of bytes `str` takes in `enc` */
function byteLengthOf(str, enc) {
  switch (enc) {
    case 'utf16le':
      return str.length * 2;
    case 'latin1':
    case 'ascii':
      return str.length;
    case 'hex':
      return str.length >> 1;
    case 'utf8':
      return encode(str, 'utf8').length;
    default:
      return encode(str, enc).length;
  }
}

/* ---- helpers ---- */

const isU8 = v => v instanceof Uint8Array;
const isAnyArrayBuffer = v => isArrayBuffer(v) || (typeof SharedArrayBuffer != 'undefined' && v instanceof SharedArrayBuffer);

/* a Buffer viewing [byteOffset, byteOffset + length) of `ab` */
const view = (ab, byteOffset, length) => Reflect.construct(Uint8Array, [ab, byteOffset, length], Buffer);

/* a zero-filled Buffer of `size` bytes */
function allocate(size) {
  return Reflect.construct(Uint8Array, [size], Buffer);
}

function checkSize(size) {
  if(typeof size != 'number') throw argType('size', 'of type number', size);
  if(!(size >= 0 && size <= kMaxLength)) throw outOfRange('size', `>= 0 && <= ${kMaxLength}`, size);
}

/* an integer offset argument (undefined -> def) */
function toOffset(v, name, def) {
  if(v === undefined) return def;
  if(typeof v != 'number') throw argType(name, 'of type number', v);
  if(!Number.isInteger(v)) throw outOfRange(name, 'an integer', v);

  return v;
}

/* copy of the bytes of `u8` as a fresh Buffer */
const copyOf = u8 => {
  const out = allocate(u8.length);

  if(u8.length) copyArrayBuffer(out.buffer, out.byteOffset, out.byteOffset + u8.length, u8.buffer, u8.byteOffset, u8.byteOffset + u8.length);

  return out;
};

/* Buffer(arg, encodingOrOffset, length) and new Buffer(...): the deprecated constructor, also
 * what TypedArray species calls with (arrayBuffer, byteOffset, length) for slice()/subarray() */
export function Buffer(arg, encodingOrOffset, length) {
  if(typeof arg == 'number') {
    if(typeof encodingOrOffset == 'string') throw argType('string', 'of type string', arg);

    checkSize(arg);
    return allocate(arg);
  }

  return Buffer.from(arg, encodingOrOffset, length);
}

Object.setPrototypeOf(Buffer.prototype, Uint8Array.prototype);
Object.setPrototypeOf(Buffer, Uint8Array);

Buffer.poolSize = 8192;

/* ---- static methods ---- */

Buffer.alloc = function alloc(size, fill, encoding) {
  checkSize(size);

  const b = allocate(size);

  if(fill !== undefined && fill !== 0 && size > 0) b.fill(fill, encoding);

  return b;
};

Buffer.allocUnsafe = function allocUnsafe(size) {
  checkSize(size);
  return allocate(size);
};

Buffer.allocUnsafeSlow = function allocUnsafeSlow(size) {
  checkSize(size);
  return allocate(size);
};

Buffer.from = function from(value, encodingOrOffset, length) {
  if(typeof value == 'string') {
    const bytes = encode(value, normalizeEncoding(encodingOrOffset));
    const b = allocate(bytes.length);

    b.set(bytes);
    return b;
  }

  if(typeof value == 'object' && value !== null) {
    if(isAnyArrayBuffer(value)) {
      const offset = encodingOrOffset === undefined ? 0 : +encodingOrOffset;
      const max = value.byteLength - (Number.isNaN(offset) ? 0 : offset);

      if(Number.isNaN(offset)) return view(value, 0, length === undefined ? value.byteLength : +length);
      if(offset < 0 || offset > value.byteLength) throw nodeError(RangeError, 'ERR_BUFFER_OUT_OF_BOUNDS', '"offset" is outside of buffer bounds');
      if(length !== undefined && (+length < 0 || +length > max)) throw nodeError(RangeError, 'ERR_BUFFER_OUT_OF_BOUNDS', '"length" is outside of buffer bounds');

      return view(value, offset, length === undefined ? max : +length);
    }

    if(ArrayBuffer.isView(value) && !(value instanceof DataView)) {
      const b = allocate(value.length);

      if(isU8(value)) {
        if(value.length) copyArrayBuffer(b.buffer, b.byteOffset, b.byteOffset + value.length, value.buffer, value.byteOffset, value.byteOffset + value.length);
      } else for(let i = 0; i < value.length; i++) b[i] = Number(value[i]) & 255;

      return b;
    }

    const prim = value.valueOf && value.valueOf();

    if(prim != null && prim !== value && (typeof prim == 'string' || typeof prim == 'object')) return Buffer.from(prim, encodingOrOffset, length);

    if(typeof value.length == 'number' || Array.isArray(value)) {
      const n = value.length > 0 ? Math.floor(value.length) : 0;
      const b = allocate(n);

      for(let i = 0; i < n; i++) b[i] = value[i] & 255;

      return b;
    }

    if(value.type === 'Buffer' && Array.isArray(value.data)) return Buffer.from(value.data);

    if(typeof value[Symbol.toPrimitive] == 'function') {
      const s = value[Symbol.toPrimitive]('string');

      if(typeof s == 'string') return Buffer.from(s, encodingOrOffset);
    }
  }

  throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The first argument must be of type string or an instance of Buffer, ArrayBuffer, or Array or an Array-like Object.${received(value)}`);
};

Buffer.copyBytesFrom = function copyBytesFrom(source, offset = 0, length) {
  if(!ArrayBuffer.isView(source) || source instanceof DataView) throw argType('view', 'an instance of TypedArray', source);

  const end = length === undefined ? source.length : Math.min(source.length, offset + length);
  const bytes = source.subarray(offset, end);

  return Buffer.from(new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength));
};

Buffer.byteLength = function byteLength(value, encoding) {
  if(typeof value == 'string') return byteLengthOf(value, normalizeEncoding(encoding));
  if(ArrayBuffer.isView(value) || isAnyArrayBuffer(value)) return value.byteLength;

  throw argType('string', 'of type string or an instance of Buffer or ArrayBuffer', value);
};

Buffer.compare = function compare(a, b) {
  if(!isU8(a)) throw argType('buf1', 'an instance of Buffer or Uint8Array', a);
  if(!isU8(b)) throw argType('buf2', 'an instance of Buffer or Uint8Array', b);

  return compareRanges(a, 0, a.length, b, 0, b.length);
};

Buffer.concat = function concat(list, totalLength) {
  if(!Array.isArray(list)) throw argType('list', 'an instance of Array', list);

  if(totalLength === undefined) {
    totalLength = 0;

    for(const item of list) {
      if(!isU8(item)) throw argType(`list[${list.indexOf(item)}]`, 'an instance of Buffer or Uint8Array', item);

      totalLength += item.length;
    }
  } else if(typeof totalLength != 'number') throw argType('length', 'of type number', totalLength);

  const out = allocate(totalLength);
  let pos = 0;

  if(list.length && list.every(item => isU8(item) && isArrayBuffer(item.buffer))) {
    const args = [];

    for(const item of list) args.push(item.buffer, item.byteOffset, item.byteOffset + item.length);

    const joined = new Uint8Array(concatArrayBuffer(...args));

    copyBytes(joined, 0, Math.min(joined.length, totalLength), out, 0);

    return out;
  }

  for(let i = 0; i < list.length && pos < totalLength; i++) {
    const item = list[i];

    if(!isU8(item)) throw argType(`list[${i}]`, 'an instance of Buffer or Uint8Array', item);

    pos += copyBytes(item, 0, item.length, out, pos);
  }

  return out;
};

Buffer.isBuffer = function isBuffer(b) {
  return b instanceof Buffer;
};

Buffer.isEncoding = function isEncoding(enc) {
  return typeof enc == 'string' && ENCODINGS[enc.toLowerCase()] !== undefined;
};

/* ---- byte-range primitives (absolute indexes into each view) ---- */

/* three-way comparison of a[aStart,aEnd) with b[bStart,bEnd): -1, 0 or 1; a shorter prefix sorts first */
function compareRanges(a, aStart, aEnd, b, bStart, bEnd) {
  const n = Math.min(aEnd - aStart, bEnd - bStart);

  if(n > 0) {
    const r = compareArrayBuffer(a.buffer, a.byteOffset + aStart, a.byteOffset + aStart + n, b.buffer, b.byteOffset + bStart, b.byteOffset + bStart + n);

    if(r) return r < 0 ? -1 : 1;
  }

  const da = aEnd - aStart, db = bEnd - bStart;

  return da < db ? -1 : da > db ? 1 : 0;
}

/* copies src[sStart,sEnd) to dst[dStart..] (as much as fits); returns the byte count */
function copyBytes(src, sStart, sEnd, dst, dStart) {
  const n = Math.min(sEnd - sStart, dst.length - dStart);

  if(n <= 0) return 0;

  if(src.buffer === dst.buffer) dst.set(new Uint8Array(src.buffer, src.byteOffset + sStart, n), dStart);
  else copyArrayBuffer(dst.buffer, dst.byteOffset + dStart, dst.byteOffset + dStart + n, src.buffer, src.byteOffset + sStart, src.byteOffset + sStart + n);

  return n;
}

/* the first index >= from where `needle` starts in `hay`, or -1 */
function find(hay, needle, from) {
  if(needle.length == 0) return Math.min(from, hay.length);
  if(from + needle.length > hay.length) return -1;

  const r = searchArrayBuffer(hay.buffer, needle.buffer.byteLength == needle.length && needle.byteOffset == 0 ? needle.buffer : new Uint8Array(needle).buffer, hay.byteOffset + from, hay.length - from);

  return r === null ? -1 : r - hay.byteOffset;
}

/* ---- instance methods ---- */

const proto = Buffer.prototype;

function define(name, fn) {
  Object.defineProperty(proto, name, { value: fn, writable: true, enumerable: false, configurable: true });
}

Object.defineProperty(proto, 'parent', { get() { return this.buffer; }, enumerable: true, configurable: true });
Object.defineProperty(proto, 'offset', { get() { return this.byteOffset; }, enumerable: true, configurable: true });

define('toString', function toString(encoding, start, end) {
  const len = this.length;

  if(arguments.length == 0) return decode(this, 0, len, 'utf8');

  start = start === undefined || Number.isNaN(+start) ? 0 : Math.max(0, Math.trunc(+start));
  end = end === undefined || Number.isNaN(+end) ? len : Math.min(len, Math.trunc(+end));

  if(start >= len || end <= start) return '';

  return decode(this, start, end, normalizeEncoding(encoding));
});

define('toLocaleString', proto.toString);

define('toJSON', function toJSON() {
  return { type: 'Buffer', data: Array.from(this) };
});

define('equals', function equals(other) {
  if(!isU8(other)) throw argType('otherBuffer', 'an instance of Buffer or Uint8Array', other);

  return this.length == other.length && compareRanges(this, 0, this.length, other, 0, other.length) == 0;
});

define('compare', function compare(target, targetStart = 0, targetEnd = target?.length, sourceStart = 0, sourceEnd = this.length) {
  if(!isU8(target)) throw argType('target', 'an instance of Buffer or Uint8Array', target);

  if(targetStart < 0 || targetStart > target.length) throw outOfRange('targetStart', `>= 0 && <= ${target.length}`, targetStart);
  if(targetEnd < 0 || targetEnd > target.length) throw outOfRange('targetEnd', `>= 0 && <= ${target.length}`, targetEnd);
  if(sourceStart < 0 || sourceStart > this.length) throw outOfRange('sourceStart', `>= 0 && <= ${this.length}`, sourceStart);
  if(sourceEnd < 0 || sourceEnd > this.length) throw outOfRange('sourceEnd', `>= 0 && <= ${this.length}`, sourceEnd);

  if(sourceStart >= sourceEnd) return targetStart >= targetEnd ? 0 : -1;
  if(targetStart >= targetEnd) return 1;

  return compareRanges(this, sourceStart, sourceEnd, target, targetStart, targetEnd);
});

define('copy', function copy(target, targetStart = 0, sourceStart = 0, sourceEnd = this.length) {
  if(!isU8(target)) throw argType('target', 'an instance of Buffer or Uint8Array', target);

  targetStart = toOffset(targetStart, 'targetStart', 0);
  sourceStart = toOffset(sourceStart, 'sourceStart', 0);
  sourceEnd = toOffset(sourceEnd, 'sourceEnd', this.length);

  if(targetStart < 0) throw outOfRange('targetStart', '>= 0', targetStart);
  if(sourceStart < 0) throw outOfRange('sourceStart', '>= 0', sourceStart);
  if(sourceStart > this.length) throw outOfRange('sourceStart', `>= 0 && <= ${this.length}`, sourceStart);
  if(sourceEnd < 0) throw outOfRange('sourceEnd', '>= 0', sourceEnd);

  sourceEnd = Math.min(sourceEnd, this.length);

  if(targetStart >= target.length || sourceStart >= sourceEnd) return 0;

  return copyBytes(this, sourceStart, sourceEnd, target, targetStart);
});

define('slice', function slice(start, end) {
  return this.subarray(start, end);
});

define('fill', function fill(value, offset, end, encoding) {
  if(typeof offset == 'string') {
    encoding = offset;
    offset = 0;
    end = this.length;
  } else if(typeof end == 'string') {
    encoding = end;
    end = this.length;
  }

  offset = offset === undefined ? 0 : offset;
  end = end === undefined ? this.length : end;

  if(typeof offset != 'number') throw argType('offset', 'of type number', offset);
  if(typeof end != 'number') throw argType('end', 'of type number', end);
  if(offset < 0 || offset > this.length) throw outOfRange('offset', `>= 0 && <= ${this.length}`, offset);
  if(end < 0 || end > this.length) throw outOfRange('end', `>= 0 && <= ${this.length}`, end);

  offset = Math.trunc(offset);
  end = Math.trunc(end);

  if(offset >= end) return this;

  let pattern;

  if(typeof value == 'string') {
    const enc = normalizeEncoding(encoding);

    pattern = encode(value, enc);

    if(pattern.length == 0) {
      if(value.length > 0) throw nodeError(TypeError, 'ERR_INVALID_ARG_VALUE', `The argument 'value' is invalid. Received '${value}'`);

      pattern = new Uint8Array(1);
    }
  } else if(isU8(value)) {
    pattern = value;

    if(pattern.length == 0) throw nodeError(TypeError, 'ERR_INVALID_ARG_VALUE', "The argument 'value' is invalid. Received <Buffer >");
  } else {
    pattern = new Uint8Array([typeof value == 'boolean' ? +value : value & 255]);
  }

  if(pattern.length == 1) {
    Uint8Array.prototype.fill.call(this, pattern[0], offset, end);
    return this;
  }

  let filled = copyBytes(pattern, 0, pattern.length, this, offset);

  /* double the filled prefix until the range is full */
  while(offset + filled < end) {
    const n = Math.min(filled, end - offset - filled);

    this.copyWithin(offset + filled, offset, offset + n);
    filled += n;
  }

  return this;
});

/* the byteOffset argument of indexOf/lastIndexOf, resolved against `len` */
function resolveOffset(byteOffset, len, def) {
  if(typeof byteOffset == 'string') return def;

  let o = +byteOffset;

  if(byteOffset === undefined || Number.isNaN(o)) return def;

  o = Math.trunc(o);

  return o < 0 ? Math.max(len + o, 0) : o;
}

/* the bytes to look for: [bytes, kind] with bytes a Uint8Array */
function needleOf(value, encoding) {
  if(typeof value == 'string') return encode(value, normalizeEncoding(encoding));
  if(typeof value == 'number') return new Uint8Array([value & 255]);
  if(isU8(value)) return value;

  throw argType('value', 'one of type number or string or an instance of Buffer or Uint8Array', value);
}

define('indexOf', function indexOf(value, byteOffset, encoding) {
  if(typeof byteOffset == 'string') {
    encoding = byteOffset;
    byteOffset = undefined;
  }

  const needle = needleOf(value, encoding);
  const from = resolveOffset(byteOffset, this.length, 0);

  if(needle.length == 0) return Math.min(from, this.length);

  return find(this, needle, from);
});

define('lastIndexOf', function lastIndexOf(value, byteOffset, encoding) {
  if(typeof byteOffset == 'string') {
    encoding = byteOffset;
    byteOffset = undefined;
  }

  const needle = needleOf(value, encoding);
  const last = resolveOffset(byteOffset, this.length, this.length);

  if(needle.length == 0) return Math.min(last, this.length);

  let result = -1;

  for(let pos = find(this, needle, 0); pos != -1 && pos <= last; pos = find(this, needle, pos + 1)) result = pos;

  return result;
});

define('includes', function includes(value, byteOffset, encoding) {
  return this.indexOf(value, byteOffset, encoding) !== -1;
});

define('write', function write(string, offset, length, encoding) {
  if(typeof string != 'string') throw argType('argument', 'of type string', string);

  if(offset === undefined) {
    encoding = 'utf8';
    length = this.length;
    offset = 0;
  } else if(length === undefined && typeof offset == 'string') {
    encoding = offset;
    length = this.length;
    offset = 0;
  } else {
    offset = toOffset(offset, 'offset', 0);

    if(offset < 0 || offset > this.length) throw outOfRange('offset', `>= 0 && <= ${this.length}`, offset);

    const remaining = this.length - offset;

    if(length === undefined) length = remaining;
    else if(typeof length == 'string') {
      encoding = length;
      length = remaining;
    } else {
      length = toOffset(length, 'length', remaining);

      if(length < 0 || length > this.length) throw outOfRange('length', `>= 0 && <= ${this.length}`, length);
      if(length > remaining) length = remaining;
    }
  }

  const enc = normalizeEncoding(encoding);
  const bytes = encode(string, enc);
  let n = Math.min(bytes.length, length);

  if(enc == 'utf8') while(n > 0 && n < bytes.length && (bytes[n] & 0xc0) == 0x80) n--;
  else if(enc == 'utf16le') n &= ~1;

  copyBytes(bytes, 0, n, this, offset);

  return n;
});

function swap(buf, size, label) {
  if(buf.length % size) throw nodeError(RangeError, 'ERR_INVALID_BUFFER_SIZE', `Buffer size must be a multiple of ${label}-bits`);

  for(let i = 0; i < buf.length; i += size)
    for(let a = i, b = i + size - 1; a < b; a++, b--) {
      const t = buf[a];

      buf[a] = buf[b];
      buf[b] = t;
    }

  return buf;
}

define('swap16', function swap16() { return swap(this, 2, '16'); });
define('swap32', function swap32() { return swap(this, 4, '32'); });
define('swap64', function swap64() { return swap(this, 8, '64'); });

define(inspectCustom, function inspect() {
  const max = INSPECT_MAX_BYTES;
  const shown = Array.from(this.subarray(0, max), b => (b < 16 ? '0' : '') + b.toString(16)).join(' ');
  const more = this.length > max ? ` ... ${this.length - max} more byte${this.length - max > 1 ? 's' : ''}` : '';

  return `<${this.constructor.name} ${shown}${more}>`.replace(' >', '>');
});

/* ---- fixed-size integer, bigint and float accessors ---- */

const dv = buf => new DataView(buf.buffer, buf.byteOffset, buf.byteLength);

/* the checked offset of a read/write of `size` bytes */
function checkAccess(buf, offset, size) {
  offset = toOffset(offset, 'offset', 0);

  if(buf.length - size < 0) throw outOfBounds();
  if(offset < 0 || offset > buf.length - size) throw outOfRange('offset', `>= 0 and <= ${buf.length - size}`, offset);

  return offset;
}

function checkValue(value, min, max, name = 'value') {
  if(typeof value == 'bigint' ? (value < BigInt(min) || value > BigInt(max)) : (value < min || value > max)) {
    const range = typeof min == 'bigint' || (typeof max == 'bigint') ? `>= ${min} and <= ${max}` : `>= ${min < -(2 ** 31) ? `-(2 ** ${Math.log2(-min)})` : min} and ${max > 2 ** 31 ? `< 2 ** ${Math.log2(max + 1)}` : `<= ${max}`}`;

    throw outOfRange(name, range, value);
  }
}

/* defines read<Name>/write<Name> (+ LE/BE for size > 1) over a DataView getter/setter */
function accessor(name, size, getter, setter, min, max, aliases = []) {
  const ends = size == 1 ? [['', false]] : [['LE', true], ['BE', false]];
  const big = typeof min == 'bigint';

  for(const [suffix, little] of ends) {
    const reader = function(offset) {
      return dv(this)[getter](checkAccess(this, offset, size), little);
    };
    const writer = function(value, offset) {
      value = big ? value : +value;

      if(big && typeof value != 'bigint') throw argType('value', 'of type bigint', value);
      if(min !== undefined) checkValue(value, min, max);

      offset = checkAccess(this, offset, size);
      dv(this)[setter](offset, value, little);

      return offset + size;
    };

    for(const n of [name, ...aliases]) {
      define(`read${n}${suffix}`, Object.defineProperty(reader, 'name', { value: `read${n}${suffix}` }));
      define(`write${n}${suffix}`, Object.defineProperty(writer, 'name', { value: `write${n}${suffix}` }));
    }
  }
}

accessor('UInt8', 1, 'getUint8', 'setUint8', 0, 255, ['Uint8']);
accessor('Int8', 1, 'getInt8', 'setInt8', -128, 127);
accessor('UInt16', 2, 'getUint16', 'setUint16', 0, 0xffff, ['Uint16']);
accessor('Int16', 2, 'getInt16', 'setInt16', -32768, 32767);
accessor('UInt32', 4, 'getUint32', 'setUint32', 0, 0xffffffff, ['Uint32']);
accessor('Int32', 4, 'getInt32', 'setInt32', -(2 ** 31), 2 ** 31 - 1);
accessor('BigUInt64', 8, 'getBigUint64', 'setBigUint64', 0n, 2n ** 64n - 1n, ['BigUint64']);
accessor('BigInt64', 8, 'getBigInt64', 'setBigInt64', -(2n ** 63n), 2n ** 63n - 1n);
accessor('Float', 4, 'getFloat32', 'setFloat32');
accessor('Double', 8, 'getFloat64', 'setFloat64');

/* ---- variable-size integers: read/write(U)Int(LE|BE)(offset, byteLength) ---- */

function checkByteLength(byteLength) {
  if(byteLength === undefined) throw argType('byteLength', 'of type number', byteLength);
  if(typeof byteLength != 'number') throw argType('byteLength', 'of type number', byteLength);
  if(!Number.isInteger(byteLength) || byteLength < 1 || byteLength > 6) throw outOfRange('byteLength', '>= 1 and <= 6', byteLength);
}

for(const [suffix, little] of [['LE', true], ['BE', false]]) {
  for(const alias of ['UInt', 'Uint']) {
    define(`read${alias}${suffix}`, function(offset, byteLength) {
      checkByteLength(byteLength);
      offset = checkAccess(this, offset, byteLength);

      let v = 0;

      for(let i = 0; i < byteLength; i++) v += this[offset + (little ? i : byteLength - 1 - i)] * 2 ** (8 * i);

      return v;
    });

    define(`write${alias}${suffix}`, function(value, offset, byteLength) {
      checkByteLength(byteLength);
      value = +value;
      checkValue(value, 0, 2 ** (8 * byteLength) - 1);
      offset = checkAccess(this, offset, byteLength);

      for(let i = 0; i < byteLength; i++) {
        this[offset + (little ? i : byteLength - 1 - i)] = value % 256;
        value = Math.floor(value / 256);
      }

      return offset + byteLength;
    });
  }

  define(`readInt${suffix}`, function(offset, byteLength) {
    const v = this[`readUInt${suffix}`](offset, byteLength);

    return v >= 2 ** (8 * byteLength - 1) ? v - 2 ** (8 * byteLength) : v;
  });

  define(`writeInt${suffix}`, function(value, offset, byteLength) {
    checkByteLength(byteLength);
    value = +value;
    checkValue(value, -(2 ** (8 * byteLength - 1)), 2 ** (8 * byteLength - 1) - 1);

    return this[`writeUInt${suffix}`](value < 0 ? value + 2 ** (8 * byteLength) : value, offset, byteLength);
  });
}

/* ---- module functions ---- */

export function SlowBuffer(size) {
  return Buffer.allocUnsafeSlow(size);
}

/* btoa(binary): base64 of a Latin-1 string; atob(b64) is the inverse, padding optional */
export function btoa(data) {
  const str = String(data);

  for(let i = 0; i < str.length; i++)
    if(str.charCodeAt(i) > 255) throw Object.assign(new Error('The string to be encoded contains characters outside of the Latin1 range.'), { name: 'InvalidCharacterError', code: 5 });

  return decode(encode(str, 'latin1'), 0, str.length, 'base64');
}

export function atob(data) {
  const invalid = () => Object.assign(new Error('The string to be decoded is not correctly encoded.'), { name: 'InvalidCharacterError', code: 5 });
  let str = String(data).replace(/[\t\n\f\r ]/g, '');

  if(str.length % 4 == 0) str = str.replace(/==?$/, '');
  if(str.length % 4 == 1 || /[^A-Za-z0-9+/]/.test(str)) throw invalid();

  const bytes = encode(str, 'base64');

  return decode(bytes, 0, bytes.length, 'latin1');
}

/* isUtf8(input) / isAscii(input): whether the bytes of an ArrayBuffer or view are valid UTF-8 / 7-bit ASCII */
function bytesOf(input) {
  if(isAnyArrayBuffer(input)) return new Uint8Array(input);
  if(ArrayBuffer.isView(input)) return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);

  throw argType('input', 'an instance of ArrayBuffer or ArrayBufferView', input);
}

export function isAscii(input) {
  return bytesOf(input).every(b => b < 128);
}

export function isUtf8(input) {
  const bytes = bytesOf(input);
  let i = 0;

  while(i < bytes.length) {
    const b = bytes[i];
    const n = b < 0x80 ? 1 : b >= 0xc2 && b <= 0xdf ? 2 : b >= 0xe0 && b <= 0xef ? 3 : b >= 0xf0 && b <= 0xf4 ? 4 : 0;

    if(!n || i + n > bytes.length) return false;

    for(let k = 1; k < n; k++) if((bytes[i + k] & 0xc0) != 0x80) return false;

    if(n == 3 && ((b == 0xe0 && bytes[i + 1] < 0xa0) || (b == 0xed && bytes[i + 1] >= 0xa0))) return false;
    if(n == 4 && ((b == 0xf0 && bytes[i + 1] < 0x90) || (b == 0xf4 && bytes[i + 1] >= 0x90))) return false;

    i += n;
  }

  return true;
}

export { Blob };

export default { Buffer, SlowBuffer, Blob, atob, btoa, isAscii, isUtf8, constants, kMaxLength, kStringMaxLength, INSPECT_MAX_BYTES };
