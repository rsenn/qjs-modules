export const AsyncGeneratorExtensions = {
  async *filter(fn, thisArg) {
    let i = 0;
    for await(const value of this) if(await fn.call(thisArg, value, i++, this)) yield value;
  },
  async *map(fn, thisArg) {
    let i = 0;
    for await(const value of this) yield await fn.call(thisArg, value, i++, this);
  },
  async forEach(fn, thisArg) {
    let i = 0;
    for await(const value of this) await fn.call(thisArg, value, i++, this);
  },
  async reduce(t = (a, n) => ((a ?? []).push(n), a), c) {
    let i = 0;
    for await(let n of this) c = await t(c, n, i++, this);
    return c;
  },
  async every(fn, thisArg) {
    let i = 0;
    for await(const value of this) if(!(await fn.call(thisArg, value, i++, this))) return false;
    return true;
  },
  async some(fn, thisArg) {
    let i = 0;
    for await(const value of this) if(await fn.call(thisArg, value, i++, this)) return true;
    return false;
  },
  async includes(searchElement) {
    for await(const value of this) if(value === searchElement) return true;
    return false;
  }
};

export const AsyncGeneratorPrototype = Object.getPrototypeOf((async function* () {})()).constructor.prototype;
export const AsyncGeneratorConstructor = AsyncGeneratorPrototype.constructor;

export function extendAsyncGenerator(proto = AsyncGeneratorPrototype) {
  let desc = Object.getOwnPropertyDescriptors(AsyncGeneratorExtensions);

  //Object.assign(globalThis[AsyncGeneratorConstructor.name], AsyncGeneratorConstructor);

  for(let name in desc) {
    desc[name].enumerable = false;
    if(!desc[name].get) desc[name].writable = false;
    /* delete proto[name];
    Object.defineProperty(proto, name, desc[name]);*/
  }

  Object.defineProperties(proto, desc);
  return proto;
}

export default extendAsyncGenerator;
