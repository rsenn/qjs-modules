import * as path from 'path';
import Console from '../lib/console.js';
import inspect from 'inspect';
import * as std from 'std';
('use strict');
('use math');

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log('Wrote "' + file + '": ' + data.length + ' bytes');
}

function CallPathFunction(name, ...args) {
  let fn = path[name];

  let ret = fn.call(path, ...args);
  console.log(`path.${name}(`, ...args.reduce((acc, arg) => (acc.length ? [...acc, ', ', arg] : [arg]), []), ') =', ret);
  return ret;
}

async function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      depth: 5,
      _stringBreakNewline: false,
      maxArrayLength: 10,
      compact: 1,
      maxStringLength: 120
    }
  });

  let file = args[0] ?? '/etc/fonts/fonts.conf';
  console.log('file:', file);

  let base = path.basename(file, /\.[^.]*$/);
  console.log('path:', path);
  console.log('base:', base);
  console.log(`exists(${file}):`, path.exists(file));
  console.log('gethome(1000):', path.gethome(1000));
  console.log('gethome(1000):', path.gethome(1000));
  console.log('path.delimiter:', path.delimiter);

  CallPathFunction('extname', file);
  CallPathFunction('readlink', '/home/roman/Sources');
  CallPathFunction('resolve', '/home/roman/Sources');
  CallPathFunction('realpath', '/home/roman/Sources');
  CallPathFunction('join', '/home/roman', 'Sources', 'plot-cv/quickjs', 'modules');
  CallPathFunction('isAbsolute', 'c:/windows');
  CallPathFunction('isAbsolute', '/etc');
  CallPathFunction('isAbsolute', '../tmp');
  CallPathFunction('isRelative', '../tmp');
  CallPathFunction('isSymlink', '/dev/stdin');
  CallPathFunction('parse', '/home/roman/Sources/plot-cv/CMakeLists.txt');
  CallPathFunction('parse', 'C:/Windows/System32/mshtml.dll');
  CallPathFunction('components', '/home/roman/Sources/plot-cv/quickjs');
  CallPathFunction('format', {
    root: 'c:/',
    dir: 'C:/Windows/System32/drivers',
    name: 'tcpip',
    ext: '.sys'
  });
  /* CallPathFunction('resolve', 'wwwroot', 'static_files/png/', '../gif/image.gif');
  CallPathFunction('resolve', '..');
  CallPathFunction('resolve', '/etc', 'fonts', 'fonts.conf');*/

  CallPathFunction('getcwd');
  let cwd = path.getcwd();
  console.log(`cwd = ${cwd}`);
  CallPathFunction('relative', '/home/roman/Projects/plot-cv/quickjs', cwd);
  CallPathFunction('relative', '/home/roman/Projects/editline', cwd);
  CallPathFunction('normalize', '/home/roman/Projects/plot-cv/quickjs/modules/build/x86_64-linux-gnu/CMakeFiles/../../..');
  if(path.slice) CallPathFunction('slice', '/home/roman/Projects/plot-cv/quickjs/modules/build/x86_64-linux-gnu/CMakeFiles/../../..', 2, -3);

  /*let data = std.loadFile(file, 'utf-8');
  console.log('data:', data.substring(0, 100));

  let result = xml.read(data);
  console.log('result:', result);
  let str = xml.write(result);
  console.log('write:', str);
  WriteFile(base + '.json', JSON.stringify(result, null, 2));
  WriteFile(base + '.xml', str);*/

  await import('std').then(std => std.gc());
}

main(...scriptArgs.slice(1))
  .then(() => console.log('SUCCESS'))
  .catch(error => {
    console.log(`FAIL: ${error.message}\n${error.stack}`);
    std.exit(1);
  });