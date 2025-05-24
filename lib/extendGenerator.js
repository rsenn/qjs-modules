import { Iterator, IteratorPrototype } from 'iterator';

export const GeneratorExtensions = {
  includes(searchElement) {
    return IteratorPrototype.some.call(this, value => value == searchElement);
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
      if(i >= end) break;
      if(i >= start) yield value;
      ++i;
    }
  },
};

export const GeneratorPrototype = Object.getPrototypeOf((function* () {})()).constructor.prototype;
export const GeneratorConstructor = GeneratorPrototype.constructor;

export function extendGenerator(proto = GeneratorPrototype) {
  Object.setPrototypeOf(proto, Iterator.prototype);

  let desc = Object.getOwnPropertyDescriptors(GeneratorExtensions);

  //Object.assign(globalThis[GeneratorConstructor.name], GeneratorConstructor);

  for(let name in desc) {
    desc[name].enumerable = false;
    if(!desc[name].get) desc[name].writable = false;
  }

  Object.defineProperties(proto, desc);

  Object.defineProperty(proto, Symbol.toStringTag, {
    value: 'Generator',
  });
  return proto;
}

export default extendGenerator;
