/**
 * Composable, serializable predicate/matcher objects.
 * Port of quickjs-predicate.c (native module).
 */

const TYPE_UNDEFINED = 1;
const TYPE_NULL = 2;
const TYPE_BOOL = 4;
const TYPE_INT = 8;
const TYPE_OBJECT = 16;
const TYPE_STRING = 32;
const TYPE_SYMBOL = 64;
const TYPE_BIG_INT = 128;
const TYPE_FLOAT64 = 256;
const TYPE_NUMBER = TYPE_INT | TYPE_BIG_INT | TYPE_FLOAT64;
const TYPE_PRIMITIVE = TYPE_UNDEFINED | TYPE_NULL | TYPE_BOOL | TYPE_STRING | TYPE_SYMBOL | TYPE_NUMBER;
const TYPE_ALL = TYPE_PRIMITIVE | TYPE_OBJECT;
const TYPE_FUNCTION = 1024;
const TYPE_ARRAY = 2048;

const PREDICATE_TYPE = 0;
const PREDICATE_CHARSET = 1;
const PREDICATE_STRING = 2;
const PREDICATE_NOTNOT = 3;
const PREDICATE_NOT = 4;
const PREDICATE_BNOT = 5;
const PREDICATE_SQRT = 6;
const PREDICATE_ADD = 7;
const PREDICATE_SUB = 8;
const PREDICATE_MUL = 9;
const PREDICATE_DIV = 10;
const PREDICATE_MOD = 11;
const PREDICATE_BOR = 12;
const PREDICATE_BAND = 13;
const PREDICATE_POW = 14;
const PREDICATE_ATAN2 = 15;
const PREDICATE_OR = 16;
const PREDICATE_AND = 17;
const PREDICATE_XOR = 18;
const PREDICATE_REGEXP = 19;
const PREDICATE_INSTANCEOF = 20;
const PREDICATE_PROTOTYPEIS = 21;
const PREDICATE_EQUAL = 22;
const PREDICATE_PROPERTY = 23;
const PREDICATE_HAS = 24;
const PREDICATE_MEMBER = 25;
const PREDICATE_SHIFT = 26;
const PREDICATE_SLICE = 27;
const PREDICATE_INDEX = 28;
const PREDICATE_FUNCTION = 29;
const PREDICATE_SOME = 30;
const PREDICATE_EVERY = 31;

function jsValueType(v) {
  if(v === undefined) return TYPE_UNDEFINED;
  if(v === null) return TYPE_NULL;
  if(typeof v == 'boolean') return TYPE_BOOL;
  if(typeof v == 'number') return Number.isInteger(v) ? TYPE_INT : TYPE_FLOAT64;
  if(typeof v == 'string') return TYPE_STRING;
  if(typeof v == 'symbol') return TYPE_SYMBOL;
  if(typeof v == 'bigint') return TYPE_BIG_INT;
  if(typeof v == 'function') return TYPE_FUNCTION;
  if(Array.isArray(v)) return TYPE_ARRAY | TYPE_OBJECT;
  return TYPE_OBJECT;
}

function jsValueTypeName(type) {
  switch (type) {
    case TYPE_UNDEFINED:
      return 'undefined';
    case TYPE_NULL:
      return 'null';
    case TYPE_BOOL:
      return 'bool';
    case TYPE_INT:
      return 'int';
    case TYPE_OBJECT:
      return 'object';
    case TYPE_STRING:
      return 'string';
    case TYPE_SYMBOL:
      return 'symbol';
    case TYPE_BIG_INT:
      return 'big_int';
    case TYPE_FLOAT64:
      return 'float64';
    case TYPE_NUMBER:
      return 'number';
    case TYPE_PRIMITIVE:
      return 'primitive';
    case TYPE_ALL:
      return 'all';
    case TYPE_FUNCTION:
      return 'function';
    case TYPE_ARRAY:
      return 'array';
    default:
      return 'unknown';
  }
}

