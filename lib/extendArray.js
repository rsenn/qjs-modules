export function extendArray(proto = Array.prototype) {
  Object.defineProperties(proto, {
    contains: {
      value(item) {
        return this.indexOf(item) != -1;
      },
      configurable: true
    },
    last: {
      get() {
        return this[this.length - 1];
      },
      configurable: true
    },
    at: {
      value(index) {
        const { length } = this;
        return this[((index % length) + length) % length];
      },
      configurable: true
    },
    clear: {
      value() {
        this.splice(0, this.length);
      },
      configurable: true
    },
    findLastIndex: {
      value(predicate) {
        for(let i = this.length - 1; i >= 0; --i) {
          const x = this[i];
          if(predicate(x, i, this)) return i;
        }
        return -1;
      },
      configurable: true
    },
    findLast: {
      value(predicate) {
        let i;
        if((i = this.findLastIndex(predicate)) == -1) return null;
        return this[i];
      },
      configurable: true
    },
    unique: {
      value() {
        return [...new Set(this)];
      },
      configurable: true
    },
    pushUnique: {
      value(...args) {
        for(let arg of args) if(this.indexOf(arg) === -1) this.push(arg);
      },
      configurable: true
    }
  });
}

export default extendArray;
