import { tryFunction, catchFunction, isFunction, types, define, nonenumerable } from './util.js';
import { AsyncFunctionExtensions } from './extendAsyncFunction.js';

const { isAsyncFunction } = types;

export const FunctionExtensions = nonenumerable({
  catch(onRejected) {
    return catchFunction(this, onRejected);

    /*if(isAsyncFunction(onRejected)) {
      return AsyncFunctionExtensions.catch.call(this, onRejected);
    }

    const self = this;

    return function(...args) {
      let r;
      try {
        r = self.apply(this, args);
      } catch(error) {
        return onRejected(error, ...args);
      }
      return r;
    };*/
  },
  then(onFulfilled, onRejected) {
    return tryFunction(this, onFulfilled, onRejected);
  },
  finally(onFinally) {
    if(isAsyncFunction(onFinally)) {
      return AsyncFunctionExtensions.finally.call(this, onFinally);
    }

    return FunctionExtensions.then.call(
      this,
      (value, ...args) => {
        onFinally(...args);
        return value;
      },
      (error, ...args) => {
        onFinally(...args);
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
