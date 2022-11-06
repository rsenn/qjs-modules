export const AsyncGeneratorExtensions = {
  async *map(fn, thisArg) {
    for await(const value of this) yield await fn.call(thisArg, value);
  },
  async reduce(t = (a, n) => ((a ??= []).push(n), a), c) {
    for await(let n of this) c = await t(c, n);
    return c;
  }
};

export const AsyncGeneratorPrototype = Object.getPrototypeOf((async function* () {})());
export const AsyncGeneratorConstructor = AsyncGeneratorPrototype.constructor;

export function extendAsyncGenerator(proto = AsyncGeneratorPrototype) {
  let desc = Object.getOwnPropertyDescriptors(AsyncGeneratorExtensions);
  Object.setPrototypeOf(AsyncGeneratorExtensions, AsyncGeneratorPrototype);

  for(let name in desc) {
    desc[name].enumerable = false;
    if(!desc[name].get) desc[name].writable = false;
    delete proto[name];
    // console.log('',{ name,desc: desc[name] });
    //desc[name].enumerable = true;
    Object.defineProperty(proto, name, desc[name]);
  }

  Object.defineProperties(proto, desc);
  return proto;
}

export default extendAsyncGenerator;
