import { searchArrayBuffer, dupArrayBuffer, toArrayBuffer, toString, toPointer } from 'misc';
import { weakDefine, nonenumerable } from 'util';

export const ArrayBufferExtensions = {
  includes(searchElement) {
    return some.call(this, value => value == searchElement);
  },
  enumerate() {
    return map.call(this, (value, i) => [i, value]);
  },
  async *chain(...iterables) {
    yield* this;
    for(const iter of iterables) yield* iter;
  },
  async *chainAll(iterables) {
    yield* this;
    for await(const iter of iterables) yield* iter;
  },
  async *range(start = 0, count = Infinity) {
    let i = 0,
      end = start + count;
    for await(const value of this) {
      if(i >= end) break;
      if(i >= start) yield value;
      ++i;
    }
  },
};

export function extendArrayBuffer(proto = ArrayBuffer.prototype, ctor) {
  weakDefine(proto, nonenumerable(ArrayBufferExtensions));

  weakDefine(
    ctor ?? proto.constructor,
    nonenumerable({
      fromString(...args) {
        return toArrayBuffer(...args);
      },
    }),
  );
}

export default extendArrayBuffer;
