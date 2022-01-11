export const ArrayExtensions = {
  contains(item) {
    return this.indexOf(item) != -1;
  },
  /* prettier-ignore */ get last() {
        return this[this.length - 1];
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
    if((i = this.findLastIndex(predicate)) == -1) return null;
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
        this.push(item);
      }
    }
    return this;
  },
  search(
    item,
    pred = (a, b) =>
      a == b
        ? true
        : a && b && a.constructor && b.constructor && a.constructor === b.constructor && a.constructor.equal
        ? a.constructor.equal(a, b)
        : false
  ) {
    for(let i = 0; i < this.length; i++) if(pred(item, this[i])) return i;
    return -1;
  },

  pushIf(item, pred = (arr, item) => arr.indexOf(item) == -1) {
    if(pred(this, item)) this.push(item);

    return this;
  },
  inserter(pred = (arr, item) => arr.search(item) == -1) {
    return (...items) => {
      for(let item of items) {
        if(pred(this, item)) this.push(item);
      }
      return this;
    };
  },
  delete(...items) {
    for(let item of items) {
      let index;
      if((index = this.indexOf(item)) != -1) this.splice(index, 1);
    }
    return this;
  },
  remove(...args) {
    const predicate = item => args.indexOf(item) != -1;
    return this.removeIf(predicate);
  },
  removeIf(predicate) {
    if(!(typeof predicate == 'function') && isObject(predicate) && predicate instanceof RegExp) {
      let re = predicate;
      predicate = item => re.test(item);
    }
    for(let i = this.length - 1; i >= 0; --i) if(predicate(this[i], i, this)) this.splice(i, 1);
    return this;
  },
  rotateRight(n) {
    this.unshift(...this.splice(n, this.length - n));
    return this;
  },
  rotateLeft(n) {
    this.push(...this.splice(0, n));
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
  }
};

export function extendArray(proto = Array.prototype) {
  let desc = Object.getOwnPropertyDescriptors(ArrayExtensions);

  for(let name in desc) {
    try {
      desc[name].enumerable = false;
      if(!desc[name].get) desc[name].writable = false;
      delete proto[name];
    } catch(e) {}
  }

  Object.defineProperties(proto, desc);
  return proto;
}

export default extendArray;
