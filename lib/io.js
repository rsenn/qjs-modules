import { close } from 'os';
import { setReadHandler as onRead } from 'os';
import { setWriteHandler as onWrite } from 'os';
import { assert } from 'util';
import { define } from 'util';
import { isFunction } from 'util';
import { nonenumerable } from 'util';
import { fstat } from 'misc';
import { toArrayBuffer } from 'misc';
import { toString } from 'misc';
import { Error as errorMap } from 'std';
import { getenv } from 'std';
import { SEEK_CUR } from 'std';
import { sprintf } from 'std';
import { strerror } from 'std';

export const READ = 0;
export const WRITE = 1;

const { EBADF } = errorMap;

const DEBUG = /\bio\b/.test(getenv('DEBUG') ?? '') ? (...args) => console.log('\x1b[1;33mIO\x1b[0m', ...args.map(s => s.replace(/\s+/g, ' '))) : () => {};

export function setHandler(fd, m, callback) {
  switch (m) {
    case READ:
      onRead(fd, callback);
      break;
    case WRITE:
      onWrite(fd, callback);
      break;
  }
}

export function checkFileDescriptor(fd) {
  if(typeof fd != 'number') return false;
  let ret = fstat(fd);
  //console.log(`fstat(${fd}) =`, ret);
  let [st, err] = ret;

  if(err) {
    if(err != EBADF) throw new Error(`No such error on fstat(${fd}, ...): "${strerror(err)}" (errno = ${err})`);
    return false;
  }
  return true;
}

export class HandlerEntry extends Array {
  static isHandlers(obj) {
    return typeof obj == 'object' && obj !== null && obj instanceof HandlerEntry;
  }

  constructor() {
    super(2);
    this[READ] = null;
    this[WRITE] = null;
  }

  get active() {
    return isFunction(this[READ]) || isFunction(this[WRITE]);
  }
}

define(HandlerEntry.prototype, { [Symbol.toStringTag]: 'HandlerEntry' });

export class DescriptorMap extends Array {
  get([fd, m]) {
    let ret = this[fd];
    if(m !== undefined) ret = ret?.[m] ?? null;
    return ret;
  }

  set([fd, m], callback) {
    assert(isFunction(callback), 'expecting a function');
    const e = (this[fd] ??= new HandlerEntry());
    const old = e[m];
    e[m] = callback;
    this[fd] = e;
    return old;
  }

  delete([fd, m]) {
    const e = this[fd];
    let old;
    if(e && (old = e[m])) {
      e[m] = null;
      if(e.every(callback => !callback)) {
        delete this[fd];
        if(fd == this.length - 1) {
          while(fd >= 0 && !HandlerEntry.isHandlers(this[fd])) --fd;
          this.slice(fd + 1, this.length - (fd + 1));
        }
      }
      return old;
    }
  }

  *keys() {
    for(const fd of super.keys()) if(this[fd]?.active) yield fd;
  }
}

define(DescriptorMap.prototype, { [Symbol.toStringTag]: 'DescriptorMap' });

export class Multiplexer {
  #fds = new DescriptorMap();

  on([fd, m], callback) {
    let old;

    if(isFunction(callback)) {
      assert(checkFileDescriptor(fd), `invalid file descriptor ${fd}`);

      const e = (this.#fds[fd] ??= new HandlerEntry());
      if((old = e[m]) !== callback) {
        e[m] = callback;
        setHandler(fd, m, () => callback(fd, m));
      }
    } else {
      if((old = this.#fds.delete([fd, m]))) setHandler(fd, m, null);
    }
    return old;
  }

  once([fd, m], callback) {
    assert(isFunction(callback), 'expecting a function');
    assert(checkFileDescriptor(fd), 'invalid file descriptor');

    const e = (this.#fds[fd] ??= new HandlerEntry());
    const old = e[m];

    if(old !== callback) {
      e[m] = callback;
      setHandler(fd, m, () => callback == this.#fds.delete([fd, m]) && (setHandler(fd, m, null), callback(fd, m)));
    }
    return old;
  }

  fds(mode) {
    let ret = [...this.#fds.keys()];
    if(typeof mode == 'number') ret = ret.filter(fd => !!this.#fds.get([fd, mode]));
    return ret;
  }

  getHandler([fd, m]) {
    return this.#fds.get([fd, m]);
  }

  check(mode) {
    for(const fd of this.fds(mode))
      if(!checkFileDescriptor(fd))
        this.#fds[fd].forEach((fd, m) => {
          console.log(`Got a ${m ? 'writ' : 'read'}able handler for invalid file descriptor ${fd}`);
          if(this.#fds.delete([fd, m])) setHandler(fd, m, null);
        });
  }

  close(fd) {
    for(const m of [READ, WRITE]) {
      let e;
      if((e = this.#fds.get([fd, m]))) throw new Error(`Cannot close fd ${fd}, because there is still a ${m ? 'write' : 'read'} handler`);
    }
    close(fd);
  }
}

define(Multiplexer.prototype, { [Symbol.toStringTag]: 'Multiplexer' });

const io = (Multiplexer.instance = new Multiplexer());

export function setReadHandler(fd, callback) {
  DEBUG(`setReadHandler(${fd}, ${callback})`);
  return io.on([fd, READ], callback);
}

export function setWriteHandler(fd, callback) {
  DEBUG(`setWriteHandler(${fd}, ${callback})`);
  return io.on([fd, WRITE], callback);
}

export const IOReadDecorator = nonenumerable({
  readAsString(n) {
    n ??= this.size;
    const b = new ArrayBuffer(n);
    return toString(b, 0, this.read(b, 0, n));
  },
  getByte() {
    const u8 = new Uint8Array(1);
    if(this.read(u8.buffer) > 0) return u8[0];
    return -1;
  },
  getline() {
    const u8 = new Uint8Array(6),
      dec = new TextDecoder();
    let s = '',
      res;

    while((res = this.read(u8.buffer, 0, 1)) > 0) {
      let n = res;

      while((res = dec.decode(u8.buffer.slice(0, n))) === '') {
        if((res = this.read(u8.buffer, n, 1)) > 0) n += res;
      }
      const [code, len] = res;
      if(code == 10) break;
      s += String.fromCodePoint(code);
    }
    return res === 0 ? null : s;
  },
  tell() {
    return this.seek(0, SEEK_CUR);
  },
});

export const IOWriteDecorator = nonenumerable({
  puts(s) {
    const b = toArrayBuffer(s);
    return this.write(b);
  },
  printf(fmt, ...args) {
    return this.puts(sprintf(fmt, ...args));
  },
  putByte(c) {
    const u8 = new Uint8Array([c]);
    return this.write(u8.buffer, 0, 1);
  },
});

export default io;
