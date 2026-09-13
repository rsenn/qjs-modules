export class Pointer {
  static fromArray(array) {
    return Object.setPrototypeOf([...array], Pointer.prototype);
  }

  static fromString(str) {
    return this.fromArray(Pointer.parse(str));
  }

  static from(other) {
    if(typeof other == 'string') return this.fromString(other);
    return this.fromArray([...other]);
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

    if(slash != -1 && (dot == -1 || slash < dot) && (bracket == -1 || slash < bracket)) return Pointer.parseRFC6901(str);

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
    if(typeof ptr == 'string') return Pointer.fromString(ptr);
    if(ptr !== undefined) return Pointer.from(ptr);
  }

  deref(obj) {}

  hier() {
    const { length } = this;
    const r = [];
    for(let i = 1; i <= length; i++) r.push(this.slice(0, i));
    return r;
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

    for(let i = 0; i < this.length; i++) s += '/' + String(this[i]).replace(/~/g, '~0').replace(/\//g, '~1');

    return s;
  }

  /*toArray() {}
  shift() {}
  push() {}
  concat() {}
  slice() {}
  keys() {}
  values() {}
  hier() {}
  toPrimitive() {}
  path() {}
  atoms() {}
  map() {}
  reduce() {}
  forEach() {}*/

  *[Symbol.iterator]() {
    const { length } = this;
    for(let i = 0; i < length; i++) yield this[i];
  }
}

Pointer.prototype.push = Array.prototype.push;
Pointer.prototype.slice = Array.prototype.slice;
Pointer.prototype.shift = Array.prototype.shift;
Pointer.prototype.keys = Array.prototype.keys;
Pointer.prototype.values = Array.prototype.values;
Pointer.prototype.map = Array.prototype.map;
Pointer.prototype.reduce = Array.prototype.reduce;
Pointer.prototype.forEach = Array.prototype.forEach;
Pointer.prototype.concat = Array.prototype.concat;
Pointer.prototype.length = 0;
