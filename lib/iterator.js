export class Iterator {
  static from(iter, ...args) {
    if(Symbol.iterator in iter) iter = iter[Symbol.iterator](...args);
    if('next' in iterable && typeof iter.next == 'function') iter = (...args) => iter.next(...args);
    if(typeof iter == 'function') return Object.setPrototypeOf({ next: iter }, Iterator.prototype);

    throw TypeError('argument 1 must be Iterable or Iterator');
  }

  *drop(n = 0) {
    while(n-- > 0) this.next();
    for(;;) {
      const { value, done } = this.next();
      if(done) return value;
      yield value;
    }
  }

  *takeWhile(fn = value => false) {
    let i = 0;
    for(;;) {
      const { value, done } = this.next();
      if(done) return value;
      if(!fn(value, i++, this)) break;
      yield value;
    }
  }
  *dropWhile(fn = value => false) {
    let i = 0;
    for(;;) {
      const { value, done } = this.next();
      if(done) return value;
      if(!fn(value, i++, this)) break;
    }
    for(;;) {
      const { value, done } = this.next();
      if(done) return value;
      yield value;
    }
  }

  every(fn, thisArg) {
    let i = 0;
    for(;;) {
      const { value, done } = this.next();
      if(done) break;
      if(!fn.call(thisArg, value, i++, this)) return false;
    }
    return true;
  }
  *filter(fn, thisArg) {
    let i = 0;
    for(;;) {
      const { value, done } = this.next();
      if(done) break;
      if(fn.call(thisArg, value, i++, this)) yield value;
    }
  }
  find(fn = value => false) {
    let i = 0;
    for(;;) {
      const { value, done } = this.next();
      if(done) break;
      if(fn(value, i++, this)) return value;
    }
  }
  forEach(fn, thisArg) {
    let i = 0;
    for(;;) {
      const { value, done } = this.next();
      if(done) break;
      fn.call(thisArg, value, i++, this);
    }
  }
  *map(fn, thisArg) {
    let i = 0;
    for(;;) {
      const { value, done } = this.next();
      if(done) break;
      yield fn.call(thisArg, value, i++, this);
    }
  }
  reduce(t = (a, n) => ((a ??= []).push(n), a), c) {
    let i = 0;
    for(;;) {
      const { value, done } = this.next();
      if(done) break;
      c = t(c, n, i++, this);
    }
    return c;
  }
  some(fn, thisArg) {
    let i = 0;
    for(;;) {
      const { value, done } = this.next();
      if(done) break;
      if(fn.call(thisArg, value, i++, this)) return true;
    }
    return false;
  }
  *take(n = 1) {
    for(; n > 0; n--) {
      const { value, done } = this.next();
      if(done) break;

      yield value;
    }
  }
  *flatMap(fn) {
    let i = 0;
    for(;;) {
      const { value, done } = this.next();
      if(done) break;
      let result = fn.call(thisArg, value, i++, this);

      try {
        let it = Iterator.from(result);
        yield* it;
      } catch(e) {
        yield result;
      }
    }
  }
  toArray() {
    const arr = [];
    for(;;) {
      const { value, done } = this.next();
      if(done) break;
      arr.push(value);
    }
    return arr;
  }

  [Symbol.iterator]() {
    return this;
  }
}

export const IteratorPrototype = Iterator.prototype;
export const IteratorConstructor = Iterator;

Object.defineProperty(Iterator.prototype, Symbol.toStringTag, {
  value: 'Iterator',
});

export default () => (globalThis.Iterator = Iterator);
