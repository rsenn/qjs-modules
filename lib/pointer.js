export class DereferenceError extends Error {
  #pointer;
  #pos;
  #root;

  constructor(message, { pointer, pos, root } = {}) {
    super(message ?? 'Dereference failed');
    this.name = 'DereferenceError';
    this.#pointer = pointer;
    this.#pos = pos;
    this.#root = root;
  }

  get pointer() {
    return this.#pointer;
  }

  get pos() {
    return this.#pos;
  }

  get root() {
    return this.#root;
  }

  get [Symbol.toStringTag]() {
    return 'DereferenceError';
  }
}

function toPointer(other) {
  return other instanceof Pointer ? other : Pointer.from(other);
}

function atomsEqual(a, b) {
  return a === b || String(a) === String(b);
}

export class Pointer {
  static get [Symbol.species]() {
    return this;
  }

  static fromArray(array) {
    return Object.setPrototypeOf([...array], Pointer.prototype);
  }

  static fromAtoms(atoms) {
    return Pointer.fromArray(atoms);
  }

  static fromString(str) {
    return this.fromArray(Pointer.parse(str));
  }

  static from(other) {
    if(other instanceof Pointer) return Pointer.fromArray(other);
    if(typeof other === 'string') return this.fromString(other);
    if(other != null && typeof other[Symbol.iterator] === 'function') {
      return Pointer.fromArray(other);
    }
    throw new TypeError('Pointer.from expects a string, array, iterable, or Pointer');
  }

  static of(...keys) {
    return Pointer.fromArray(keys);
  }

  static ofAtoms(...atoms) {
    return Pointer.fromArray(atoms);
  }

  static isPointer(value) {
    return value instanceof Pointer;
  }

  /* Parses either RFC 6901 JSON Pointer syntax ("/foo/bar/1", with '~0'/'~1'
   * escaping) or this module's own dot/bracket path syntax ("foo.bar[1]",
   * backslash-escaped '.'), matching whichever native quickjs-pointer.c's
   * pointer_parse() would pick: slash format wins when '/' occurs before
   * any '.'/'[' delimiter, or there's no '.'/'[' at all. */
  static parse(str) {
    if(str === '') return [];

    const slash = str.indexOf('/');
    const dot = str.indexOf('.');
    const bracket = str.indexOf('[');

    if(slash != -1 && (dot == -1 || slash < dot) && (bracket == -1 || slash < bracket)) {
      return Pointer.parseRFC6901(str);
    }

    return Pointer.parseDotBracket(str);
  }

  static parseRFC6901(str) {
    const s = str[0] == '/' ? str.slice(1) : str;

    return s.split('/').map(tok => {
      const decoded = tok.replace(/~1/g, '/').replace(/~0/g, '~');
      return /^(0|[1-9][0-9]*)$/.test(decoded) ? +decoded : decoded;
    });
  }

  static parseDotBracket(str) {
    const s = str[0] == '.' ? str.slice(1) : str;
    const atoms = [];
    let i = 0;

    while(i < s.length) {
      const bracket = s[i] == '[';
      const start = bracket ? i + 1 : i;
      const delims = bracket ? ']' : '.[';
      let end = start;

      for(;;) {
        while(end < s.length && !delims.includes(s[end])) end++;

        if(end < s.length && end > start && s[end - 1] == '\\') {
          end++;
          continue;
        }

        break;
      }

      const raw = s.slice(start, end).replace(/\\(.)/g, '$1');
      atoms.push(/^-?[0-9]+$/.test(raw) ? +raw : raw);

      i = bracket && s[end] == ']' ? end + 1 : end;
      if(s[i] == '.') i++;
    }

    return atoms;
  }

  constructor(ptr) {
    if(typeof ptr === 'string') return Pointer.fromString(ptr);
    if(ptr !== undefined) return Pointer.from(ptr);
    return Pointer.fromArray([]);
  }

  get [Symbol.toStringTag]() {
    return 'Pointer';
  }

  get path() {
    return this.toArray();
  }

  set path(value) {
    const next = Array.isArray(value) ? [...value] : Pointer.from(value).toArray();
    this.splice(0, this.length, ...next);
  }

  get atoms() {
    return this.toArray();
  }

  deref(root) {
    let cur = root;
    const { length } = this;

    for(let i = 0; i < length; i++) {
      const key = this[i];

      if(cur === null || typeof cur !== 'object') {
        throw new DereferenceError(`Cannot dereference atom "${String(key)}" at index ${i}`, {
          pointer: this,
          pos: i,
          root,
        });
      }

      if(Array.isArray(cur)) {
        if(typeof key === 'number') {
          if(!Number.isInteger(key) || key < 0 || key >= cur.length) {
            throw new DereferenceError(`Array index ${key} out of bounds at index ${i}`, {
              pointer: this,
              pos: i,
              root,
            });
          }
          cur = cur[key];
          continue;
        }
        if(key === 'length') {
          cur = cur.length;
          continue;
        }
        if(!/^(0|[1-9][0-9]*)$/.test(String(key))) {
          throw new DereferenceError(`Invalid array key "${String(key)}" at index ${i}`, {
            pointer: this,
            pos: i,
            root,
          });
        }
        const idx = Number(key);
        if(idx < 0 || idx >= cur.length) {
          throw new DereferenceError(`Array index ${idx} out of bounds at index ${i}`, {
            pointer: this,
            pos: i,
            root,
          });
        }
        cur = cur[idx];
        continue;
      }

      const prop = String(key);
      if(!Object.prototype.hasOwnProperty.call(cur, prop)) {
        throw new DereferenceError(`Missing property "${prop}" at index ${i}`, {
          pointer: this,
          pos: i,
          root,
        });
      }
      cur = cur[prop];
    }

    return cur;
  }

