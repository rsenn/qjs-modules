import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';
import * as xml from 'xml.so';
import { Predicate } from 'predicate.so';
import Console from './console.js';

('use strict');
('use math');

globalThis.inspect = inspect;

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log(`Wrote '${file}': ${data.length} bytes`);
}
function DumpPredicate(lex) {
  const { id } = lex;

  return `Predicate ${inspect({ id })}`;
}

function main(...args) {
  console = new Console({
    colors: true,
    depth: 4,
    maxArrayLength: 100,
    maxStringLength: 100,
    compact: 1
  });
  let str = std.loadFile(args[0] ?? scriptArgs[0], 'utf-8');
  let len = str.length;
  console.log('len', len);
  console.log('Predicate', Predicate);
  console.log('Predicate.charset', Predicate.charset);

  let isNL = Predicate.charset('\n', 1);
  let isUpper = Predicate.charset('ABCDEFGHIJKLMNOPQRSTUVWXYZ', 26);
  let isLower = Predicate.charset('abcdefghijklmnopqrstuvwxyz', 26);
  let isDigit = Predicate.charset('0123456789', 10);
  let isXDigit = Predicate.charset('0123456789ABCDEFabcdef', 22);

  let isNotNL = Predicate.not(isNL);
  let isNotUpper = Predicate.not(isUpper);
  let isNotLower = Predicate.not(isLower);
  let isAlpha = Predicate.or(isLower, isUpper);
  let isAlnum = Predicate.or(isAlpha, isDigit);
  let isIdentifier = Predicate.regexp('^([A-Za-z_]+)([A-Za-z0-9_]*)$', 'g');
  let isNumber = Predicate.regexp('^([-+]?)([0-9]*).([0-9]+)$', 'g');
  console.log(`isIdentifier.toString()`, isIdentifier.toString());
  console.log(`isNumber.toString()`, isNumber.toString());

  let predicates = [isUpper, isLower, isNL, isNotNL, isNotUpper, isNotLower];

  for(let p of predicates) console.log('p:', p.toString());
  console.log(`isUpper.eval('a')`, isUpper.eval('a'));
  console.log(`isLower.eval('a')`, isLower.eval('a'));
  console.log(`isNotUpper.eval('a')`, isNotUpper.eval('a'));
  console.log(`isNotLower.eval('a')`, isNotLower.eval('a'));
  console.log(`isNotNL.eval('\\n')`, isNotNL.eval('\n'));
  console.log(`isNotNL.eval('\\r')`, isNotNL.eval('\r'));
  console.log(`isNL.eval('\\r')`, isNL.eval('\r'));

  for(let ch of ['_', '2', 'A', 'a', 'Z', 'z', '?', '-'])
    console.log(`isXDigit.eval('${ch}') =`, isXDigit.eval(ch));

  for(let ch of ['_', '2', 'A', 'a', '?', '-'])
    console.log(`isAlpha.eval('${ch}') =`, isAlpha.eval(ch));

  for(let ch of ['_', '2', 'A', 'a', '?', '-'])
    console.log(`isAlnum.eval('${ch}') =`, isAlnum.eval(ch));

  for(let str of ['_ABC3', '1ABC', '_1ABC', 'A1B2C3'])
    console.log(`isIdentifier.eval('${str}') =`,
      isIdentifier.eval(str, captures => {
        captures = new Uint32Array(captures);
        console.log('captures:', captures);
      })
    );

  for(let str of ['_ABC3', '1ABC', '_1ABC', 'A1B2C3']) {
    let a = [];
    console.log(`isIdentifier.eval('${str}') =`, isIdentifier.eval(str, a));
    console.log('a:', a);
  }

  for(let str of ['-120', '.0707', '+3.141592', '10000.00', '0.12345e08']) {
    let a = [];
    console.log(`isNumber.eval('${str}') =`, isNumber.eval(str, a));
    console.log('a:', a);
  }

  let combined = Predicate.or(isIdentifier, isNumber, isNL, isUpper);
  console.log('combined:', combined.toString());
  let s = '1abc';
  for(let pred of [isIdentifier, isNumber, isNL, isUpper]) {
    console.log(`pred.eval('${s}') =`, pred.eval(s));
  }

  for(let str of ['-120', '0.12345e08', 'ABC', '1abc']) {
    let a = [];
    console.log(`combined.eval('${str}') =`, combined.eval(str, a));
    //  console.log("a:", a);
  }
  console.log(`combined.values =`, combined.values);
  console.log(`combined.values =`,
    combined.values.map(v => v.toString())
  );
  let re = /^([-+])?([0-9]*\.)?([0-9]+)$/g;
  console.log(`re =`, re);
  console.log(`re =`, re + '');
  console.log(`re =`, re.toString());
  console.log(`re =`, Object.prototype.toString.call(re));
  /*console.log(`ArrayBuffer =`, ArrayBuffer+'');
    console.log(`ArrayBuffer =`,  Function.prototype.toString.call(ArrayBuffer));*/

  let dummy = new ArrayBuffer(1024);
  let arri32 = new Int32Array(1024);

  let io = Predicate.instanceOf(ArrayBuffer);
  let pt = Predicate.prototypeIs(ArrayBuffer.prototype);
  let pr = new Predicate(re);
  let eq1234 = Predicate.equal(1234);
  let eqBLAH = Predicate.equal('BLAH');
  console.log(`io =`, io);
  console.log(`io =`, io.toString());
  console.log(`pt =`, pt.toString());
  console.log(`pr =`, pr.toString());
  console.log(`io.eval(dummy) =`, io.eval(dummy));
  console.log(`pt.eval(dummy) =`, pt.eval(dummy));
  console.log(`io.eval(arri32.buffer) =`, io.eval(arri32.buffer));
  console.log(`pt.eval(arri32.buffer) =`, pt.eval(arri32.buffer));
  console.log(`eq1234 =`, eq1234.toString());
  console.log(`eqBLAH =`, eqBLAH.toString());

  for(let s2 of ['-120', '0.12345', '+12.345678', '-.9090']) {
     console.log(`pr.eval('${s2}') =`, pr.eval(s2));
  }
  let mt = Predicate.type(Predicate.TYPE_INT | Predicate.TYPE_OBJECT);
  console.log(`mt =`, mt.toString());

  for(let item of [1234, /*Symbol.iterator,*/ 'abcd', {}])
    console.log(`mt.eval(${item}) = `, mt.eval(item));
  let cp = Predicate.charset('ABCDEFGHIJKLMNOPQRSTUVWXYZ\u2605\u29bf\u2754');
  console.log(`cp =`, cp.toString());

  for(let str2 of ['abcd', 'X⦿Y', '❔X', 'ABC★']) console.log(`cp.eval(${str2}) =`, cp.eval(str2));

  std.gc();
}

main(...scriptArgs.slice(1));
