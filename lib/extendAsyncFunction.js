import { define, nonenumerable } from './util.js';

export const AsyncFunctionExtensions = nonenumerable({
  catch(onRejected) {
    const self = this;

    return async function(...args) {
      let r;
      try {
        r = await self.apply(this, args);
      } catch(error) {
        return await onRejected(error);
      }
      return r;
    };
  },
  then(onFulfilled, onRejected) {
    const self = this;

    if(onRejected)
      return async function(...args) {
        let r;
        try {
          r = await self.apply(this, args);
        } catch(error) {
          return await onRejected(error);
        }
        return await onFulfilled(r);
      };

    return async function(...args) {
      return await onFulfilled(await self.apply(this, args));
    };
  },
  finally(onFinally) {
    return AsyncFunctionExtensions.then.call(
      this,
      async value => {
        await onFinally();
        return value;
      },
      async error => {
        await onFinally();
        throw error;
      },
    );
  },
  before(onBefore) {
    const self = this;

    return async function(...args) {
      const r = await onBefore(...args);
      return await self.apply(this, Array.isArray(r) ? r : []);
    };
  },
});

export const AsyncFunctionPrototype = Object.getPrototypeOf(async function() {}).constructor.prototype;
export const AsyncFunction = AsyncFunctionPrototype.constructor;

export function extendAsyncFunction(proto = AsyncFunctionPrototype) {
  return define(proto, AsyncFunctionExtensions);
}

export default extendAsyncFunction;
