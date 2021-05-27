import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { Location } from 'misc';

('use strict');
('use math');

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 8,
      breakLength: 100,
      maxStringLength: Infinity,
      maxArrayLength: Infinity,
      compact: 0,
      showHidden: false
    }
  });
  console.log('console.options', console.options);
  let loc = new Location('test.js:12:1');
  console.log('loc', loc);
  loc = new Location('test.js', 12, 1);
  console.log('loc', loc);
  let loc2 = new Location(loc);
  console.log('loc2', loc2);

  console.log('loc.toString()', loc.toString());
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
