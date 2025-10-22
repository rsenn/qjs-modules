export class AsyncIterator {
  static from(iter, ...args) {
    if(Symbol.asyncIterator in iter) iter = iter[Symbol.asyncIterator](...args);
    if('next' in iterable && typeof iter.next == 'function') iter = (...args) => iter.next(...args);
    if(typeof iter == 'function') return Object.setPrototypeOf({ next: iter }, AsyncIterator.prototype);

    throw TypeError('argument 1 must be AsyncIterable or AsyncIterator');
  }

  async *drop(n = 0) {
    while(n-- > 0) {
      const { value, done } = await this.next();
      if(done) return;
    }
    for(;;) {
      const { value, done } = await this.next();
      if(done) return;
      yield value;
    }
  }
  async *takeWhile(fn = value => false) {
    let i = 0;
    for(;;) {
      const { value, done } = await this.next();
      if(done) return value;
      if(!(await fn(value, i++, this))) break;
      yield value;
    }
  }
  async *dropWhile(fn = value => false) {
    let i = 0;
    for(;;) {
      const { value, done } = await this.next();
      if(done) return value;
      if(!(await fn(value, i++, this))) break;
    }
    for(;;) {
      const { value, done } = await this.next();
      if(done) return value;
      yield value;
    }
  }

  async every(fn, thisArg) {
    let i = 0;
    for(;;) {
      const { value, done } = await this.next();
      if(done) break;
      if(!(await fn.call(thisArg, value, i++, this))) return false;
    }
    return true;
  }
  async *filter(fn, thisArg) {
    let i = 0;
    for(;;) {
      const { value, done } = await this.next();
      if(done) break;
      if(await fn.call(thisArg, value, i++, this)) yield value;
    }
  }
  async find(fn = value => false) {
    let i = 0;
    for(;;) {
      const { value, done } = await this.next();
      if(done) break;
      if(await fn(value, i++, this)) return value;
    }
  }
  async forEach(fn, thisArg) {
    let i = 0;
    for(;;) {
      const { value, done } = await this.next();
      if(done) break;
      await fn.call(thisArg, value, i++, this);
    }
  }
  async *map(fn, thisArg) {
    let i = 0;
    for(;;) {
      const { value, done } = await this.next();
      if(done) break;
      yield await fn.call(thisArg, value, i++, this);
    }
  }
  async reduce(t = async (a, n) => ((a ??= []).push(n), a), c) {
    let i = 0;
    for(;;) {
      const { value, done } = await this.next();
      if(done) break;
      c = await t(c, n, i++, this);
    }
    return c;
  }
  async some(fn, thisArg) {
    let i = 0;
    for(;;) {
      const { value, done } = await this.next();
      if(done) break;
      if(await fn.call(thisArg, value, i++, this)) return true;
    }
    return false;
  }
  async *take(n = 1) {
    for(; n > 0; n--) {
      const { value, done } = await this.next();
      if(done) break;

      yield value;
    }
  }
  async *flatMap(fn) {
    let i = 0;
    for(;;) {
      const { value, done } = await this.next();
      if(done) break;
      let result = await fn.call(thisArg, value, i++, this);

      try {
        let it = AsyncIterator.from(result);
        yield* it;
      } catch(e) {
        yield result;
      }
    }
  }
  async toArray() {
    const arr = [];
    for(;;) {
      const { value, done } = await this.next();
      if(done) break;
      arr.push(value);
    }
    return arr;
  }

  [Symbol.asyncIterator]() {
    return this;
  }
}

export const AsyncIteratorPrototype = AsyncIterator.prototype;

Object.defineProperty(AsyncIterator.prototype, Symbol.toStringTag, {
  value: 'AsyncIterator',
});

export default AsyncIterator;
