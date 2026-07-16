function describeClass(Ctor, opts = {}) {
  if(typeof Ctor !== 'function') throw new TypeError('describeClass expects a class/constructor function');

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
    for(const key of Object.getOwnPropertyNames(Symbol)) {
      if(Symbol[key] === sym) return `Symbol.${key}`;
    }
    return sym.toString();
  }

  function describeMembers(obj) {
    const skip = typeof obj === 'function' ? ['constructor', 'prototype', 'length', 'name'] : ['constructor'];
    const members = { methods: [], getters: [], setters: [], fields: [] };

    function process(key, label) {
      const desc = Object.getOwnPropertyDescriptor(obj, key);
      if(desc.get || desc.set) {
        if(desc.get) members.getters.push(label);
        if(desc.set) members.setters.push(label);
      } else if(typeof desc.value === 'function') {
        members.methods.push(describeFunction(desc.value, label));
      } else {
        members.fields.push({ name: label, type: typeof desc.value, value: desc.value });
      }
    }

    for(const key of Object.getOwnPropertyNames(obj)) {
      if(skip.includes(key)) continue;
      process(key, key);
    }
    for(const sym of Object.getOwnPropertySymbols(obj)) {
      process(sym, symbolName(sym));
    }
    return members;
  }

  const result = {
    name: Ctor.name || '(anonymous)',
    constructorParams: paramNames(Ctor),
    staticChain: [],
    prototypeChain: [],
  };

  let sctor = Ctor;
  let sdepth = 0;
  while(sctor && sctor !== Function.prototype && sdepth < 20) {
    result.staticChain.push({
      level: sdepth,
      constructorName: sctor.name || '(anonymous)',
      ...describeMembers(sctor),
    });
    sctor = Object.getPrototypeOf(sctor);
    sdepth++;
  }

  let proto = Ctor.prototype;
  let depth = 0;
  while(proto && proto !== Object.prototype && depth < 20) {
    result.prototypeChain.push({
      level: depth,
      constructorName: proto.constructor?.name || '(anonymous)',
      ...describeMembers(proto),
    });
    proto = Object.getPrototypeOf(proto);
    depth++;
  }

  if(opts.instance) {
    result.instanceFields = describeMembers(opts.instance).fields;
  }

  return result;
}
