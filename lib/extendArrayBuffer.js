import { nonenumerable } from 'util';
import { weakDefine } from 'util';
import { dupArrayBuffer } from 'misc';
import { searchArrayBuffer } from 'misc';
import { toArrayBuffer } from 'misc';
import { toPointer } from 'misc';

export const ArrayBufferExtensions = nonenumerable({
  search(needle, ...range) {
    return searchArrayBuffer(this, needle, ...range);
  },
  *searchAll(needle, pos = 0, size) {
    if(size !== undefined && (size = BigInt(size)) > 0n) size += BigInt(pos) - BigInt(this.byteLength);

    while((pos = searchArrayBuffer(this, needle, pos, size)) !== null) yield pos++;
  },
  view(offset, length) {
    return dupArrayBuffer(this, offset, length);
  },
  get address() {
    return BigInt(toPointer(this));
  },
});

export function extendArrayBuffer(proto = ArrayBuffer.prototype, ctor = proto.constructor) {
  weakDefine(proto, ArrayBufferExtensions);

  weakDefine(
    ctor,
    nonenumerable({
      fromString(str, byteOffset, byteLength) {
        return toArrayBuffer(str, byteOffset, byteLength);
      },
      fromAddress(address, length) {
        return toArrayBuffer(address, length);
      },
    }),
  );
}

export default extendArrayBuffer;