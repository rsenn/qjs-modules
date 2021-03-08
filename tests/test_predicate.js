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
  let isNL = Predicate.charset('\n', 1);

 let isNotNL = Predicate.not(isNL);

 let predicates=[isNL,isNotNL];

for(let p of predicates)
  console.log('p:', p.toString());
  console.log(`isNotNL.eval('\\n')`, isNotNL.eval('\n'));
  console.log(`isNotNL.eval('\\r')`, isNotNL.eval('\r'));
  console.log(`isNL.eval('\\r')`, isNL.eval('\r'));

  std.gc();
}

main(...scriptArgs.slice(1));
