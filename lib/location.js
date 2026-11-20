/**
 * Location — pure-JS implementation of the QuickJS native module
 * (`quickjs-location.c` / `location.md`).
 *
 * Represents a position in a source text: 1-based line, 1-based column,
 * character/byte offsets and a file name. Used by lexers/parsers.
 *
 * Internal state is 0-based (`line`, `column`) with `-1` meaning "unset";
 * the getters expose the 1-based public view (and yield `undefined` when
 * unset), exactly like the C accessors.
 */

const UNSET = -1;

/**
 * This class describes a location.
 *
 * @class      Location (name)
 *
 * new Location()
 * new Location("file:line:column")
 * new Location([file], line, column, charOffset [, file])
 *
 * new Location({ line, column, charOffset, byteOffset, file })
 *   (aliases: lineNumber, columnNumber, fileName)
 *
 * new Location(otherLocation)
 */
export class Location {
  constructor(...args) {
    this._line = UNSET;
    this._column = UNSET;
    this._charOffset = UNSET;
    this._byteOffset = UNSET;
    this._file = undefined;

    const first = args[0];

    if(args.length >= 1 && (first instanceof Location || (first !== null && typeof first === 'object'))) {
      /* From a Location or a location-like object. */
      if(first instanceof Location) {
        this._line = first._line;
        this._column = first._column;
        this._charOffset = first._charOffset;
        this._byteOffset = first._byteOffset;
        this._file = first._file;
      } else {
        readProps(first, this);
      }
      return;
    }

    if(args.length === 1 && typeof first === 'string') {
      /* From "file:line:column" (numeric segments are consumed from the 
         right; the remainder becomes the file name). */
      const parts = first.split(':');
      const nums = [];
      while(parts.length && /^\d+$/.test(parts[parts.length - 1])) nums.unshift(parseInt(parts.pop(), 10));

      const file = parts.join(':');
      if(file !== '') this._file = file;
      if(nums.length >= 2) this._line = nums[nums.length - 2] - 1;
      if(nums.length >= 1) this._column = nums[nums.length - 1] - 1;
      return;
    }

    if(args.length >= 1) {
      /* From arguments: [file], line, column, charOffset [, file] */
      let i = 0;

      if(i < args.length && typeof args[i] !== 'number') {
        this._file = args[i] == null ? undefined : String(args[i]);
        i++;
      }

      if(i < args.length && typeof args[i] === 'number') this._line = args[i++] | 0;
      if(i < args.length && typeof args[i] === 'number') this._column = args[i++] | 0;
      if(i < args.length && typeof args[i] === 'number') this._charOffset = args[i++];
      if(i < args.length && this._file === undefined && typeof args[i] !== 'number') this._file = args[i] == null ? undefined : String(args[i]);

      if(this._line !== UNSET) this._line--;
      if(this._column !== UNSET) this._column--;
    }
  }

  get line() {
    return this._line !== UNSET ? this._line + 1 : undefined;
  }

  get column() {
    return this._column !== UNSET ? this._column + 1 : undefined;
  }

  get charOffset() {
    return this._charOffset !== UNSET ? this._charOffset : undefined;
  }

  get byteOffset() {
    return this._byteOffset !== UNSET ? this._byteOffset : undefined;
  }

  get file() {
    return this._file;
  }

  set file(value) {
    if(value instanceof Location) this._file = value._file;
    else this._file = value == null ? undefined : String(value);
  }

  /** Compares this location with another for equality. */
  equal(other) {
    other = Location._coerce(other);
    return this._line === other._line && this._column === other._column && this._charOffset === other._charOffset && this._byteOffset === other._byteOffset && this._file === other._file;
  }

  /** Returns a copy of the location. */
  clone() {
    return new Location(this);
  }

  /**
   * Overwrites this location in place with `other`
   * (Location or location-like object, aliases accepted).
   */
  copy(other) {
    const tmp = new Location(other);
    this._line = tmp._line;
    this._column = tmp._column;
    this._charOffset = tmp._charOffset;
    this._byteOffset = tmp._byteOffset;
    this._file = tmp._file;
    return this;
  }

  /**
   * Advances the location by one character.
   * `input` may be a numeric code point, or an ArrayBuffer / typed array /
   * string to decode the next UTF-8 code point from.
   * Returns the number of bytes consumed.
   */
  nextChar(input) {
    let code = -1;

    if(typeof input === 'number') {
      code = input | 0;
    } else {
      const bytes = toBytes(input);
      if(bytes !== null && bytes.length > 0) {
        const dec = utf8DecodeFirst(bytes);
        if(dec) code = dec.code;
      }
    }

    if(code < 0) throw new TypeError('argument 1 must be Number | ArrayBuffer | TypedArray | string');

    /* Assume a counted position when unset (counts start at line 0/col 0). */
    if(this._line === UNSET) this._line = 0;
    if(this._column === UNSET) this._column = 0;
    if(this._charOffset === UNSET) this._charOffset = 0;
    if(this._byteOffset === UNSET) this._byteOffset = 0;

    const bytes = utf8Length(code);

    this._charOffset += 1;
    this._byteOffset += bytes;

    if(code === 0x0a /* \n */) {
      this._line += 1;
      this._column = 0;
    } else this._column += 1;

    return bytes;
  }

