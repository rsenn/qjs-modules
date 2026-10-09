/* tty.js: isatty, ReadStream, WriteStream; Node's `tty` on std FILEs.
 * qjsm only (std, os, misc). streams are FILE objects with Node's
 * tty properties and a minimal event surface, not Readable/Writable. */
import { isatty as osIsatty, signal, ttyGetWinSize } from 'os';
import { ttySetRaw } from 'misc';
import { declare, define, SIGWINCH } from 'util';
import { fdopen, out } from 'std';

const listeners = new WeakMap();

const invalidFd = fd => {
  const e = new RangeError(`The value of "fd" is out of range. It must be >= 0 && <= 2147483647. Received ${fd}`);
  e.code = 'ERR_OUT_OF_RANGE';
  return e;
};

/* checkFd: validates `fd` like Node; ERR_INVALID_ARG_TYPE / ERR_OUT_OF_RANGE */
const checkFd = fd => {
  if(typeof fd != 'number' || !Number.isInteger(fd)) {
    const e = new TypeError(`The "fd" argument must be of type number. Received ${typeof fd == 'number' ? fd : typeof fd}`);
    e.code = 'ERR_INVALID_ARG_TYPE';
    throw e;
  }
  if(fd < 0 || fd > 2147483647) throw invalidFd(fd);
};

const initFailed = fd => {
  const e = new Error(`TTY initialization failed: invalid argument (fd ${fd} is not a tty)`);
  e.code = 'ERR_TTY_INIT_FAILED';
  e.name = 'SystemError';
  return e;
};

/* isatty: true when fd is an open terminal; false for non-integer/negative fd */
export function isatty(fd) {
  if(!Number.isInteger(fd) || fd < 0 || fd > 2147483647) return false;
  try {
    return !!osIsatty(fd);
  } catch(e) {
    return false;
  }
}

/* eventing: on/once/off/emit for the stream objects */
const events = {
  on(type, fn) {
    const map = listeners.get(this) ?? listeners.set(this, new Map()).get(this);
    (map.get(type) ?? map.set(type, []).get(type)).push(fn);
    return this;
  },
  once(type, fn) {
    const wrap = (...args) => {
      this.off(type, wrap);
      return fn.apply(this, args);
    };
    wrap.listener = fn;
    return this.on(type, wrap);
  },
  off(type, fn) {
    const list = listeners.get(this)?.get(type);
    const i = list ? list.findIndex(l => l === fn || l.listener === fn) : -1;
    if(i != -1) list.splice(i, 1);
    return this;
  },
  removeAllListeners(type) {
    if(type === undefined) listeners.delete(this);
    else listeners.get(this)?.delete(type);
    return this;
  },
  emit(type, ...args) {
    const list = listeners.get(this)?.get(type);
    if(!list?.length) return false;
    for(const fn of [...list]) fn.apply(this, args);
    return true;
  },
  listenerCount(type) {
    return listeners.get(this)?.get(type)?.length ?? 0;
  },
};
events.addListener = events.on;
events.removeListener = events.off;

/* colors: getColorDepth([env]) / hasColors([count][, env]) as in Node */
const colors = {
  getColorDepth(env = globalThis.process?.env ?? {}) {
    if(!isatty(this.fd) && !('FORCE_COLOR' in env)) return 1;
    if('NO_COLOR' in env || env.TERM == 'dumb') return 1;
    if(/truecolor|24bit/i.test(env.COLORTERM ?? '')) return 24;
    if(/256/.test(env.TERM ?? '')) return 8;
    if(env.COLORTERM || /^(screen|xterm|vt100|vt220|rxvt|color|ansi|cygwin|linux)/i.test(env.TERM ?? '')) return 4;
    return env.TERM ? 4 : 1;
  },
  hasColors(count, env) {
    if(count !== undefined && typeof count != 'number') {
      env = count;
      count = 16;
    }
    return 2 ** this.getColorDepth(env) >= (count ?? 16);
  },
};

const done = cb => {
  if(typeof cb == 'function') Promise.resolve().then(cb);
  return true;
};

export function ReadStream(fd) {
  checkFd(fd);
  if(!isatty(fd)) throw initFailed(fd);

  const f = fdopen(fd, 'r');
  Object.setPrototypeOf(f, (new.target ?? ReadStream).prototype);
  declare(f, { fd, isRaw: false, isTTY: true });
  return f;
}

declare(ReadStream.prototype, {
  ...events,
  setRawMode(mode) {
    ttySetRaw(this.fd, !mode);
    this.isRaw = !!mode;
    return this;
  },
  get rawMode() {
    return this.isRaw;
  },
  [Symbol.toStringTag]: 'ReadStream',
});

Object.setPrototypeOf(ReadStream.prototype, Object.getPrototypeOf(out));

const writeStreams = new Set();

const addStream = stream => {
  const { size } = writeStreams;
  writeStreams.add(stream);

  if(size == 0 && writeStreams.size > 0)
    signal(SIGWINCH, () => {
      for(const stream of writeStreams) updateStream(stream);
    });
};

const updateStream = stream => {
  const [columns, rows] = stream.getWindowSize();

  declare(stream, { columns, rows });

  if(typeof stream.onresize == 'function') stream.onresize.call(stream);
  stream.emit('resize');
};

export function WriteStream(fd) {
  checkFd(fd);
  if(!isatty(fd)) throw initFailed(fd);

  const f = Object.setPrototypeOf(fdopen(fd, 'w'), (new.target ?? WriteStream).prototype);

  declare(f, { fd, isTTY: true });
  updateStream(f);
  addStream(f);
  return f;
}

declare(WriteStream.prototype, {
  ...events,
  ...colors,
  getWindowSize() {
    return ttyGetWinSize(this.fileno()) ?? [80, 24];
  },
  write(chunk, encoding, cb) {
    this.puts(typeof chunk == 'string' ? chunk : String(chunk));
    this.flush();
    return done(typeof encoding == 'function' ? encoding : cb);
  },
  /* clearLine(dir): -1 left of cursor, 1 right of cursor, 0 whole line */
  clearLine(dir, cb) {
    this.puts(`\x1b[${dir < 0 ? 1 : dir > 0 ? 0 : 2}K`);
    this.flush();
    return done(cb);
  },
  clearScreenDown(cb) {
    this.puts(`\x1b[0J`);
    this.flush();
    return done(cb);
  },
  /* cursorTo(x[, y]): 0-based column (and row) */
  cursorTo(x, y, cb) {
    if(typeof y == 'function') {
      cb = y;
      y = undefined;
    }
    if(typeof x != 'number') return done(cb);
    this.puts(typeof y != 'number' ? `\x1b[${x + 1}G` : `\x1b[${y + 1};${x + 1}H`);
    this.flush();
    return done(cb);
  },
  moveCursor(dx, dy, cb) {
    let s = '';
    if(dx < 0) s += `\x1b[${-dx}D`;
    else if(dx > 0) s += `\x1b[${dx}C`;
    if(dy < 0) s += `\x1b[${-dy}A`;
    else if(dy > 0) s += `\x1b[${dy}B`;
    if(s) {
      this.puts(s);
      this.flush();
    }
    return done(cb);
  },
  [Symbol.toStringTag]: 'WriteStream',
});

Object.setPrototypeOf(WriteStream.prototype, Object.getPrototypeOf(out));

export default { isatty, ReadStream, WriteStream };
