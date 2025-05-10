function isConstructor(val) {
  return typeof val == 'function' && 'prototype' in val;
}

function isArrowFunction(val) {
  return typeof val == 'function' && /\s=>\s/gs.test(val + '');
}

/* prettier-ignore */ class JSValue { constructor(value, type) { if(value !== undefined) this.value = value; if(type) this.type = type; } }
/* prettier-ignore */ class JSNumber extends JSValue { constructor(value, type) { super(value, type); } }
/* prettier-ignore */ class JSString extends JSValue { constructor(value) { super(value); } }
/* prettier-ignore */ class JSBoolean extends JSValue { constructor(value) { super(value); } }
/* prettier-ignore */ class JSSymbol extends JSValue { constructor(value, global) { super(value); if(global) this.global = global; } }
/* prettier-ignore */ class JSRegExp extends JSValue { constructor(source, flags) { super(); this.source = source; this.flags = flags; } }
/* prettier-ignore */ class JSObject extends JSValue { constructor(members, proto) { super(); if(members) this.members = members; if(proto) this.proto = proto; } }
/* prettier-ignore */ class JSArray extends JSObject { constructor(arr) { super(arr ? [...arr] : undefined); } }
/* prettier-ignore */ class JSTypedArray extends JSObject { constructor(arr) { super(arr ? [...arr] : undefined); this.class = (arr?.[Symbol.toStringTag] ?? arr?.constructor?.name); } }
/* prettier-ignore */ class JSFunction extends JSObject { constructor(code, members, proto) { super(members,proto); if(code) this.code = code; } }
/* prettier-ignore */ class JSConstructor extends JSFunction { constructor(code, members, proto) { super(code, members, proto); } }
/* prettier-ignore */ class JSProperty   { constructor(get, set) { if(get) this.get = get;  if(set) this.set = set; } }

Object.setPrototypeOf(JSValue.prototype, null);

Symbol.proto = Symbol.base = Symbol.for('proto');

define(JSValue.prototype, {
  [Symbol.toStringTag]: 'JSValue',
  toJSON() {
    const obj = Object.setPrototypeOf({ ...this }, null);
    if('type' in this) obj.type = this.type;
    if(Symbol.toStringTag in this) obj.class = this[Symbol.toStringTag];
    if(Symbol.for('proto') in this) {
      obj._proto_ = this[Symbol.for('proto')]?.toJSON();
      delete obj[Symbol.for('proto')];
    }
    return obj;
  },
});
/* prettier-ignore */ define(JSNumber.prototype, { [Symbol.toStringTag]: 'JSNumber', type: 'number' });
/* prettier-ignore */ define(JSString.prototype, { [Symbol.toStringTag]: 'JSString', type: 'string'  });
define(JSBoolean.prototype, { [Symbol.toStringTag]: 'JSBoolean', type: 'boolean' });
define(JSSymbol.prototype, { [Symbol.toStringTag]: 'JSSymbol', type: 'symbol' });
define(JSRegExp.prototype, { [Symbol.toStringTag]: 'JSRegExp', type: 'regexp' });
define(JSObject.prototype, { [Symbol.toStringTag]: 'JSObject', type: 'object' });
define(JSArray.prototype, { [Symbol.toStringTag]: 'JSArray', type: 'array' });
define(JSTypedArray.prototype, { [Symbol.toStringTag]: 'JSTypedArray', type: 'typedarray', class: undefined });
define(JSFunction.prototype, { [Symbol.toStringTag]: 'JSFunction', type: 'function' });
define(JSConstructor.prototype, { [Symbol.toStringTag]: 'JSConstructor', type: 'constructor' });
define(JSProperty.prototype, { [Symbol.toStringTag]: 'JSProperty', type: 'property' });

export { JSValue, JSNumber, JSString, JSBoolean, JSSymbol, JSRegExp, JSObject, JSArray, JSTypedArray, JSFunction, JSConstructor, JSProperty };

const C = globalThis?.console?.config ? (depth = 2, opt = {}) => console.config({ depth, compact: depth - 1, ...opt }) : () => '';

const TypedArrayPrototype = Object.getPrototypeOf(Uint32Array.prototype);
const TypedArrayConstructor = TypedArrayPrototype.constructor;

export { TypedArrayPrototype, TypedArrayConstructor };

export function hasPrototype(obj, proto) {
  const ws = new WeakSet();
  while((obj = Object.getPrototypeOf(obj))) {
    if(ws.has(obj)) throw new Error(`hasPrototype loop`);
    if(obj == proto) return true;
    ws.add(obj);
  }
  return false;
}

