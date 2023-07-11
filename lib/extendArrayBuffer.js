import { searchArrayBuffer, sliceArrayBuffer, toArrayBuffer } from 'misc';
import { define } from 'util';

export function extendArrayBuffer(proto = ArrayBuffer.prototype, ctor = ArrayBuffer) {
  define(proto, {
    indexOf(needle, startPos = 0) {
      return searchArrayBuffer(this, needle, startPos);
    },
    toString(...args) {
      return toString(this, ...args);
    }
  });

  define(ctor, {
    fromString(...args) {
      return toArrayBuffer(...args);
    }
  });
}

export default extendArrayBuffer;
