import { isFunction, isInstanceOf, isString, nonenumerable, weakDefine } from 'util';

export const ArrayPrototype = Array.prototype;

const { includes, filter, findIndex, findLastIndex, indexOf, push, splice } = ArrayPrototype;

export const ArrayExtensions = nonenumerable({
  get front() {
    return this[0];
  },
  set front(value) {
    if(this.length) this[0] = value;
  },
  get back() {
    if(this.length) return this[this.length - 1];
  },
  set back(value) {
    if(this.length) this[this.length - 1] = value;
  },
  at(i) {
    return this[i < 0 ? i + this.length : i];
  },
  clear() {
    splice.call(this, 0, this.length);
  },
  findLastIndex(pred) {
    for(let i = this.length - 1; i >= 0; --i) {
      const x = this[i];
      if(pred(x, i, this)) return i;
    }
    return -1;
  },
  findLast(pred) {
    let i;
    if((i = this.findLastIndex(pred)) == -1) return null;
    return this[i];
  },
  unique() {
    return [...new Set(this)];
  },
  add(...items) {
    let r = false;
    for(const e of items) {
      if(ArrayExtensions.search.call(this, e) == -1) {
        r = true;
        push.call(this, e);
      }
    }
    return this;
  },
  search(item, compare = (a, b) => a === b) {
    return findIndex.call(this, item2 => compare(item, item2));
  },
  pushIf(item, pred = (arr, item) => !includes.call(arr, item)) {
    if(pred(this, e)) push.call(this, item);
    return this;
  },
  pushUnique(...args) {
    return args.reduce((acc, e) => (includes.call(this, e) ? acc : (push.call(this, e), acc + 1)), 0);
  },
  unshiftUnique(...args) {
    return args.reduce((acc, e) => (includes.call(this, e) ? acc : (unshift.call(this, e), acc + 1)), 0);
  },
  insert(item, sortFn = (a, b) => (a < b ? -1 : a > b ? 1 : 0)) {
    let i = findIndex.call(this, item2 => sortFn(item, item2) <= 0);
    if(i == -1) i = this.length;
    splice.call(this, i, 0, item);
    return this;
  },
  inserter(pred = (arr, item) => !includes.call(arr, item)) {
    return (...items) => {
      for(const item of items) if(pred(this, item)) push.call(this, item);
      return this;
    };
  },
  delete(...items) {
    for(const e of items) {
      let i;
      while((i = indexOf.call(this, e)) != -1) splice.call(this, i, 1);
    }
    return this;
  },
  remove(...args) {
    return ArrayExtensions.removeIf.call(this, item => includes.call(args, item));
  },
  removeIf(pred) {
    if(!isFunction(pred) && isInstanceOf(RegExp, pred)) {
      const re = pred;
      pred = e => isString(e) && re.test(e);
    }
    for(let i = this.length - 1; i >= 0; --i) if(pred(this[i], i, this)) splice.call(this, i, 1);
    return this;
  },
  rotateRight(n) {
    unshift.apply(this, splice.call(this, n, this.length - n));
    return this;
  },
  rotateLeft(n) {
    push.apply(this, splice.call(this, 0, n));
    return this;
  },
  get head() {
    return this[0];
  },
  set head(value) {
    this[0] = value;
  },
  get tail() {
    return this[(this.length || 1) - 1];
  },
  set tail(value) {
    this[(this.length || 1) - 1] = value;
  },
  match(pred, thisArg) {
    if(!isFunction(pred) && isInstanceOf(RegExp, pred)) {
      const re = pred;
      pred = e => isString(e) && re.test(e);
    }
    return filter.call(this, pred, thisArg);
  },
  group(pred, thisArg) {
    let obj = {},
      i = 0;
    for(const e of this) (obj[pred.call(thisArg, e, i++, this)] ??= []).push(e);
    return obj;
  },
  groupToMap(pred, thisArg) {
    let map = new Map(),
      i = 0;
    for(const e of this) {
      let a,
        key = pred.call(thisArg, e, i++, this);
      if(!map.has(key)) map.set(key, (a = []));
      else a = map.get(key);
      a.push(e);
    }
    return map;
  },
  equal(other) {
    const { length } = this;
    if(this === other) return true;
    if(length !== other.length) return false;
    for(let i = 0; i < length; i++) if(this[i] !== other[i]) return false;
    return true;
  },
  partition(length) {
    const result = [];
    for(let i = 0; i < this.length; i++) {
      if(i % length === 0) result.push([]);
      result[result.length - 1].push(this[i]);
    }
    return result;
  },
});

export function extendArray(proto = ArrayPrototype) {
  return weakDefine(proto, ArrayExtensions);
}

export default extendArray;
