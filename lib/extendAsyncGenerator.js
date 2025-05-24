import { AsyncIterator, AsyncIteratorPrototype } from 'asyncIterator';

export const AsyncGeneratorExtensions = {
  includes(searchElement) {
    return AsyncIteratorPrototype.some.call(this, value => value == searchElement);
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