function toNumber(v) {
  if(v === null) return 0;
  if(v === undefined) return NaN;
  if(typeof v == 'number') return v;
  if(typeof v == 'bigint') return Number(v);
  if(typeof v == 'boolean') return v ? 1 : 0;
  if(typeof v == 'string') return v.length ? parseFloat(v) : 0;
  if(Array.isArray(v)) return v.length;
  return NaN;
}

function toInt64(v) {
  const n = toNumber(v);
  return n | 0;
}

function wrap(left, right, op, opStr, tosrc) {
  const fn = (...args) => {
    const a = left === undefined ? args[0] : typeof left == 'function' ? left(...args) : left;
    const b = right === undefined ? args[1] : typeof right == 'function' ? right(...args) : right;
    return op(toNumber(a), toNumber(b));
  };
  const leftSrc = left === undefined ? '(null)' : typeof left == 'function' ? 'function' : left;
  const rightSrc = right === undefined ? '(null)' : typeof right == 'function' ? 'function' : right;
  return makePredicate(fn, `(${leftSrc} ${opStr} ${rightSrc})`, tosrc);
}

function makePredicate(fn, tostr, tosrc) {
  if(typeof fn == 'string') fn = new Function('arg', 'return ' + fn);
  return Object.defineProperties(Object.setPrototypeOf(fn, Predicate.prototype), {
    toString: { value: () => tostr },
    toSource: { value: (arg = 'arg') => tosrc(arg) },
    id: { get: () => fn._id, configurable: true },
    length: { get: () => fn._length, configurable: true },
  });
}

function unaryPredicate(value, id, name, tosrc) {
  const fn = (...args) => {
    const v = value === undefined ? args[0] : typeof value == 'function' ? value(...args) : value;
    return tosrc(v);
  };
  const src = value === undefined ? '(null)' : typeof value == 'function' ? 'function' : value;
  return makePredicate(fn, `Predicate.${name}(${src})`, tosrc);
}

function binaryPredicate(left, right, id, name, opStr, tosrc) {
  const fn = (...args) => {
    const a = left === undefined ? args[0] : typeof left == 'function' ? left(...args) : left;
    const b = right === undefined ? args[1] : typeof right == 'function' ? right(...args) : right;
    return opStr(toNumber(a), toNumber(b));
  };
  const leftSrc = left === undefined ? '(null)' : typeof left == 'function' ? 'function' : left;
  const rightSrc = right === undefined ? '(null)' : typeof right == 'function' ? 'function' : right;
  return makePredicate(fn, `(${leftSrc} ${opStr} ${rightSrc})`, tosrc);
}

export class Predicate extends Function {
  static property(name, pred) {
    return makePredicate(
      arg => {
        if(typeof arg == 'object' && arg != null && name in arg) return pred(arg[name]);
      },
      `.${name}`,
      (a = 'arg') => pred.toSource(`${a}.${name}`),
    );
  }

  static regexp(re, flags = '') {
    if(typeof re != 'object' || re == null || !(re instanceof RegExp)) re = new RegExp(re, flags);
    return makePredicate(
      arg => re.test(arg),
      `${re}`,
      (a = 'arg') => `${re}.test(${a})`,
    );
  }

  static string(str) {
    if(typeof str != 'string') str = str + '';
    return makePredicate(
      arg => arg == str || 0 == str.localeCompare(arg),
      `'${str}'`,
      (a = 'arg') => `arg => '${str}' == ${a}`,
    );
  }

  static and(...predicates) {
    return makePredicate(
      (...args) => predicates.every(pred => pred(...args)),
      predicates.map(pred => pred + '').join(' && '),
      (a = 'args...') => `(args...) => ` + predicates.map(pred => pred + '(...args)').join(' && '),
    );
  }

  static shift(offset, pred) {
    return makePredicate(
      (...args) => pred(args[offset]),
      `[${offset}]${pred}`,
      (a = 'args') => `(${a}) => ` + pred.toSource(`${a}[${offset}]`),
    );
  }

