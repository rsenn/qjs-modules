import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect';
import * as deep from 'deep';
import Console from 'console';

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

  console.log('test:', 1337);

  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  std.err.puts(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
