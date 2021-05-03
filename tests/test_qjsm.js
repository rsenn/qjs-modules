import * as os from 'os';
import * as std from 'std';

function main(...args) {
  console.log('modules:', moduleList);

  let moduleExports = /*new Map*/ moduleList.map(module => [
    getModuleName(module),
    getModuleExports(module)
  ]);
  console.log('moduleExports:', moduleExports);

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
