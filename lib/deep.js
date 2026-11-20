/**
 * Deep (recursive) traversal and manipulation of nested objects/arrays.
 *
 * Mirrors quickjs-deep.c. The native module addresses nodes by a path (array
 * of keys, a pointer, or a string) and exposes a rich set of flags controlling
 * what `find`/`select`/`iterate` yield (value / path / both), how paths are
 * rendered (array / string / pointer), how the predicate filters, and how deep
 * to recurse.
 *
 * In pure JS we model:
 *   - paths as arrays of keys/indices (the canonical form; strings and pointers
 *     are derived renderings of the same path),
 *   - traversal as an explicit stack of frames (object + own property keys + cursor),
 *   - the predicate protocol: `fn(value, path)` returns a status bitmask.
 */

export const YIELD = 1;
export const YIELD_NO_RECURSE = 3;
export const RECURSE = 0;
export const NO_RECURSE = 2;

export const RETURN_VALUE_PATH = 0 << 24;
export const RETURN_PATH = 1 << 24;
export const RETURN_VALUE = 2 << 24;
export const RETURN_PATH_VALUE = 3 << 24;
export const RETURN_MASK = 3 << 24;

export const PATH_AS_ARRAY = 0 << 26;
export const PATH_AS_STRING = 1 << 26;
export const PATH_AS_POINTER = 2 << 26;
export const PATH_AS_MASK = 3 << 26;

export const NO_THROW = 1 << 28;

export const FILTER_KEY_OF = 0 << 30;
export const FILTER_HAS_KEY = 1 << 30;
export const FILTER_NEGATE = 2 << 30;
export const FILTER_MASK = 1 << 30;

export const MAXDEPTH_MASK = (1 << 24) - 1;

/* Value-type bitmask constants (mirror defines.h) */
export const TYPE_UNDEFINED = 1;
export const TYPE_NULL = 2;
export const TYPE_BOOL = 4;
export const TYPE_INT = 8;
export const TYPE_OBJECT = 16;
export const TYPE_STRING = 32;
export const TYPE_SYMBOL = 64;
export const TYPE_BIG_INT = 128;
export const TYPE_FLOAT64 = 256;
export const TYPE_NAN = 512;
export const TYPE_FUNCTION = 1024;
export const TYPE_ARRAY = 2048;
export const TYPE_MODULE = 4096;
export const TYPE_FUNCTION_BYTECODE = 8192;
export const TYPE_UNINITIALIZED = 16384;
export const TYPE_CATCH_OFFSET = 32768;
export const TYPE_EXCEPTION = 65536;
export const TYPE_NUMBER = TYPE_INT | TYPE_FLOAT64 | TYPE_NAN;
export const TYPE_PRIMITIVE = TYPE_UNDEFINED | TYPE_NULL | TYPE_BOOL | TYPE_STRING | TYPE_SYMBOL | TYPE_NUMBER;
export const TYPE_ALL = 4095;

const STATUS_YIELD = s => (s & YIELD) !== 0;
const STATUS_RECURSE = s => (s & NO_RECURSE) === 0;

function flagsMaxDepth(flags) {
  return flags & MAXDEPTH_MASK || MAXDEPTH_MASK;
}

/**
 * Compute the value-type bitmask for a JS value. Matches the C
 * `js_value_type` / `1 << js_value_type_get()` split: arrays are TYPE_ARRAY
 * only; functions are TYPE_FUNCTION; other objects TYPE_OBJECT.
 */
function valueType(v) {
  switch (typeof v) {
    case 'undefined':
      return TYPE_UNDEFINED;
    case 'boolean':
      return TYPE_BOOL;
    case 'string':
      return TYPE_STRING;
    case 'symbol':
      return TYPE_SYMBOL;
    case 'bigint':
      return TYPE_BIG_INT;
    case 'number':
      if(Number.isNaN(v)) return TYPE_NAN;
      if(Number.isInteger(v)) return TYPE_INT;
      return TYPE_FLOAT64;
    case 'function':
      return TYPE_FUNCTION;
    case 'object':
      if(v === null) return TYPE_NULL;
      if(Array.isArray(v)) return TYPE_ARRAY;
      return TYPE_OBJECT;
    default:
      return TYPE_UNDEFINED;
  }
}

