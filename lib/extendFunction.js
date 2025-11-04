import { isAsync } from './util.js';

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

export const FunctionExtensions = {
  catch(onRejected) {
    return function(...args) {
      let r;
      try {
        r = self.apply(this, args);
      } catch(error) {
        return onRejected(error);
      }
      return r;
    };
  },
  then(onFulfilled, onRejected) {
    const self = this;
    if(onRejected)
      return function(...args) {
        let r;
        try {
          r = self.apply(this, args);
        } catch(error) {
          return onRejected(error);
        }
        return onFulfilled(r);
      };
    return function(...args) {
      return onFulfilled(self.apply(this, args));
    };
  },
  finally(onFinally) {
    return this.then(
      value => {
        onFinally();
        return value;
      },
      error => {
        onFinally();
        throw error;
      },
    );
  },
};

export const FunctionPrototype = Object.getPrototypeOf(function () {}).constructor.prototype;
export const Function = FunctionPrototype.constructor;

export function extendFunction(proto = FunctionPrototype) {
  const d = Object.getOwnPropertyDescriptors(FunctionExtensions);

  for(const k in d) {
    d[k].enumerable = false;
    if(!d[k].get) d[k].writable = false;
  }

  Object.defineProperties(proto, d);
  return proto;
}

export default extendFunction;
