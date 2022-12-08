export const GeneratorExtensions = {
  *filter(fn, thisArg) {
    let i = 0;
    for(const value of this) if(fn.call(thisArg, value, i++, this)) yield value;
  },
  *map(fn, thisArg) {
    for(const value of this) yield fn.call(thisArg, value);
  },
  reduce(t = (a, n) => ((a ??= []).push(n), a), c) {
    for(let n of this) c = t(c, n);
    return c;
  },
  every(fn, thisArg) {
    for(const value of this) if(!fn.call(thisArg, value, i++, this)) return false;
    return true;
  },
  some(fn, thisArg) {
    for(const value of this) if(fn.call(thisArg, value, i++, this)) return true;
    return false;
  },
  includes(searchElement) {
    for(const value of this) if(value === searchElement) return true;
    return false;
  },
  *take(n = 1) {
    while(n-- > 0) yield this.next().value;
  },
  *takeWhile(fn = value => false) {
    for(;;) {
      const { value, done } = this.next();
      if(done) return value;
      if(!fn(value)) break;
      yield value;
    }
  },
  *drop(n = 0) {
    while(n-- > 0) this.next();
    for(;;) {
      const { value, done } = this.next();
      if(done) return value;
      yield value;
    }
  },
  find(fn = value => false) {
    for(;;) {
      const { value, done } = this.next();
      if(done) return value;
      if(fn(value)) return value;
    }
  },
  *dropWhile(fn = value => false) {
    for(;;) {
      const { value, done } = this.next();
      if(done) return value;
      if(!fn(value)) break;
    }
    for(;;) {
      const { value, done } = this.next();
      if(done) return value;
      yield value;
    }
  },
  *enumerate() {
    let i = 0;
    for(let value of this) yield [i++, value];
  },
  *chain(...iterables) {
    yield* this;
    for(let iter of iterables) yield* iter;
  },
  *chainAll(iterables) {
    yield* this;
    for(let iter of iterables) yield* iter;
  },
  *range(start = 0, count = Infinity) {
    let i = 0,
      end = start + count;
    for(let value of this) {
      if(i >= start && i < end) yield value;
      ++i;
    }
  }
};

export const GeneratorPrototype = Object.getPrototypeOf((function* () {})()).constructor.prototype;
export const GeneratorConstructor = GeneratorPrototype.constructor;

export function extendGenerator(proto = GeneratorPrototype) {
  let desc = Object.getOwnPropertyDescriptors(GeneratorExtensions);

  for(let name in desc) {
    desc[name].enumerable = false;
    if(!desc[name].get) desc[name].writable = false;
  }

  Object.defineProperties(proto, desc);
  return proto;
}

export default extendGenerator;
