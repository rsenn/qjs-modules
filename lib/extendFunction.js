export const FunctionExtensions = {
  catch(fn = error => {}) {
    const self = this;
    return function(...args) {
      let r;
      try {
        r = self.call(this, ...args);
      } catch(error) {
        fn(error);
      }
      return r;
    };
  },
  then(fn = result => result) {
    const self = this;
    return function(...args) {
      const r = self.call(this, ...args);
      return fn(r);
    };
  },
};

export const FunctionPrototype = Object.getPrototypeOf(function () {}).constructor.prototype;
export const Function = FunctionPrototype.constructor;

export function extendFunction(proto = FunctionPrototype) {
  const d = Object.getOwnPropertyDescriptors(FunctionExtensions);

  for(const k in d) {
    d[k].enumerable = false;
    if(!d[k].get) d[k].writable = false;
  }

  Object.defineProperties(proto, d);
  return proto;
}

export default extendFunction;