  // Full factory functions
  static type(flags) {
    const fn = arg => (jsValueType(arg) & flags) !== 0;
    const names = [];
    if(flags & TYPE_UNDEFINED) names.push('undefined');
    if(flags & TYPE_NULL) names.push('null');
    if(flags & TYPE_BOOL) names.push('bool');
    if(flags & TYPE_INT) names.push('int');
    if(flags & TYPE_OBJECT) names.push('object');
    if(flags & TYPE_STRING) names.push('string');
    if(flags & TYPE_SYMBOL) names.push('symbol');
    if(flags & TYPE_BIG_INT) names.push('big_int');
    if(flags & TYPE_FLOAT64) names.push('float64');
    return makePredicate(fn, `Predicate.type(type == ${names.join('|')})`, (a = 'value') => names.map(n => `typeof ${a} == '${n}'`).join(' || '));
  }

  static charset(set, len) {
    if(typeof set == 'string') len = set.length;
    const arr = Array.from(set || '');
    const fn = arg => {
      if(typeof arg != 'string') return false;
      return arr.every(c => arg.includes(c));
    };
    return makePredicate(fn, `Predicate.charset('${set}')`, (a = 'chars') => `'${set}'.indexOf(${a}) != -1`);
  }

  static not(value) {
    return unaryPredicate(value, PREDICATE_NOT, 'not', v => `!(${!!v})`);
  }

  static notnot(value) {
    return unaryPredicate(value, PREDICATE_NOTNOT, 'notnot', v => `!!(${!!v})`);
  }

  static bnot(value) {
    return unaryPredicate(value, PREDICATE_BNOT, 'bnot', v => ~toInt64(v));
  }

  static sqrt(value) {
    return unaryPredicate(value, PREDICATE_SQRT, 'sqrt', v => Math.sqrt(toNumber(v)));
  }

  static add(left, right) {
    return binaryPredicate(left, right, PREDICATE_ADD, 'add', '+', (a = 'arg') => `(${left === undefined ? '(null)' : left} + ${right === undefined ? '(null)' : right})`);
  }

  static sub(left, right) {
    return binaryPredicate(left, right, PREDICATE_SUB, 'sub', '-', (a = 'arg') => `(${left === undefined ? '(null)' : left} - ${right === undefined ? '(null)' : right})`);
  }

  static mul(left, right) {
    return binaryPredicate(left, right, PREDICATE_MUL, 'mul', '*', (a = 'arg') => `(${left === undefined ? '(null)' : left} * ${right === undefined ? '(null)' : right})`);
  }

  static div(left, right) {
    return binaryPredicate(left, right, PREDICATE_DIV, 'div', '/', (a = 'arg') => `(${left === undefined ? '(null)' : left} / ${right === undefined ? '(null)' : right})`);
  }

  static mod(left, right) {
    return binaryPredicate(left, right, PREDICATE_MOD, 'mod', '%', (a = 'arg') => `(${left === undefined ? '(null)' : left} % ${right === undefined ? '(null)' : right})`);
  }

  static bor(left, right) {
    return binaryPredicate(left, right, PREDICATE_BOR, 'bor', '|', (a = 'arg') => `(${left === undefined ? '(null)' : left} | ${right === undefined ? '(null)' : right})`);
  }

  static band(left, right) {
    return binaryPredicate(left, right, PREDICATE_BAND, 'band', '&', (a = 'arg') => `(${left === undefined ? '(null)' : left} & ${right === undefined ? '(null)' : right})`);
  }

  static pow(left, right) {
    return binaryPredicate(left, right, PREDICATE_POW, 'pow', '**', (a = 'arg') => `(${left === undefined ? '(null)' : left} ** ${right === undefined ? '(null)' : right})`);
  }

  static atan2(left, right) {
    return binaryPredicate(left, right, PREDICATE_ATAN2, 'atan2', ' atan2 ', (a = 'arg') => `Math.atan2(${left === undefined ? '(null)' : left}, ${right === undefined ? '(null)' : right})`);
  }

