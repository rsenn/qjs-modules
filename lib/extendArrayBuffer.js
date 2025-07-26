import { searchArrayBuffer, dupArrayBuffer, toArrayBuffer, toString, toPointer } from 'misc';
import { define, nonenumerable } from 'util';

export function extendArrayBuffer(proto = ArrayBuffer.prototype, ctor = ArrayBuffer) {
  nonenumerable(
    {
      indexOf(needle, startPos = 0) {
        return searchArrayBuffer(this, needle, startPos);
      },
      toString(...args) {
        return toString(this, ...args);
      },
      view(start, length) {
        return dupArrayBuffer(this, start, length);
      },
      *searchIndex(needle) {
        const { byteLength } = needle;

        for(let tmp, pos = 0; (tmp = searchArrayBuffer(this, needle, pos)) !== null; pos = tmp + byteLength) yield tmp;
      },
      *split(needle) {
        const { byteLength } = needle;
        for(let pos = 0; ; ) {
          const tmp = searchArrayBuffer(this, needle, pos);
          yield this.view(pos, (tmp == -1 ? byteLength : tmp) - pos);
          if(tmp == -1) break;
          pos = tmp + byteLength;
        }
      },
      get memoryAddress() {
        return toPointer(this);
      },
    },
    proto,
  );

  nonenumerable(
    {
      fromString(...args) {
        return toArrayBuffer(...args);
      },
    },
    ctor,
  );
}

export default extendArrayBuffer;
