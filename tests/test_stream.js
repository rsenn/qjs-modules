import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { ReadableStream, WritableStream } from 'strean';

('use strict');
('use math');

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 256,
      compact: 2,
      showHidden: false
    }
  });
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