  /** Renders as `file:line:column` (or `line:column` when no file). */
  toString() {
    const line = this.line ?? '';
    const column = this.column ?? '';
    return this._file !== undefined ? `${this._file}:${line}:${column}` : `${line}:${column}`;
  }

  /** Primitive coercion: "number" hint → charOffset, otherwise string. */
  [Symbol.toPrimitive](hint) {
    return hint === 'number' ? this._charOffset : this.toString();
  }

  /** Coerce a value to a Location (used by equal()). */
  static _coerce(value) {
    return value instanceof Location ? value : new Location(value);
  }

  /**
   * Counts lines/columns by scanning a string (or buffer), producing a
   * Location. charOffset = characters scanned, byteOffset = UTF-8 bytes.
   */
  static count(input) {
    const bytes = toBytes(input);
    if(bytes === null) throw new TypeError('argument 1 must be string | ArrayBuffer | TypedArray');

    const loc = new Location();
    loc._line = 0;
    loc._column = 0;
    loc._charOffset = 0;
    loc._byteOffset = 0;

    let i = 0;
    while(i < bytes.length) {
      const dec = utf8DecodeFirst(bytes.subarray(i));
      if(!dec) throw new TypeError('invalid UTF-8 in input');

      i += dec.bytes;
      loc._charOffset += 1;
      loc._byteOffset += dec.bytes;

      if(dec.code === 0x0a /* \n */) {
        loc._line += 1;
        loc._column = 0;
      } else loc._column += 1;
    }

    return loc;
  }

  static get [Symbol.species]() {
    return Location;
  }
}

/* Enumerable, getter-only accessors with no setter (assignment throws
 * in strict mode, silently no-ops in sloppy mode — same as the C
 * accessors defined without a setter). */
for(const name of ['line', 'column', 'charOffset', 'byteOffset']) {
  const desc = Object.getOwnPropertyDescriptor(Location.prototype, name);
  desc.enumerable = true;
  Object.defineProperty(Location.prototype, name, desc);
}

Object.defineProperty(Location.prototype, Symbol.toStringTag, {
  value: 'Location',
  configurable: true,
});

/**
 * Decode the first UTF-8 code point from `bytes`.
 * Returns { code, bytes } or null on invalid input.
 */
function utf8DecodeFirst(bytes) {
  if(bytes.length === 0) return null;

  const b0 = bytes[0];

  if(b0 < 0x80) return { code: b0, bytes: 1 };

  let len, code;

  if(b0 >= 0xc2 && b0 <= 0xdf) {
    len = 2;
    code = b0 & 0x1f;
  } else if(b0 >= 0xe0 && b0 <= 0xef) {
    len = 3;
    code = b0 & 0x0f;
  } else if(b0 >= 0xf0 && b0 <= 0xf7) {
    len = 4;
    code = b0 & 0x07;
  } else return null; /* continuation byte or invalid lead byte */

  if(bytes.length < len) return null;

  for(let i = 1; i < len; i++) {
    const b = bytes[i];
    if(b < 0x80 || b > 0xbf) return null;
    code = (code << 6) | (b & 0x3f);
  }

  /* reject overlong encodings and out-of-range code points */
  if(code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return null;
  if((len === 2 && code < 0x80) || (len === 3 && code < 0x800) || (len === 4 && code < 0x10000)) return null;

  return { code, bytes: len };
}

/** UTF-8 byte length of a code point. */
function utf8Length(code) {
  if(code < 0x80) return 1;
  if(code < 0x800) return 2;
  if(code < 0x10000) return 3;
  return 4;
}

/** Normalize the `nextChar` input argument into a Uint8Array or null. */
function toBytes(input) {
  if(typeof input === 'string') {
    if(typeof TextEncoder !== 'undefined') return new TextEncoder().encode(input);

    /* Fallback for environments without TextEncoder. */
    const bytes = [];

    for(let i = 0; i < input.length; i++) {
      let cp = input.charCodeAt(i);

      if(cp >= 0xd800 && cp <= 0xdbff && i + 1 < input.length) {
        const lo = input.charCodeAt(i + 1);

        if(lo >= 0xdc00 && lo <= 0xdfff) {
          cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
          i++;
        }
      }

      const n = utf8Length(cp);

      for(let s = (n - 1) * 6; s >= 0; s -= 6) bytes.push(s === (n - 1) * 6 ? (n === 1 ? cp : (cp >> s) | ((0xff << (8 - n)) & 0xff)) : 0x80 | ((cp >> s) & 0x3f));
    }

    return Uint8Array.from(bytes);
  }

  if(input instanceof ArrayBuffer) return new Uint8Array(input);
  if(ArrayBuffer.isView(input)) return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);

  return null;
}

/* Property reading with aliases (copy() / constructor from object) */
function readProps(source, target) {
  const get = names => {
    for(const name of names) if(source[name] !== undefined) return source[name];

    return undefined;
  };

  let v = get(['line', 'lineNumber']);
  if(v !== undefined) target._line = (v | 0) - 1;

  v = get(['column', 'columnNumber']);
  if(v !== undefined) target._column = (v | 0) - 1;

  v = get(['file', 'fileName']);
  if(v !== undefined) target._file = v == null ? undefined : String(v);

  v = get(['charOffset']);
  if(v !== undefined) target._charOffset = Number(v);

  v = get(['byteOffset']);
  if(v !== undefined) target._byteOffset = Number(v);
}

export default Location;
