import { join, normalize, resolve } from 'path';
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

/* fileURLToPath(url): the path of a `file:` URL (string or URL); Node's `url.fileURLToPath`
 *
 * ```js
 * fileURLToPath('file:///a%20b/c.txt')   // '/a b/c.txt'
 * ```
 *
 * throws TypeError (code ERR_INVALID_URL_SCHEME, ERR_INVALID_FILE_URL_HOST, ERR_INVALID_FILE_URL_PATH).
 */
export function fileURLToPath(url) {
  const u = typeof url == 'string' ? new URL(url) : url;
  const fail = (code, message) => {
    throw Object.assign(new TypeError(message), { code });
  };

  if(u.protocol != 'file:') fail('ERR_INVALID_URL_SCHEME', 'The URL must be of scheme file');
  if(u.hostname !== '' && u.hostname !== 'localhost') fail('ERR_INVALID_FILE_URL_HOST', 'File URL host must be "localhost" or empty on linux');
  if(/%2f/i.test(u.pathname)) fail('ERR_INVALID_FILE_URL_PATH', 'File URL path must not include encoded / characters');

  return decodeURIComponent(u.pathname);
}

/* pathToFileURL(path): a `file:` URL for `path`, made absolute first; Node's `url.pathToFileURL`
 *
 * ```js
 * pathToFileURL('/a b/c#d.txt').href   // 'file:///a%20b/c%23d.txt'
 * ```
 */
export function pathToFileURL(path) {
  const abs = resolve(String(path));
  const keep = /%(24|26|2B|2C|3A|3B|3D|40)/gi;
  const encoded = abs
    .split('/')
    .map(seg => encodeURIComponent(seg).replace(keep, (m, hex) => String.fromCharCode(parseInt(hex, 16))))
    .join('/');

  return new URL('file://' + encoded + (/\/$/.test(String(path)) && !encoded.endsWith('/') ? '/' : ''));
}

/* domainToASCII(), domainToUnicode(): the host parser of the WHATWG URL standard.
 *
 *   domainToASCII('español.com')         // 'xn--espaol-zwa.com'
 *   domainToUnicode('xn--espaol-zwa.com') // 'español.com'
 *   domainToASCII('123.456')             // '123.0.1.200', an IPv4 address
 *   domainToASCII('ex ample.com')        // '', the host is invalid
 *
 * The mapping step of UTS 46 is NFKC plus lowercase, with the ignored and
 * disallowed code points of the common scripts. Not done: the full
 * disallowed table, the bidi rules, the context rules of joiners (a joiner
 * is invalid).
 */
const BASE = 36;
const TMIN = 1;
const TMAX = 26;

function adapt(delta, points, first) {
  let k = 0;

  delta = first ? Math.floor(delta / 700) : delta >> 1;
  delta += Math.floor(delta / points);

  for(; delta > ((BASE - TMIN) * TMAX) >> 1; k += BASE) delta = Math.floor(delta / (BASE - TMIN));

  return k + Math.floor(((BASE - TMIN + 1) * delta) / (delta + 38));
}

const threshold = (k, bias) => (k <= bias ? TMIN : k >= bias + TMAX ? TMAX : k - bias);
const digitChar = d => String.fromCharCode(d + 22 + 75 * (d < 26));

/* RFC 3492: a string of any code points as its ASCII form, without the xn-- */
function punyEncode(input) {
  const cps = [...input].map(c => c.codePointAt(0));
  let out = cps.filter(c => c < 128).map(c => String.fromCharCode(c)).join('');
  const basic = out.length;
  let handled = basic,
    n = 128,
    delta = 0,
    bias = 72;

  if(basic) out += '-';

  while(handled < cps.length) {
    const m = Math.min(...cps.filter(c => c >= n));

    delta += (m - n) * (handled + 1);
    n = m;

    for(const c of cps) {
      if(c < n) delta++;
      if(c != n) continue;

      let q = delta;

      for(let k = BASE; ; k += BASE) {
        const t = threshold(k, bias);

        if(q < t) break;

        out += digitChar(t + ((q - t) % (BASE - t)));
        q = Math.floor((q - t) / (BASE - t));
      }

      out += digitChar(q);
      bias = adapt(delta, handled + 1, handled == basic);
      delta = 0;
      handled++;
    }

    delta++;
    n++;
  }

  return out;
}