/** Render a path array as a string (e.g. `a.b[0].c`). */
function pathToString(path) {
  let s = '';
  for(const key of path) {
    if(typeof key === 'number' || /^\d+$/.test(String(key))) {
      s += `[${key}]`;
    } else if(typeof key === 'symbol') {
      s += `[${String(key)}]`;
    } else {
      s += s === '' ? String(key) : `.${key}`;
    }
  }
  return s;
}

/**
 * Return a "pointer-like" rendering for a path. The C module returns a
 * `Pointer` object (quickjs-pointer.c). Since we don't have a Pointer class
 * here, we return a small object that records the segments; callers that just
 * read `.segments` can use it, and it stringifies like the dotted path.
 */
function pathToPointer(path) {
  return {
    segments: path.slice(),
    toString() {
      return pathToString(this.segments);
    },
  };
}

function formatPath(path, flags) {
  switch (flags & PATH_AS_MASK) {
    case PATH_AS_STRING:
      return pathToString(path);
    case PATH_AS_POINTER:
      return pathToPointer(path);
    default:
      return path.slice();
  }
}

/**
 * A traversal frame: `{ obj, keys, index, path }`.
 * `keys` is a snapshot of the object's own enumerable keys; `index` is the
 * cursor; `path` is the path *to* this frame's node (empty for the root).
 */
function makeFrame(obj, path) {
  const keys = [];

  /* Skip array "index" duplicates of length — the C enumeration just uses
     own property names; arrays get 0..len-1 plus any extra props. */
  for(const k of Object.keys(obj)) keys.push(k);

  /* For arrays, make sure numeric indices come first, in ascending order. */
  if(Array.isArray(obj)) {
    keys.sort((a, b) => {
      const na = /^\d+$/.test(a) ? +a : Infinity;
      const nb = /^\d+$/.test(b) ? +b : Infinity;
      return na - nb;
    });
  }

  return { obj, keys, index: 0, path };
}

/** The key at the current cursor, or undefined if exhausted. */
function frameKey(frame) {
  return frame.index < frame.keys.length ? frame.keys[frame.index] : undefined;
}

/** The value at the current cursor, or undefined if exhausted. */
function frameValue(frame) {
  const k = frameKey(frame);
  if(k === undefined) return undefined;
  return frame.obj[k];
}

class FrameStack {
  constructor() {
    this.frames = [];
  }

  get depth() {
    return this.frames.length;
  }
  top() {
    return this.frames.length ? this.frames[this.frames.length - 1] : null;
  }

  /** Current path (keys from root to the *current cursor node*). */
  currentPath() {
    const f = this.top();
    if(!f) return [];
    const k = frameKey(f);
    return k === undefined ? f.path.slice() : [...f.path, k];
  }

  /** Push the root. */
  pushRoot(obj) {
    this.frames.push(makeFrame(obj, []));
    return this.top();
  }

  /**
   * Advance to the next sibling within the current frame.
   * Returns true if there is a current node, false if the frame is exhausted.
   */
  next() {
    const f = this.top();
    if(!f) return false;
    f.index++;
    return f.index < f.keys.length;
  }

  /**
   * Descend into the current node (if it's an object/array). Returns true if
   * descent happened, false otherwise.
   */
  descend() {
    const f = this.top();
    if(!f) return false;
    const v = frameValue(f);
    if(v === null || typeof v !== 'object') return false;

    /* If the child has no own keys, treat as leaf (descend produces empty frame). */
    this.frames.push(makeFrame(v, this.currentPath()));
    return true;
  }

  /** Pop the current frame. */
  pop() {
    this.frames.pop();
    return this.top();
  }

  /** Skip the rest of the current frame (used by `skip()`). */
  skipFrame() {
    const f = this.top();
    if(f) f.index = f.keys.length;
  }

  /** Free — no-op in JS. */
  free() {
    this.frames.length = 0;
  }
}

/**
 * Invoke the user predicate with the C contract: `fn(value, path)`.
 * Returns a numeric status (YIELD / RECURSE / YIELD_NO_RECURSE / NO_RECURSE)
 * or a truthy/falsy that gets coerced to YIELD/RECURSE.
 *
 * If `pred` is nullish, the default behavior is YIELD.
 */
function invokePredicate(pred, value, path) {
  if(pred == null) return YIELD;
  let r;
  try {
    r = pred(value, path);
  } catch(e) {
    return RECURSE; /* C swallows predicate exceptions and treats them as RECURSE */
  }
  if(typeof r === 'boolean') return r ? YIELD : RECURSE;
  if(typeof r === 'number') return r | 0;
  return r ? YIELD : RECURSE;
}

