import { generate, isFunction } from 'util';

export const ObjectExtensions = {
  getMemberNames: generate(getMemberNames),
  getMethodNames(o) {
    const r = [];
    for(const k of getMemberNames(o, (k, o) => o != Object.prototype && isFunction(o[k]))) 
      r.push(k);
    return r;
  },
  getPropertyNames(o) {
    const r = [];
    for(const k of getMemberNames(o, (k, o) => k != '__proto__' && !isFunction(o[k]))) 
      r.push(k);
    return r;
  },
};

export const Object = Object;

export function extendObject(proto = Object) {
  const d = Object.getOwnPropertyDescriptors(ObjectExtensions);

  for(const k in d) {
    d[k].enumerable = false;
    if(!d[k].get) d[k].writable = false;
  }

  Object.defineProperties(proto, d);
  return proto;
}

function* getMemberNames(o, pred = (k, o) => true) {
  let r = [],
    depth = 0;

  while(o) {
    for(const k of Object.getOwnPropertyNames(o)) {
      if(r.indexOf(k) == -1) {
        let r;
        try {
          r = pred(k, o, depth);
        } catch(e) {}

        if(r) {
          yield k;
          r.push(k);
        }
      }
    }
    o = Object.getPrototypeOf(o);
  }
  return r;
}

export default extendObject;