  toArray() {
    return Array.prototype.slice.call(this);
  }

  at(index) {
    return Array.prototype.at.call(this, index);
  }

  slice(start, end) {
    return Pointer.fromArray(Array.prototype.slice.call(this, start, end));
  }

  concat(...items) {
    const combined = this.toArray();
    for(const item of items) {
      if(item instanceof Pointer || Array.isArray(item)) {
        combined.push(...item);
      } else {
        combined.push(item);
      }
    }
    return Pointer.fromArray(combined);
  }

  hier() {
    const { length } = this;
    const r = [];
    for(let i = 1; i <= length; i++) r.push(this.slice(0, i));
    return r;
  }

  up(n = 1) {
    const count = Math.max(0, Number(n) || 0);
    if(count === 0) return Pointer.fromArray(this);
    const nextLen = Math.max(0, this.length - count);
    return this.slice(0, nextLen);
  }

  truncate(n = 1) {
    return this.up(n);
  }

  down(...parts) {
    const next = this.toArray();
    for(const part of parts) {
      if(part === undefined) continue;
      if(part instanceof Pointer) {
        next.push(...part);
      } else if(Array.isArray(part)) {
        next.push(...part);
      } else if(typeof part === 'string') {
        next.push(...Pointer.parse(part));
      } else {
        next.push(part);
      }
    }
    return Pointer.fromArray(next);
  }

  equal(other) {
    const p = toPointer(other);
    if(this.length !== p.length) return false;
    for(let i = 0; i < this.length; i++) {
      if(!atomsEqual(this[i], p[i])) return false;
    }
    return true;
  }

  compare(other) {
    const p = toPointer(other);
    const minLen = Math.min(this.length, p.length);
    for(let i = 0; i < minLen; i++) {
      const a = this[i];
      const b = p[i];
      if(typeof a === 'number' && typeof b === 'number') {
        if(a !== b) return a < b ? -1 : 1;
      } else {
        const sa = String(a);
        const sb = String(b);
        if(sa !== sb) return sa < sb ? -1 : 1;
      }
    }
    if(this.length === p.length) return 0;
    return this.length < p.length ? -1 : 1;
  }

  common(other) {
    const p = toPointer(other);
    const minLen = Math.min(this.length, p.length);
    let i = 0;
    while(i < minLen && atomsEqual(this[i], p[i])) i++;
    return this.slice(0, i);
  }

  startsWith(other) {
    const p = toPointer(other);
    if(p.length > this.length) return false;
    for(let i = 0; i < p.length; i++) {
      if(!atomsEqual(this[i], p[i])) return false;
    }
    return true;
  }

  endsWith(other) {
    const p = toPointer(other);
    if(p.length > this.length) return false;
    const offset = this.length - p.length;
    for(let i = 0; i < p.length; i++) {
      if(!atomsEqual(this[offset + i], p[i])) return false;
    }
    return true;
  }

  relativeTo(other) {
    const base = toPointer(other);
    if(!this.startsWith(base)) {
      throw new Error(`Pointer "${this.toString()}" is not relative to "${base.toString()}"`);
    }
    return this.slice(base.length);
  }

  toPrimitive(hint) {
    if(hint === 'number') return this.length;
    return this.toString();
  }

  [Symbol.toPrimitive](hint) {
    return this.toPrimitive(hint);
  }

  /* Dot/bracket path format, matching quickjs-pointer.c's atoms_serialize():
   * numeric atoms as "[N]", string atoms joined by ".", with literal '.'
   * backslash-escaped. */
  toString() {
    let s = '';

    for(let i = 0; i < this.length; i++) {
      const atom = this[i];

      if(typeof atom == 'number' || /^-?[0-9]+$/.test(String(atom))) {
        s += `[${atom}]`;
      } else {
        if(i > 0) s += '.';
        s += String(atom).replace(/\./g, '\\.');
      }
    }

    return s;
  }

  /* RFC 6901 §5: "/foo/bar/1", escaping '~' as "~0" and '/' as "~1" (in
   * that order). An empty Pointer serializes to the empty string (the
   * "whole document" pointer). */
  toRFC6901() {
    let s = '';

    for(let i = 0; i < this.length; i++) {
      s += '/' + String(this[i]).replace(/~/g, '~0').replace(/\//g, '~1');
    }

    return s;
  }

  *[Symbol.iterator]() {
    const { length } = this;
    for(let i = 0; i < length; i++) yield this[i];
  }
}

Pointer.prototype.push = Array.prototype.push;
Pointer.prototype.pop = Array.prototype.pop;
Pointer.prototype.shift = Array.prototype.shift;
Pointer.prototype.unshift = Array.prototype.unshift;
Pointer.prototype.splice = Array.prototype.splice;
Pointer.prototype.keys = Array.prototype.keys;
Pointer.prototype.values = Array.prototype.values;
Pointer.prototype.map = Array.prototype.map;
Pointer.prototype.reduce = Array.prototype.reduce;
Pointer.prototype.forEach = Array.prototype.forEach;
Pointer.prototype.length = 0;
