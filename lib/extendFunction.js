import { wrapFunction, tryFunction, catchFunction, finallyFunction, isFunction, types, define, nonenumerable } from './util.js';
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
  indirect(onIndirect) {
    return wrapFunction(this, onIndirect);
  },
  bindArguments(...bound) {
    return wrapFunction(this, (call, args) => call(...bound, ...args));
  },
  bindArray(bound) {
    return wrapFunction(this, (call, args) => call(...bound, ...args));
  },
  bindThis(thisObj) {
    return define((...args) => this.apply(thisObj, args), { name: 'bound ' + this.name, length: this.length });
  },
  /*map(mapFn) {
    return wrapFunction(this, (call, args) => mapFn(call(...args), ...args));
  },*/
});

export const FunctionPrototype = Object.getPrototypeOf(function () {}).constructor.prototype;
export const Function = FunctionPrototype.constructor;

export function extendFunction(proto = FunctionPrototype) {
  return define(proto, FunctionExtensions);
}

export default extendFunction;
