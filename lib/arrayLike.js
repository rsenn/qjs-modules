export class ArrayLike {
  at(index) {
    if(index < 0) index += this.length;
    return this[index];
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
}

export const ArrayLikePrototype = ArrayLike.prototype;

Object.defineProperty(ArrayLike.prototype, Symbol.toStringTag, {
  value: 'ArrayLike',
});

export default ArrayLike;
