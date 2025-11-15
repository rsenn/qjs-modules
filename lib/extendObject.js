import { define, prototypeIterator, generate, isFunction, keys, extend, isString, isSymbol } from 'util';

export const ObjectExtensions = {};

export const ObjectStatic = {
  getMemberNames(o, start, end) {
    return keys(o, start, end).filter(k => !isSymbol(k) && k != '__proto__');
  },
  getMemberSymbols(o, start, end) {
    return keys(o, start, end).filter(k => isSymbol(k));
  },
  getMethodNames(o, start, end) {
    return keys(o, start, end).filter(k => !isSymbol(k) && k != '__proto__' && isFunction(o[k]));
  },
  getMethodSymbols(o, start, end) {
    return keys(o, start, end).filter(k => isSymbol(k) && isFunction(o[k]));
  },
  getPropertyNames(o, start, end) {
    return keys(o, start, end).filter(k => !isSymbol(k) && k != '__proto__' && !isFunction(o[k]));
  },
  getPropertySymbols(o, start, end) {
    return keys(o, start, end).filter(k => isSymbol(k) && !isFunction(o[k]));
  },
  getPropertyDescriptor(o, name, start = 0, end = Infinity) {
    return [...prototypeIterator(o, start, end)].reduce((acc, desc) => acc ?? Object.getOwnPropertyDescriptor(desc, name), null);
  },
  getPropertyDescriptors(o, start, end) {
    return [...prototypeIterator(o, start, end)].reduceRight((acc, desc) => Object.assign(acc, Object.getOwnPropertyDescriptors(desc)), {});
  },
};

export function extendObject(proto = Object.prototype, ctor = Object) {
  extend(proto, ObjectExtensions);
  extend(ctor, ObjectStatic);
}

export default extendObject;
