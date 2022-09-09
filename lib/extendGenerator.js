export const GeneratorExtensions = {
  *map(fn, thisArg) {
    for(const value of this) yield fn.call(thisArg, value);
  }
};

export const GeneratorPrototype = Object.getPrototypeOf((function* () {})());
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
