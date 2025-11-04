import { isAsync, isFunction, define, nonenumerable } from './util.js';

export const AsyncFunctionExtensions = nonenumerable({
  before(onBefore) {
    const self = this;

    return async function(...args) {
      let result,
        calls = 0;
      const delegate = (...args) => {
        calls++;
        result ??= self.apply(this, args);
      };
      await onBefore(delegate, args);
      if(!calls) delegate(...args);
      return await result;
    };
  },
});

export const AsyncFunctionPrototype = Object.getPrototypeOf(async function() {}).constructor.prototype;
export const AsyncFunction = AsyncFunctionPrototype.constructor;

export function extendAsyncFunction(proto = AsyncFunctionPrototype) {
  return define(proto, AsyncFunctionExtensions);
}

export default extendAsyncFunction;
