export const ArrayPrototype = Array.prototype;
export const ArrayExtensions = {
  contains(item) {
    return this.indexOf(item) != -1;
  },
  get last() {
    return this[this.length - 1];
  },
  set last(value) {
    this[this.length - 1] = value;
  },
  at(index) {
    const { length } = this;
    return this[((index % length) + length) % length];
  },
  clear() {
    this.splice(0, this.length);
  },
  findLastIndex(predicate) {
    for(let i = this.length - 1; i >= 0; --i) {
      const x = this[i];
      if(predicate(x, i, this)) return i;
    }
    return -1;
  },
  findLast(predicate) {
    let i;

    if((i = ArrayPrototype.findLastIndex.call(this, predicate)) == -1) return null;

    return this[i];
  },
  unique() {
    return [...new Set(this)];
  },
  add(...items) {
    let ret = false;

    for(let item of items) {
      if(this.search(item) == -1) {
        ret = true;
        ArrayPrototype.push.call(this, item);
      }
    }

    return this;
  },
  search(item, pred = (a, b) => (a == b ? true : a && b && a.constructor && b.constructor && a.constructor === b.constructor && a.constructor.equal ? a.constructor.equal(a, b) : false)) {
    for(let i = 0; i < this.length; i++) if(pred(item, this[i])) return i;
    return -1;
  },
  pushIf(item, pred = (arr, item) => arr.indexOf(item) == -1) {
    if(pred(this, item)) ArrayPrototype.push.call(this, item);

    return this;
  },
  pushUnique(...args) {
    return args.reduce((acc, item) => (ArrayPrototype.indexOf.call(this, item) == -1 ? (ArrayPrototype.push.call(this, item), acc + 1) : acc), 0);
  },
  unshiftUnique(...args) {
    return args.reduce((acc, item) => (ArrayPrototype.indexOf.call(this, item) == -1 ? (ArrayPrototype.unshift.call(this, item), acc + 1) : acc), 0);
  },
  insert(item, sortFn = (a, b) => (a < b ? -1 : a > b ? 1 : 0)) {
    let index = ArrayPrototype.findIndex.call(this, item2 => sortFn(item, item2) <= 0);

    if(index == -1) index = this.length;

    this.splice(index, 0, item);
    return this;
  },
  inserter(pred = (arr, item) => arr.search(item) == -1) {
    return (...items) => {
      for(let item of items) if(pred(this, item)) ArrayPrototype.push.call(this, item);
      return this;
    };
  },
  delete(...items) {
    for(let item of items) {
      let index;
      if((index = ArrayPrototype.indexOf.call(this, item)) != -1) ArrayPrototype.splice.call(this, index, 1);
    }
    return this;
  },
  remove(...args) {
    const predicate = item => args.indexOf(item) != -1;
    return this.removeIf(predicate);
  },
  removeIf(predicate) {
    if(!(typeof predicate == 'function') && typeof predicate == 'object' && predicate != null && predicate instanceof RegExp) {
      let re = predicate;
      predicate = item => re.test(item);
    }

    for(let i = this.length - 1; i >= 0; --i) if(predicate(this[i], i, this)) ArrayPrototype.splice.call(this, i, 1);

    return this;
  },
  rotateRight(n) {
    ArrayPrototype.unshift.call(this, ...ArrayPrototype.splice.call(this, n, this.length - n));
    return this;
  },
  rotateLeft(n) {
    ArrayPrototype.push.call(this, ...ArrayPrototype.splice.call(this, 0, n));
    return this;
  },
  /* prettier-ignore */ get first() {
    return this[0];
  },
  /* prettier-ignore */ get head() {
    return this[0];
  },
  /* prettier-ignore */ get tail() {
    return this[this.length - 1];
  },
  /* prettier-ignore */ get last() {
    return this[this.length - 1];
  },
  match(callbackFn, thisArg) {
    if(typeof callbackFn == 'object' && callbackFn != null && callbackFn instanceof RegExp) {
      let re = callbackFn;
      callbackFn = item => typeof item == 'string' && re.test(item);
    }
    return ArrayPrototype.filter.call(this, callbackFn, thisArg);
  },
  group(callbackFn, thisArg) {
    let obj = {},
      i = 0;
    for(let item of this) (obj[callbackFn.call(thisArg, item, i++, this)] ??= []).push(item);
    return obj;
  },
  groupToMap(callbackFn, thisArg) {
    let map = new Map(),
      i = 0;

    for(let item of this) {
      let a,
        key = callbackFn.call(thisArg, item, i++, this);

      if(!map.has(key)) map.set(key, (a = []));
      else a = map.get(key);

      a.push(item);
    }

    return map;
  },
  equal(other) {
    const { length } = this;

    if(this === other) return true;
    if(length !== other.length) return false;

    for(let i = 0; i < length; i++) if(this[i] !== other[i]) return false;

    return true;
  }
};

export function extendArray(proto = ArrayPrototype) {
  let desc = Object.getOwnPropertyDescriptors(ArrayExtensions);

  for(let name in desc) {
    try {
      desc[name].enumerable = false;
      if(!desc[name].get) desc[name].writable = false;
      delete proto[name];
      Object.defineProperty(proto, name, desc[name]);
    } catch(e) {}
  }

  //Object.defineProperties(proto, desc);
  return proto;
}

export default extendArray;
