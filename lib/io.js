import { Error as errorMap, strerror, getenv } from 'std';
import { setReadHandler as onRead, setWriteHandler as onWrite, close } from 'os';
import { assert, define, isFunction, fstat } from 'util';

export const READ = 0;
export const WRITE = 1;

const { EBADF } = errorMap;

const DBG = getenv('DEBUG')
  ? (...args) => console.log('\x1b[1;33mIO\x1b[0m', ...args.map(s => s.replace(/\s+/g, ' ')))
  : () => {};

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

  let [st, err] = fstat(fd);

  if(err) {
    if(err != EBADF)
      throw new Error(`No such error on fstat(${fd}, ...): "${strerror(err)}" (errno = ${err})`);
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
    for(let fd of super.keys()) if(this[fd]?.active) yield fd;
  }
}

define(DescriptorMap.prototype, { [Symbol.toStringTag]: 'DescriptorMap' });

export class Multiplexer {
  #fds = new DescriptorMap();

  on([fd, m], callback) {
    let old;

    if(isFunction(callback)) {
      assert(checkFileDescriptor(fd), 'invalid file descriptor');
      const e = (this.#fds[fd] ??= new HandlerEntry());
      old = e[m];
      e[m] = callback;
      setHandler(fd, m, () => callback(fd, m));
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
    e[m] = callback;

    setHandler(
      fd,
      m,
      () => callback == this.#fds.delete([fd, m]) && (setHandler(fd, m, null), callback(fd, m))
    );

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
    for(let fd of this.fds(mode))
      if(!checkFileDescriptor(fd))
        this.#fds[fd].forEach((fd, m) => {
          console.log(`Got a ${m ? 'writ' : 'read'}able handler for invalid file descriptor ${fd}`);
          if(this.#fds.delete([fd, m])) setHandler(fd, m, null);
        });
  }

  close(fd) {
    for(let m of [READ, WRITE]) {
      let e;
      if((e = this.#fds.get([fd, m])))
        throw new Error(
          `Cannot close fd ${fd}, because there is still a ${m ? 'write' : 'read'} handler`
        );
    }
    close(fd);
  }
}

define(Multiplexer.prototype, { [Symbol.toStringTag]: 'Multiplexer' });

const io = new Multiplexer();

export function setReadHandler(fd, callback) {
  DBG(`setReadHandler(${fd}, ${callback})`);
  return io.on([fd, READ], callback);
}

export function setWriteHandler(fd, callback) {
  DBG(`setWriteHandler(${fd}, ${callback})`);
  return io.on([fd, WRITE], callback);
}

export default io;
