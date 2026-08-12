import { join, normalize } from 'path';
import { define, declare } from 'util';

export class URLSearchParams {
  #entries = [];

  constructor(q) {
    if(typeof q === 'string') {
      if(q.charAt(0) === '?') q = q.slice(1);

      for(let p = q.split('&'), i = 0, length = p.length; i < length; i++) {
        const v = p[i];
        const j = v.indexOf('=');
        if(-1 < j) {
          this.append(decode(v.slice(0, j)), decode(v.slice(j + 1)));
        } else if(v.length) {
          this.append(decode(v.slice(0, j)), '');
        }
      }
    } else if(typeof q == 'object') {
      if(Symbol.iterator in q) {
        for(const [k, v] of q) this.append(k, v);
      } else {
        for(const k in q) this.append(k, q[k]);
      }
    }

    function decode(str) {
      return decodeURIComponent(str.replace(/\+/g, ' '));
    }
  }

  append(name, value) {
    this.#entries.push([name, value]);
  }

  delete(name, value) {
    this.#entries = this.#entries.filter(value === undefined ? ([k]) => !(k == name) : ([k, v]) => !(k == name && v == value));
  }

  get(name) {
    return this.#entries.find(([key]) => key == name)?.[1];
  }

  getAll(name) {
    return this.#entries.filter(([key]) => key == name).map(([key, value]) => value);
  }

  has(name, value) {
    return this.#entries.some(value === undefined ? ([k]) => !(k == name) : ([k, v]) => !(k == name && v == value));
  }

  set(name, value) {
    const index = this.#entries.findIndex(([key]) => key == name);
    this.delete(name);
    this.#entries.splice(index, 0, [name, value]);
  }

