export function describeObject(obj, opts = {}) {
  if(obj === null || (typeof obj !== 'object' && typeof obj !== 'function')) throw new TypeError('describeObject expects an object or function');

  function paramNames(fn) {
    const src = Function.prototype.toString.call(fn);
    const match = src.match(/^[^(]*\(([^)]*)\)/);
    if(!match) return [];
    return match[1]
      .split(',')
      .map(p => p.trim())
      .filter(Boolean)
      .map(p => p.replace(/=.*$/, '').replace(/\{.*$/, '{...}').replace(/\[.*$/, '[...]').trim());
  }

  function describeFunction(fn, key) {
    const src = Function.prototype.toString.call(fn);
    return {
      name: key,
      kind: /^class\s/.test(src) ? 'class' : /^async\s*\*/.test(src) ? 'async-generator' : /^\s*\*/.test(src) ? 'generator' : /^async\s/.test(src) ? 'async' : 'function',
      params: paramNames(fn),
      arity: fn.length,
    };
  }

  function symbolName(sym) {
    for(const key of Object.getOwnPropertyNames(Symbol))
      if(Symbol[key] === sym) return `Symbol.${key}`;

    return sym.toString();
  }

  function describeMembers(o) {
    const skip = typeof o === 'function' ? ['constructor', 'prototype', 'length', 'name'] : ['constructor'];
    const members = { methods: [], getters: [], setters: [], fields: [] };

    function process(key, label) {
      const desc = Object.getOwnPropertyDescriptor(o, key);
      if(desc.get || desc.set) {
        if(desc.get) members.getters.push(label);
        if(desc.set) members.setters.push(label);
      } else if(typeof desc.value === 'function') {
        members.methods.push(describeFunction(desc.value, label));
      } else {
        members.fields.push({ name: label, type: typeof desc.value, value: desc.value });
      }
    }

    for(const key of Object.getOwnPropertyNames(o)) {
      if(skip.includes(key)) continue;
      process(key, key);
    }

    for(const sym of Object.getOwnPropertySymbols(o))
      process(sym, symbolName(sym));

    return members;
  }

  const result = {
    name: (typeof obj === 'function' ? obj.name : obj.constructor?.name) || '(anonymous)',
    type: typeof obj,
    ...describeMembers(obj),
    prototypeChain: [],
  };

  if(typeof obj === 'function') result.constructorParams = paramNames(obj);

  let proto = Object.getPrototypeOf(obj), depth = 0;

  while(proto && proto !== Object.prototype && proto !== Function.prototype && depth < 20) {
    result.prototypeChain.push({
      level: depth,
      constructorName: proto.constructor?.name || '(anonymous)',
      ...describeMembers(proto),
    });
    proto = Object.getPrototypeOf(proto);
    depth++;
  }

  return result;
}
