export const MathExtensions = {
  exp10(n) {
    return Math.floor(Math.log10(n));
  },
  mantissa(n) {
    const mask = (1n << 52n) - 1n;
    const a = new BigUint64Array(new Float64Array([n]).buffer);

    return a[0] & mask;
  },
  exponent(n) {
    const mask = (1n << 11n) - 1n;
    const a = new BigUint64Array(new Float64Array([n]).buffer);

    return Number((a[0] >> 52n) & mask);
  },
  fsign(n) {
    const a = new BigUint64Array(new Float64Array([n]).buffer);
    return !!Number((a[0] >> 63n) & 1n);
  },
  float64(mantissa, exponent, sign) {
    const a = new BigUint64Array([BigInt(mantissa) & ((1n << 52n) - 1n), BigInt(exponent) & ((1n << 11n) - 1n), BigInt(sign) & 1n, 0n]);

    a[1] <<= 52n;
    a[2] <<= 63n;

    a[3] |= a[0];
    a[3] |= a[1];
    a[3] |= a[2];

    const f64a = new Float64Array(a.buffer, 8 * 3);

    //if(isNaN(f64a[0]))
    console.log(
      'float64',
      [...a].map(n => '0b' + n.toString(2).padStart(64, '0')),
    );

    return f64a[0];
  },
};

export const Math = Math;

export function extendMath(ctor = Math) {
  const d = Object.getOwnPropertyDescriptors(MathExtensions);

  for(const k in d) {
    d[k].enumerable = false;
    if(!d[k].get) d[k].writable = false;
  }

  Object.defineProperties(ctor, d);
  return ctor;
}

export default extendMath;
