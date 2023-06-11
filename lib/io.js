import { setReadHandler, setWriteHandler } from 'os';
import { Error } from 'std';
import { assert, define, mapWrapper, fstat, isFunction } from 'util';

const READABLE = 0;
const WRITEABLE = 1;

function setHandler(fd, event, fn) {
  switch (event) {
    case READABLE:
      setReadHandler(fd, fn);
      break;
    case WRITEABLE:
      setWriteHandler(fd, fn);
      break;
  }
}

class FileDescriptorMap extends Array {
  get([fd, event]) {
    let ret;
    if(!(ret = this[fd])) ret = this[fd] = [null, null];
    if(event !== undefined) ret = ret[event];
    return ret;
  }

  set([fd, event], fn) {
    const entry = this[fd] ?? [null, null];
    const old = entry[event];
    entry[event] = fn;
    this[fd] = entry;
    return old;
  }

  delete([fd, event]) {
    const entry = this[fd];
    let old;
    if(entry && (old = entry[event])) {
      entry[event] = null;
      if(entry.every(fn => !fn)) {
        if(fd == this.length - 1) {
          let idx = this.findLastIndex(
            entry => Array.isArray(entry) && entry.some(fn => fn !== null)
          );
          this.slice(idx + 1, this.length - (idx + 1));
        } else {
          delete this[fd];
        }
      }
      return old;
    }
  }

  keys() {
    return this.reduce(
      (acc, item, fd) => (Array.isArray(item) && (item[0] || item[1]) ? (acc.push(fd), acc) : acc),
      []
    );
  }
}

export class IO {
  static #fds = new FileDescriptorMap();

  static on([fd, event], fn) {
    let old;
    if(isFunction(fn)) {
      const entry = (this.#fds[fd] ??= [null, null]);
      old = entry[event];
      entry[event] = fn;
      setHandler(fd, event, () => fn(fd, event));
    } else {
      if((old = this.#fds.delete([fd, event]))) setHandler(fd, event, null);
    }
    return old;
  }

  static once([fd, event], fn) {
    assert(isFunction(fn), true, 'expecting a function');

    const entry = (this.#fds[fd] ??= [null, null]);
    const old = entry[event];
    entry[event] = fn;

    setHandler(fd, event, () => {
      if(entry[event] === fn) {
        setHandler(fd, event, null);
        this.#fds.delete([fd, event]);
        fn(fd, event);
      }
    });

    return old;
  }

  static get fds() {
    return this.#fds.keys();
  }

  static setReadHandler(fd, callback) {
    return this.on.read(fd, callback);
  }

  static setWriteHandler(fd, callback) {
    return this.on.write(fd, callback);
  }
}

define(IO, { READABLE, WRITEABLE });
define(IO.on, {
  read(fd, fn) {
    return this([fd, READABLE], fn);
  },
  write(fd, fn) {
    return this([fd, WRITEABLE], fn);
  }
});

export default IO;
