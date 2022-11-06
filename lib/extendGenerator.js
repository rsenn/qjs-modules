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
