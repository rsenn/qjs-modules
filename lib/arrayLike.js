export class ArrayLike {
  at(index) {
    if(index < 0) index += this.length;
    return this[index];
  }

  push(...args) {
    let n = this.length;
    for(const arg of args) this[n++] = arg;
    if(this.length !== n) this.length = n;
  }

  pop() {
    const n = this.length - 1;
    const r = this[n];
    delete this[n];
    if(this.length !== n) this.length = n;
    return r;
  }

  splice(start, deleteCount, ...items) {
    return Array.prototype.splice.call(this, start, deleteCount, ...items);
  }

  indexOf(value, fromIndex = 0) {
    if(fromIndex < -this.length) return -1;
    for(let i = fromIndex + (fromIndex < 0 ? this.length : 0); i < this.length; ++i) if(this[i] === value) return i;
    return -1;
  }

  lastIndexOf(value, fromIndex) {
    fromIndex ??= this.length;
    if(fromIndex < -this.length) return -1;
    for(let i = fromIndex + (fromIndex < 0 ? this.length : 0); i >= 0; --i) if(this[i] === value) return i;
    return -1;
  }

  forEach(fn) {
    const { length } = this;
    for(let i = 0; i < length; ++i) fn(this[i], i, this);
  }

  map(fn) {
    const r = [],
      n = this.length;
    for(let i = 0; i < n; ++i) r.push(fn(this[i], i, this));
    return r;
  }

  findIndex(fn) {
    const r = [],
      n = this.length;
    for(let i = 0; i < n; ++i) if(fn(this[i], i, this)) return i;
    return -1;
  }

  find(fn) {
    return this[this.findIndex(fn)];
  }

  findLastIndex(fn) {
    const r = [],
      n = this.length;
    for(let i = n - 1; i >= 0; --i) if(fn(this[i], i, this)) return i;
    return -1;
  }

  findLast(fn) {
    return this[this.findLastIndex(fn)];
  }

  filter(fn) {
    const r = [],
      n = this.length;
    for(let i = 0; i < n; ++i) if(fn(this[i], i, this)) r.push(this[i]);
    return r;
  }

  reduce(fn, initialValue) {
    const { length } = this;
    let o = initialValue;
    for(let i = 0; i < length; ++i) o = fn(o, this[i], i, this);
    return o;
  }

  reduceRight(fn, initialValue) {
    const { length } = this;
    let o = initialValue;
    for(let i = length - 1; i >= 0; --i) o = fn(o, this[i], i, this);
    return o;
  }
}

export const ArrayLikePrototype = ArrayLike.prototype;

Object.defineProperty(ArrayLike.prototype, Symbol.toStringTag, {
  value: 'ArrayLike',
});

export default ArrayLike;
