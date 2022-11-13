export const FunctionExtensions = {
  catch(fn = error => {}) {
    let self = this;
    return function(...args) {
      let ret;
      try {
        ret = self.call(this, ...args);
      } catch(error) {
        fn(error);
      }
      return ret;
    };
  },
  then(fn = result => result) {
    let self = this;
    return function(...args) {
      let ret;
      ret = self.call(this, ...args);
      return fn(ret);
    };
  }
};

export const FunctionPrototype = Object.getPrototypeOf(function () {}).constructor.prototype;
export const FunctionConstructor = FunctionPrototype.constructor;

export function extendFunction(proto = FunctionPrototype) {
  let desc = Object.getOwnPropertyDescriptors(FunctionExtensions);

  for(let name in desc) {
    desc[name].enumerable = false;
    if(!desc[name].get) desc[name].writable = false;
  }

  Object.defineProperties(proto, desc);
  return proto;
}

export default extendFunction;
