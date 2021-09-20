export class socklen_t extends ArrayBuffer {
  constructor(v) {
    super(4);

    if(v !== undefined) new Uint32Array(this)[0] = v | 0;
  }

  [Symbol.toPrimitive](hint) {
    return new Uint32Array(this)[0];
  }
  
  valueOf() {
    return new Uint32Array(this)[0];
  }

  get [Symbol.toStringTag]() {
    return 'socklen_t';
  }

  [Symbol.inspect]() {
    return `\x1b[1;31msocklen_t\x1b[0m [` + this.valueOf() + `]`;
  }
}

export default socklen_t;
