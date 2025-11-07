import { searchArrayBuffer, dupArrayBuffer, toArrayBuffer, toPointer } from 'misc';
import { weakDefine, nonenumerable } from 'util';

export const ArrayBufferExtensions = nonenumerable({
  search(needle, start = 0) {
    return searchArrayBuffer(this, needle, start);
  },
  view(offset, length) {
    return dupArrayBuffer(this, offset, length);
  },
  get address() {
    return BigInt(toPointer(this));
  },
});

export function extendArrayBuffer(proto = ArrayBuffer.prototype, ctor) {
  weakDefine(proto, ArrayBufferExtensions);

  weakDefine(
    ctor ?? proto.constructor,
    nonenumerable({
      fromString(str, offset, length) {
        return toArrayBuffer(str, offset, length);
      },
      fromPointer(address, length) {
        return toArrayBuffer(address, length);
      },
    }),
  );
}

export default extendArrayBuffer;
