/* QuickJS operator overloading (Operators.create / Symbol.operatorSet).
 * Needs an engine built with CONFIG_BIGNUM; skipped otherwise. */
import { assert, eq, tests } from '../../lib/tinytest.js';

if(typeof Operators == 'undefined') {
  console.log('SKIP: engine has no Operators (built without CONFIG_BIGNUM)');
} else {
  class Vec {
    constructor(x, y) {
      this.x = x;
      this.y = y;
    }
  }

  Vec.prototype[Symbol.operatorSet] = Operators.create(
    {
      '+': (a, b) => new Vec(a.x + b.x, a.y + b.y),
      '-': (a, b) => new Vec(a.x - b.x, a.y - b.y),
      '==': (a, b) => a.x === b.x && a.y === b.y,
      '<': (a, b) => a.x * a.x + a.y * a.y < b.x * b.x + b.y * b.y,
      pos: a => new Vec(a.x, a.y),
      neg: a => new Vec(-a.x, -a.y),
    },
    { left: Number, '*': (n, v) => new Vec(n * v.x, n * v.y) },
    { right: Number, '*': (v, n) => new Vec(v.x * n, v.y * n) },
  );

  tests({
    'binary + and -'() {
      const sum = new Vec(1, 2) + new Vec(3, 4);
      const diff = new Vec(3, 4) - new Vec(1, 2);

      eq('4,6', [sum.x, sum.y].join());
      eq('2,2', [diff.x, diff.y].join());
    },

    'unary - and +'() {
      const n = -new Vec(1, 2);

      eq('-1,-2', [n.x, n.y].join());
      eq('1,2', [(+new Vec(1, 2)).x, (+new Vec(1, 2)).y].join());
    },

    'mixed Number * Vec on either side'() {
      const l = 2 * new Vec(1, 2);
      const r = new Vec(1, 2) * 3;

      eq('2,4', [l.x, l.y].join());
      eq('3,6', [r.x, r.y].join());
    },

    '== and <'() {
      assert(new Vec(1, 2) == new Vec(1, 2), 'equal');
      assert(!(new Vec(1, 2) == new Vec(2, 1)), 'not equal');
      assert(new Vec(1, 1) < new Vec(2, 2), 'less');
    },
  });
}