function formatReturn(value, path, flags) {
  switch (flags & RETURN_MASK) {
    case RETURN_VALUE:
      return value;
    case RETURN_PATH:
      return formatPath(path, flags);
    case RETURN_VALUE_PATH:
      return [value, formatPath(path, flags)];
    case RETURN_PATH_VALUE:
      return [formatPath(path, flags), value];
    default:
      return value;
  }
}

/**
 * Convert an iterable of keys into an array (for FILTER_KEY_OF / FILTER_HAS_KEY).
 * Strings are treated as a single key, not as iterables of characters.
 */
function atomsFromIterable(arg) {
  if(arg == null) return null;
  if(typeof arg === 'string') return [arg];
  if(typeof arg[Symbol.iterator] === 'function') return [...arg];
  return null;
}

function atomSkipped(atoms, key) {
  return atoms.indexOf(key) === -1;
}

function atomsInObject(atoms, obj) {
  if(obj == null || typeof obj !== 'object') return false;
  for(const a of atoms) if(a in obj) return true;

  return false;
}

export function find(root, predicate, flags = RETURN_VALUE_PATH, mask = TYPE_ALL, props = undefined) {
  const maxDepth = flagsMaxDepth(flags);
  flags &= ~MAXDEPTH_MASK;
  const atoms = atomsFromIterable(props);
  const stack = new FrameStack();
  let ret;

  if(root === null || typeof root !== 'object') throw new TypeError('argument 1 (root) is not an object');

  if(typeof predicate !== 'function') throw new TypeError('argument 2 (predicate) is not a function');

  stack.pushRoot(root);
  let frame = stack.top();

  while(frame) {
    const key = frameKey(frame);
    const value = frameValue(frame);
    const type = valueType(value);
    const isArray = type & TYPE_ARRAY;

    const filterKeyOf = (flags & FILTER_MASK) === FILTER_KEY_OF && atoms != null && atomSkipped(atoms, key) ^ !!(flags & FILTER_NEGATE);

    let status = RECURSE;

    if(isArray || !filterKeyOf) {
      const filterHasKey = (flags & FILTER_MASK) === FILTER_HAS_KEY && type & (TYPE_OBJECT | TYPE_FUNCTION) && !isArray && !atomsInObject(atoms, value) ^ !!(flags & FILTER_NEGATE);

      if(!filterHasKey) {
        if(type & mask) status = invokePredicate(predicate, value, formatPath(stack.currentPath(), flags));
        else status = RECURSE;
      }
    }

    if(STATUS_YIELD(status)) {
      ret = formatReturn(value, stack.currentPath(), flags);
      break;
    }

    /* Advance */
    const descend = STATUS_RECURSE(status) && stack.depth < maxDepth && value !== null && typeof value === 'object' && Object.keys(value).length > 0;

    if(filterKeyOf || !descend) {
      if(!stack.next()) {
        /* Pop until we find a frame with more keys, or exhaust. */
        while(stack.frames.length) {
          stack.pop();
          const top = stack.top();
          if(!top) break;
          if(stack.next()) {
            frame = top;
            break;
          }
        }

        if(!stack.frames.length) {
          frame = null;
          break;
        }
        frame = stack.top();
      } else frame = stack.top();
    } else {
      stack.descend();
      frame = stack.top();
    }
  }

  stack.free();
  return ret;
}

export function select(root, predicate, flags = RETURN_VALUE_PATH, mask = TYPE_ALL, props = undefined) {
  const maxDepth = flagsMaxDepth(flags);
  flags &= ~MAXDEPTH_MASK;
  const atoms = atomsFromIterable(props);
  const out = [];
  const stack = new FrameStack();

  if(typeof predicate !== 'function') throw new TypeError('argument 1 (predicate) is not a function');

  stack.pushRoot(root);
  let frame = stack.top();

  while(frame) {
    const key = frameKey(frame);
    const value = frameValue(frame);
    const type = valueType(value);
    const isArray = type & TYPE_ARRAY;

    const filterKeyOf = (flags & FILTER_MASK) === FILTER_KEY_OF && atoms != null && atomSkipped(atoms, key) ^ !!(flags & FILTER_NEGATE);

    let status = RECURSE;

    if(!filterKeyOf) {
      if(type & mask) status = invokePredicate(predicate, value, formatPath(stack.currentPath(), flags));
      if(STATUS_YIELD(status)) out.push(formatReturn(value, stack.currentPath(), flags));
    }

    const descend = STATUS_RECURSE(status) && stack.depth < maxDepth && value !== null && typeof value === 'object' && Object.keys(value).length > 0;

    if(!descend) {
      if(!stack.next()) {
        while(stack.frames.length) {
          stack.pop();
          const top = stack.top();
          if(!top) break;
          if(stack.next()) {
            frame = top;
            break;
          }
        }

        if(!stack.frames.length) {
          frame = null;
          break;
        }
        frame = stack.top();
      } else frame = stack.top();
    } else {
      stack.descend();
      frame = stack.top();
    }
  }

  stack.free();
  return out;
}

