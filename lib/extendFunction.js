import { tryFunction, catchFunction, finallyFunction, isFunction, types, define, nonenumerable } from './util.js';
import { AsyncFunctionExtensions } from './extendAsyncFunction.js';

const { isAsyncFunction } = types;

export const FunctionExtensions = nonenumerable({
  catch(onRejected) {
    return catchFunction(this, onRejected);
  },
  then(onFulfilled, onRejected) {
    return tryFunction(this, onFulfilled, onRejected);
  },
  finally(onFinally) {
    return finallyFunction(this, onFinally);
  },
  before(onBefore) {
    if(isAsyncFunction(onBefore)) {
      return AsyncFunctionExtensions.before.call(this, onBefore);
    }

    const self = this;

    return function(...args) {
      let result,
        calls = 0;
      const delegate = (...args) => {
        calls++;
        result ??= self.apply(this, args);
      };
      onBefore(delegate, args);
      if(!calls) delegate(...args);
      return result;
    };
  },
});

export const FunctionPrototype = Object.getPrototypeOf(function () {}).constructor.prototype;
export const Function = FunctionPrototype.constructor;

export function extendFunction(proto = FunctionPrototype) {
  return define(proto, FunctionExtensions);
}

export default extendFunction;
