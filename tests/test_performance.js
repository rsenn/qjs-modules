import * as os from 'os';
import * as std from 'std';
import { now } from 'performance';

function main(...args) {
  console.log('now()', now());

  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
