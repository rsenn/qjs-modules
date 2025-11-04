import { isFunction, define, nonenumerable } from './util.js';

export const AsyncFunctionExtensions = nonenumerable({
  catch(onRejected) {
    const self = this;

    return function(...args) {
      return self.apply(this, args).catch(e => onRejected(e, ...args));
    };

    /*return async function(...args) {
      let r;
      try {
        r = await self.apply(this, args);
      } catch(error) {
        return await onRejected(error);
      }
      return r;
    };*/
  },
  then(onFulfilled, onRejected) {
    const self = this;
    const callbacks = [onFulfilled, onRejected].map(f => (isFunction(f) ? a => f(a, ...args) : undefined));

    return function(...args) {
      return self.apply(this, args).then(...callbacks);
    };

    /*if(onRejected)
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
    };*/
  },
  finally(onFinally) {
    const self = this;

    return function(...args) {
      return self.apply(this, args).finally(() => onFinally(...args));
    };

    /* return AsyncFunctionExtensions.then.call(
      this,
      async (value, ...args) => {
        await onFinally(...args);
        return value;
      },
      async (error, ...args) => {
        await onFinally(...args);
        throw error;
      },
    );*/
  },
  before(onBefore) {
    const self = this;

    return async function(...args) {
      let ret, called;
      const caller = (...args) => ((ret ??= self.apply(this, args)), (called = true));

      await onBefore(caller, ...args);

      return await ret;
    };

    const replacer =
      args =>
      (...items) => (args.splice(0, args.length, ...items), undefined);
  },
});

export const AsyncFunctionPrototype = Object.getPrototypeOf(async function() {}).constructor.prototype;
export const AsyncFunction = AsyncFunctionPrototype.constructor;

export function extendAsyncFunction(proto = AsyncFunctionPrototype) {
  return define(proto, AsyncFunctionExtensions);
}

export default extendAsyncFunction;
