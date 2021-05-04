import * as os from 'os';
import * as std from 'std';
import { inspect } from 'util';
import util from 'util';

function main(...args) {
  //console.log('console.options:', console.options);

  console.log('util:', inspect(util));
  console.log('os:', inspect(os, { compact: false, breakLength: 80 }));

  /*   console.log('modules:', moduleList);
 let moduleExports =  moduleList.map(module => [
    getModuleName(module),
    getModuleExports(module)
  ]);
  console.log('moduleExports:', moduleExports);
*/
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
