import * as os from 'os';
import Console from '../lib/console.js';
import inspect from 'inspect';
import * as std from 'std';

('use strict');

('use math');

globalThis.inspect = inspect;

Map.prototype.emplace = function emplace(key, handler) {
  var map = this;
  var value = map.has(key) && 'update' in handler ? handler.update(map.get(key), key, map) : handler.insert(key, map);
  map.set(key, value);
  return value;
};

class Test {
  constructor() {}

  inspect(level, options = {}) {
    inspect(this, options);
  }
}

async function main(...args) {
  //console.log('main:', args);

  globalThis.console = new Console({
    inspectOptions: { depth: 1, compact: Infinity }
  });

  let winsz;

  try {
    winsz = await os.ttyGetWinSize(1);
  } catch(err) {}

  winsz ??= [];
  //console.log('winsz:', winsz);

  const options = {
    showHidden: false,
    customInspect: true,
    showProxy: false,
    getters: false,
    depth: 50,
    maxStringLength: 200,
    breakLength: winsz[0] || 80,
    compact: Infinity,
    hideKeys: ['loc', 'range', 'inspect', Symbol.for('nodejs.util.inspect.custom')]
  };
  let arr = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
  console.log('arr', arr);

  console.log('inspect(Test.prototype)', inspect(Test.prototype, options));
  const dumpObj = (obj, depth, options) =>
    '{' +
    Object.entries(obj)
      .map(([k, v]) => '\n' + '  '.repeat(options.depth - depth + 1) + k + '=' + inspect(v, depth - 1, options))
      .join(',') +
    '}';
  console.log('main', args);
  let value = 0;
  let str =
    '2.6 Test262 (ECMAScript Test Suite)\nA test262 runner is included in the QuickJS archive. The test262 tests can be installed in the\nQuickJS source directory with:\ngit clone https://github.com/tc39/test262.git test262\ncd test262\npatch -p1 < ../tests/test262.patch\ncd ..\nThe patch adds the implementation specific harness functions and optimizes the inefficient\nRegExp character classes and Unicode property escapes tests (the tests themselves are not\nmodified, only a slow string initialization function is optimized).\nThe tests can be run with\nmake test2\nThe configuration files test262.conf (resp. test262o.conf for the old ES5.1 tests1 )) contain\nthe options to run the various tests. Tests can be excluded based on features or filename.\nThe file test262_errors.txt contains the current list of errors. The runner displays a\nmessage when a new error appears or when an existing error is corrected or modified. Use the\n-u option to update the current list of errors (or make test2-update).\nThe file test262_report.txt contains the logs of all the tests. It is useful to have a clearer\nanalysis of a particular error. In case of crash, the last line corresponds to the failing test.\nUse the syntax ./run-test262 -c test262.conf -f filename.js to run a single test. Use\nthe syntax ./run-test262 -c test262.conf N to start testing at test number N.\nFor more information, run ./run-test262 to see the command line options of the test262\nrunner.\nrun-test262 accepts the -N option to be invoked from test262-harness2 thru eshost.\nUnless you want to compare QuickJS with other engines under the same conditions, we do not\nrecommend to run the tests this way as it is much slower (typically half an hour instead of about\n100 seconds).\n';
  let obj2 = {
    a: 1,
    b: true,
    c: 'prop',
    d: [...arr, { x: 99, y: Infinity, z: NaN }],

    inspect(depth, options) {
      const { hideKeys, ...opts } = options;
      print('inspect hideKeys ' + hideKeys.join(','));
      return ''; //dumpObj(this, depth, options);
    },
    str: 'this is a string\0!'
  };
  arr.fn = function TestFn() {};
  let fn = function test() {};
  fn.test = 'ABCD';
  let obj = {
    '@nan': NaN,

    '-inf': Number.NEGATIVE_INFINITY,
    '+inf': Number.POSITIVE_INFINITY,
    1337: -0,
    array: arr,
    string: str,
    boolean: true,
    null: null,
    symbol: Symbol.for('inspect'),
    number: 1234,
    undef: undefined,
    fn,
    bigint: 314159265358979323846264n,
    object: obj2,
    /* prettier-ignore */ get value() { return value; },
    /* prettier-ignore */ set value(v) { value = v; },
    /* prettier-ignore */ set v(v) { value = v; },
    inspect(depth, options) {
      const { hideKeys, ...opts } = options;
      print('inspect hideKeys ' + hideKeys.join(','));
      return ''; //dumpObj(this, depth, options);
    }
  };

  console.log('inspect(NaN)', inspect(NaN, options));
  // for(let value of Object.values(obj)) console.log('inspect', inspect(value, options));

  console.log('inspect', inspect(obj2, options));
  console.log(inspect('test \x1btest!', options));

  let outfile = std.open('output.txt', 'w+');

  outfile.puts(inspect('test \x1btest!', options));
  outfile.close();
  let deepObj = {
    a: {
      [1]: {
        [Symbol.species]: { test: { [0]: { x: [{ z: { ['?']: [{}] } }] } } }
      }
    }
  };

  console.log('inspect(deepObj)', inspect(deepObj, options));

  let s = new Set();
  ['a', 'b', 'c', 'd', 1, 2, 3, 4].forEach(item => s.add(item));

  console.log('inspect(s)', inspect(s, options));

  //for(let item of s) console.log('item:', item);

  std.gc();
  const re = /[a-z].*!$/g;
  console.log('inspect(re)', re);
  console.log('inspect(str)', 'DEADBEEF');

  console.log('inspect(num)', Math.PI);

  /*const PI_STR =
    '3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117067982148086513282306647093844609550582231725359408128481117450284102701938521105559644622948954930381964428810975665933446128475648233786783165271201909145648566923460348610454326648213393607260249141273724587006606315588174881520920962';
  BigFloatEnv.setPrec(() => {
    console.log('BigFloatEnv.prec', BigFloatEnv.prec);
    console.log('inspect(BigFloat)', BigFloat(PI_STR));
    console.log('inspect(BigDecimal)', BigDecimal(PI_STR));
    console.log('inspect(BigInt)', BigInt(PI_STR));
  }, 1024);*/

  let map = new Map([
    ['A', 'a'],
    ['B', 'b'],
    ['C', 'c'],
    ['D', 'd'],
    ['1', 1],
    ['2', 2],
    ['3', 3],
    ['4', 4],
    ['5', 5]
  ]);

  map.emplace('E', { insert: () => 0, update: v => v + 1 });
  map.emplace('E', { insert: () => 0, update: v => v + 1 });
  map.emplace('6', { insert: () => 6, update: v => -v });

  console.log('inspect(map)', inspect(map, { compact: Infinity }));

  std.gc();
  return;
}

main(...scriptArgs.slice(1))
  .then(() => console.log('SUCCESS'))
  .catch(error => {
    console.log(`FAIL: ${error.message}\n${error.stack}`);
    std.exit(1);
  });
