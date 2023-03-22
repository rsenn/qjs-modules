import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { MySQL, MySQLResult } from 'mysql';

('use strict');
('use math');

async function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 256,
      compact: 2,
      showHidden: false,
      customInspect: false
    }
  });

  let my = new MySQL();

  console.log(
    'my.connect() =',
    await my.connect('localhost', 'root', 'tD51o7xf', 'mysql', 0, '/var/run/mysqld/mysqld.sock')
  );

  let res = await my.query('SELECT host,user,password FROM user;');
  console.log('my.query() =', res);

  for await(let row of res) {
    console.log('row =', row);
  }
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
