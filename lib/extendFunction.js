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
    return define(
      wrapFunction(this, (call, args) => call(...bound, ...args)),
      typeof this.name == 'string' ? { name: 'bound ' + this.name.replace(/^bound\s/, '') } : {},
    );
  },
  bindArray(bound) {
    return define(
      wrapFunction(this, (call, args) => call(...bound, ...args)),
      typeof this.name == 'string' ? { name: 'bound ' + this.name.replace(/^bound\s/, '') } : {},
    );
  },
  bindThis(thisObj) {
    return define((...args) => this.apply(thisObj, args), { name: 'bound ' + this.name, length: this.length, next: this, thisObj });
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