  static or(...predicates) {
    const fn = (...args) => predicates.some(pred => !!pred(...args));
    return makePredicate(fn, `Predicate.or(${predicates.join(', ')})`, (a = 'args...') => `(args...) => ` + predicates.map(p => `(${p}(...args))`).join(' || '));
  }

  static and(...predicates) {
    const fn = (...args) => predicates.every(pred => !!pred(...args));
    return makePredicate(fn, `Predicate.and(${predicates.join(', ')})`, (a = 'args...') => `(args...) => ` + predicates.map(p => `(${p}(...args))`).join(' && '));
  }

  static xor(...predicates) {
    const fn = (...args) => {
      let r = 0;
      for(const pred of predicates) r ^= toInt64(pred(...args));
      return r;
    };
    return makePredicate(fn, `Predicate.xor(${predicates.join(', ')})`, (a = 'args...') => `(args...) => ` + predicates.map(p => `(${p}(...args))`).join(' ^ '));
  }

  static instanceOf(ctor) {
    return unaryPredicate(ctor, PREDICATE_INSTANCEOF, 'instanceOf', v => v instanceof ctor);
  }

  static prototypeIs(proto) {
    return unaryPredicate(proto, PREDICATE_PROTOTYPEIS, 'prototypeIs', v => Object.getPrototypeOf(v) === proto);
  }

  static equal(value, deep = false) {
    const fn = arg => {
      if(deep && typeof value == 'object' && typeof arg == 'object') {
        return JSON.stringify(value) === JSON.stringify(arg);
      }
      return arg === value;
    };
    return makePredicate(fn, `Predicate.equal(${JSON.stringify(value)})`, (a = 'value') => `${a} == ${JSON.stringify(value)}`);
  }

  static property(name, pred) {
    const fn = arg => {
      if(arg == null || typeof arg != 'object') return undefined;
      if(!(name in arg)) return undefined;
      return pred ? pred(arg[name]) : arg[name];
    };
    return makePredicate(fn, `.${name}`, (a = 'arg') => (pred ? pred.toSource(`${a}.${name}`) : `${a}.${name}`));
  }

  static has(name) {
    const fn = arg => arg != null && typeof arg == 'object' && name in arg;
    return makePredicate(fn, `.${name}`, (a = 'arg') => `${a}.${name}`);
  }

  static member(obj, pred) {
    const fn = key => {
      if(obj == null || typeof obj != 'object') return undefined;
      if(!(key in obj)) return undefined;
      return pred ? pred(obj[key]) : obj[key];
    };
    return makePredicate(fn, `[${JSON.stringify(obj)}]`, (a = 'key') => (pred ? pred.toSource(`${a}`) : a));
  }

  static shift(offset, pred) {
    const fn = (...args) => pred(args[offset]);
    return makePredicate(fn, `[${offset}]${pred}`, (a = 'args') => `(${a}) => ${pred.toSource(`${a}[${offset}]`)}`);
  }

  static slice(start, end) {
    const fn = arg => {
      if(typeof arg == 'string') return arg.slice(start, end);
      if(Array.isArray(arg)) return arg.slice(start, end);
      if(ArrayBuffer.isView(arg)) return new arg.constructor(arg.buffer, arg.byteOffset + start, end - start);
      return undefined;
    };
    return makePredicate(fn, `.slice(${start}, ${end})`, (a = 'value') => `${a}.slice(${start}, ${end})`);
  }

  static index(pos, pred) {
    const fn = arg => {
      if(!arg || !arg.length) return undefined;
      const len = arg.length;
      const idx = pos < 0 ? len + (pos % len) : pos % len;
      return pred ? pred(arg[idx]) : arg[idx];
    };
    return makePredicate(fn, `@${pos}`, (a = 'arr') => (pred ? pred.toSource(`${a}[${pos}]`) : `${a}[${pos}]`));
  }

  static function(func, thisObj, arity) {
    const fn = (...args) => func.apply(thisObj, args.slice(0, arity));
    const src = func.toString();
    return makePredicate(fn, src, (a = 'args...') => src);
  }

