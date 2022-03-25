import writeXML from './xml/write.js';

export const Pointer =
  process.release.name == 'quickjs'
    ? requireModule('pointer').Pointer
    : (() => {
        class Pointer {
          static fromArray(array) {
            return Object.setPrototypeOf([...array], Pointer.prototype);
          }

          static fromString(str) {
            return this.fromArray(str.split(/\b\.\b/g));
          }

          static from(other) {
            return this.fromArray([...other]);
          }

          constructor(ptr) {
            if(typeof ptr == 'string') return Pointer.fromString(ptr);
            if(ptr !== undefined) return Pointer.from(ptr);
          }

          deref(obj) {}
          /*   toString() {}
          toArray() {}
          shift() {}
          push() {}
          concat() {}
          slice() {}
          keys() {}
          values() {}
          hier() {}
          toPrimitive() {}
           path() {}
          atoms() {}
          map() {}
          reduce() {}
          forEach() {}
*/
          *[Symbol.iterator]() {
            const { length } = this;
            for(let i = 0; i < length; i++) yield this[i];
          }
        }
        Pointer.prototype.push = Array.prototype.push;
        Pointer.prototype.slice = Array.prototype.slice;
        Pointer.prototype.shift = Array.prototype.shift;
        Pointer.prototype.keys = Array.prototype.keys;
        Pointer.prototype.values = Array.prototype.values;
        Pointer.prototype.map = Array.prototype.map;
        Pointer.prototype.reduce = Array.prototype.reduce;
        Pointer.prototype.forEach = Array.prototype.forEach;
        Pointer.prototype.concat = Array.prototype.concat;
        Pointer.prototype.length = 0;

        return Pointer;
      })();