/* RFC 3492: the string for an ASCII form without the xn--, null when it is no valid encoding */
function punyDecode(input) {
  const split = input.lastIndexOf('-');
  const out = split > 0 ? [...input.slice(0, split)].map(c => c.codePointAt(0)) : [];
  let n = 128,
    i = 0,
    bias = 72;

  if(out.some(c => c >= 128)) return null;

  for(let at = split > 0 ? split + 1 : 0; at < input.length; ) {
    const before = i;

    for(let w = 1, k = BASE; ; k += BASE) {
      if(at >= input.length) return null;

      const c = input.charCodeAt(at++);
      const d = c >= 48 && c <= 57 ? c - 22 : c >= 65 && c <= 90 ? c - 65 : c >= 97 && c <= 122 ? c - 97 : BASE;

      if(d >= BASE) return null;

      i += d * w;

      if(i > 0x7fffffff) return null;

      const t = threshold(k, bias);

      if(d < t) break;

      w *= BASE - t;
    }

    bias = adapt(i - before, out.length + 1, before == 0);
    n += Math.floor(i / (out.length + 1));
    i %= out.length + 1;

    if(n > 0x10ffff || (n >= 0xd800 && n <= 0xdfff)) return null;

    out.splice(i++, 0, n);
  }

  return String.fromCodePoint(...out);
}

