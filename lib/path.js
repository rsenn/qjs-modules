import { statSync, lstatSync, realpathSync, existsSync } from 'fs';

export const sep = '/';
export const delimiter = ':';

export const FNM_NOMATCH = 1;
export const FNM_PATHNAME = 1;
export const FNM_NOESCAPE = 2;
export const FNM_PERIOD = 4;

const isSep = c => c === '/';

function invalidArg(v, name, expected) {
  let received;
  if(v === undefined) received = 'undefined';
  else if(v === null) received = 'null';
  else if(typeof v === 'function') received = 'function ' + (v.name || '');
  else if(typeof v === 'object') {
    const ctor = v.constructor;
    const cname = ctor && ctor.name;
    received = 'an instance of ' + (cname || 'Object');
  } else {
    const type = typeof v;
    received = `type ${type} (${String(v)}${type === 'bigint' ? 'n' : ''})`;
  }
  const err = new TypeError(`The "${name}" argument must be of type ${expected}. Received ${received}`);
  err.code = 'ERR_INVALID_ARG_TYPE';
  throw err;
}

function checkString(v, name) {
  if(typeof v !== 'string') invalidArg(v, name, 'string');
  return v;
}

function dirlen(path) {
  const n = path.length;
  if(n === 0) return 0;
  const hasroot = isSep(path[0]);
  let matched = 1;
  let end = -1;
  for(let i = n - 1; i >= 1; --i) {
    if(isSep(path[i])) {
      if(!matched) {
        end = i;
        break;
      }
    } else matched = 0;
  }
  if(end === -1) return hasroot ? 1 : 0;
  return hasroot && end === 1 ? 2 : end;
}

function basenameOff(path) {
  const n = path.length;
  let i = n - 1;
  while(i >= 0 && isSep(path[i])) --i;
  if(i < 0) return { pos: n, len: 0 };
  const end = i + 1;
  while(i >= 0 && !isSep(path[i])) --i;
  return { pos: i + 1, len: end - (i + 1) };
}

function extrange(path) {
  const n = path.length;
  const { pos: bpos, len: blen } = basenameOff(path);
  if(blen === 0) return { pos: n, len: 0 };
  let dot = -1;
  for(let i = bpos; i < bpos + blen; ++i) {
    if(path[i] === '.') dot = i;
  }
  if(dot === -1 || dot === bpos) return { pos: n, len: 0 };
  return { pos: dot, len: bpos + blen - dot };
}

function normalizeString(path) {
  const n = path.length;
  if(n === 0) return '.';
  const hasroot = isSep(path[0]);
  const out = [];
  let i = 0;
  while(i < n) {
    while(i < n && isSep(path[i])) ++i;
    if(i >= n) break;
    let j = i;
    while(j < n && !isSep(path[j])) ++j;
    const comp = path.slice(i, j);
    i = j;
    if(comp === '.') continue;
    if(comp === '..') {
      if(out.length > 0 && out[out.length - 1] !== '..') out.pop();
      else if(!hasroot) out.push('..');
      continue;
    }
    out.push(comp);
  }
  let res = (hasroot ? '/' : '') + out.join('/');
  if(res === '') res = hasroot ? '/' : '.';
  if(n > 0 && isSep(path[n - 1]) && res !== '/' && !isSep(res[res.length - 1])) res += '/';
  return res;
}

function absolute(p) {
  if(!(p.length > 0 && isSep(p[0]))) {
    const cwd = process.cwd();
    if(p.length === 0) p = cwd;
    else p = (cwd.endsWith('/') ? cwd : cwd + '/') + p;
  }
  return normalizeString(p);
}

export function basename(path, suffix) {
  checkString(path, 'path');
  if(suffix !== undefined) checkString(suffix, 'suffix');
  const { pos, len } = basenameOff(path);
  let str = path.slice(pos, pos + len);
  if(typeof suffix === 'string' && suffix.length > 0 && str.endsWith(suffix)) str = str.slice(0, str.length - suffix.length);
  return str;
}

export function dirname(path) {
  checkString(path, 'path');
  const len = dirlen(path);
  return len > 0 ? path.slice(0, len) : '.';
}

export function extname(path) {
  checkString(path, 'path');
  const { pos, len } = extrange(path);
  return path.slice(pos, pos + len);
}

