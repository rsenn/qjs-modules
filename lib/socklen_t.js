export class socklen_t extends ArrayBuffer {
  constructor(v) {
    super(4);
    Object.setPrototypeOf(this, new ArrayBuffer(4));
    if(v != undefined) new Uint32Array(this)[0] = v | 0;
  }
  [Symbol.toPrimitive](hint) {
    return new Uint32Array(this)[0];
  }
  [Symbol.toStringTag] = `[object socklen_t]`;
}

export default socklen_t;