export function get(root, path, flags = 0) {
  /* Curried form: get(path) returns a function(root) => value */
  if(path === undefined) return r => get(r, path, flags);

  /* Curried form: get(pathFn) — treat as an accessor */
  if(typeof path === 'function') return path(root);

  const segs = Array.isArray(path) ? path : pathToString(path);
  const arr = Array.isArray(segs)
    ? segs
    : String(segs)
        .split(/[.[\]]/)
        .filter(Boolean);
  let cur = root;

  for(const k of arr) {
    if(cur == null) {
      if(flags & NO_THROW) return null;
      throw new TypeError(`cannot read property '${k}' of ${cur}`);
    }

    cur = cur[k];
  }

  return cur;
}

export function set(root, path, value) {
  /* Curried: set(path) returns (root, value) => root */
  if(path === undefined) return (r, v) => set(r, path, v);

  /* Curried: set(root, path) returns (value) => root */
  if(value === undefined && arguments.length < 3) return v => set(root, path, v);

  const segs = Array.isArray(path)
    ? path
    : String(path)
        .split(/[.[\]]/)
        .filter(Boolean);
  if(segs.length === 0) return root;

  let cur = root;
  for(let i = 0; i < segs.length - 1; i++) {
    const k = segs[i];
    if(cur[k] == null || typeof cur[k] !== 'object') cur[k] = {};
    cur = cur[k];
  }

  cur[segs[segs.length - 1]] = value;
  return root;
}

export function unset(root, path, strict = false) {
  if(path === undefined) return (r, s) => unset(r, path, s);

  const segs = Array.isArray(path)
    ? path
    : String(path)
        .split(/[.[\]]/)
        .filter(Boolean);
  if(segs.length === 0) return root;

  let cur = root;
  for(let i = 0; i < segs.length - 1; i++) {
    if(cur == null) {
      if(strict) throw new TypeError(`cannot read property '${segs[i]}' of ${cur}`);
      return root;
    }

    cur = cur[segs[i]];
  }

  if(cur != null && typeof cur === 'object') {
    const ok = delete cur[segs[segs.length - 1]];
    if(!ok && strict) throw new TypeError(`cannot delete property '${segs[segs.length - 1]}'`);
  }

  return root;
}

export function flatten(root, target, mask = TYPE_ALL) {
  const out = target && typeof target === 'object' ? target : {};
  const stack = new FrameStack();
  stack.pushRoot(root);
  let frame = stack.top();

  while(frame) {
    const value = frameValue(frame);
    const type = valueType(value);

    if((mask & type) !== 0) {
      const pathStr = pathToString(stack.currentPath());
      out[pathStr] = value;
    }

    const descend = value !== null && typeof value === 'object' && Object.keys(value).length > 0;

    if(!descend) {
      if(!stack.next()) {
        while(stack.frames.length) {
          stack.pop();
          const top = stack.top();
          if(!top) break;
          if(stack.next()) {
            frame = top;
            break;
          }
        }
        if(!stack.frames.length) {
          frame = null;
          break;
        }
        frame = stack.top();
      } else frame = stack.top();
    } else {
      stack.descend();
      frame = stack.top();
    }
  }

  stack.free();
  return out;
}

export function pathOf(root, value, flags = RETURN_VALUE_PATH) {
  const stack = new FrameStack();
  stack.pushRoot(root);
  let frame = stack.top();

  while(frame) {
    const v = frameValue(frame);

    if(Object.is(v, value) || (typeof v === 'object' && v !== null && v === value)) {
      stack.free();
      return formatPath(stack.currentPath(), flags);
    }

    const descend = v !== null && typeof v === 'object' && Object.keys(v).length > 0;

    if(!descend) {
      if(!stack.next()) {
        while(stack.frames.length) {
          stack.pop();
          const top = stack.top();
          if(!top) break;
          if(stack.next()) {
            frame = top;
            break;
          }
        }

        if(!stack.frames.length) {
          frame = null;
          break;
        }
        frame = stack.top();
      } else frame = stack.top();
    } else {
      stack.descend();
      frame = stack.top();
    }
  }

  stack.free();
  return undefined;
}

