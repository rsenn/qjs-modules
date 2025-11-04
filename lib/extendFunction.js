import { beforeFunction, tryFunction, catchFunction, finallyFunction, isFunction, types, define, nonenumerable } from './util.js';
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
    return beforeFunction(this, onBefore);
  },
});

export const FunctionPrototype = Object.getPrototypeOf(function () {}).constructor.prototype;
export const Function = FunctionPrototype.constructor;

export function extendFunction(proto = FunctionPrototype) {
  return define(proto, FunctionExtensions);
}

export default extendFunction;
