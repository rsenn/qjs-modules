import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';
import child_process from 'child-process.so';
import Console from './console.js';

('use strict');

globalThis.inspect = inspect;

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log('Wrote "' + file + '": ' + data.length + ' bytes');
}

const inspectOptions = {
  colors: true,
  showHidden: false,
  customInspect: true,
  showProxy: false,
  getters: false,
  depth: 4,
  maxArrayLength: 10,
  maxStringLength: 200,
  compact: 2,
  hideKeys: ['loc', 'range', 'inspect', Symbol.for('nodejs.util.inspect.custom')]
};

function main(...args) {
  console = new Console(inspectOptions);

  let cp = child_process.spawn('ls', ['-la'], { stdio: 'pipe' });
  console.log('cp:', cp);
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log('FAIL:', error.message);
  std.exit(1);
} finally {
  console.log('SUCCESS');
  std.exit(0);
}
