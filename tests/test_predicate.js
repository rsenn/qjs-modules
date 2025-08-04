import * as os from 'os';
import Console from '../lib/console.js';
import inspect from 'inspect';
import { equal, index, instanceOf, Predicate, PredicateOperators, PredicateOperatorSet, property } from 'predicate';
import * as std from 'std';

globalThis.inspect = inspect;

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log('Wrote "' + file + '": ' + data.length + ' bytes');
}

function DumpPredicate(lex) {
  const { id } = lex;

  return `Predicate ${inspect({ id })}`;
}

async function waitFor(msecs) {
  let promise, clear, timerId;
  promise = new Promise(async (resolve, reject) => {
    timerId = os.setTimeout(() => resolve(), msecs);
    clear = () => {
      os.clearTimeout(timerId);
      reject();
    };
  });
  promise.clear = clear;
  return promise;
}

async function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      depth: 4,
      breakLength: 80,
      maxArrayLength: 100,
      maxStringLength: 100,
      compact: false,
    },
  });
  let str = std.loadFile(args[0] ?? scriptArgs[0], 'utf-8');
  let len = str.length;
  /* console.log('len', len);
  console.log('Predicate', Predicate);
  console.log('Predicate.charset', charset);

  let eq1234 = equal(1234);
  console.log('eq1234 =', eq1234.toString()); 

  let promise = waitFor(100).then(() => 1234);

  let result = eq1234(promise);
  console.log('promise', promise);
  console.log('result', await result);

  let isNL = charset('\n', 1);
  let isUpper = charset('ABCDEFGHIJKLMNOPQRSTUVWXYZ', 26);
  let isLower = charset('abcdefghijklmnopqrstuvwxyz', 26);
  let isDigit = charset('0123456789', 10);
  let isXDigit = charset('0123456789ABCDEFabcdef', 22);

  let isNotNL = not(isNL);
  let isNotUpper = not(isUpper);
  let isNotLower = not(isLower);
  let isAlpha = or(isLower, isUpper);
  let isAlnum = or(isAlpha, isDigit);
  let isIdentifier = regexp('^([A-Za-z_]+)([A-Za-z0-9_]*)$', 'g');
  let isNumber = regexp('^([-+]?)([0-9]*).([0-9]+)$', 'g');
  console.log('isIdentifier.toString()', isIdentifier.toString());
  console.log('isNumber.toString()', isNumber.toString());

  let predicates = [isUpper, isLower, isNL, isNotNL, isNotUpper, isNotLower];

  for(let p of predicates) console.log('p:', p.toString());
  console.log("isUpper('a')", isUpper('a'));
  console.log("isLower('a')", isLower('a'));
  console.log("isNotUpper('a')", isNotUpper('a'));
  console.log("isNotLower('a')", isNotLower('a'));
  console.log("isNotNL('\\n')", isNotNL('\n'));
  console.log("isNotNL('\\r')", isNotNL('\r'));
  console.log("isNL('\\r')", isNL('\r'));

  for(let ch of ['_', '2', 'A', 'a', 'Z', 'z', '?', '-'])
    console.log(`isXDigit('${ch}') =`, isXDigit(ch));

  for(let ch of ['_', '2', 'A', 'a', '?', '-']) console.log(`isAlpha('${ch}') =`, isAlpha(ch));

  for(let ch of ['_', '2', 'A', 'a', '?', '-']) console.log(`isAlnum('${ch}') =`, isAlnum(ch));

  let propToString = property('toString');
  let propTest = property('test');

  console.log('propToString(Object.create(null, {}))', propToString(Object.create(null, {})));
  console.log(
    'propToString(function(){})',
    propToString(function () {})
  );

  console.log('propTest({})', propTest({}));
  console.log('propTest({test: undefined})', propTest({ test: undefined }));
*/
  /*

  for(let str of ['_ABC3', '1ABC', '_1ABC', 'A1B2C3'])
    console.log(`isIdentifier('${str}') =`,
      isIdentifier(str, captures => {
        captures = new Uint32Array(captures);
        console.log('captures:', captures);
      })
    );

  for(let str of ['_ABC3', '1ABC', '_1ABC', 'A1B2C3']) {
    let a = [];
    console.log(`isIdentifier('${str}') =`, isIdentifier(str, a));
    console.log('a:', a);
  }

  for(let str of ['-120', '.0707', '+3.141592', '10000.00', '0.12345e08']) {
    let a = [];
    console.log(`isNumber('${str}') =`, isNumber(str, a));
    console.log('a:', a);
  }

  let combined = or(isIdentifier, isNumber, isNL, isUpper);
  console.log('combined:', combined.toString());
  let s = '1abc';
  for(let pred of [isIdentifier, isNumber, isNL, isUpper]) {
    console.log(`pred('${s}') =`, pred(s));
  }
  for(let str of ['-120', '0.12345e08', 'ABC', '1abc']) {
    let a = [];
    console.log(`combined('${str}') =`, combined(str, a));

  }
  console.log('combined.values =', combined.values);
  console.log('combined.values =',
    combined.values.map(v => v.toString())
  );
  let re = /^([-+])?([0-9]*\.)?([0-9]+)$/g;
  console.log('re =', re);
  console.log('re =', re + '');
  console.log('re =', re.toString());
  let dummy = new ArrayBuffer(1024);
  let arri32 = new Int32Array(1024);

  let io = instanceOf(ArrayBuffer);
  let pt = prototypeIs(ArrayBuffer.prototype);
  let pr = new Predicate(re);
  let eqBLAH = equal('BLAH');
  console.log('io =', io);
  console.log('io =', io.toString());
  console.log('pt =', pt.toString());
  console.log('pr =', pr.toString());
  console.log('io(dummy) =', io(dummy));
  console.log('pt(dummy) =', pt(dummy));
  console.log('io(arri32.buffer) =', io(arri32.buffer));
  console.log('pt(arri32.buffer) =', pt(arri32.buffer));
  console.log('eqBLAH =', eqBLAH.toString());
  console.log("eqBLAH('BLAH') =", eqBLAH('BLAH'));

  for(let s2 of ['-120', '0.12345', '+12.345678', '-.9090']) {
    console.log(`pr('${s2}') =`, pr(s2));
  }
  let mt = type(Predicate.TYPE_INT | Predicate.TYPE_OBJECT);
  console.log('mt =', mt.toString());

  for(let item of [1234,  'abcd', {}]) console.log(`mt(${item}) = `, mt(item));
  let cp = charset('ABCDEFGHIJKLMNOPQRSTUVWXYZ\u2605\u29bf\u2754');
  console.log('cp =', cp.toString());

  for(let str2 of ['abcd', 'X⦿Y', '❔X', 'ABC★']) console.log(`cp(${str2}) =`, cp(str2));
  console.log('re =', Object.prototype.toString.call(re));
  console.log('  =', BigDecimal.prototype.toString.call(31337m));
  console.log('  =', BigInt.prototype.toString.call(31337n));
  console.log('  =', eval('31337l'));
  console.log('  =', eval('31337m'));
  console.log('  =', eval('31337n'));
*/
  console.log('Object.getOwnPropertyNames(Predicate)', Object.getOwnPropertyNames(Predicate));

  console.log('PredicateOperators', PredicateOperators);
  console.log("PredicateOperators['*']", PredicateOperators['*']);

  Predicate.prototype[Symbol.operatorSet] = PredicateOperatorSet;

  console.log('Predicate', new Map(Object.entries(Predicate)));

  let mul = Predicate.mul(null, 8);
  let div = Predicate.div(mul, 0.5);
  let add = Predicate.add(div, 5);
  let term = Predicate.mod(add, 2);
  console.log('add', add);
  console.log('add.toString()', add.toString());
  console.log('term', term);
  console.log('term.toString()', term.toString());
  console.log('term.toSource()', term.toSource());
  console.log('term.args', term.args);
  console.log('div(18)', div(18));
  console.log('mul(10)', mul(10));
  console.log('add(20)', add(20));
  console.log('term(19)', term(19));

  let pred = 2 ** mul;
  console.log('pred.toString()', pred.toString());
  console.log('pred', pred);
  console.log('pred.toSource()', pred.toSource());
  console.log('pred(1)', pred(1));
  console.log('pred(2)', pred(2));
  console.log('pred(3)', pred(3));
  console.log('pred(4)', pred(4));

  console.log(
    new Map([
      ['2 ** mul', [pred.toSource(), [1, 2, 3, 4].map(n => pred(n))]],
      ['mul & 2', (mul & 0b11111111111111111111111111111101).toSource()],
      ['mul | 2', (mul | 2).toSource()],
    ]),
  );

  let check = Predicate.instanceOf(Predicate);
  let getProp = Predicate.property('and');
  let getProp2 = Predicate.member(Predicate);
  console.log('Predicate.property', Predicate.property);
  console.log('Predicate.member', Predicate.member);

  let getMember = Predicate.member({ [1]: 'one', [2]: 'two', [3]: 'three' });

  console.log('getMember', getMember);
  console.log('getMember(1)', getMember(1));

  console.log('check(add)', check(add));
  console.log('check(new Date())', check(new Date()));
  console.log('getProp', getProp);
  console.log('getProp(Predicate)', getProp(Predicate));
  console.log('getProp2', getProp2);
  console.log("getProp2('sub')", getProp2('sub'));

  let op_create = Operators.create;
  console.log('op_create', op_create);
  let op_set = Symbol.operatorSet;
  console.log('op_set', op_set);
  console.log('PredicateOperatorSet', PredicateOperatorSet);
  console.log("PredicateOperatorSet['*']", PredicateOperatorSet['*']);

  let ip = Predicate.index(-1, Predicate.equal(4));
  let shp = Predicate.shift(2, (...args) => args);

  console.log('ip([1,2,3,4])', ip([1, 2, 3, 4]));

  console.log('shp', shp);
  console.log('shp(1,2,3,4)', shp(1, [2, 3, 4]));

  std.gc();
}

main(...scriptArgs.slice(1))
  .then(() => console.log('SUCCESS'))
  .catch(error => {
    console.log(`FAIL: ${error.message}\n${error.stack}`);
    std.exit(1);
  });