export function makeFunctionWithArgs(code, args = []) {
  return `((${args.join(', ')}) => (${code}))`;
}

export function* getKeys(obj, t = (desc, key) => true) {
  const a = Object.getOwnPropertyDescriptors(obj);
  for(let key in a) {
    const desc = a[key];
    if(t(desc, key, a)) yield key;
  }
}

function cleanFunctionCode(s) {
  if(s) return (s + '').replace(/^(function\s*|)(get\s*|)(set\s*|)(.*)/gs, 'function $4').replace(/\[native code\]/gs, '');
}

export function EncodeJS(val, stack = [], mapFn = () => {}) {
  const type = typeof val;
  let info;

  switch (type) {
    case 'function':
    case 'object':
      if(type == 'object' && val === null) {
        info = new JSValue();
        info.type = 'null';
        break;
      }
      const is_function = type == 'function';
      const is_constructor = isConstructor(val) || (is_function && 'prototype' in val);
      const is_array = hasPrototype(val, Array.prototype) && val !== Array.prototype;
      const is_typedarray = hasPrototype(val, TypedArrayPrototype) && val !== TypedArrayPrototype;
      const is_arraybuffer = hasPrototype(val, ArrayBuffer.prototype) && val !== ArrayBuffer.prototype;
      const is_object = !(is_array || is_typedarray);
      const is_circular = stack.indexOf(val) != -1;
      if(is_circular) return Symbol.for('circular');

      if(is_function) {
        const is_class = /^[^\n]*\bclass\b/g.test(val + '');
        const getCtorCode = (str, m) => ((m ??= /^class\s.*\n([\s]*)\b(constructor\s*\(.*)/gs.exec(str)), m ? extractCtor(m) : undefined);
        function extractCtor([, ws, ctor]) {
          const needle = '\n' + ws + '}';
          const index = ctor.indexOf(needle);
          if(index != -1) {
            let s = ctor.slice(0, index + needle.length);
            s = s.replace(/^\s*constructor\b/g, 'function ' + val.name);
            return s;
          }
        }
        const is_arrowfunction = /^[^\n]*=>/gs.test(val + '');
        const code = is_class ? /*getCtorCode*/ val + '' ?? `function ${val.name}() {}` : is_arrowfunction ? val + '' /*.replace(/^\((.*)\)\s*=>\s/, '($1) => ')*/ : cleanFunctionCode(val);
        info = new (is_constructor ? JSConstructor : JSFunction)();
        define(info, { code });
        if('name' in val) info.name = val.name;
        if('length' in val) info.length = val.length;
      } else if(val instanceof RegExp) {
        info = new JSRegExp(val.source, val.flags);
      } else if(is_array || is_typedarray) {
        info = new (is_typedarray ? JSTypedArray : JSArray)(is_typedarray ? null : [...val].map(v => EncodeJS(v, [...stack, val], mapFn)));
      } else {
        info = new JSObject();
      }

      const tag = val?.[Symbol.toStringTag] ?? val?.constructor?.name;
      const proto = Object.getPrototypeOf(val);

      if(tag) define(info, { class: tag });
      if(is_arraybuffer) info.data = [...new Uint8Array(val)].reduce((a, n) => (a ? a + ',' : '') + n.toString(16).padStart(2, '0'), '');
      if(is_typedarray) (info.members ??= {}).buffer = EncodeJS(val.buffer, [...stack, val], mapFn);
      if(!is_function || is_constructor || is_object) {
        const keys = [...getKeys(val, (desc, key) => true)];
        if(!('members' in info))
          if(keys.length > 0) {
            Object.assign(info, {
              members: EncodeObj(val, (key, desc) => (is_function ? ['length', 'name'].indexOf(key) == -1 : true), [...stack, val], mapFn),
            });
          }
      }

      if(proto && (!is_function || is_constructor) && [Object.prototype, Function.prototype, val].indexOf(proto) == -1)
        Object.assign(info, { [Symbol.for('proto')]: EncodeJS(proto, [...stack, val], mapFn) });

      break;
    case 'number':
    case 'bigfloat':
    case 'bigint':
    case 'bigdecimal':
      info = new JSNumber(val, type == 'number' ? undefined : type);
      break;
    case 'string':
      info = new JSString(val);
      break;
    case 'boolean':
      info = new JSBoolean(val + '');
      break;
    case 'undefined':
      info = new JSValue();
      info.type = type;
      break;
    case 'symbol':
      const str = val.toString();
      if(/Symbol\(Symbol\.([^)]*)\)/.test(str)) info = new JSSymbol(undefined, str.replace(/Symbol\(Symbol\.([^)]*)\)/g, '$1'));
      else info = new JSSymbol(str.replace(/Symbol\(([^)]*)\)/g, '$1'));
      break;
    default:
      info = new JSValue();
      info.type = type;
      info.value = val + '';
      break;
  }

  mapFn(info, val);

  return info;
}