export function skip(path, pos) {
  path = String(path);
  const n = path.length;
  let p = 0;
  if(pos !== undefined) {
    p = Number(pos) >>> 0;
    if(p > n) p = n;
  }
  let i = p;
  while(i < n && !isSep(path[i])) ++i;
  while(i < n && isSep(path[i])) ++i;
  return i === n ? -1 : i;
}

export function exists(path) {
  return existsSync(String(path));
}

export function isAbsolute(path) {
  checkString(path, 'path');
  return path.length > 0 && isSep(path[0]);
}

export function isDirectory(path) {
  try {
    return statSync(String(path)).isDirectory();
  } catch {
    return false;
  }
}

export function isFile(path) {
  try {
    return statSync(String(path)).isFile();
  } catch {
    return false;
  }
}

export function isSymlink(path) {
  try {
    return lstatSync(String(path)).isSymbolicLink();
  } catch {
    return false;
  }
}

export function getcwd() {
  return process.cwd();
}

export function gethome() {
  const home = process.env.HOME;
  return home === undefined ? null : home;
}

export function fnmatch(pattern, string, flags) {
  return fnmatchImpl(String(pattern), String(string), flags | 0) ? 0 : FNM_NOMATCH;
}

function fnmatchImpl(pattern, str, flags) {
  const pathname = (flags & FNM_PATHNAME) !== 0;
  const noescape = (flags & FNM_NOESCAPE) !== 0;
  const period = (flags & FNM_PERIOD) !== 0;
  function match(pi, si, atStart) {
    while(pi < pattern.length) {
      const c = pattern[pi];
      if(c === '*') {
        let p2 = pi + 1;
        while(p2 < pattern.length && pattern[p2] === '*') ++p2;
        if(p2 >= pattern.length) {
          if(pathname) return str.indexOf('/', si) === -1;
          return true;
        }
        for(let k = si; k <= str.length; ++k) {
          if(k > si && pathname && str[k - 1] === '/') return false;
          if(k > si && period && atStart && str[si] === '.') return false;
          if(match(p2, k, false)) return true;
        }
        return false;
      }
      if(c === '?') {
        if(si >= str.length) return false;
        if(pathname && str[si] === '/') return false;
        if(period && atStart && str[si] === '.') return false;
        ++pi;
        ++si;
        atStart = false;
        continue;
      }
      if(c === '[') {
        if(si >= str.length) return false;
        if(pathname && str[si] === '/') return false;
        if(period && atStart && str[si] === '.') return false;
        let j = pi + 1;
        let negate = false;
        if(pattern[j] === '!' || pattern[j] === '^') {
          negate = true;
          ++j;
        }
        let matched = false;
        let first = true;
        while(j < pattern.length && (first || pattern[j] !== ']')) {
          first = false;
          let lo = pattern[j];
          if(lo === '\\' && !noescape && j + 1 < pattern.length) {
            ++j;
            lo = pattern[j];
          }
          if(j + 2 < pattern.length && pattern[j + 1] === '-' && pattern[j + 2] !== ']') {
            let hi = pattern[j + 2];
            if(hi === '\\' && !noescape && j + 3 < pattern.length) {
              hi = pattern[j + 3];
              j += 1;
            }
            if(str[si] >= lo && str[si] <= hi) matched = true;
            j += 3;
          } else {
            if(str[si] === lo) matched = true;
            ++j;
          }
        }
        if(j >= pattern.length) return false;
        if(matched === negate) return false;
        pi = j + 1;
        ++si;
        atStart = false;
        continue;
      }
      let ch = c;
      if(c === '\\' && !noescape && pi + 1 < pattern.length) {
        ch = pattern[pi + 1];
        pi += 1;
      }
      if(si >= str.length || str[si] !== ch) return false;
      if(pathname && ch === '/') atStart = true;
      else atStart = false;
      ++pi;
      ++si;
    }
    return si === str.length;
  }
  return match(0, 0, true);
}

export function normalize(path) {
  checkString(path, 'path');
  return normalizeString(path);
}

export function realpath(path) {
  checkString(path, 'path');
  try {
    if(!existsSync(path)) return null;
    return realpathSync(path);
  } catch {
    return null;
  }
}

