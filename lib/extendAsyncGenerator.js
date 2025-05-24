import { AsyncIterator, AsyncIteratorPrototype } from 'asyncIterator';

export const AsyncGeneratorExtensions = {
  includes(searchElement) {
    return AsyncIteratorPrototype.some.call(this, value => value == searchElement);
  },
  enumerate() {
    return AsyncIteratorPrototype.map.call(this, (value, i) => [i, value]);
  },
  async *chain(...iterables) {
    yield* this;
    for(let iter of iterables) yield* iter;
  },
  async *chainAll(iterables) {
    yield* this;
    for await(let iter of iterables) yield* iter;
  },
  async *range(start = 0, count = Infinity) {
    let i = 0,
      end = start + count;
    for await(let value of this) {
      if(i >= end) break;
      if(i >= start) yield value;
      ++i;
    }
  },
};

export const AsyncGeneratorPrototype = Object.getPrototypeOf((async function* () {})()).constructor.prototype;
export const AsyncGeneratorConstructor = AsyncGeneratorPrototype.constructor;

export function extendAsyncGenerator(proto = AsyncGeneratorPrototype) {
  let desc = Object.getOwnPropertyDescriptors(AsyncGeneratorExtensions);

  Object.setPrototypeOf(proto, AsyncIterator.prototype);

  for(let name in desc) {
    desc[name].enumerable = false;
    if(!desc[name].get) desc[name].writable = false;
  }

  Object.defineProperties(proto, desc);

  Object.defineProperty(proto, Symbol.toStringTag, {
    value: 'AsyncGenerator',
  });
  return proto;
}

export default extendAsyncGenerator;
