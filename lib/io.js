import { setReadHandler, setWriteHandler } from 'os';
import { Error } from 'std';
import { define, mapWrapper, fstat } from 'util';

export class IO {
  static READABLE = 0;
  static WRITEABLE = 1;

  static #map = mapWrapper(
    new (class IOMap extends Array {
      get([fd, event]) {
        let ret = this[fd] ?? [null, null];
        if(event !== undefined) ret = ret[event];
        return ret;
      }

      set(...args) {
        console.log('args', args);
        const [[fd, event], fn] = args;
        let entry = this[fd] ?? [null, null];
        let old = entry[event];
        entry[event] = fn;
        this[fd] = entry;
        return old;
      }
    })()
  );

  static #on = define(
    function on([fd, event], fn) {
      const ret = IO.#map([fd, event], fn);
      switch (event) {
        case IO.READABLE:
          setReadHandler(fd, () => IO.#handle(fd, event));
          break;
        case IO.WRITEABLE:
          setWriteHandler(fd, () => IO.#handle(fd, event));
          break;
      }
      return ret;
    },
    {
      read(fd, fn) {
        return this([fd, IO.READABLE], fn);
      },
      write(fd, fn) {
        return this([fd, IO.WRITEABLE], fn);
      }
    }
  );

  static get fds() {
    return this.#map();
  }

  static setReadHandler(fd, callback) {
    return this.#on.read(fd, callback);
  }
  static setWriteHandler(fd, callback) {
    return this.#on.write(fd, callback);
  }

  static #handle(fd, event) {
    try {
      this.#map([fd, event])(fd, event);
    } catch(error) {}
  }
}

export default IO;
