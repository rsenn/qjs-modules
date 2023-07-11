import { concat } from 'misc';
import { toArrayBuffer } from 'misc';

export class Buffer {
  static from(arg) {
    if('string' == typeof arg) return toArrayBuffer(arg);

    throw new TypeError(`Unsupported argument: ${arg}`);
  }

  static concat(list) {
    return concat(...list);
  }
}
