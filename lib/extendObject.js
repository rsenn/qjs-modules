export const ObjectExtensions = {
  getMethodNames(obj) {
    while(obj) {
      let result = [];
      let names = Object.getOwnPropertyNames(obj);
      for(let name of names) {
        if(typeof obj[name] == 'function') if (result.indexOf(name) == -1) result.push(name);
      }
      obj = Object.getPrototypeOf(obj);
    }
    return result;
  }
};

export const ObjectPrototype = Object.getPrototypeOf(function () {}).constructor.prototype;
export const ObjectConstructor = ObjectPrototype.constructor;

export function extendObject(proto = ObjectPrototype) {
  let desc = Object.getOwnPropertyDescriptors(ObjectExtensions);

  for(let name in desc) {
    desc[name].enumerable = false;
    if(!desc[name].get) desc[name].writable = false;
  }

  Object.defineProperties(proto, desc);
  return proto;
}

export default extendObject;
