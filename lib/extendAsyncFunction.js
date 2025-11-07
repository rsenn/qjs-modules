import { isAsync, isFunction, weakDefine, nonenumerable } from './util.js';

export const AsyncFunctionExtensions = nonenumerable({});

export const AsyncFunctionPrototype = Object.getPrototypeOf(async function() {}).constructor.prototype;
export const AsyncFunction = AsyncFunctionPrototype.constructor;

export function extendAsyncFunction(proto = AsyncFunctionPrototype) {
  return weakDefine(proto, AsyncFunctionExtensions);
}

export default extendAsyncFunction;
