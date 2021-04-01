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
    compact: false
  });
  let str = std.loadFile(args[0] ?? scriptArgs[0], 'utf-8');
  let len = str.length;
  console.log('len', len);
  console.log('Predicate', Predicate);
  console.log('Predicate.oneOf', Predicate.oneOf);

  let isNL = Predicate.oneOf('\n', 1);
  let isUpper = Predicate.oneOf('ABCDEFGHIJKLMNOPQRSTUVWXYZ', 26);
  let isLower = Predicate.oneOf('abcdefghijklmnopqrstuvwxyz', 26);
  let isDigit = Predicate.oneOf('0123456789', 10);
  let isXDigit = Predicate.oneOf('0123456789ABCDEFabcdef', 22);

  let isNotNL = Predicate.not(isNL);
  let isNotUpper = Predicate.not(isUpper);
  let isNotLower = Predicate.not(isLower);
  let isAlpha = Predicate.or(isLower, isUpper);
  let isAlnum = Predicate.or(isAlpha, isDigit);

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

  std.gc();
}

main(...scriptArgs.slice(1));
