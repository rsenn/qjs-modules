import { inspect, format, formatWithOptions } from 'util';
import util from 'util';

function main(...args) {
  //console.log('console.options:', console.options);

  console.log('format:', format);
  console.log('regexp:', /TEST/);
  //console.log('util:', inspect(util));

  console.log(format('%s %o\n', 'TEST', { x: 1, y: 2, z: 3 }));

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
