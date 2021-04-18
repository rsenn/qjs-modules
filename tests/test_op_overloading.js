import Console from '../lib/console.js';

('use strict');
('use math');

const OverloadNames = [
  '+',
  '-',
  '*',
  '/',
  '%',
  '**',
  '|',
  '&',
  '^',
  '<<',
  '>>',
  '>>>',
  '==',
  '<',
  'pos',
  'neg',
  '++',
  '--',
  '~'
];

const OperatorsObjects = Expr => [
  {
    '+'(...args) {
      return new Expr('+', ...args);
    },
    '-'(...args) {
      return new Expr('-', ...args);
    },
    '*'(...args) {
      return new Expr('*', ...args);
    },
    '/'(...args) {
      return new Expr('/', ...args);
    },
    '%'(...args) {
      return new Expr('%', ...args);
    },
    '**'(...args) {
      return new Expr('**', ...args);
    },
    '|'(...args) {
      return new Expr('|', ...args);
    },
    '&'(...args) {
      return new Expr('&', ...args);
    },
    '^'(...args) {
      return new Expr('^', ...args);
    },
    '<<'(...args) {
      return new Expr('<<', ...args);
    },
    '>>'(...args) {
      return new Expr('>>', ...args);
    },
    '>>>'(...args) {
      return new Expr('>>>', ...args);
    },
    '=='(...args) {
      return new Expr('==', ...args);
    },
    '<'(...args) {
      return new Expr('<', ...args);
    },
    pos(...args) {
      return new Expr('pos', ...args);
    },
    neg(...args) {
      return new Expr('neg', ...args);
    },
    '++'(...args) {
      return new Expr('++', ...args);
    },
    '--'(...args) {
      return new Expr('--', ...args);
    },
    '~'(...args) {
      return new Expr('~', ...args);
    }
  },
  {
    left: String,
    '*'(a, b) {
      return new Expr('*', a, b);
    }
  },
  {
    right: String,
    '*'(a, b) {
      return new Expr('*', a, b);
    }
  }
];

function assert(actual, expected, message) {
  if(arguments.length == 1) expected = true;

  if(actual === expected) return;

  if(actual !== null &&
    expected !== null &&
    typeof actual == 'object' &&
    typeof expected == 'object' &&
    actual.toString() === expected.toString()
  )
    return;

  throw Error('assertion failed: got |' +
      actual +
      '|' +
      ', expected |' +
      expected +
      '|' +
      (message ? ' (' + message + ')' : '')
  );
}

/* operators overloading with Operators.create() */
function test_operators_create() {
  class Expr {
    constructor(op, ...args) {
      this.op = op;
      this.children = args;
    }

    toString() {
      return `Expr{${this.op}}(` + this.children.map(e => e.toString()).join(', ') + ')';
    }
  }

  Expr.prototype[Symbol.operatorSet] = Operators.create(...OperatorsObjects(Expr));

  let a = new Expr(3);
  let b = new Expr(6);

  console.log('a+b', a + b);
  console.log('a*b', a * b);
  console.log('-(a/b)', -(a / b));
  console.log('+(a/b)', +(a / b));
}

/* operators overloading thru inheritance */
function test_operators() {
  let Expr;
  const ExprOps = Operators({
      '+'(...args) {
        return new Expr('+', ...args);
      },
      '-'(...args) {
        return new Expr('-', ...args);
      },
      '*'(...args) {
        return new Expr('*', ...args);
      },
      '/'(...args) {
        return new Expr('/', ...args);
      },
      '%'(...args) {
        return new Expr('%', ...args);
      },
      '**'(...args) {
        return new Expr('**', ...args);
      },
      '|'(...args) {
        return new Expr('|', ...args);
      },
      '&'(...args) {
        return new Expr('&', ...args);
      },
      '^'(...args) {
        return new Expr('^', ...args);
      },
      '<<'(...args) {
        return new Expr('<<', ...args);
      },
      '>>'(...args) {
        return new Expr('>>', ...args);
      },
      '>>>'(...args) {
        console.log(`method '>>>'`, ...args);
        return new Expr('>>>', ...args);
      },
      '=='(...args) {
        console.log(`method '=='`, ...args);
        return new Expr('==', ...args);
      },
      '<'(...args) {
        console.log(`method '<'`, ...args);
        return new Expr('<', ...args);
      },
      pos(...args) {
        return new Expr('pos', ...args);
      },
      neg(...args) {
        return new Expr('neg', ...args);
      },
      '++'(...args) {
        return new Expr('++', ...args);
      },
      '--'(...args) {
        return new Expr('--', ...args);
      },
      '~'(...args) {
        return new Expr('~', ...args);
      }
    },
    {
      left: String,
      '*'(a, b) {
        return new Expr('*', a, b);
      }
    },
    {
      right: String,
      '*'(a, b) {
        return new Expr('*', a, b);
      }
    }
  );

  Expr = class Expr extends ExprOps {
    constructor(op, ...args) {
      super();
      this.op = op;
      this.children = args;
    }

    toString() {
      return `Expr{${this.op}}(` + this.children.map(e => e.toString()).join(', ') + ')';
    }
  };

  let a = new Expr(3);
  let b = new Expr(6);

  console.log('a + b', a + b);
  console.log('a - b', a - b);
  console.log('a * b', a * b);
  console.log('a / b', a / b);
  console.log('a % b', a % b);
  console.log('a ** b', a ** b);
  console.log('a | b', a | b);
  console.log('a & b', a & b);
  console.log('a ^ b', a ^ b);
  console.log('a << b', a << b);
  console.log('a >> b', a >> b);
  console.log('a >>> b', a >>> b);
  console.log('a == b', a == b);
  console.log('a < b', a < b);
  console.log('pos a', +a);
  console.log('neg a', -a);
  console.log('++a', ++a);
  console.log('--a', --a);
  //console.log('~a',  ~a);
}

function test_default_op() {
  assert(Object(1) + 2, 3);
  assert(Object(1) + true, 2);
  assert(-Object(1), -1);
}

function main() {
  console = new Console({
    colors: true,
    depth: 5,
    maxArrayLength: 10,
    compact: 1,
    maxStringLength: 120
  });

  try {
    test_operators_create();
    test_operators();
    test_default_op();
  } catch(error) {
    console.log('ERROR:', error.message);
    throw error;
  }
}

main();
