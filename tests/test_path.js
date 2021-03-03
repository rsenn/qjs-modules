import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';
import * as path from 'path.so';
import Console from './console.js';

('use strict');
('use math');

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log(`Wrote '${file}': ${data.length} bytes`);
}

function CallPathFunction(name, ...args) {
  let fn = path[name];

 let ret = fn.call(path, ...args);
 console.log(`path.${name}(`, ...args.reduce((acc,arg) => acc.length ? [...acc, ', ', arg] : [arg], []), `) = ${ret}`);
 return ret;
}

async function main(...args) {
  console = new Console({ colors: true, depth: 5, _stringBreakNewline: false,maxArrayLength:10,compact:1, maxStringLength: 60 });

  let file = args[0] ?? '/etc/fonts/fonts.conf';
  console.log('file:', file);

  let base = path.basename(file, /\.[^.]*$/);
  console.log('path:', path);
console.log('base:', base);
console.log(`exists(${file}):`, path.exists(file));
console.log(`gethome(1000):`, path.gethome(1000));
console.log(`gethome(1000):`, path.gethome(1000));

CallPathFunction('readlink', '/home/roman/Sources');
CallPathFunction('canonicalize', '/home/roman/Sources');
CallPathFunction('realpath', '/home/roman/Sources');
CallPathFunction('relative', '/home/roman/Sources/plot-cv/quickjs', '/home/roman');

  let data = std.loadFile(file, 'utf-8');
  console.log('data:', data.substring(0, 100));

   let result = xml.read(data);

  console.log('result:', result);

  let str = xml.write(result);
  console.log('write:', str);


  console.log(`Writing '${base + '.json'}'...`);
  WriteFile(base + '.json',JSON.stringify(result, null, 2));

  console.log(`Writing '${base + '.xml'}'...`);
  WriteFile(base + '.xml', str);

  await import('std').then(std => std.gc());
}
console.log('test');
main(...scriptArgs.slice(1));
