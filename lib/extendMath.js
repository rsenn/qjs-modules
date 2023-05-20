export const MathExtensions = {
  exp10(n) {
    return Math.floor(Math.log10(n));
  },
  mantissa(n) {
    return n / 10 ** this.exp10(n);
  }
};

export const MathConstructor = Math;

export function extendMath(ctor = MathConstructor) {
  let desc = Object.getOwnPropertyDescriptors(MathExtensions);

  //Object.assign(globalThis[MathConstructor.name], MathConstructor);

  for(let name in desc) {
    desc[name].enumerable = false;
    if(!desc[name].get) desc[name].writable = false;
  }

  Object.defineProperties(ctor, desc);
  return ctor;
}

export default extendMath;