export function forEach(root, fn, thisArg = undefined, flags = 0, mask = TYPE_ALL) {
  const maxDepth = flagsMaxDepth(flags);
  flags &= ~MAXDEPTH_MASK;
  const stack = new FrameStack();
  stack.pushRoot(root);
  let frame = stack.top();

  while(frame) {
    const value = frameValue(frame);
    const type = valueType(value);

    if((type & mask) !== 0) fn.call(thisArg, value, stack.currentPath(), root);

    const descend = stack.depth < maxDepth && value !== null && typeof value === 'object' && Object.keys(value).length > 0;

    if(!descend) {
      if(!stack.next()) {
        while(stack.frames.length) {
          stack.pop();
          const top = stack.top();
          if(!top) break;
          if(stack.next()) {
            frame = top;
            break;
          }
        }

        if(!stack.frames.length) {
          frame = null;
          break;
        }
        frame = stack.top();
      } else frame = stack.top();
    } else {
      stack.descend();
      frame = stack.top();
    }
  }

  stack.free();
  return undefined;
}

export function equals(a, b) {
  if(a === b) return true;
  if(a === null || b === null || typeof a !== 'object' || typeof b !== 'object') return Object.is(a, b);

  if(Array.isArray(a) !== Array.isArray(b)) return false;

  const ka = Object.keys(a);
  const kb = Object.keys(b);
  if(ka.length !== kb.length) return false;

  /* Match the C module: sort keys and compare values positionally, but we can
     just check each key exists on the other side and recursively compare. */
  ka.sort();
  kb.sort();

  for(let i = 0; i < ka.length; i++) if(ka[i] !== kb[i]) return false;

  for(const k of ka) if(!equals(a[k], b[k])) return false;

  return true;
}

export function clone(root, predicate, flags = RETURN_VALUE_PATH, mask = TYPE_ALL) {
  if(root === null || typeof root !== 'object') return root;

  /* Normalise overload: (root, flags) or (root, mask) or (root, pred, flags, mask) */
  let pred = undefined;
  let f = flags;
  let m = mask;

  if(typeof predicate === 'function') {
    pred = predicate;
  } else if(typeof predicate === 'number') {
    f = predicate;
    m = flags;
  }

  const maxDepth = flagsMaxDepth(f);
  f &= ~MAXDEPTH_MASK;

  const out = Array.isArray(root) ? [] : {};
  const stack = new FrameStack();
  const outStack = [out];

  stack.pushRoot(root);
  let frame = stack.top();

  while(frame) {
    const key = frameKey(frame);
    const value = frameValue(frame);
    const type = valueType(value);

    let status = RECURSE;
    if(type & m) status = pred == null ? YIELD : invokePredicate(pred, value, formatPath(stack.currentPath(), f));

    let childOut;
    if(STATUS_YIELD(status)) {
      childOut = cloneValue(value);
      const parent = outStack[outStack.length - 1];
      parent[key] = childOut;
    }

    const descend = STATUS_RECURSE(status) && value !== null && typeof value === 'object' && stack.depth < maxDepth && Object.keys(value).length > 0;

    if(!descend) {
      if(!stack.next()) {
        while(stack.frames.length) {
          stack.pop();
          outStack.pop();
          const top = stack.top();
          if(!top) break;
          if(stack.next()) {
            frame = top;
            break;
          }
        }

        if(!stack.frames.length) {
          frame = null;
          break;
        }

        frame = stack.top();
      } else frame = stack.top();
    } else {
      const child = Array.isArray(value) ? [] : {};
      outStack[outStack.length - 1][key] = child;
      stack.descend();
      outStack.push(child);
      frame = stack.top();
    }
  }

  stack.free();
  return out;
}

function cloneValue(v) {
  if(v === null || typeof v !== 'object') return v;
  if(Array.isArray(v)) return v.map(cloneValue);
  const o = {};
  for(const k of Object.keys(v)) o[k] = cloneValue(v[k]);
  return o;
}

export class DeepIterator {
  #stack;
  #flags;
  #mask;
  #pred;
  #atoms;
  #root;
  #done;
  #status;

