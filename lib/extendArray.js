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
  pushUnique(...args) {
    for(let arg of args) if(this.indexOf(arg) === -1) this.push(arg);
  },
  remove(predicate) {
    if(typeof predicate != 'function') {
      let needle=predicate;
      predicate = item => item == needle;
    }
    for(let i = this.length - 1; i >= 0; --i) {
      if(predicate(this[i], i, this))
        this.splice(i, 1);
    }
    return this;
  }
};

export function extendArray(proto = Array.prototype) {
  let desc = Object.getOwnPropertyDescriptors(ArrayExtensions);

  for(let name in desc) {
    desc[name].enumerable = false;
    if(!desc[name].get) desc[name].writable = false;
  }

  Object.defineProperties(proto, desc);
  return proto;
}

export default extendArray;
