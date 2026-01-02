import { Iterator } from 'iterator';
import { IteratorPrototype } from 'iterator';

export const GeneratorExtensions = {
  includes(searchElement) {
    return IteratorPrototype.some.call(this, value => value == searchElement);
  },
  *enumerate() {
    let i = 0;
    for(const value of this) yield [i++, value];
  },
  *chain(...iterables) {
    yield* this;
    for(const iter of iterables) yield* iter;
  },
  *chainAll(iterables) {
    yield* this;
    for(const iter of iterables) yield* iter;
  },
  *range(start = 0, count = Infinity) {
    let i = 0,
      end = start + count;
    for(const value of this) {
      if(i >= end) break;
      if(i >= start) yield value;
      ++i;
    }
  },
};

export const GeneratorPrototype = Object.getPrototypeOf((function* () {})()).constructor.prototype;
export const Generator = GeneratorPrototype.constructor;

export function extendGenerator(proto = GeneratorPrototype) {
  Object.setPrototypeOf(proto, Iterator.prototype);

  const d = Object.getOwnPropertyDescriptors(GeneratorExtensions);

  for(const k in d) {
    d[k].enumerable = false;
    if(!d[k].get) d[k].writable = false;
  }

  Object.defineProperties(proto, d);
  Object.defineProperty(proto, Symbol.toStringTag, {
    value: 'Generator',
  });

  return proto;
}

export default extendGenerator;