/* UTS 46 ignored and disallowed code points, as far as covered, see above */
const IGNORED = /[\u00ad\u034f\u180b-\u180d\u180f\u200b\u2060\u2064\ufe00-\ufe0f\ufeff\u{e0100}-\u{e01ef}]/gu;
const DISALLOWED = /[\u0000-\u001f\u007f-\u009f\u200c-\u200f\u2028-\u202e\u2066-\u2069\ud800-\udfff\ue000-\uf8ff\ufdd0-\ufdef\ufffd-\uffff\u{f0000}-\u{10ffff}]/u;
const FORBIDDEN = /[\u0000-\u0020#%/:<>?@[\\\]^|\u007f]/;

/* the UTS 46 mapping of a string, null when it holds a disallowed code point */
function mapDomain(domain) {
  let s = domain.replace(/[\u3002\uff0e\uff61]/g, '.').replace(IGNORED, '');

  s = [...s.normalize('NFKC').replace(/\u1e9e/g, 'ss')].map(c => c.toLowerCase()).join('').normalize('NFKC');

  return DISALLOWED.test(s) ? null : s;
}

/* a domain as its ASCII form, null when a label is invalid */
function toASCII(domain) {
  const mapped = mapDomain(domain);

  if(mapped === null) return null;

  const labels = [];

  for(const label of mapped.split('.')) {
    if(label.startsWith('xn--')) {
      const decoded = punyDecode(label.slice(4));

      if(decoded === null || /^[\u0000-\u007f]*$/.test(decoded) || /^\p{M}/u.test(decoded) || mapDomain(decoded) !== decoded) return null;

      labels.push(label);
    } else if(/[^\u0000-\u007f]/.test(label)) {
      if(/^\p{M}/u.test(label)) return null;

      labels.push('xn--' + punyEncode(label));
    } else labels.push(label);
  }

  return labels.join('.');
}

/* the last label is a number: decimal, or 0x and hex digits */
function endsInNumber(host) {
  const parts = host.split('.');

  if(parts[parts.length - 1] === '') {
    if(parts.length == 1) return false;
    parts.pop();
  }

  return /^([0-9]+|0[xX][0-9a-fA-F]*)$/.test(parts[parts.length - 1]);
}

function ipv4Number(part) {
  if(part === '') return NaN;
  if(/^0[xX]/.test(part)) return /^0[xX][0-9a-fA-F]*$/.test(part) ? parseInt(part.slice(2) || '0', 16) : NaN;
  if(part.length > 1 && part[0] == '0') return /^[0-7]+$/.test(part) ? parseInt(part, 8) : NaN;

  return /^[0-9]+$/.test(part) ? parseInt(part, 10) : NaN;
}

/* the dotted form of an address with 1 to 4 numbers, as 127.1 or 0x7f000001, null when invalid */
function parseIPv4(host) {
  const parts = host.split('.');

  if(parts[parts.length - 1] === '' && parts.length > 1) parts.pop();
  if(parts.length > 4) return null;

  const numbers = parts.map(ipv4Number);
  const last = numbers.pop();

  if(numbers.some(n => !(n <= 255)) || !(last < 256 ** (4 - numbers.length))) return null;

  const address = numbers.reduce((sum, n, i) => sum + n * 256 ** (3 - i), last);

  return [3, 2, 1, 0].map(i => Math.floor(address / 256 ** i) % 256).join('.');
}

/* the compressed form of an IPv6 address, null when invalid */
function parseIPv6(input) {
  const halves = input.split('::');

  if(!/^[0-9a-fA-F:.]+$/.test(input) || halves.length > 2) return null;

  const groups = half => (half === '' ? [] : half.split(':'));
  const front = groups(halves[0]);
  const back = halves.length == 2 ? groups(halves[1]) : [];
  const tail = halves.length == 2 ? back : front;
  const words = [];
  const v4 = tail.length && tail[tail.length - 1].includes('.') ? tail.pop() : null;
  const parts = v4 === null ? null : v4.split('.');

  if(parts && (parts.length != 4 || parts.some(p => !/^(0|[1-9][0-9]{0,2})$/.test(p) || p > 255))) return null;

  const hex = list => list.map(g => (/^[0-9a-fA-F]{1,4}$/.test(g) ? parseInt(g, 16) : NaN));
  const a = hex(front);
  const b = hex(halves.length == 2 ? back : []);
  const extra = parts ? [(parts[0] << 8) | parts[1], (parts[2] << 8) | parts[3]] : [];

  (halves.length == 2 ? b : a).push(...extra);

  if(a.some(isNaN) || b.some(isNaN)) return null;
  if(halves.length == 2 ? a.length + b.length > 7 : a.length != 8) return null;

  words.push(...a, ...(halves.length == 2 ? new Array(8 - a.length - b.length).fill(0) : []), ...b);

  let best = -1,
    length = 1;

  for(let i = 0; i < 8; ) {
    let j = i;

    while(j < 8 && words[j] == 0) j++;

    if(j - i > length) (best = i), (length = j - i);

    i = Math.max(j, i + 1);
  }

  const text = words.map(w => w.toString(16));

  return best < 0 ? text.join(':') : text.slice(0, best).join(':') + '::' + text.slice(best + length).join(':');
}

/* the host of a string as the URL standard parses it, null when invalid */
function parseHost(input) {
  if(input.startsWith('[')) {
    const address = input.endsWith(']') ? parseIPv6(input.slice(1, -1)) : null;

    return address === null ? null : `[${address}]`;
  }

  const ascii = toASCII(input.replace(/(%[0-9a-fA-F]{2})+/g, m => {
    try {
      return decodeURIComponent(m);
    } catch(e) {
      return m;
    }
  }));

  if(!ascii || FORBIDDEN.test(ascii)) return null;

  return endsInNumber(ascii) ? parseIPv4(ascii) : ascii;
}

export function domainToASCII(domain) {
  if(arguments.length == 0) throw Object.assign(new TypeError('The "domain" argument must be specified'), { code: 'ERR_MISSING_ARGS' });

  return parseHost(String(domain)) ?? '';
}

export function domainToUnicode(domain) {
  if(arguments.length == 0) throw Object.assign(new TypeError('The "domain" argument must be specified'), { code: 'ERR_MISSING_ARGS' });

  const host = parseHost(String(domain));

  return host === null ? '' : host.split('.').map(label => (label.startsWith('xn--') ? punyDecode(label.slice(4)) ?? label : label)).join('.');
}

/* the module object, what import x from 'url' gives in Node */
export default { URL, URLSearchParams, fileURLToPath, pathToFileURL, domainToASCII, domainToUnicode };
