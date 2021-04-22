export function extendArray(proto = Array.prototype) {
  Object.defineProperties(proto, {
    last: {
      get() {
        return this[this.length - 1];
      }
    },
    at: {
      value(index) {
        const { length } = this;
        return this[((index % length) + length) % length];
      }
    },
    clear: {
      value() {
        this.splice(0, this.length);
      }
    },
    findLastIndex: {
      value(predicate) {
        for(let i = this.length - 1; i >= 0; --i) {
          const x = this[i];
          if(predicate(x, i, this)) return i;
        }
        return -1;
      }
    },
    findLast: {
      value(predicate) {
        let i;
        if((i = this.findLastIndex(predicate)) == -1) return null;
        return this[i];
      }
    },
    unique: {
      value() {
        return [...new Set(this)];
      }
    }
  });
}

export default extendArray;