export function relative(from, to) {
  checkString(from, 'from');
  checkString(to, 'to');
  let f = absolute(from);
  let t = absolute(to);
  let fn = f.length;
  let tn = t.length;
  while(fn > 1 && isSep(f[fn - 1])) --fn;
  while(tn > 1 && isSep(t[tn - 1])) --tn;
  f = f.slice(0, fn);
  t = t.slice(0, tn);
  if(f === t) return '';
  const fparts = f.split('/').filter(x => x.length > 0);
  const tparts = t.split('/').filter(x => x.length > 0);
  if(f.length > 0 && isSep(f[0]) && !(t.length > 0 && isSep(t[0]))) return t;
  if(t.length > 0 && isSep(t[0]) && !(f.length > 0 && isSep(f[0]))) return t;
  let i = 0;
  while(i < fparts.length && i < tparts.length && fparts[i] === tparts[i]) ++i;
  const up = [];
  for(let k = i; k < fparts.length; ++k) up.push('..');
  const rest = tparts.slice(i);
  const res = up.concat(rest).join('/');
  return res;
}

export function join(...parts) {
  let joined = '';

  for(const part of parts) {
    checkString(part, 'path');

    if(part.length) joined = joined.length ? joined + sep + part : part;
  }

  return joined.length ? normalizeString(joined) : '.';
}

export function parse(path) {
  checkString(path, 'path');
  const n = path.length;
  const root = n > 0 && isSep(path[0]) ? path[0] : '';
  const { pos: basepos, len: baselen } = basenameOff(path);
  let dir, extpos, extlen;
  if(baselen === 0) {
    dir = root;
    extpos = n;
    extlen = 0;
  } else {
    dir = basepos === 0 ? '' : basepos === 1 && root ? root : path.slice(0, basepos - 1);
    const ex = extrange(path);
    extpos = ex.pos;
    extlen = ex.len;
  }
  return {
    root,
    dir,
    base: path.slice(basepos, basepos + baselen),
    ext: path.slice(extpos, extpos + extlen),
    name: path.slice(basepos, extlen ? extpos : basepos + baselen),
  };
}

function getstr(obj, name) {
  const v = obj[name];
  if(v === undefined || v === null) return undefined;
  const s = String(v);
  return s.length === 0 ? undefined : s;
}

export function format(obj) {
  if(obj === null || typeof obj !== 'object' || typeof obj === 'function') invalidArg(obj, 'pathObject', 'object');
  const root = getstr(obj, 'root');
  const dir = getstr(obj, 'dir');
  const base = getstr(obj, 'base');
  let bb = '';
  if(base !== undefined) {
    bb = base;
  } else {
    const name = getstr(obj, 'name');
    const ext = getstr(obj, 'ext');
    if(name !== undefined) bb += name;
    if(ext !== undefined) {
      if(ext[0] !== '.') bb += '.';
      bb += ext;
    }
  }
  let d = '';
  const dd = dir !== undefined ? dir : root;
  if(dd !== undefined) {
    d = dd;
    if(!(root && dd === root)) d += sep;
  }
  return d + bb;
}

export function resolve(...parts) {
  let start = 0;
  for(let i = parts.length - 1; i >= 0; --i) {
    if(typeof parts[i] !== 'string') invalidArg(parts[i], `paths[${i}]`, 'string');
    if(parts[i].length > 0 && isSep(parts[i][0])) {
      start = i;
      break;
    }
  }
  const strs = [];
  for(let i = start; i < parts.length; ++i) {
    checkString(parts[i], `paths[${i}]`);
    if(parts[i].length > 0) strs.push(parts[i]);
  }
  let out = absolute(strs.join('/'));
  while(out.length > 1 && isSep(out[out.length - 1])) out = out.slice(0, -1);
  return out;
}

export function toNamespacedPath(path) {
  return path;
}

export const _makeLong = toNamespacedPath;

const path = {
  basename,
  dirname,
  extname,
  skip,
  exists,
  isAbsolute,
  isDirectory,
  isFile,
  isSymlink,
  getcwd,
  gethome,
  fnmatch,
  normalize,
  realpath,
  relative,
  join,
  parse,
  format,
  resolve,
  toNamespacedPath,
  _makeLong,
  delimiter,
  sep,
  FNM_NOMATCH,
  FNM_PATHNAME,
  FNM_NOESCAPE,
  FNM_PERIOD,
};
path.posix = path;

export default path;
export { path as posix };
