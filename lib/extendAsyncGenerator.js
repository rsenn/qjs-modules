export const AsyncGeneratorExtensions = {
  *map(fn, thisArg) {
    for(const value of this) yield fn.call(thisArg, value);
  }
};

export const AsyncGeneratorPrototype = Object.getPrototypeOf((async function* () {})());
export const AsyncGeneratorConstructor = AsyncGeneratorPrototype.constructor;

export function extendAsyncGenerator(proto = AsyncGeneratorPrototype) {
  let desc = Object.getOwnPropertyDescriptors(AsyncGeneratorExtensions);

  for(let name in desc) {
    desc[name].enumerable = false;
    if(!desc[name].get) desc[name].writable = false;
  }

  Object.defineProperties(proto, desc);
  return proto;
}

export default extendAsyncGenerator;
