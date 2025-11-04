import { types, define, nonenumerable } from './util.js';
import { AsyncFunctionExtensions } from './extendAsyncFunction.js';

const { isAsyncFunction } = types;

export const FunctionExtensions = nonenumerable({
  catch(onRejected) {
    if(isAsyncFunction(onRejected)) {
      return AsyncFunctionExtensions.catch.call(this, onRejected);
    }

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
    if(isAsyncFunction(onRejected) || isAsyncFunction(onFulfilled)) {
      return AsyncFunctionExtensions.then.call(this, onFulfilled, onRejected);
    }

    const self = this;

    return onRejected
      ? function(...args) {
          let r;
          try {
            r = self.apply(this, args);
          } catch(error) {
            return onRejected(error, ...args);
          }
          return onFulfilled(r, ...args);
        }
      : function(...args) {
          return onFulfilled(self.apply(this, args), ...args);
        };
  },
  finally(onFinally) {
    if(isAsyncFunction(onFinally)) {
      return AsyncFunctionExtensions.finally.call(this, onFinally);
    }

    return FunctionExtensions.then.call(
      this,
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
    if(isAsyncFunction(onBefore)) {
      return AsyncFunctionExtensions.before.call(this, onBefore);
    }

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