  forEach(callback, thisArg) {
    for(const [key, value] of this.#entries) callback.call(thisArg, value, key, this);
  }

  toString() {
    const q = [];
    for(const [k, v] of this.#entries) q.push(encode(k) + '=' + encode(v));
    return q.join('&');

    function encode(str) {
      return encodeURIComponent(str).replace(/[!'\(\)~]|%20|%00/g, match => replace[match]);
    }
  }

  *entries() {
    yield* this.#entries;
  }

  *keys() {
    yield* this.#entries.map(([k]) => k);
  }

  *values() {
    yield* this.#entries.map(([, v]) => v);
  }

  sort() {
    this.#entries.sort((a, b) => a[0] > b[0]);
  }

  get size() {
    return this.#entries.length;
  }
}

propertyDescriptors(URLSearchParams.prototype, {
  [Symbol.toStringTag]: { value: 'URLSearchParams' },
  size: {
    enumerable: true,
  },
});

export class URL {
  #hash;

  constructor(url, base) {
    // stringify inputs per spec
    const urlStr = String(url);
    const baseStr = base !== undefined ? String(base) : undefined;

    // check if URL is absolute (has a scheme)
    const isAbsolute = /^[a-z][a-z0-9+.-]*:/i.test(urlStr);

    if(isAbsolute) {
      // parse absolute URL directly
      this.#parse(urlStr);
    } else if(baseStr !== undefined) {
      // URL is relative, need to resolve against base
      const baseUrl = new URL(baseStr);

      if(urlStr === '') {
        // empty relative URL means same as base
        this.#copyFrom(baseUrl);
      } else if(urlStr.startsWith('//')) {
        // protocol-relative URL
        this.#parse(baseUrl.protocol + urlStr);
      } else if(urlStr.startsWith('/')) {
        // root-relative path
        this.#copyFrom(baseUrl);
        this.pathname = urlStr;
        this.search = '';
        this.hash = '';
      } else if(urlStr.startsWith('?')) {
        // query-relative
        this.#copyFrom(baseUrl);
        this.search = urlStr;
        this.hash = '';
      } else if(urlStr.startsWith('#')) {
        // fragment-relative
        this.#copyFrom(baseUrl);
        this.hash = urlStr;
      } else {
        // relative path - resolve against base directory
        this.#copyFrom(baseUrl);
        const basePath = baseUrl.pathname;
        const lastSlash = basePath.lastIndexOf('/');
        const baseDir = lastSlash >= 0 ? basePath.slice(0, lastSlash + 1) : '/';
        this.pathname = this.#resolvePath(baseDir + urlStr);
        this.search = '';
        this.hash = '';
      }
    } else {
      // relative url with no base - error per spec
      throw new TypeError(`Invalid URL: ${urlStr}`);
    }
  }

  #parse(str) {
    // parse absolute URL
    const re = /^([a-z][a-z0-9+.-]*:)?(?:\/\/(?:(?:([^:@\/?#]*)(?::([^@\/?#]*))?@)?([^\/:?#]*)(?::(\d+))?))?([^?#]*)(\?[^#]*)?(#.*)?$/i;
    const m = str.match(re);

    if(!m) throw new TypeError(`Invalid URL: ${str}`);

    const [, protocol = '', username = '', password = '', hostname = '', port = '', pathname = '', search = '', hash = ''] = m;

    Object.assign(this, { protocol: protocol.toLowerCase(), username, password, hostname: hostname.toLowerCase(), port, pathname: pathname || '/', search, hash });
  }

  #copyFrom(other) {
    const { protocol, username, password, hostname, port, pathname, search, hash } = other;

    Object.assign(this, { protocol, username, password, hostname, port, pathname, search, hash });
  }

  #resolvePath(path) {
    // normalize path by resolving . and ..
    const segments = path.split('/');
    const result = [];

    for(const segment of segments) {
      if(segment === '.') continue;

      if(segment === '..')
        if(result.length > 0 && result[result.length - 1] !== '') result.pop();
        else result.push(segment);
    }

    let resolved = result.join('/');
    if(!resolved.startsWith('/')) resolved = '/' + resolved;
    return resolved;
  }

  get host() {
    return this.hostname + (this.port == '' ? '' : ':' + this.port);
  }

  set host(value) {
    const idx = value.indexOf(':');

    if(idx !== -1) {
      this.hostname = value.slice(0, idx);
      this.port = +value.slice(idx + 1);
    } else {
      this.hostname = value;
      this.port = '';
    }
  }

  get auth() {
    return this.username + (this.password == '' ? '' : ':' + this.password);
  }

  get origin() {
    return (this.protocol ? this.protocol + '//' : '') + this.host;
  }

  get path() {
    return this.pathname + (this.search ? this.search : '');
  }

  get search() {
    const s = this.searchParams ? this.searchParams + '' : '';
    return s == '' ? '' : '?' + s;
  }

  set search(value) {
    Object.defineProperty(this, 'searchParams', { value: new URLSearchParams(value), enumerable: false, writable: true });
  }

  get href() {
    return (this.protocol ? this.protocol + '//' : '') + (this.auth ? this.auth + '@' : '') + this.host + this.path + (this.hash ? this.hash : '');
  }

  set href(value) {
    const url = new URL((value ?? '') + '', this.origin);

    this.protocol = url.protocol;
    this.username = url.username;
    this.password = url.password;
    this.hostname = url.hostname;
    this.port = url.port;
    this.pathname = url.pathname;
    this.search = url.search;
    this.hash = url.hash;
  }

  get hash() {
    return this.#hash;
  }

  set hash(value) {
    this.#hash = value == '' ? '' : value.startsWith('#') ? value : '#' + value;
  }

  toString() {
    return this.href;
  }

  static canParse(url, base) {
    if(typeof url !== 'string') return false;

    try {
      new URL(url, base);
      return true;
    } catch {
      return false;
    }
  }

  static parse(url, base) {
    try {
      return new URL(url, base);
    } catch {
      return null;
    }
  }
}

propertyDescriptors(URL.prototype, {
  [Symbol.toStringTag]: { value: 'URL' },
  protocol: { value: null, enumerable: true, writable: true },
  hostname: { value: null, enumerable: true, writable: true },
  port: { value: null, enumerable: true, writable: true },
  pathname: { value: null, enumerable: true, writable: true },
  search: { enumerable: true },
  searchParams: { value: null, enumerable: false, writable: true },
  hash: {
    enumerable: true,
  },
});

function propertyDescriptors(obj, properties) {
  for(const prop in properties)
    try {
      const desc = Object.getOwnPropertyDescriptor(obj, prop) ?? {};
      Object.assign(desc, properties[prop]);
      Object.defineProperty(obj, prop, desc);
    } catch(e) {
      console.log('propertyDescriptors', { prop });
      throw e;
    }
}
