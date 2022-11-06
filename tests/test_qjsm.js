import { inspect } from 'inspect';
import { format, formatWithOptions } from 'util';
import Console from '../lib/console.js';

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: { colors: true, depth: 1, compact: Infinity }
  });

  console.log('console.options:', console.options);

  console.log('format:', format);
  console.log('regexp:', /TEST/);
  //console.log('util:', inspect(util));

  console.log(format('%s %i %o', 'TEST', 1337, { x: 1, y: 2, z: 3 }));

  /*  let moduleExports = moduleList.map(module => getModuleObject(module));
  console.log('moduleExports:', moduleExports);*/
  console.log('process.hrtime():', process.hrtime());
  console.log('process.arch:', process.arch);
}

try {
  main(...process.argv.slice(2));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
} finally {
  console.log('SUCCESS');
}
