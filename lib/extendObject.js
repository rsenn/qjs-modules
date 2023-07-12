import { generate, isFunction } from 'util';

export const ObjectExtensions = {
  getMemberNames: generate(getMemberNames),
  getMethodNames(obj) {
    let names = [];
    for(let name of getMemberNames(obj, (name, obj) => obj != Object.prototype && isFunction(obj[name]))) {
      names.push(name);
    }
    return names;
  },
  getPropertyNames(obj) {
    let names = [];
    for(let name of getMemberNames(obj, (name, obj) => name != '__proto__' && !isFunction(obj[name]))) {
      names.push(name);
    }
    return names;
  }
};

export const ObjectConstructor = Object;

export function extendObject(proto = ObjectConstructor) {
  let desc = Object.getOwnPropertyDescriptors(ObjectExtensions);

  for(let name in desc) {
    desc[name].enumerable = false;
    if(!desc[name].get) desc[name].writable = false;
  }

  Object.defineProperties(proto, desc);
  return proto;
}

function* getMemberNames(obj, pred = (name, obj) => true) {
  let names = [],
    depth = 0;
  while(obj) {
    for(let name of Object.getOwnPropertyNames(obj)) {
      if(names.indexOf(name) == -1) {
        let r;
        try {
          r = pred(name, obj, depth);
        } catch(e) {}

        if(r) {
          yield name;
          names.push(name);
        }
      }
    }
    obj = Object.getPrototypeOf(obj);
  }
}

export default extendObject;
