export const AsyncFunctionExtensions = {
  catch(onRejected) {
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
      return await onFulfilled(self.apply(this, args));
    };
  },
  finally(onFinally) {
    return this.then(
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
};

export const AsyncFunctionPrototype = Object.getPrototypeOf(async function() {}).constructor.prototype;
export const AsyncFunction = AsyncFunctionPrototype.constructor;

export function extendAsyncFunction(proto = AsyncFunctionPrototype) {
  const d = Object.getOwnPropertyDescriptors(AsyncFunctionExtensions);

  for(const k in d) {
    d[k].enumerable = false;
    if(!d[k].get) d[k].writable = false;
  }

  Object.defineProperties(proto, d);
  return proto;
}

export default extendAsyncFunction;