  constructor(root, pred, flags = RETURN_VALUE_PATH, mask = TYPE_ALL, props = undefined) {
    /* new DeepIterator(flags) — not part of the documented ctor shape; reject. */
    if(arguments.length === 1 && typeof root === 'number') throw new TypeError('argument 1 must be the root object');

    this.#root = root;
    this.#pred = typeof pred === 'function' ? pred : undefined;
    this.#flags = (flags | 0) >>> 0;
    this.#mask = (mask | 0) >>> 0;
    this.#atoms = atomsFromIterable(props);
    this.#stack = new FrameStack();
    this.#done = false;
    this.#status = RECURSE;

    if(root !== null && typeof root === 'object') this.#stack.pushRoot(root);
    else this.#done = true;
  }

  next() {
    if(this.#done) return { value: undefined, done: true };

    const flags = this.#flags;
    const maxDepth = flagsMaxDepth(flags);
    const stack = this.#stack;

    while(stack.frames.length) {
      const frame = stack.top();
      if(!frame) break;

      const key = frameKey(frame);
      const value = frameValue(frame);
      const type = valueType(value);
      const isArray = type & TYPE_ARRAY;

      const filterKeyOf = (flags & FILTER_MASK) === FILTER_KEY_OF && this.#atoms != null && atomSkipped(this.#atoms, key) ^ !!(flags & FILTER_NEGATE);

      let status = RECURSE;

      if(isArray || !filterKeyOf) if (type & this.#mask) status = invokePredicate(this.#pred, value, formatPath(stack.currentPath(), flags));

      this.#status = status;

      if(STATUS_YIELD(status)) {
        const ret = formatReturn(value, stack.currentPath(), flags);
        this.#advance(maxDepth);
        return { value: ret, done: false };
      }

      this.#advance(maxDepth);
    }

    this.#done = true;
    return { value: undefined, done: true };
  }

  #advance(maxDepth) {
    const stack = this.#stack;
    const flags = this.#flags;
    const frame = stack.top();
    if(!frame) return;

    const value = frameValue(frame);
    const status = this.#status;

    const descend = STATUS_RECURSE(status) && stack.depth < maxDepth && value !== null && typeof value === 'object' && Object.keys(value).length > 0;

    if(!descend) {
      if(!stack.next()) {
        while(stack.frames.length) {
          stack.pop();
          const top = stack.top();
          if(!top) break;
          if(stack.next()) return;
        }
      }
    } else stack.descend();
  }

  leave() {
    const stack = this.#stack;
    if(stack.depth === 0) return undefined;
    stack.pop();
    this.#status |= NO_RECURSE;
    return stack.depth;
  }

  skip() {
    this.#stack.skipFrame();
    return this.#stack.depth;
  }

  get path() {
    return formatPath(this.#stack.currentPath(), this.#flags);
  }

  return(value) {
    this.#stack.free();
    this.#done = true;
    return { value, done: true };
  }

  [Symbol.iterator]() {
    return this;
  }

  get [Symbol.toStringTag]() {
    return 'Deep Iterator';
  }
}

export function iterate(root, pred, flags, mask, props) {
  return new DeepIterator(root, pred, flags, mask, props);
}

export default {
  find,
  select,
  get,
  set,
  unset,
  flatten,
  pathOf,
  equals,
  iterate,
  forEach,
  clone,
  DeepIterator,
  YIELD,
  YIELD_NO_RECURSE,
  RECURSE,
  NO_RECURSE,
  RETURN_VALUE_PATH,
  RETURN_PATH,
  RETURN_VALUE,
  RETURN_PATH_VALUE,
  PATH_AS_ARRAY,
  PATH_AS_STRING,
  PATH_AS_POINTER,
  NO_THROW,
  FILTER_KEY_OF,
  FILTER_HAS_KEY,
  FILTER_NEGATE,
  TYPE_UNDEFINED,
  TYPE_NULL,
  TYPE_BOOL,
  TYPE_INT,
  TYPE_OBJECT,
  TYPE_STRING,
  TYPE_SYMBOL,
  TYPE_BIG_INT,
  TYPE_FLOAT64,
  TYPE_NAN,
  TYPE_NUMBER,
  TYPE_PRIMITIVE,
  TYPE_ALL,
  TYPE_FUNCTION,
  TYPE_ARRAY,
  TYPE_MODULE,
  TYPE_FUNCTION_BYTECODE,
  TYPE_UNINITIALIZED,
  TYPE_CATCH_OFFSET,
  TYPE_EXCEPTION,
};