export function EncodeObj(obj, keys, stack = [], mapFn) {
  const members = [];
  const props = Object.getOwnPropertyDescriptors(obj);
  const is_function = typeof keys == 'function';
  const fn = is_function && keys;

  if(!keys || is_function) keys = Object.getOwnPropertyNames(props).concat(Object.getOwnPropertySymbols(props));

  let i = 0;
  const n = keys.length;

  for(let k of keys) {
    ++i;
    const a = props[k];
    if(fn && !fn(k, props)) continue;
    let r;
    if(a.get || a.set) {
      r = new JSProperty(cleanFunctionCode(a.get), cleanFunctionCode(a.set));
    } else r = EncodeJS(obj[k], stack, mapFn);
    try {
      if(a.enumerable === false) define(r, { enumerable: a.enumerable });
      if(a.configurable === false) define(r, { configurable: a.configurable });
      if(!('value' in a) && 'writable' in a) define(r, { writable: a.writable });
    } catch(e) {}
    members.push([EncodeJS(k, stack, mapFn), r]);
  }

  return members;
}

export function DecodeJS(info) {
  let val;

  switch (info.type) {
    case 'constructor':
    case 'array':
    case 'function':
    case 'object':
      if('code' in info /*info.type == 'function'*/)
        try {
          val = eval(info.code.startsWith('function') ? '(' + info.code + ')' : info.code);
        } catch(e) {
          console.log('eval', C(1, { maxStringLength: Infinity }), { message: e.message, code: info.code });
          throw e;
        }
      else if(info.type == 'array') {
        if(info.class && info.class != 'Array' && info.members.buffer) {
          const { buffer, ...members } = info.members;
          info.members = members;
          val = new globalThis[info.class](DecodeJS(buffer));
        } else if(Array.isArray(info.members)) val = [...info.members].map(i => DecodeJS(i));
      } else if(info.class == 'ArrayBuffer') val = new Uint8Array(info.data.split(',').map(s => parseInt(s, 16))).buffer;

      val ||= {};

      if(info.members) {
        const props = {};

        for(const [k, v] of info.members) {
          const key = DecodeJS(k);

          if(typeof v == 'object' && v != null && !('value' in v) /*v.get || v.set*/) {
            const prop = { enumerable: true, configurable: true };
            if(v.get) prop.get = eval('(' + v.get + ')');
            if(v.set) prop.set = eval('(' + v.set + ')');
            if(prop.flags) prop = { ...prop, ...prop.flags };
            props[key] = prop;
          } else {
            const value = DecodeJS(v);
            props[key] = { enumerable: true, writable: true, configurable: true, value };
            console.log('DecodeJS(1)', C(2), { key, v });
          }
          try {
            Object.defineProperty(val, key, props[key]);
          } catch(e) {
            console.log('DecodeJS(2)', C(5, { reparseable: true, getters: false }), { key, desc: props[key] });
          }
        }
        //console.log('DecodeJS', C(Infinity, { reparseable: true, getters: false }), { val });
        //Object.defineProperties(val, props);
      }
      break;
    case 'boolean':
      val = info.value == 'true' ? true : false;
      break;
    case 'bigint':
      val = BigInt(info.value);
      break;
    case 'bigfloat':
      val = BigFloat(info.value);
      break;
    case 'bigdecimal':
      val = BigDecimal(info.value);
      break;
    case 'number':
      val = Number(info.value);
      break;
    case 'string':
      val = info.value + '';
      break;
    case 'regexp':
      val = new RegExp(info.source, info.flags);
      break;
    case 'symbol':
      val = info.global ? Symbol[info.global] : Symbol.for(info.value);
      break;
    case 'undefined':
      val = undefined;
      break;
    case 'null':
      val = null;
      break;
    default:
      val = info.value;
      break;
  }

  return val;
}

function define(obj, ...args) {
  for(let other of args) {
    const props = {},
      desc = Object.getOwnPropertyDescriptors(other),
      keys = [...Object.getOwnPropertyNames(other), ...Object.getOwnPropertySymbols(other)];
    for(let k of keys) props[k] = { enumerable: false, configurable: true, writable: true, value: desc[k].value };
    Object.defineProperties(obj, props);
  }
  return obj;
}
