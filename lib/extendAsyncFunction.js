import { isAsync, isFunction, define, nonenumerable } from './util.js';

export const AsyncFunctionExtensions = nonenumerable({});

export const AsyncFunctionPrototype = Object.getPrototypeOf(async function() {}).constructor.prototype;
export const AsyncFunction = AsyncFunctionPrototype.constructor;

export function extendAsyncFunction(proto = AsyncFunctionPrototype) {
  return define(proto, AsyncFunctionExtensions);
}

export default extendAsyncFunction;
