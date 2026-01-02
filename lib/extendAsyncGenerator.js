import { AsyncIterator } from 'asyncIterator';
import { AsyncIteratorPrototype } from 'asyncIterator';

const { some, map } = AsyncIteratorPrototype;

export const AsyncGeneratorExtensions = {
  includes(searchElement) {
    return some.call(this, value => value == searchElement);
  },
  enumerate() {
    return map.call(this, (value, i) => [i, value]);
  },
  async *chain(...iterables) {
    yield* this;
    for(const iter of iterables) yield* iter;
  },
  async *chainAll(iterables) {
    yield* this;
    for await(const iter of iterables) yield* iter;
  },
  async *range(start = 0, count = Infinity) {
    let i = 0,
      end = start + count;
    for await(const value of this) {
      if(i >= end) break;
      if(i >= start) yield value;
      ++i;
    }
  },
};

 const AsyncGeneratorPrototype = Object.getPrototypeOf((async function* () {})()).constructor.prototype;

export const AsyncGenerator = AsyncGeneratorPrototype.constructor;

export function extendAsyncGenerator(proto = AsyncGeneratorPrototype) {
  const d = Object.getOwnPropertyDescriptors(AsyncGeneratorExtensions);

  Object.setPrototypeOf(proto, AsyncIterator.prototype);

  for(const k in d) {
    d[k].enumerable = false;
    if(!d[k].get) d[k].writable = false;
  }

  Object.defineProperties(proto, d);
  Object.defineProperty(proto, Symbol.toStringTag, {
    value: 'AsyncGenerator',
  });

  return proto;
}

export default extendAsyncGenerator;