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

const CreateOperatorSet = Operators.create;

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

  if(
    actual !== null &&
    expected !== null &&
    typeof actual == 'object' &&
    typeof expected == 'object' &&
    actual.toString() === expected.toString()
  )
    return;

  throw Error(
    'assertion failed: got |' +
      actual +
      '|' +
      ', expected |' +
      expected +
      '|' +
      (message ? ' (' + message + ')' : '')
  );
}

/* operators overloading with CreateOperatorSet() */
function test_operators_create() {
  class Expr1 {
    constructor(op, ...args) {
      this.op = op;
      this.children = args;
    }

    toString() {
      return (
        `Expr1{${this.op}}(` +
        this.children.map(e => e.toString()).join(', ') +
        ')'
      );
    }
  }

  Expr1.prototype[Symbol.operatorSet] = CreateOperatorSet(
    ...OperatorsObjects(Expr1)
  );

  let a = new Expr1(3);
  let b = new Expr1(6);

  console.log('a+b', a + b);
  console.log('a*b', a * b);
  console.log('-(a/b)', -(a / b));
  console.log('+(a/b)', +(a / b));
}

function test_operators_ctor() {
  class Expr2 {
    constructor(op, ...args) {
      this.op = op;
      this.children = args;
    }

    toString() {
      return (
        `Expr2{${this.op}}(` +
        this.children.map(e => e.toString()).join(', ') +
        ')'
      );
    }
  }

  Expr2.prototype[Symbol.operatorSet] = CreateOperatorSet(
    ...OperatorsObjects(Expr2)
  );

  let a = new Expr2(3);
  let b = new Expr2(6);

  console.log('a+b', a + b);
  console.log('a*b', a * b);
  console.log('-(a/b)', -(a / b));
  console.log('+(a/b)', +(a / b));
}

/* operators overloading thru inheritance */
function test_operators_class() {
  let Expr3;
  const ExprOps = Operators(
    {
      '+'(...args) {
        return new Expr3('+', ...args);
      },
      '-'(...args) {
        return new Expr3('-', ...args);
      },
      '*'(...args) {
        return new Expr3('*', ...args);
      },
      '/'(...args) {
        return new Expr3('/', ...args);
      },
      '%'(...args) {
        return new Expr3('%', ...args);
      },
      '**'(...args) {
        return new Expr3('**', ...args);
      },
      '|'(...args) {
        return new Expr3('|', ...args);
      },
      '&'(...args) {
        return new Expr3('&', ...args);
      },
      '^'(...args) {
        return new Expr3('^', ...args);
      },
      '<<'(...args) {
        return new Expr3('<<', ...args);
      },
      '>>'(...args) {
        return new Expr3('>>', ...args);
      },
      '>>>'(...args) {
        console.log("method '>>>'", ...args);
        return new Expr3('>>>', ...args);
      },
      '=='(...args) {
        console.log("method '=='", ...args);
        return new Expr3('==', ...args);
      },
      '<'(...args) {
        console.log("method '<'", ...args);
        return new Expr3('<', ...args);
      },
      pos(...args) {
        return new Expr3('pos', ...args);
      },
      neg(...args) {
        return new Expr3('neg', ...args);
      },
      '++'(...args) {
        return new Expr3('++', ...args);
      },
      '--'(...args) {
        return new Expr3('--', ...args);
      },
      '~'(...args) {
        return new Expr3('~', ...args);
      }
    },
    {
      left: String,
      '*'(a, b) {
        return new Expr3('*', a, b);
      }
    },
    {
      right: String,
      '*'(a, b) {
        return new Expr3('*', a, b);
      }
    }
  );

  Expr3 = class Expr3 extends ExprOps {
    constructor(op, ...args) {
      super();
      this.op = op;
      this.children = args;
    }

    toString() {
      return (
        `Expr3{${this.op}}(` +
        this.children.map(e => e.toString()).join(', ') +
        ')'
      );
    }
  };

  let a = new Expr3(3);
  let b = new Expr3(6);

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
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 5,
      maxArrayLength: 10,
      compact: 1,
      maxStringLength: 120
    }
  });

  try {
    test_operators_create();
    test_operators_class();
    test_operators_ctor();
    test_default_op();
  } catch(error) {
    console.log('ERROR:', error.message);
    throw error;
  }
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
