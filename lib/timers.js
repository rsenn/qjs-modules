/* timers.js: Node's `timers` (setTimeout, setInterval, setImmediate and
 * their clear*), on top of os.setTimeout().
 *
 * ```js
 * const t = setTimeout((a, b) => {}, 100, 'a', 'b');
 * t.unref(); t.refresh(); clearTimeout(t);
 * ```
 *
 * limit: unref() records the flag (hasRef()) but the engine cannot let
 * the loop exit while a timer is pending.
 */
import { clearTimeout as osClearTimeout, setTimeout as osSetTimeout } from 'os';

const kMaxDelay = 2147483647;
const timeouts = new Map();
let nextId = 1;

const checkCallback = callback => {
  if(typeof callback != 'function') {
    const e = new TypeError(`The "callback" argument must be of type function. Received ${callback === null ? 'null' : typeof callback == 'object' ? 'an instance of ' + (callback.constructor?.name ?? 'Object') : `type ${typeof callback} (${String(callback)})`}`);
    e.code = 'ERR_INVALID_ARG_TYPE';
    throw e;
  }
};

/* delay out of 1..2147483647 or NaN becomes 1, fractions are truncated */
const coerceDelay = delay => {
  delay = Number(delay);
  return delay >= 1 && delay <= kMaxDelay ? Math.trunc(delay) : 1;
};

export class Timeout {
  #callback;
  #args;
  #repeat;
  #handle;
  #ref = true;
  #id = nextId++;

  constructor(callback, delay, args, repeat) {
    checkCallback(callback);
    this.#callback = callback;
    this._idleTimeout = coerceDelay(delay);
    this.#args = args;
    this.#repeat = repeat;
    timeouts.set(this.#id, this);
    this.#start();
  }

  #start() {
    this.#handle = osSetTimeout(() => {
      if(this.#repeat) this.#start();
      else this.#finish();
      this.#callback.apply(this, this.#args);
    }, this._idleTimeout);
  }

  #finish() {
    this.#handle = undefined;
    timeouts.delete(this.#id);
  }

  ref() {
    this.#ref = true;
    return this;
  }

  unref() {
    this.#ref = false;
    return this;
  }

  hasRef() {
    return this.#ref;
  }

  refresh() {
    if(this.#handle !== undefined) osClearTimeout(this.#handle);
    timeouts.set(this.#id, this);
    this.#start();
    return this;
  }

  close() {
    if(this.#handle !== undefined) osClearTimeout(this.#handle);
    this.#finish();
    return this;
  }

  [Symbol.toPrimitive]() {
    return this.#id;
  }

  [Symbol.dispose]() {
    this.close();
  }
}

export class Immediate {
  #ref = true;

  constructor(callback, args) {
    checkCallback(callback);
    this._onImmediate = callback;
    this._argv = args;
    this._destroyed = false;
  }

  ref() {
    this.#ref = true;
    return this;
  }

  unref() {
    this.#ref = false;
    return this;
  }

  hasRef() {
    return this.#ref;
  }

  [Symbol.dispose]() {
    clearImmediate(this);
  }
}

/* immediates queued while the queue runs wait for the next pass */
let queue = [];
let scheduled = false;

const runImmediates = () => {
  const list = queue;
  queue = [];
  scheduled = false;
  for(const im of list) {
    if(im._destroyed) continue;
    im._destroyed = true;
    im._onImmediate.apply(im, im._argv);
  }
};

export function setTimeout(callback, delay, ...args) {
  return new Timeout(callback, delay, args, false);
}

export function setInterval(callback, delay, ...args) {
  return new Timeout(callback, delay, args, true);
}

export function setImmediate(callback, ...args) {
  const im = new Immediate(callback, args);
  queue.push(im);
  if(!scheduled) {
    scheduled = true;
    osSetTimeout(runImmediates, 0);
  }
  return im;
}

/* clearTimeout(t): Timeout object, or its number/string primitive */
export function clearTimeout(t) {
  if(t instanceof Timeout) t.close();
  else if(t !== undefined && t !== null) {
    if(typeof t == 'string' || typeof t == 'number') timeouts.get(+t)?.close();
    else osClearTimeout(t);
  }
}

export const clearInterval = clearTimeout;

export function clearImmediate(im) {
  if(im instanceof Immediate) im._destroyed = true;
}

export default { setTimeout, clearTimeout, setInterval, clearInterval, setImmediate, clearImmediate, Timeout, Immediate };

/* Importing this module is what makes the timer functions global in QuickJS, which has none by default. */
for(const [name, fn] of Object.entries({ setTimeout, clearTimeout, setInterval, clearInterval, setImmediate, clearImmediate })) globalThis[name] = fn;
