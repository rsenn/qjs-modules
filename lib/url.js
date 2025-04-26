import { define } from 'util';

export class URLSearchParams {
  #dict;

  constructor(q) {
    const dict = (this.#dict = {});
    if(typeof q === 'string') {
      if(q.charAt(0) === '?') q = q.slice(1);
      for(let p = q.split('&'), i = 0, length = p.length; i < length; i++) {
        const v = p[i];
        const j = v.indexOf('=');
        if(-1 < j) (dict[decode(v.slice(0, j))] ??= []).push(decode(v.slice(j + 1)));
        else if(v.length) (dict[decode(v.slice(0, j))] ??= []).push('');
      }
    } else {
      if(Array.isArray(q)) {
        for(let i = 0, length = q.length; i < length; i++) {
          const [k, v] = q[i];
          (dict[k] ??= []).push(v);
        }
      } else if(q.forEach) {
        q.forEach((v, k) => (dict[k] ??= []).push(v));
      } else {
        for(let k in q) (dict[k] ??= []).push(q[k]);
      }
    }

    function decode(str) {
      return decodeURIComponent(str.replace(/\+/g, ' '));
    }
  }
  get size() {
    return Object.keys(this.#dict).length;
  }
  append(name, value) {
    (this.#dict[name] ??= []).push(value);
  }
  delete(name) {
    delete this.#dict[name];
  }
  get(name) {
    return name in this.#dict ? this.#dict[name][0] : null;
  }
  getAll(name) {
    return name in this.#dict ? this.#dict[name].slice(0) : [];
  }
  has(name) {
    return name in this.#dict;
  }
  set(name, value) {
    this.#dict[name] = ['' + value];
  }
  forEach(callback, thisArg) {
    const dict = this.#dict;
    Object.getOwnPropertyNames(dict).forEach(name => dict[name].forEach(value => callback.call(thisArg, value, name, this), this), this);
  }
  toString() {
    const q = [];
    for(let k in this.#dict) for (let i = 0, v = this.#dict[k]; i < v.length; i++) q.push(encode(k) + '=' + encode(v[i]));
    return q.join('&');

    function encode(str) {
      return encodeURIComponent(str).replace(/[!'\(\)~]|%20|%00/g, match => replace[match]);
    }
  }
  *entries() {
    for(let k in this.#dict) yield [k, this.#dict[k]];
  }
  *keys() {
    for(let k in this.#dict) yield k;
  }
  *values() {
    for(let k in this.#dict) yield this.#dict[k];
  }
}

define(URLSearchParams.prototype, { [Symbol.toStringTag]: 'URLSearchParams' });

export class URL {
  #hash;

  constructor(url, base) {
    const re = /^([^:]+:)\/\/([^:@]+(|:[^@]*)@|)([^:/]+)(:[^/:]*|)([^\?#]*)(\?[^#]*|)(#.*|)/;
    const m = url.match(re);

    if(m) {
      const [, protocol, user, pass, hostname, port, pathname, search, hash] = m;

      Object.assign(this, {
        protocol,
        username: user ? user.replace(/:.*/g, '') : '',
        password: pass ? pass.slice(1) : '',
        hostname,
        port: port ? +port.slice(1) : '',
        pathname,
      });

      this.search = search;
      this.hash = hash ? hash : '';
    }
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
    return this.username ? this.username + (this.password == '' ? '' : ':' + this.password) : '';
  }
  get origin() {
    return this.protocol + '//' + this.host;
  }
  get path() {
    return this.pathname + this.search;
  }
  get value() {
    const value = this.searchParams + '';
    return value ? '?' + value : '';
  }
  set search(value) {
    this.searchParams = new URLSearchParams(value);
  }
  get href() {
    return this.protocol + '//' + (this.auth ? this.auth + '@' : '') + this.host + this.path + (this.hash ? '#' + this.hash : '');
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

define(URL.prototype, { [Symbol.toStringTag]: 'URL', protocol: null, hostname: null, port: null, pathname: null, searchParams: null });
Object.defineProperties(URL.prototype, {
  hash: {
    ...Object.getOwnPropertyDescriptor(URL.prototype, 'hash'),
    enumerable: true,
  },
});
