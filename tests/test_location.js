import { Console } from 'console';
import { Location } from 'location';
import * as std from 'std';
('use strict');
('use math');

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 256,
      compact: 2,
      showHidden: false
    }
  });
  console.log('Location', Location);

  let location = new Location('utf-8');
  console.log('location', location);
  console.log('location', Object.getPrototypeOf(location));
  console.log('location', Object.getOwnPropertyNames(Object.getPrototypeOf(location)));
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}