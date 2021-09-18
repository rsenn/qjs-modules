export const FD_SETSIZE = 1024;

export class fd_set extends ArrayBuffer {
  constructor() {
    super(FD_SETSIZE / 8);
  }
  get size() {
    return this.byteLength * 8;
  }
  get maxfd() {
    const a = this.array;
    return a[a.length - 1];
  }
  get array() {
    const a = new Uint8Array(this);
    const n = a.byteLength;
    const r = [];
    for(let i = 0; i < n; i++) for (let j = 0; j < 8; j++) if(a[i] & (1 << j)) r.push(i * 8 + j);
    return r;
  }
  toString() {
    return `[ ${this.array.join(', ')} ]`;
  }
  [Symbol.inspect]() {
    return this.toString();
  }
}

export function FD_SET(fd, set) {
  new Uint8Array(set, fd >> 3, 1)[0] |= 1 << (fd & 0x7);
}

export function FD_CLR(fd, set) {
  new Uint8Array(set, fd >> 3, 1)[0] &= ~(1 << (fd & 0x7));
}

export function FD_ISSET(fd, set) {
  return !!(new Uint8Array(set, fd >> 3, 1)[0] & (1 << (fd & 0x7)));
}

export function FD_ZERO(fd, set) {
  const a = new Uint8Array(set);
  const n = a.length;
  for(let i = 0; i < n; i++) a[i] = 0;
}

export default fd_set;
