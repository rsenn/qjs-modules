import { define, nonenumerable } from './util.js';

export const FunctionExtensions = nonenumerable({
  catch(onRejected) {
    const self = this;
    return function(...args) {
      let r;
      try {
        r = self.apply(this, args);
      } catch(error) {
        return onRejected(error);
      }
      return r;
    };
  },
  then(onFulfilled, onRejected) {
    const self = this;
    if(onRejected)
      return function(...args) {
        let r;
        try {
          r = self.apply(this, args);
        } catch(error) {
          return onRejected(error);
        }
        return onFulfilled(r);
      };
    return function(...args) {
      return onFulfilled(self.apply(this, args));
    };
  },
  finally(onFinally) {
    return this.then(
      value => {
        onFinally();
        return value;
      },
      error => {
        onFinally();
        throw error;
      },
    );
  },
  before(onBefore) {
    const self = this;
    return function(...args) {
      const r = onBefore(...args);
      return self.apply(this, Array.isArray(r) ? r : []);
    };
  },
});

export const FunctionPrototype = Object.getPrototypeOf(function () {}).constructor.prototype;
export const Function = FunctionPrototype.constructor;

export function extendFunction(proto = FunctionPrototype) {
  return define(proto, FunctionExtensions);
}

export default extendFunction;