  static some(pred) {
    const fn = arg => {
      if(!Array.isArray(arg)) return false;
      return arg.some((v, i) => pred(v, i, arg));
    };
    return makePredicate(fn, 'some', (a = 'arr') => `(${a}).some(${pred})`);
  }

  static every(pred) {
    const fn = arg => {
      if(!Array.isArray(arg)) return true;
      return arg.every((v, i) => pred(v, i, arg));
    };
    return makePredicate(fn, 'every', (a = 'arr') => `(${a}).every(${pred})`);
  }
}

// Static constants
Predicate.TYPE = PREDICATE_TYPE;
Predicate.CHARSET = PREDICATE_CHARSET;
Predicate.STRING = PREDICATE_STRING;
Predicate.NOTNOT = PREDICATE_NOTNOT;
Predicate.NOT = PREDICATE_NOT;
Predicate.BNOT = PREDICATE_BNOT;
Predicate.SQRT = PREDICATE_SQRT;
Predicate.ADD = PREDICATE_ADD;
Predicate.SUB = PREDICATE_SUB;
Predicate.MUL = PREDICATE_MUL;
Predicate.DIV = PREDICATE_DIV;
Predicate.MOD = PREDICATE_MOD;
Predicate.BOR = PREDICATE_BOR;
Predicate.BAND = PREDICATE_BAND;
Predicate.POW = PREDICATE_POW;
Predicate.ATAN2 = PREDICATE_ATAN2;
Predicate.OR = PREDICATE_OR;
Predicate.AND = PREDICATE_AND;
Predicate.XOR = PREDICATE_XOR;
Predicate.REGEXP = PREDICATE_REGEXP;
Predicate.INSTANCEOF = PREDICATE_INSTANCEOF;
Predicate.PROTOTYPEIS = PREDICATE_PROTOTYPEIS;
Predicate.EQUAL = PREDICATE_EQUAL;
Predicate.PROPERTY = PREDICATE_PROPERTY;
Predicate.HAS = PREDICATE_HAS;
Predicate.MEMBER = PREDICATE_MEMBER;
Predicate.SHIFT = PREDICATE_SHIFT;
Predicate.SLICE = PREDICATE_SLICE;
Predicate.INDEX = PREDICATE_INDEX;
Predicate.FUNCTION = PREDICATE_FUNCTION;
Predicate.SOME = PREDICATE_SOME;
Predicate.EVERY = PREDICATE_EVERY;

// Type flags
Predicate.TYPE_UNDEFINED = TYPE_UNDEFINED;
Predicate.TYPE_NULL = TYPE_NULL;
Predicate.TYPE_BOOL = TYPE_BOOL;
Predicate.TYPE_INT = TYPE_INT;
Predicate.TYPE_OBJECT = TYPE_OBJECT;
Predicate.TYPE_STRING = TYPE_STRING;
Predicate.TYPE_SYMBOL = TYPE_SYMBOL;
Predicate.TYPE_BIG_INT = TYPE_BIG_INT;
Predicate.TYPE_FLOAT64 = TYPE_FLOAT64;
Predicate.TYPE_NUMBER = TYPE_NUMBER;
Predicate.TYPE_PRIMITIVE = TYPE_PRIMITIVE;
Predicate.TYPE_ALL = TYPE_ALL;
Predicate.TYPE_FUNCTION = TYPE_FUNCTION;
Predicate.TYPE_ARRAY = TYPE_ARRAY;

// Instance methods
Predicate.prototype.eval = function(...args) {
  return this(...args);
};
Predicate.prototype.call = Predicate.prototype.eval;
Predicate.prototype.keys = function() {
  return [];
};
Predicate.prototype.values = function() {
  return [];
};
Predicate.prototype.toString = function() {
  return this.toString();
};
Predicate.prototype.toSource = function() {
  return this.toSource();
};
Predicate.prototype[Symbol.toStringTag] = 'Predicate';
