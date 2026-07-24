import { catchFunction, finallyFunction, nonenumerable, tryFunction, weakDefine, wrapFunction } from './util.js';

export const AsyncFunctionExtensions = nonenumerable({
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
    return weakDefine(
      wrapFunction(this, (call, args) => call(...bound, ...args)),
      typeof this.name == 'string' ? { name: 'bound ' + this.name.replace(/^bound\s/, '') } : {},
    );
  },
  bindArray(bound) {
    return weakDefine(
      wrapFunction(this, (call, args) => call(...bound, ...args)),
      typeof this.name == 'string' ? { name: 'bound ' + this.name.replace(/^bound\s/, '') } : {},
    );
  },
  bindThis(thisObj) {
    return weakDefine((...args) => this.apply(thisObj, args), { name: 'bound ' + this.name, length: this.length, next: this, thisObj });
  },
});

export const AsyncFunctionPrototype = Object.getPrototypeOf(async function() {}).constructor.prototype;
export const AsyncFunction = AsyncFunctionPrototype.constructor;

export function extendAsyncFunction(proto = AsyncFunctionPrototype) {
  return weakDefine(proto, AsyncFunctionExtensions);
}

export default extendAsyncFunction;
